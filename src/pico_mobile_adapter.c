#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <mobile.h>
#include <mobile_inet.h>
#include "hardware/timer.h"
#include "pico_mobile_adapter.h"
#include "socket_impl.h"
#include "io_buffer.h"
#include "gbridge.h"
#include "linkcable.h"
#include "flash_eeprom.h"
#include "useful_qualifiers.h"

#define USE_FLASH
#define DEBUG_MAX_SIZE 0x200
#define IDLE_COMMAND 0xD2

//#define LOG_DIRECT_SEND_RECV
#ifdef LOG_DIRECT_SEND_RECV
#define LOG_BUFFER_SIZE 0x80
#endif
//#define SET_DEFAULT_DNS

static void mobile_validate_relay(void);
static void impl_serial_disable(void *user);
static void impl_serial_enable(void *user, bool mode_32bit);
static bool impl_config_read(void *user, void *dest, const uintptr_t offset, const size_t size);
static bool impl_config_write(void *user, const void *src, const uintptr_t offset, const size_t size);
static void impl_time_latch(void *user, unsigned timer);
static bool impl_time_check_ms(void *user, unsigned timer, unsigned ms);
static void impl_update_number(void *user, enum mobile_number type, const char *number);

#define DNS_DEFAULT_IP 127, 0, 0, 1
#define DNS_DEFAULT_PORT 53
const char default_dns_ip[] = {DNS_DEFAULT_IP};
const uint16_t default_dns_port = DNS_DEFAULT_PORT;

#ifdef LOG_DIRECT_SEND_RECV
uint32_t log_linkcable_buffer_out[LOG_BUFFER_SIZE];
uint32_t log_linkcable_buffer_in[LOG_BUFFER_SIZE];
#endif

//Control Flash Write
bool haveConfigToWrite = false;
uint8_t currentTicks = 0;

bool isLinkCable32 = false;
bool link_cable_data_received = false;

struct mobile_user *mobile;
upkeep_callback saved_callback = NULL;

void call_upkeep_callback(void) {
    if(saved_callback)
        saved_callback();
}

void TIME_SENSITIVE(link_cable_ISR)(void) {
    uint32_t data = linkcable_receive();
#ifdef LOG_DIRECT_SEND_RECV
    for(int i = 0; i < LOG_BUFFER_SIZE - 1; i++)
        log_linkcable_buffer_in[i] = log_linkcable_buffer_in[i + 1];
    log_linkcable_buffer_in[LOG_BUFFER_SIZE - 1] = data;
#endif
    if(isLinkCable32){
        data = mobile_transfer_32bit(mobile->adapter, data);
    }else{
        data = mobile_transfer(mobile->adapter, data);
    }
#ifdef LOG_DIRECT_SEND_RECV
    for(int i = 0; i < LOG_BUFFER_SIZE - 1; i++)
        log_linkcable_buffer_out[i] = log_linkcable_buffer_out[i + 1];
    log_linkcable_buffer_out[LOG_BUFFER_SIZE - 1] = data;
#endif
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
    
#ifdef SET_DEFAULT_DNS
    struct mobile_addr default_dns;
    default_dns._addr4.type = MOBILE_ADDRTYPE_IPV4;
    default_dns._addr4.port = default_dns_port;
    for(int i = 0; i < 4; i++)
        default_dns._addr4.host[i] = default_dns_ip[i];
    mobile_config_set_dns(mobile->adapter, &default_dns, &default_dns);
#endif

    mobile->action = MOBILE_ACTION_NONE;
    mobile->number_user[0] = '\0';
    mobile->number_peer[0] = '\0';
    for (int i = 0; i < MOBILE_MAX_TIMERS; i++)
        mobile->picow_clock_latch[i] = 0;

    mobile_start(mobile->adapter);
    
    mobile_validate_relay();
}

void irqs_disable(void) {
    linkcable_disable();
}

void irqs_enable(void) {
    linkcable_enable();
}

void pico_mobile_loop(void) {
    // Mobile Adapter Main Loop
    mobile_loop(mobile->adapter);

#ifdef USE_FLASH
    // Check if there is any new config to write on Flash
    if(haveConfigToWrite){
        bool can_disable_irqs = can_disable_linkcable_irq();
        if(can_disable_irqs) {
            SaveFlashConfig(mobile->config_eeprom, EEPROM_SIZE);
            haveConfigToWrite = false;
            currentTicks = 0;
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

#ifdef LOG_DIRECT_SEND_RECV
static void print_last_linkcable(void) {
    debug_send(log_linkcable_buffer_in, LOG_BUFFER_SIZE << 2, GBRIDGE_CMD_DEBUG_CHAR);
    debug_send(log_linkcable_buffer_out, LOG_BUFFER_SIZE << 2, GBRIDGE_CMD_DEBUG_CHAR);
}
#endif

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

static bool impl_config_write(void *user, const void *src, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    const uint8_t* src_8 = (const uint8_t *)src;
    for(int i = 0; i < size; i++){
#ifdef USE_FLASH
        if(mobile->config_eeprom[offset + i] != src_8[i])
            haveConfigToWrite = true;
#endif
        mobile->config_eeprom[offset + i] = src_8[i];
    }
    return true;
}

static void impl_time_latch(void *user, unsigned timer) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    mobile->picow_clock_latch[timer] = time_us_64();
}

static bool impl_time_check_ms(void *user, unsigned timer, unsigned ms) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    return ((time_us_64() - mobile->picow_clock_latch[timer]) >= (ms * 1000));
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

