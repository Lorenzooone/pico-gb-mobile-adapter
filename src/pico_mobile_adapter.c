#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <mobile.h>
#include <mobile_inet.h>
#include "hardware/timer.h"
#include "pico_mobile_adapter.h"
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
#if 0
//Callbacks
static bool impl_sock_open(void *user, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport);
static void impl_sock_close(void *user, unsigned conn);
static int impl_sock_connect(void *user, unsigned conn, const struct mobile_addr *addr);
static int impl_sock_send(void *user, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr);
static int impl_sock_recv(void *user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr);
static bool impl_sock_listen(void *user, unsigned conn);
static bool impl_sock_accept(void *user, unsigned conn);
#endif
static void impl_update_number(void *user, enum mobile_number type, const char *number);

//Wi-Fi Controllers
bool isConnectedWiFi = false;
char WiFiSSID[28] = "WiFi_Network";
char WiFiPASS[28] = "P@$$w0rd";

//Control Flash Write
bool haveConfigToWrite = false;
bool startWriteConfig = false;
uint8_t currentTicks = 0;

bool isLinkCable32 = false;
bool link_cable_data_received = false;

struct mobile_user *mobile;

uint32_t buffer_out[OUT_BUFFER_SIZE];
uint32_t buffer_in[IN_BUFFER_SIZE];

uint32_t buffer_pos_out_inside;
uint32_t buffer_pos_out_outside;

uint32_t buffer_pos_in_inside;
uint32_t buffer_pos_in_outside;

uint32_t get_data_out(bool* success) {
    *success = 0;
    uint32_t data = 0;
    if(buffer_pos_out_inside != buffer_pos_out_outside) {
        data = buffer_out[buffer_pos_out_outside++];
        buffer_pos_out_outside %= OUT_BUFFER_SIZE;
        *success = 1;
    }
    return data;
}

static void set_data_out(uint32_t data) {
    buffer_out[buffer_pos_out_inside] = data;
    buffer_pos_out_inside = (buffer_pos_out_inside + 1) % OUT_BUFFER_SIZE;
}

static uint32_t get_data_in(void) {
    uint32_t data;
    if(buffer_pos_in_inside == buffer_pos_in_outside)
        data = IDLE_COMMAND;
    else {
        data = buffer_in[buffer_pos_in_inside];
        buffer_pos_in_inside = (buffer_pos_in_inside + 1) % IN_BUFFER_SIZE;
    }
    return data;
}

void set_data_in(uint32_t data) {
    buffer_in[buffer_pos_in_outside++] = data;
    buffer_pos_in_outside %= IN_BUFFER_SIZE;
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
    mobile->callback = callback;

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
#if 0
    mobile_def_sock_open(mobile->adapter, impl_sock_open);
    mobile_def_sock_close(mobile->adapter, impl_sock_close);
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
#ifdef USE_SOCKET
    for (int i = 0; i < MOBILE_MAX_CONNECTIONS; i++){
        mobile->socket[i].tcp_pcb = NULL;
        mobile->socket[i].udp_pcb = NULL;
        mobile->socket[i].sock_addr = -1;
        mobile->socket[i].sock_type = SOCK_NONE;
        memset(mobile->socket[i].udp_remote_srv,0x00,sizeof(mobile->socket[i].udp_remote_srv));
        mobile->socket[i].udp_remote_port = 0;
        mobile->socket[i].client_status = false;
        memset(mobile->socket[i].buffer_rx,0x00,sizeof(mobile->socket[i].buffer_rx));
        memset(mobile->socket[i].buffer_tx,0x00,sizeof(mobile->socket[i].buffer_tx));
        mobile->socket[i].buffer_rx_len = 0;
        mobile->socket[i].buffer_tx_len = 0;
    }
#endif

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

#if 0

//Callbacks
static bool impl_sock_open(void *user, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport){
    struct mobile_user *mobile = (struct mobile_user *)user;
    // printf("mobile_impl_sock_open\n");
    return socket_impl_open(&mobile->socket[conn], socktype, addrtype, bindport);
}

static void impl_sock_close(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    // printf("mobile_impl_sock_close\n");
    return socket_impl_close(&mobile->socket[conn]);
}

static int impl_sock_connect(void *user, unsigned conn, const struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;
    // printf("mobile_impl_sock_connect\n"); 
    return socket_impl_connect(&mobile->socket[conn], addr);
}

static int impl_sock_send(void *user, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;
    // printf("mobile_impl_sock_send\n");
    return socket_impl_send(&mobile->socket[conn], data, size, addr);
}

static int impl_sock_recv(void *user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;    
    // printf("mobile_impl_sock_recv\n");
    return socket_impl_recv(&mobile->socket[conn], data, size, addr);
}

static bool impl_sock_listen(void *user, unsigned conn){ 
    struct mobile_user *mobile = (struct mobile_user *)user;
    // printf("mobile_impl_sock_listen\n");
    return socket_impl_listen(&mobile->socket[conn]);
}

static bool impl_sock_accept(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    // printf("mobile_impl_sock_accept\n"); 
    return socket_impl_accept(&mobile->socket[conn]);
}

#endif

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

