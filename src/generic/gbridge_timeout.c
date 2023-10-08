#include "gbridge_timeout.h"
#include "time_defs.h"

#define DEFAULT_MAX_RETRIES 4
#define DEFAULT_TIMEOUT 5 // Seconds, for this implementation

static timeout_time_t timeout_time = SEC(DEFAULT_TIMEOUT);
static num_retries_t num_retries = DEFAULT_MAX_RETRIES;

static timeout_time_t last_time_set = 0;
static num_retries_t curr_retries = 0;

num_retries_t get_num_retries(void) {
    return num_retries;
}

timeout_time_t get_timeout_time(void) {
    return timeout_time;
}

enum time_resolution get_timeout_resolution(void) {
    return CURR_BOARD_TIME_RESOLUTION;
}

void set_num_retries(num_retries_t new_num_retries) {
    num_retries = new_num_retries;
}

void set_timeout_time(timeout_time_t new_timeout_time, enum time_resolution new_timeout_resolution) {
    switch(new_timeout_resolution) {
        case RESOLUTION_SECONDS:
            new_timeout_time = SEC(new_timeout_time);
            break;
        case RESOLUTION_MILLI_SECONDS:
            new_timeout_time = MSEC(new_timeout_time);
            break;
        case RESOLUTION_MICRO_SECONDS:
            new_timeout_time = MUSEC(new_timeout_time);
            break;
        case RESOLUTION_NANO_SECONDS:
            new_timeout_time = NSEC(new_timeout_time);
            break;
        case RESOLUTION_PICO_SECONDS:
            new_timeout_time = PSEC(new_timeout_time);
            break;
        case RESOLUTION_MINUTES:
            new_timeout_time = MINS(new_timeout_time);
            break;
        case RESOLUTION_HOURS:
            new_timeout_time = HRS(new_timeout_time);
            break;
        default:
            new_timeout_time = timeout_time;
            break;
    }
    timeout_time = new_timeout_time;
}

void prepare_timeout(void) {
    last_time_set = TIME_FUNCTION;
}

void prepare_failure(void) {
    curr_retries = 0;
}

bool timeout_can_try_again(void) {
    if(!timeout_time)
        return true;
    timeout_time_t curr_time = TIME_FUNCTION;
    if((curr_time - last_time_set) > timeout_time)
        return false;
    return true;
}

bool failed_can_try_again(void) {
    if(!num_retries)
        return true;
    curr_retries++;
    if(curr_retries >= num_retries)
        return false;
    return true;
}
