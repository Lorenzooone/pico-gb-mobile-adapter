#ifndef UTILS_H_
#define UTILS_H_

#include <mobile.h>

#define MUSEC(x) (x)
#define MSEC(x) (MUSEC(x) * 1000)
#define SEC(x) (MSEC(x) * 1000)

uint16_t calc_checksum(const uint8_t* buffer, uint32_t size);
void set_checksum(const uint8_t* buffer, uint32_t size, uint8_t* checksum_buffer);
bool check_checksum(const uint8_t* buffer, uint32_t size, const uint8_t* checksum_buffer);

unsigned address_write(const struct mobile_addr *addr, unsigned char *buffer);
unsigned address_read(struct mobile_addr *addr, const unsigned char *buffer, unsigned size);

#endif /* UTILS_H_ */
