#ifndef PICO_MOBILE_ADAPTER_H_
#define PICO_MOBILE_ADAPTER_H_

#include <mobile.h>

#define EEPROM_SIZE MOBILE_CONFIG_SIZE

#define SOCK_NONE -1
#define SOCK_TCP 1
#define SOCK_UDP 2

#define DEBUG_PRINT_FUNCTION(x) impl_debug_log(NULL, x)

typedef void (*upkeep_callback) (void);

struct mobile_user {
    struct mobile_adapter *adapter;
    enum mobile_action action;
    unsigned long picow_clock_latch[MOBILE_MAX_TIMERS];
    uint8_t config_eeprom[EEPROM_SIZE];
    char number_user[MOBILE_MAX_NUMBER_SIZE + 1];
    char number_peer[MOBILE_MAX_NUMBER_SIZE + 1];
};

struct mobile_user* get_mobile_user(void);
void call_upkeep_callback(void);
void link_cable_ISR(void);
void pico_mobile_init(upkeep_callback callback);
void pico_mobile_loop(void);
void impl_debug_log(void *user, const char *line);

#endif /* PICO_MOBILE_ADAPTER_H_ */
