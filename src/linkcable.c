#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/timer.h"

#include "linkcable.h"
#include "gbridge.h"
#include "useful_qualifiers.h"
#include "utils.h"

//#define FAST_ALIGNMENT
//#define DEBUG_TIMEFRAMES
//#define LOG_DIRECT_SEND_RECV

#ifdef STACKSMASHING
    #include "linkcable_sm.pio.h"
#else
    #include "linkcable.pio.h"
#endif

#define LOG_BUFFER_SIZE 0x2000
#define TIMEFRAMES_BUFFER_SIZE 0x1000

#define DEFAULT_SAVED_BITS 8

typedef uint16_t timeframes_t;
timeframes_t *timeframes_across = NULL;
timeframes_t *timeframes_transfer = NULL;
uint32_t *timeframes_buffer_pos = NULL;

typedef uint8_t log_t;
log_t *log_linkcable_buffer_out = NULL;
log_t *log_linkcable_buffer_in = NULL;
uint32_t *log_buffer_pos = NULL;

static irq_handler_t linkcable_irq_handler = NULL;
static uint32_t linkcable_pio_initial_pc = 0;
static uint saved_bits = DEFAULT_SAVED_BITS;
static uint64_t saved_time = 0;
bool is_enabled = false;
bool *is_linkcable_ready = NULL;
uint64_t *last_transfer_time = NULL;

static void TIME_SENSITIVE(linkcable_isr)(void) {
    uint64_t curr_time = time_us_64();
    uint64_t since_last_transfer_time = curr_time - (*last_transfer_time);
    *last_transfer_time = curr_time;
#if defined(FAST_ALIGNMENT) || defined(DEBUG_TIMEFRAMES)
    uint64_t transfer_time = curr_time - saved_time;
#endif
#ifdef DEBUG_TIMEFRAMES
    timeframes_across[*timeframes_buffer_pos] = since_last_transfer_time;
    if(since_last_transfer_time > ((timeframes_t)(0xFFFFFFFFFFFFFFFF)))
        timeframes_across[*timeframes_buffer_pos] = ((timeframes_t)(0xFFFFFFFFFFFFFFFF));
    timeframes_transfer[*timeframes_buffer_pos] = transfer_time;
    if(transfer_time > ((timeframes_t)(0xFFFFFFFFFFFFFFFF)))
        timeframes_transfer[*timeframes_buffer_pos] = ((timeframes_t)(0xFFFFFFFFFFFFFFFF));
    *timeframes_buffer_pos = ((*timeframes_buffer_pos) + 1) % TIMEFRAMES_BUFFER_SIZE;
#endif
#ifdef FAST_ALIGNMENT
    uint64_t dest_time = curr_time + ((transfer_time + saved_bits - 1) / saved_bits);
#endif
    if (linkcable_irq_handler) linkcable_irq_handler();
    if (pio_interrupt_get(LINKCABLE_PIO, 0)) pio_interrupt_clear(LINKCABLE_PIO, 0);
#ifdef FAST_ALIGNMENT
    curr_time = time_us_64();
    if(dest_time > curr_time) {
        if((dest_time - curr_time) < MSEC(100))
            busy_wait_us(dest_time - curr_time);
    }
#ifdef STACKSMASHING
    linkcable_activate(LINKCABLE_PIO, LINKCABLE_SM);
#endif
#endif
}

bool linkcable_is_enabled(void) {
    return is_enabled;
}

bool can_disable_linkcable_irq(void) {
    if(!is_enabled)
        return true;
    uint64_t old_time = *last_transfer_time;
    uint64_t curr_time = time_us_64();
    if((curr_time - old_time) >= SEC(1))
        return true;
    return false;
}

#if defined(FAST_ALIGNMENT) || defined(DEBUG_TIMEFRAMES)
static void TIME_SENSITIVE(linkcable_time_isr)(void) {
    saved_time = time_us_64();
    if (pio_interrupt_get(LINKCABLE_PIO, 1)) pio_interrupt_clear(LINKCABLE_PIO, 1);
}
#endif

static void prepare_debug_buffer(void* buffer, void* data, uint32_t pos, uint32_t num_elems, uint32_t size_elem) {
    memcpy(buffer, ((uint8_t*)data) + (pos * size_elem), (num_elems - pos) * size_elem);
    memcpy(((uint8_t*)buffer) + ((num_elems - pos) * size_elem), data, pos * size_elem);
}

void print_last_linkcable(void) {
#ifdef LOG_DIRECT_SEND_RECV
    log_t debug_buffer[LOG_BUFFER_SIZE];

    prepare_debug_buffer(debug_buffer, log_linkcable_buffer_in, *log_buffer_pos, LOG_BUFFER_SIZE, sizeof(log_t));
    debug_send((uint8_t*)debug_buffer, sizeof(debug_buffer), GBRIDGE_CMD_DEBUG_LOG_IN);

    prepare_debug_buffer(debug_buffer, log_linkcable_buffer_out, *log_buffer_pos, LOG_BUFFER_SIZE, sizeof(log_t));
    debug_send((uint8_t*)debug_buffer, sizeof(debug_buffer), GBRIDGE_CMD_DEBUG_LOG_OUT);
#endif
#ifdef DEBUG_TIMEFRAMES
    timeframes_t debug_buffer_timeframes[TIMEFRAMES_BUFFER_SIZE];

    prepare_debug_buffer(debug_buffer_timeframes, timeframes_transfer, *timeframes_buffer_pos, TIMEFRAMES_BUFFER_SIZE, sizeof(timeframes_t));
    debug_send((uint8_t*)debug_buffer_timeframes, sizeof(debug_buffer_timeframes), GBRIDGE_CMD_DEBUG_TIME_TR);

    prepare_debug_buffer(debug_buffer_timeframes, timeframes_across, *timeframes_buffer_pos, TIMEFRAMES_BUFFER_SIZE, sizeof(timeframes_t));
    debug_send((uint8_t*)debug_buffer_timeframes, sizeof(debug_buffer_timeframes), GBRIDGE_CMD_DEBUG_TIME_AC);
#endif
}

uint32_t TIME_SENSITIVE(linkcable_receive)(void) {
    uint32_t retval = (pio_sm_get(LINKCABLE_PIO, LINKCABLE_SM) & ((1 << saved_bits) - 1));
#ifdef LOG_DIRECT_SEND_RECV
    log_linkcable_buffer_in[*log_buffer_pos] = retval;
#endif
    return retval;
}

void TIME_SENSITIVE(linkcable_send)(uint32_t data) {
#ifdef LOG_DIRECT_SEND_RECV
    log_linkcable_buffer_out[*log_buffer_pos] = data;
    *log_buffer_pos = ((*log_buffer_pos) + 1) % LOG_BUFFER_SIZE;
#endif
    uint32_t sendval = (data << (32 - saved_bits));
    pio_sm_put(LINKCABLE_PIO, LINKCABLE_SM, sendval);
}

void TIME_SENSITIVE(clean_linkcable_fifos)(void) {
    pio_sm_clear_fifos(LINKCABLE_PIO, LINKCABLE_SM);
}

void linkcable_enable(void) {
    while(!(*is_linkcable_ready));
    is_enabled = true;
    pio_sm_set_enabled(LINKCABLE_PIO, LINKCABLE_SM, true);
}

void linkcable_disable(void) {
    linkcable_reset(false);
}

void linkcable_reset(bool re_enable) {
    pio_sm_set_enabled(LINKCABLE_PIO, LINKCABLE_SM, false);
    is_enabled = false;
    pio_sm_clear_fifos(LINKCABLE_PIO, LINKCABLE_SM);
    pio_sm_restart(LINKCABLE_PIO, LINKCABLE_SM);
    pio_sm_clkdiv_restart(LINKCABLE_PIO, LINKCABLE_SM);
    pio_sm_exec(LINKCABLE_PIO, LINKCABLE_SM, pio_encode_jmp(linkcable_pio_initial_pc));
    if(re_enable)
        linkcable_enable();
}

void linkcable_set_is_32(bool is_32) {
    if(is_32)
        saved_bits = 32;
    else
        saved_bits = 8;
#ifdef STACKSMASHING
    linkcable_select_mode(LINKCABLE_PIO, LINKCABLE_SM, saved_bits);
#endif
}

void linkcable_pre_split(void) {
    is_linkcable_ready = malloc(sizeof(bool));
    last_transfer_time = malloc(sizeof(uint64_t));
    timeframes_buffer_pos = malloc(sizeof(uint32_t));
    log_buffer_pos = malloc(sizeof(uint32_t));
    *log_buffer_pos = 0;
    *timeframes_buffer_pos = 0;
    *last_transfer_time = 0;
    *is_linkcable_ready = false;
#ifdef DEBUG_TIMEFRAMES
    timeframes_across = malloc(TIMEFRAMES_BUFFER_SIZE * sizeof(timeframes_t));
    timeframes_transfer = malloc(TIMEFRAMES_BUFFER_SIZE * sizeof(timeframes_t));
#endif
#ifdef LOG_DIRECT_SEND_RECV
    log_linkcable_buffer_out = malloc(LOG_BUFFER_SIZE * sizeof(log_t));
    log_linkcable_buffer_in = malloc(LOG_BUFFER_SIZE * sizeof(log_t));
#endif
}

void linkcable_init(irq_handler_t onDataReceive) {
    saved_bits = DEFAULT_SAVED_BITS;
#ifdef STACKSMASHING
    linkcable_sm_program_init(LINKCABLE_PIO, LINKCABLE_SM, linkcable_pio_initial_pc = pio_add_program(LINKCABLE_PIO, &linkcable_sm_program), DEFAULT_SAVED_BITS);
#else
    linkcable_program_init(LINKCABLE_PIO, LINKCABLE_SM, linkcable_pio_initial_pc = pio_add_program(LINKCABLE_PIO, &linkcable_program), CABLE_PINS_START);
#endif
//    pio_sm_put_blocking(LINKCABLE_PIO, LINKCABLE_SM, LINKCABLE_BITS - 1);
    pio_enable_sm_mask_in_sync(LINKCABLE_PIO, (1u << LINKCABLE_SM));

    if (onDataReceive) {
        linkcable_irq_handler = onDataReceive;
        pio_set_irq0_source_enabled(LINKCABLE_PIO, pis_interrupt0, true);
        irq_set_exclusive_handler(PIO0_IRQ_0, linkcable_isr);
        irq_set_enabled(PIO0_IRQ_0, true);
    }
#if defined(FAST_ALIGNMENT) || defined(DEBUG_TIMEFRAMES)
    pio_set_irq1_source_enabled(LINKCABLE_PIO, pis_interrupt1, true);
    irq_set_exclusive_handler(PIO0_IRQ_1, linkcable_time_isr);
    irq_set_enabled(PIO0_IRQ_1, true);
#endif
    *is_linkcable_ready = true;
}
