#include <stddef.h>
#include <stdbool.h>
#include "upkeep_callback.h"

upkeep_callback_t saved_callback = NULL;

void call_upkeep_callback(void) {
    if(saved_callback)
        saved_callback(true);
}

void set_upkeep_callback(upkeep_callback_t callback) {
    saved_callback = callback;
}
