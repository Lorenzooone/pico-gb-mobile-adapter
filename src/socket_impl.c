#include <mobile.h>

#include "socket_impl.h"
#include "pico_mobile_adapter.h"
#include "linkcable.h"
#include "gbridge.h"
#include "utils.h"

#define BUF_SIZE 0x80
#define ADDRESS_MAXLEN (3 + MOBILE_HOSTLEN_IPV6)

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

    return (int8_t)buffer[1];
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

    return (int16_t)((buffer[1] << 8) | (buffer[2]));
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
    buffer[4] = 1;
    if(!data)
        buffer[4] = 0;

    send_x_bytes(buffer, 5, true, true, true);

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

    if(!get_x_bytes(data, size, true, false, size, &result_size))
        return -1;

    return sent_size;
}
