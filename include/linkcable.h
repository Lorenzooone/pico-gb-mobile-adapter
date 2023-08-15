#ifndef _LINKCABLE_H_INCLUDE_
#define _LINKCABLE_H_INCLUDE_

#include "hardware/pio.h"

#define STACKSMASHING       1

#define LINKCABLE_PIO       pio0
#define LINKCABLE_SM        0

#define LINKCABLE_BITS      8
#define LINKCABLE_BITS_FULL 32

#define CABLE_PINS_START    0

static inline uint32_t linkcable_receive(void) {
    return pio_sm_get(LINKCABLE_PIO, LINKCABLE_SM);
}

static inline void linkcable_send(uint32_t data) {
    pio_sm_put(LINKCABLE_PIO, LINKCABLE_SM, data);
}

void linkcable_set_is_32(uint32_t is_32);
void linkcable_reset(void);
void linkcable_init(irq_handler_t onReceive);

#endif
