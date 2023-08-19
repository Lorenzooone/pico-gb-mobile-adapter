#ifndef _LINKCABLE_H_INCLUDE_
#define _LINKCABLE_H_INCLUDE_

#include "hardware/pio.h"

#define STACKSMASHING       1

#define LINKCABLE_PIO       pio0
#define LINKCABLE_SM        0

#define LINKCABLE_BITS      8
#define LINKCABLE_BITS_FULL 32

#define CABLE_PINS_START    0

uint32_t linkcable_receive(void);
void linkcable_send(uint32_t data);
void clean_linkcable_fifos(void);
void linkcable_set_is_32(uint32_t is_32);
void linkcable_disable(void);
void linkcable_enable(void);
void linkcable_reset(bool re_enable);
void linkcable_init(irq_handler_t onReceive);
bool get_linkcable_can_interrupt(void);
bool disable_temporarily_if_timeout(void);

#endif
