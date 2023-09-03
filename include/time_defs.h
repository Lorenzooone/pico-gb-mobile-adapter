#ifndef _TIME_DEFS_H_
#define _TIME_DEFS_H_

// Hardware specific timing related functions!
// Change this file to suit your needs!
// IMPLEMENTATION-SPECIFIC

#include "hardware/timer.h"

#define MUSEC(x) (x)
#define MSEC(x) (MUSEC(x) * 1000)
#define SEC(x) (MSEC(x) * 1000)
#define MINS(x) (SEC(x) * 60)
#define HRS(x) (MINS(x) * 60)
#define NSEC(x) (MUSEC(x) / 1000)
#define PSEC(x) (NSEC(x) / 1000)

#define TIME_FUNCTION time_us_64()

typedef uint64_t user_time_t;

#endif /* _TIME_DEFS_H_ */
