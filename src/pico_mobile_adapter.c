#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <mobile.h>
#include <mobile_inet.h>
#include "hardware/timer.h"
#include "pico_mobile_adapter.h"
#include "socket_impl.h"
#include "linkcable.h"

//#define USE_FLASH
#define OUT_BUFFER_SIZE 0x100
#define IN_BUFFER_SIZE 0x100
#define IDLE_COMMAND 0xD2

static void mobile_validate_relay(void);
static void impl_debug_log(void *user, const char *line);
static void impl_serial_disable(void *user);
static void impl_serial_enable(void *user, bool mode_32bit);
static bool impl_config_read(void *user, void *dest, const uintptr_t offset, const size_t size);
static bool impl_config_write(void *user, const void *src, const uintptr_t offset, const size_t size);
static void impl_time_latch(void *user, unsigned timer);
static bool impl_time_check_ms(void *user, unsigned timer, unsigned ms);
static void impl_update_number(void *user, enum mobile_number type, const char *number);

//Control Flash Write
bool haveConfigToWrite = false;
bool startWriteConfig = false;
uint8_t currentTicks = 0;

bool isLinkCable32 = false;
bool link_cable_data_received = false;

struct mobile_user *mobile;
upkeep_callback saved_callback;

uint8_t buffer_out[OUT_BUFFER_SIZE];
uint8_t buffer_in[IN_BUFFER_SIZE];

uint32_t buffer_pos_out_inside;
uint32_t buffer_pos_out_outside;

uint32_t buffer_pos_in_inside;
uint32_t buffer_pos_in_outside;

uint8_t get_data_out(bool* success) {
    *success = 0;
    uint32_t data = 0;
    if(buffer_pos_out_inside != buffer_pos_out_outside) {
        data = buffer_out[buffer_pos_out_outside++];
        buffer_pos_out_outside %= OUT_BUFFER_SIZE;
        *success = 1;
    }
    return data;
}

void set_data_out(uint8_t* buffer, uint32_t size) {
    for(int i = 0; i < size; i++) {
        buffer_out[buffer_pos_out_inside++] = buffer[i];
        buffer_pos_out_inside %= OUT_BUFFER_SIZE;
    }
}

uint32_t get_data_in(void) {
    uint32_t data;
    if(buffer_pos_in_inside == buffer_pos_in_outside)
        data = -1;
    else {
        data = buffer_in[buffer_pos_in_inside];
        buffer_pos_in_inside = (buffer_pos_in_inside + 1) % IN_BUFFER_SIZE;
    }
    return data;
}

void set_data_in(uint8_t* buffer, uint32_t size) {
    for(int i = 0; i < size; i++) {
        buffer_in[buffer_pos_in_outside++] = buffer[i];
        buffer_pos_in_outside %= IN_BUFFER_SIZE;
    }
}

void call_upkeep_callback(void) {
    saved_callback();
}

void link_cable_ISR(void) {
    uint32_t data;
    if(isLinkCable32){
        data = mobile_transfer_32bit(mobile->adapter, linkcable_receive());
    }else{
        data = mobile_transfer(mobile->adapter, linkcable_receive());
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
    ReadFlashConfig(mobile->config_eeprom, WiFiSSID, WiFiPASS);
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
#if 0
    mobile_def_sock_connect(mobile->adapter, impl_sock_connect);
    mobile_def_sock_listen(mobile->adapter, impl_sock_listen);
    mobile_def_sock_accept(mobile->adapter, impl_sock_accept);
    mobile_def_sock_send(mobile->adapter, impl_sock_send);
    mobile_def_sock_recv(mobile->adapter, impl_sock_recv);
#endif
    mobile_def_update_number(mobile->adapter, impl_update_number);

    mobile_config_load(mobile->adapter);

#ifdef USE_FLASH
    BootMenuConfig(mobile,WiFiSSID,WiFiPASS);
#endif

    mobile->action = MOBILE_ACTION_NONE;
    mobile->number_user[0] = '\0';
    mobile->number_peer[0] = '\0';
    for (int i = 0; i < MOBILE_MAX_TIMERS; i++)
        mobile->picow_clock_latch[i] = 0;

    mobile_start(mobile->adapter);
    
    mobile_validate_relay();
}

void pico_mobile_loop(void) {
    // Mobile Adapter Main Loop
    mobile_loop(mobile->adapter);

#ifdef USE_FLASH
    // Check if there is any new config to write on Flash
    if(haveConfigToWrite){
        bool checkSockStatus = false;
        for (int i = 0; i < MOBILE_MAX_CONNECTIONS; i++){
            if(mobile->socket[i].tcp_pcb || mobile->socket[i].udp_pcb){
                checkSockStatus = true;
                break;
            } 
        }
        if(!checkSockStatus && startWriteConfig){
            SaveFlashConfig(mobile->config_eeprom);
            haveConfigToWrite = false;
            startWriteConfig = false;
            currentTicks = 0;
        }
    }
#endif
}

static void mobile_validate_relay(){
    struct mobile_addr relay = {0};    
    mobile_config_get_relay(mobile->adapter, &relay);
}

static void impl_debug_log(void *user, const char *line){
    (void)user;
    fprintf(stderr, "%s\n", line);
}

static void impl_serial_disable(void *user) {
    struct mobile_user *mobile = (struct mobile_user *)user;

#ifdef USE_FLASH
    if(haveConfigToWrite && !startWriteConfig){
        if(currentTicks >= TICKSWAIT){
            startWriteConfig = true;
        }else{
            currentTicks++;
        }
    }
#endif
    linkcable_disable(); 
}

static void impl_serial_enable(void *user, bool mode_32bit) {
    struct mobile_user *mobile = (struct mobile_user *)user;

    isLinkCable32 = mode_32bit;
    linkcable_set_is_32(mode_32bit);
    linkcable_enable();
}

static bool impl_config_read(void *user, void *dest, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    for(int i = 0; i < size; i++){
        ((char *)dest)[i] = (char)mobile->config_eeprom[OFFSET_MAGB + offset + i];
    }
    return true;
}

static bool impl_config_write(void *user, const void *src, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    for(int i = 0; i < size; i++){
        mobile->config_eeprom[OFFSET_MAGB + offset + i] = ((uint8_t *)src)[i];
    }
#ifdef USE_FLASH
    haveConfigToWrite = true;
#endif
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

