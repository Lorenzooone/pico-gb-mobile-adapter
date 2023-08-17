#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "bsp/board.h"

#include "gb_printer.h"
#include "linkcable.h"

volatile enum printer_state printer_state = PRN_STATE_WAIT_FOR_SYNC_1;

#define PRINTER_RESET (printer_state = PRN_STATE_WAIT_FOR_SYNC_1)

// printer packet state machine
void protocol_reset() {
    PRINTER_RESET;
}

uint8_t protocol_data_init() {
    return 0x00;
}

uint32_t protocol_data_process(uint32_t data_in) {
    static uint8_t printer_status = PRN_STATUS_OK, next_printer_status = PRN_STATUS_OK;
    static uint8_t printer_command = 0;
    static uint16_t receive_byte_counter = 0;
    static uint16_t packet_data_length = 0, printer_checksum = 0;
    static bool data_commit = false;
    static uint64_t last_print_moment = 0;
    static uint32_t start_ms = 0;
    static uint8_t curr_is_32 = 0;
    
    // Blink every interval ms
    uint32_t curr_time_ms = board_millis();
    if ((curr_time_ms - start_ms) >= (2 * 1000)) {
        start_ms = curr_time_ms;
        curr_is_32 = !curr_is_32;
    }
    
    linkcable_set_is_32(curr_is_32);

    return data_in + 0x11111111;
}
