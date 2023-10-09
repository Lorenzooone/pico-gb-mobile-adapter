#include <string.h>
#include <mobile.h>

#include "utils.h"

uint64_t read_big_endian(const uint8_t* buffer, size_t size) {
    if(size > 8)
        return 0;
    uint64_t result = 0;
    for(size_t i = 0; i < size; i++) {
        result <<= 8;
        result |= buffer[i];
    }
    return result;
}

void write_big_endian(uint8_t* buffer, uint64_t data, size_t size) {
    if(size > 8)
        return;
    for(int i = 0; i < size; i++) {
        buffer[size - (i + 1)] = data & 0xFF;
        data >>= 8;
    }
}

uint16_t calc_checksum(const uint8_t* buffer, uint32_t size) {
    uint16_t checksum = 0;
    for(int i = 0; i < size; i++)
        checksum += buffer[i];
    return checksum;
}

void set_checksum(const uint8_t* buffer, uint32_t size, uint8_t* checksum_buffer) {
    uint16_t checksum = calc_checksum(buffer, size);
    write_big_endian(checksum_buffer, checksum, 2);
}

bool check_checksum(const uint8_t* buffer, uint32_t size, const uint8_t* checksum_buffer) {
    uint16_t checksum_prepared = read_big_endian(checksum_buffer, 2);
    uint16_t checksum = calc_checksum(buffer, size);
    return checksum == checksum_prepared;
}

unsigned address_write(const struct mobile_addr *addr, unsigned char *buffer)
{
    if (!addr) {
        buffer[0] = MOBILE_ADDRTYPE_NONE;
        return 1;
    } else if (addr->type == MOBILE_ADDRTYPE_IPV4) {
        struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
        buffer[0] = MOBILE_ADDRTYPE_IPV4;
        buffer[1] = (addr4->port >> 8) & 0xFF;
        buffer[2] = (addr4->port >> 0) & 0xFF;
        memcpy(buffer + 3, addr4->host, MOBILE_HOSTLEN_IPV4);
        return 3 + MOBILE_HOSTLEN_IPV4;
    } else if (addr->type == MOBILE_ADDRTYPE_IPV6) {
        struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
        buffer[0] = MOBILE_ADDRTYPE_IPV6;
        buffer[1] = (addr6->port >> 8) & 0xFF;
        buffer[2] = (addr6->port >> 0) & 0xFF;
        memcpy(buffer + 3, addr6->host, MOBILE_HOSTLEN_IPV6);
        return 3 + MOBILE_HOSTLEN_IPV6;
    } else {
        buffer[0] = MOBILE_ADDRTYPE_NONE;
        return 1;
    }
}

unsigned address_read(struct mobile_addr *addr, const unsigned char *buffer, unsigned size)
{
    if (size < 1) return 0;
    if (buffer[0] == MOBILE_ADDRTYPE_NONE) {
        if (addr) {
            addr->type = MOBILE_ADDRTYPE_NONE;
        }
        return 1;
    } else if (buffer[0] == MOBILE_ADDRTYPE_IPV4) {
        if (size < 3 + MOBILE_HOSTLEN_IPV4) return 0;
        if (addr) {
            addr->type = MOBILE_ADDRTYPE_IPV4;
            struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
            addr4->port = buffer[1] << 8 | buffer[2];
            memcpy(addr4->host, buffer + 3, MOBILE_HOSTLEN_IPV4);
        }
        return 3 + MOBILE_HOSTLEN_IPV4;
    } else if (buffer[0] == MOBILE_ADDRTYPE_IPV6) {
        if (size < 3 + MOBILE_HOSTLEN_IPV6) return 0;
        if (addr) {
            addr->type = MOBILE_ADDRTYPE_IPV6;
            struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
            addr6->port = buffer[1] << 8 | buffer[2];
            memcpy(addr6->host, buffer + 3, MOBILE_HOSTLEN_IPV6);
        }
        return 3 + MOBILE_HOSTLEN_IPV6;
    }
    return 0;
}
