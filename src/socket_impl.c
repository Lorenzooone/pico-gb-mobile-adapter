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

bool impl_sock_open(void *user, unsigned conn, enum mobile_socktype type, enum mobile_addrtype addrtype, unsigned bindport)
{
    uint8_t buffer[BUF_SIZE];
    uint32_t result_size;
    (void)user;
    uint8_t cmd = GBRIDGE_PROT_MA_CMD_OPEN;
    buffer[0] = cmd;
    buffer[1] = conn;
    buffer[2] = type;
    buffer[3] = addrtype;
    buffer[4] = (bindport >> 8) & 0xFF;
    buffer[5] = bindport & 0xFF;
    
    send_x_bytes(buffer, 6, true, true, true);
    
    if(!get_x_bytes(buffer, 2, true, true, BUF_SIZE, &result_size))
        return false;

    if (result_size != 2)
        return false;
    if (buffer[0] != cmd)
        return false;

    return buffer[1];
}

void impl_sock_close(void *user, unsigned conn)
{
    uint8_t buffer[BUF_SIZE];
    uint32_t result_size;
    (void)user;
    uint8_t cmd = GBRIDGE_PROT_MA_CMD_CLOSE;
    buffer[0] = cmd;
    buffer[1] = conn;
    
    send_x_bytes(buffer, 2, true, true, true);
    
    if(!get_x_bytes(buffer, 1, true, true, BUF_SIZE, &result_size))
        return;

    if (result_size != 1)
        return;
    if (buffer[0] != cmd)
        return;
}

int impl_sock_connect(void* user, unsigned conn, const struct mobile_addr *addr)
{
    uint8_t buffer[BUF_SIZE];
    uint32_t result_size;
    (void)user;
    uint8_t cmd = GBRIDGE_PROT_MA_CMD_CONNECT;
    buffer[0] = cmd;
    buffer[1] = conn;
    unsigned addrlen = address_write(addr, buffer + 2);
    
    send_x_bytes(buffer, addrlen + 2, true, true, true);

    if(!get_x_bytes(buffer, 2, true, true, BUF_SIZE, &result_size))
        return -1;

    if (result_size != 2)
        return -1;
    if (buffer[0] != cmd)
        return -1;

    return (char)buffer[1];
}

bool impl_sock_listen(void* user, unsigned conn)
{
    uint8_t buffer[BUF_SIZE];
    uint32_t result_size;
    (void)user;
    uint8_t cmd = GBRIDGE_PROT_MA_CMD_LISTEN;
    buffer[0] = cmd;
    buffer[1] = conn;

    send_x_bytes(buffer, 2, true, true, true);

    if(!get_x_bytes(buffer, 2, true, true, BUF_SIZE, &result_size))
        return false;

    if (result_size != 2)
        return false;
    if (buffer[0] != cmd)
        return false;

    return buffer[1];
}

bool impl_sock_accept(void* user, unsigned conn)
{
    uint8_t buffer[BUF_SIZE];
    uint32_t result_size;
    (void)user;
    uint8_t cmd = GBRIDGE_PROT_MA_CMD_ACCEPT;
    buffer[0] = cmd;
    buffer[1] = conn;

    send_x_bytes(buffer, 2, true, true, true);

    if(!get_x_bytes(buffer, 2, true, true, BUF_SIZE, &result_size))
        return false;

    if (result_size != 2)
        return false;
    if (buffer[0] != cmd)
        return false;

    return buffer[1];
}

int impl_sock_send(void* user, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr)
{
    uint8_t buffer[BUF_SIZE];
    uint32_t result_size;
    (void)user;
    uint8_t cmd = GBRIDGE_PROT_MA_CMD_SEND;
    buffer[0] = cmd;
    buffer[1] = conn;
    unsigned addrlen = address_write(addr, buffer + 2);
    
    send_x_bytes(buffer, addrlen + 2, true, true, true);

    send_x_bytes(data, size, true, true, false);

    if(!get_x_bytes(buffer, 3, true, true, BUF_SIZE, &result_size))
        return -1;

    if (result_size != 3)
        return -1;
    if (buffer[0] != cmd)
        return -1;

    return (buffer[1] << 8) | (buffer[2]);
}

int impl_sock_recv(void* user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr)
{
    uint8_t buffer[BUF_SIZE];
    uint32_t result_size;
    (void)user;
    uint8_t cmd = GBRIDGE_PROT_MA_CMD_RECV;
    buffer[0] = cmd;
    buffer[1] = conn;
    buffer[2] = size >> 8;
    buffer[3] = size & 0xFF;
    
    send_x_bytes(buffer, 4, true, true, true);

    if(!get_x_bytes(buffer, 3, true, true, BUF_SIZE, &result_size))
        return -1;

    if (result_size < 3)
        return -1;
    if (buffer[0] != cmd)
        return -1;

    int16_t sent_size = (buffer[1] << 8) | (buffer[2]);
    unsigned recv_addrlen = address_read(addr, buffer + 3, result_size - 3);
    if (!recv_addrlen)
        return -1;
    if (result_size != recv_addrlen + 3)
        return -1;
    if (sent_size <= 0)
        return sent_size;

    size = sent_size;

    if(!get_x_bytes(buffer, size, true, false, size, &result_size))
        return -1;

    return sent_size;
}
