#include <string.h>
#include <mobile.h>

#include "socket_impl.h"
#include "pico_mobile_adapter.h"
#include "linkcable.h"
#include "gbridge.h"

#define BUF_SIZE 0x80
#define ADDRESS_MAXLEN (3 + MOBILE_HOSTLEN_IPV6)

static unsigned address_write(const struct mobile_addr *addr, unsigned char *buffer)
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

static unsigned address_read(struct mobile_addr *addr, const unsigned char *buffer, unsigned size)
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

static bool get_x_bytes(uint8_t* buffer, uint32_t size, bool run_callback) {
    while(1) {
        if(run_callback)
            call_upkeep_callback();
        for(uint32_t i = 0; i < size; i++) {
            uint32_t data_read = get_data_in();
            if(data_read == -1) {
                if(i == 0)
                    break;
                else
                    return false;
            }
            else
                buffer[i] = data_read;
        }
    }

    return true;
}

bool impl_sock_open(void *user, unsigned conn, enum mobile_socktype type, enum mobile_addrtype addrtype, unsigned bindport)
{
    uint8_t buffer[BUF_SIZE];
    (void)user;
    buffer[0] = GBRIDGE_PROT_MA_CMD_OPEN;
    buffer[1] = conn;
    buffer[2] = type;
    buffer[3] = addrtype;
    buffer[4] = (bindport >> 8) & 0xFF;
    buffer[5] = (bindport >> 0) & 0xFF;
    
    set_data_out(buffer, 6);
    
    if(!get_x_bytes(buffer, 3, 1))
        return false;

    const struct gbridge_data* recv_data = (const struct gbridge_data*)buffer;
    if (recv_data->size != 2)
        return false;
    if (recv_data->cmd != GBRIDGE_PROT_MA_CMD_OPEN)
        return false;
    bool res = recv_data->buffer[0] != 0;
    return res;
}

void impl_sock_close(void *user, unsigned conn)
{
    uint8_t buffer[BUF_SIZE];
    (void)user;
    buffer[0] = GBRIDGE_PROT_MA_CMD_CLOSE;
    
    set_data_out(buffer, 2);
    
    if(!get_x_bytes(buffer, 2, 1))
        return;

    const struct gbridge_data* recv_data = (const struct gbridge_data*)buffer;
    if (recv_data->size != 1)
        return;
    if (recv_data->cmd != GBRIDGE_PROT_MA_CMD_OPEN)
        return;
}

