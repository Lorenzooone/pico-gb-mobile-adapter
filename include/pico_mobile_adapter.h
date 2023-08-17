#ifndef PICO_MOBILE_ADAPTER_H_
#define PICO_MOBILE_ADAPTER_H_

#include <mobile.h>

//#define USE_SOCKET

#define EEPROM_SIZE 0x200

#define OFFSET_CONFIG 0
#define OFFSET_MAGB 16
#define OFFSET_SSID MOBILE_CONFIG_SIZE+OFFSET_MAGB
#define OFFSET_PASS OFFSET_SSID+32

#define SOCK_NONE -1
#define SOCK_TCP 1
#define SOCK_UDP 2

struct mobile_user {
    struct mobile_adapter *adapter;
    enum mobile_action action;
    unsigned long picow_clock_latch[MOBILE_MAX_TIMERS];
    uint8_t config_eeprom[EEPROM_SIZE];
#ifdef USE_SOCKET
    struct socket_impl socket[MOBILE_MAX_CONNECTIONS];
#endif
    char number_user[MOBILE_MAX_NUMBER_SIZE + 1];
    char number_peer[MOBILE_MAX_NUMBER_SIZE + 1];
};

void link_cable_ISR(void);
void pico_mobile_init(void);
void pico_mobile_loop(void);

#endif /* PICO_MOBILE_ADAPTER_H_ */
