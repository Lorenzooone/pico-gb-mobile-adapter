#include "io_buffer.h"

#define OUT_BUFFER_SIZE 0x100
#define IN_BUFFER_SIZE 0x100

#ifdef BIG_BUFFER
#define DEBUG_OUT_BUFFER_SIZE 0x9000
#else
#define DEBUG_OUT_BUFFER_SIZE 0x800
#endif

struct io_buffer {
    uint8_t *p;
    uint32_t size;
    uint32_t pos_inside;
    uint32_t pos_outside;
};

uint8_t buffer_in[IN_BUFFER_SIZE];
uint8_t buffer_out[OUT_BUFFER_SIZE];
#ifdef DO_SEND_DEBUG
uint8_t debug_buffer_out[DEBUG_OUT_BUFFER_SIZE];
#endif

struct io_buffer b_in = {buffer_in, IN_BUFFER_SIZE, 0, 0};
struct io_buffer b_out = {buffer_out, OUT_BUFFER_SIZE, 0, 0};
#ifdef DO_SEND_DEBUG
struct io_buffer b_debug_out = {debug_buffer_out, DEBUG_OUT_BUFFER_SIZE, 0, 0};
#endif

static uint8_t _get_data_out(bool* success, struct io_buffer* b) {
    *success = 0;
    uint32_t data = 0;
    if(b->pos_inside != b->pos_outside) {
        data = b->p[b->pos_outside++];
        b->pos_outside %= b->size;
        *success = 1;
    }
    return data;
}

uint8_t get_data_out(bool* success) {
    return _get_data_out(success, &b_out);
}

uint8_t get_data_out_debug(bool* success) {
#ifdef DO_SEND_DEBUG
    return _get_data_out(success, &b_debug_out);
#else
    *success = 0;
    return 0;
#endif
}

static uint32_t _set_data_out(const uint8_t* buffer, uint32_t size, uint32_t pos, struct io_buffer* b) {
    for(uint32_t i = pos; i < size; i++) {
        if(((b->pos_inside + 1) % b->size) == b->pos_outside)
                return i;
        b->p[b->pos_inside++] = buffer[i];
        b->pos_inside %= b->size;
    }
    return size;
}

uint32_t set_data_out(const uint8_t* buffer, uint32_t size, uint32_t pos) {
    return _set_data_out(buffer, size, pos, &b_out);
}

uint32_t set_data_out_debug(const uint8_t* buffer, uint32_t size, uint32_t pos) {
#ifdef DO_SEND_DEBUG
    return _set_data_out(buffer, size, pos, &b_debug_out);
#else
    return size;
#endif
}

static uint32_t _available_data_out(struct io_buffer* b) {
    uint32_t result = b->pos_outside;
    if(b->pos_outside <= b->pos_inside)
        result += b->size;
    result -= (b->pos_inside + 1);
    return result;
}

uint32_t available_data_out(void) {
    return _available_data_out(&b_out);
}

uint32_t available_data_out_debug(void) {
#ifdef DO_SEND_DEBUG
    return _available_data_out(&b_debug_out);
#else
    return 0;
#endif
}

static uint32_t _get_data_in(struct io_buffer* b) {
    uint32_t data;
    if(b->pos_inside == b->pos_outside)
        data = -1;
    else {
        data = b->p[b->pos_inside];
        b->pos_inside = (b->pos_inside + 1) % b->size;
    }
    return data;
}

uint32_t get_data_in(void) {
    return _get_data_in(&b_in);
}

static void _set_data_in(uint8_t* buffer, uint32_t size, struct io_buffer* b) {
    for(int i = 0; i < size; i++) {
        b->p[b->pos_outside++] = buffer[i];
        b->pos_outside %= b->size;
    }
}

void set_data_in(uint8_t* buffer, uint32_t size) {
    _set_data_in(buffer, size, &b_in);
}

