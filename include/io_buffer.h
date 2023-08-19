#ifndef IO_BUFFER_H_
#define IO_BUFFER_H_

#include <stdint.h>
#include <stdbool.h>

uint8_t get_data_out(bool* success);
uint8_t get_data_out_debug(bool* success);
uint32_t set_data_out(const uint8_t* buffer, uint32_t size, uint32_t pos);
uint32_t set_data_out_debug(const uint8_t* buffer, uint32_t size, uint32_t pos);
uint32_t get_data_in(void);
void set_data_in(uint8_t* buffer, uint32_t size);
uint32_t available_data_out(void);
uint32_t available_data_out_debug(void);

#endif /* IO_BUFFER_H_ */
