#ifndef UTILS_H_
#define UTILS_H_

#include <mobile.h>

uint16_t calc_checksum(const uint8_t* buffer, uint32_t size);
void set_checksum(const uint8_t* buffer, uint32_t size, uint8_t* checksum_buffer);
bool check_checksum(const uint8_t* buffer, uint32_t size, const uint8_t* checksum_buffer);

unsigned address_write(const struct mobile_addr *addr, unsigned char *buffer);
unsigned address_read(struct mobile_addr *addr, const unsigned char *buffer, unsigned size);

#endif /* UTILS_H_ */
