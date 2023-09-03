#ifndef PICO_MOBILE_ADAPTER_H_
#define PICO_MOBILE_ADAPTER_H_

#include <mobile.h>
#include "time_defs.h"
#include "upkeep_callback.h"

#define EEPROM_SIZE MOBILE_CONFIG_SIZE

#define DEBUG_PRINT_FUNCTION(x) impl_debug_log(NULL, x)

struct mobile_user {
    struct mobile_adapter *adapter;
    user_time_t clock[MOBILE_MAX_TIMERS];
    uint8_t config_eeprom[EEPROM_SIZE];
    char number_user[MOBILE_MAX_NUMBER_SIZE + 1];
    char number_peer[MOBILE_MAX_NUMBER_SIZE + 1];
    bool started;
    bool automatic_save;
    bool force_save;
};

void init_disable_handler(void);
void handle_disable_request(void);

struct mobile_user* get_mobile_user(void);
void call_upkeep_callback(void);
void link_cable_handler(void);
void impl_debug_log(void *user, const char *line);
bool impl_config_write(void *user, const void *src, const uintptr_t offset, const size_t size);

void pico_mobile_init(upkeep_callback_t callback);
void pico_mobile_loop(bool is_same_core);

#endif /* PICO_MOBILE_ADAPTER_H_ */
