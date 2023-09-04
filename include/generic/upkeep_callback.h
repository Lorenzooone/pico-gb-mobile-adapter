#ifndef UPKEEP_CALLBACK_H_
#define UPKEEP_CALLBACK_H_

#include <stdbool.h>

typedef void (*upkeep_callback_t) (bool);

void call_upkeep_callback(void);
void set_upkeep_callback(upkeep_callback_t callback);

#endif /* UPKEEP_CALLBACK_H_ */
