#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <mobile.h>
#include "hardware/timer.h"
#include "pico_mobile_adapter.h"
#include "socket_impl.h"
#include "io_buffer.h"
#include "gbridge.h"
#include "linkcable.h"
#include "flash_eeprom.h"
#include "useful_qualifiers.h"
#include "utils.h"
#include "sync.h"

#define DEBUG_MAX_SIZE 0x200

static void mobile_validate_relay(void);
static void impl_serial_disable(void *user);
static void impl_serial_enable(void *user, bool mode_32bit);
static bool impl_config_read(void *user, void *dest, const uintptr_t offset, const size_t size);
static void impl_time_latch(void *user, unsigned timer);
static bool impl_time_check_ms(void *user, unsigned timer, unsigned ms);
static void impl_update_number(void *user, enum mobile_number type, const char *number);

#define DNS_DEFAULT_IP 127, 0, 0, 1
#define DNS_DEFAULT_PORT 53
const char default_dns_ip[] = {DNS_DEFAULT_IP};
const uint16_t default_dns_port = DNS_DEFAULT_PORT;

//Control Flash Write
bool haveConfigToWrite = false;
static uint64_t time_last_config_edit = 0;

struct mobile_user *mobile = NULL;
upkeep_callback saved_callback = NULL;
sync_t ack_disable;
bool isLinkCable32 = false;

void init_disable_handler(void) {
    init_sync(&ack_disable);
}

void TIME_SENSITIVE(handle_disable_request)(void) {   
    ack_sync_req(&ack_disable);
}

void call_upkeep_callback(void) {
    if(saved_callback)
        saved_callback(true);
}

void TIME_SENSITIVE(link_cable_ISR)(void) {
    uint32_t data = linkcable_receive();
    if(isLinkCable32){
        data = mobile_transfer_32bit(mobile->adapter, data);
    }else{
        data = mobile_transfer(mobile->adapter, data);
    }
    clean_linkcable_fifos();
    linkcable_send(data);
}

void pico_mobile_init(upkeep_callback callback) {
    //Libmobile Variables
    mobile = malloc(sizeof(struct mobile_user));
    saved_callback = callback;

    memset(mobile->config_eeprom,0x00,sizeof(mobile->config_eeprom));
#ifdef USE_FLASH
    ReadFlashConfig(mobile->config_eeprom, EEPROM_SIZE);
#endif

    // Initialize mobile library
    mobile->adapter = mobile_new(mobile);
    mobile_def_debug_log(mobile->adapter, impl_debug_log);
    mobile_def_serial_disable(mobile->adapter, impl_serial_disable);
    mobile_def_serial_enable(mobile->adapter, impl_serial_enable);
    mobile_def_config_read(mobile->adapter, impl_config_read);
    mobile_def_config_write(mobile->adapter, impl_config_write);
    mobile_def_time_latch(mobile->adapter, impl_time_latch);
    mobile_def_time_check_ms(mobile->adapter, impl_time_check_ms);
    mobile_def_sock_open(mobile->adapter, impl_sock_open);
    mobile_def_sock_close(mobile->adapter, impl_sock_close);
    mobile_def_sock_connect(mobile->adapter, impl_sock_connect);
    mobile_def_sock_listen(mobile->adapter, impl_sock_listen);
    mobile_def_sock_accept(mobile->adapter, impl_sock_accept);
    mobile_def_sock_send(mobile->adapter, impl_sock_send);
    mobile_def_sock_recv(mobile->adapter, impl_sock_recv);
    mobile_def_update_number(mobile->adapter, impl_update_number);

    mobile_config_load(mobile->adapter);

    mobile->started = false;
    mobile->number_user[0] = '\0';
    mobile->number_peer[0] = '\0';
    for (int i = 0; i < MOBILE_MAX_TIMERS; i++)
        mobile->clock[i] = 0;

    mobile_start(mobile->adapter);
    mobile->started = true;

    mobile_validate_relay();
}

void pico_mobile_loop(bool is_same_core) {
    // Mobile Adapter Main Loop
    mobile_loop(mobile->adapter);

#ifdef USE_FLASH
    // Check if there is any new config to write on Flash
    if(haveConfigToWrite){
        bool can_disable_irqs = can_disable_linkcable_irq();
        uint64_t curr_time_last_config_edit = time_last_config_edit;
        if(((time_us_64() - curr_time_last_config_edit) > SEC(1)) && can_disable_irqs) {
            bool prev_state = linkcable_is_enabled();
            if((!is_same_core) && prev_state)
                linkcable_disable();
            SaveFlashConfig(mobile->config_eeprom, EEPROM_SIZE);
            haveConfigToWrite = false;
            if((!is_same_core) && prev_state)
                linkcable_enable();
        }
    }
#endif
}

static void mobile_validate_relay(){
    struct mobile_addr relay = {0};    
    mobile_config_get_relay(mobile->adapter, &relay);
}

struct mobile_user* get_mobile_user(void) {
    return mobile;
}

void impl_debug_log(void *user, const char *line){
    (void)user;
#ifdef DO_SEND_DEBUG
    uint8_t debug_buffer[DEBUG_MAX_SIZE];
    uint32_t printed = snprintf(debug_buffer, DEBUG_MAX_SIZE - 1, "%s\n", line);
    debug_buffer[DEBUG_MAX_SIZE - 1] = 0;
    debug_send(debug_buffer, printed + 1, GBRIDGE_CMD_DEBUG_LINE);
#else
    (void)line;
#endif
}

static void impl_serial_disable(void *user) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    linkcable_disable();
    wait_for_sync(&ack_disable);
    print_last_linkcable();
}

static void impl_serial_enable(void *user, bool mode_32bit) {
    struct mobile_user *mobile = (struct mobile_user *)user;

    isLinkCable32 = mode_32bit;
    linkcable_set_is_32(isLinkCable32);
    linkcable_enable();
}

static bool impl_config_read(void *user, void *dest, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    for(int i = 0; i < size; i++){
        ((char *)dest)[i] = (char)mobile->config_eeprom[offset + i];
    }
    return true;
}

bool impl_config_write(void *user, const void *src, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    const uint8_t* src_8 = (const uint8_t *)src;
    for(int i = 0; i < size; i++){
#ifdef USE_FLASH
        if(mobile->config_eeprom[offset + i] != src_8[i])
            haveConfigToWrite = true;
#endif
        mobile->config_eeprom[offset + i] = src_8[i];
    }
    if(haveConfigToWrite)
        time_last_config_edit = time_us_64();
    return true;
}

static void impl_time_latch(void *user, unsigned timer) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    mobile->clock[timer] = time_us_64();
}

static bool impl_time_check_ms(void *user, unsigned timer, unsigned ms) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    return ((time_us_64() - mobile->clock[timer]) >= MSEC(ms));
}

static void impl_update_number(void *user, enum mobile_number type, const char *number){
    struct mobile_user *mobile = (struct mobile_user *)user;
    char *dest = NULL;

    switch (type) {
        case MOBILE_NUMBER_USER: dest = mobile->number_user; break;
        case MOBILE_NUMBER_PEER: dest = mobile->number_peer; break;
        default: assert(false); return;
    }

    if (number) {
        strncpy(dest, number, MOBILE_MAX_NUMBER_SIZE);
        dest[MOBILE_MAX_NUMBER_SIZE] = '\0';
    } else {
        dest[0] = '\0';
    }
}

