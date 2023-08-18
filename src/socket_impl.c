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

static uint16_t calc_checksum(const uint8_t* buffer, uint32_t size) {
    uint16_t checksum = 0;
    for(int i = 0; i < size; i++)
        checksum += buffer[i];
    return checksum;
}

static void set_checksum(const uint8_t* buffer, uint32_t size, uint8_t* checksum_buffer) {
    uint16_t checksum = calc_checksum(buffer, size);
    checksum_buffer[0] = checksum >> 8;
    checksum_buffer[1] = checksum & 0xFF;
}

static bool check_checksum(const uint8_t* buffer, uint32_t size, uint8_t* checksum_buffer) {
    uint16_t checksum_prepared = (checksum_buffer[0] << 8) | checksum_buffer[1];
    uint16_t checksum = calc_checksum(buffer, size);
    return checksum == checksum_prepared;
}

static bool get_section(uint8_t* buffer, uint32_t size, bool run_callback, bool is_cmd) {
    uint32_t pos = 0;
    while(pos < size) {
        if(run_callback)
            call_upkeep_callback();
        for(; pos < size; pos++) {
            uint32_t data_read = get_data_in();
            if(data_read == -1)
                    break;
            else
                buffer[pos] = data_read;
        }
    }
    return true;
}

static bool get_x_bytes(uint8_t* buffer, uint32_t size, bool run_callback, bool expected_data, uint32_t limit) {
    bool try = true;
    uint8_t cmd_data[] = {0, 0, 0};
    uint8_t checksum_data[] = {0, 0};
    uint32_t cmd_len = 3;
    uint8_t wanted_cmd = GBRIDGE_CMD_STREAM_PC;
    if(expected_data) {
        cmd_len = 2;
        wanted_cmd = GBRIDGE_CMD_DATA_PC;
    }

    while(try) {
        if(!get_section(cmd_data, cmd_len, run_callback, true))
            return false;
        if(wanted_cmd != cmd_data[0])
            return false;
        uint32_t cmd_size = (cmd_data[1] << 8) | cmd_data[2];
        if(expected_data)
            cmd_size = cmd_data[1];
        if(cmd_size > limit)
            cmd_size = limit;
        if(size > cmd_size)
            cmd_size = size;
        if(!get_section(buffer, cmd_size, run_callback, false))
            return false;
        if(!get_section(checksum_data, 2, run_callback, false))
            return false;
        if(check_checksum(buffer, cmd_size, checksum_data))
            try = false;
        uint8_t buffer_send[] = {GBRIDGE_CMD_REPLY_F | wanted_cmd};
        if(try)
            buffer_send[0] += 1;
        if(run_callback) {
            uint32_t pos = 0;
            while(!pos) {
                pos = set_data_out(buffer_send, 1, pos);
                call_upkeep_callback();
            }
        }
    }
    return true;
}

static bool send_section(const uint8_t* buffer, uint32_t size, uint32_t* pos) {
    *pos = set_data_out(buffer, size, *pos);
    if((*pos) == size) {
        *pos = 0;
        return true;
    }
    return false;
}

static void send_x_bytes(const uint8_t* buffer, uint32_t size, bool run_callback, bool send_checksum, bool is_data) {
    uint8_t checksum_buffer[2];
    uint8_t cmd = GBRIDGE_CMD_STREAM;
    uint8_t cmd_length = 3;
    if(is_data) {
        cmd = GBRIDGE_CMD_DATA;
        cmd_length = 2;
    }
    uint8_t command_buffer[3] = {cmd, (size >> 8) & 0xFF, size & 0xFF};
    if(is_data)
        command_buffer[1] = size & 0xFF;
    set_checksum(buffer, size, checksum_buffer);
    bool try = true;
    while(try) {
        uint32_t pos = 0;
        bool completed = false;
        bool completed_cmd = false;
        bool completed_data = false;
        bool completed_checksum = true;
        if(send_checksum)
            completed_checksum = false;
        while(!completed) {
            if(!completed_cmd)
                completed_cmd = send_section(command_buffer, cmd_length, &pos);
            if(completed_cmd && (!completed_data))
                completed_data = send_section(buffer, size, &pos);
            if(completed_data && (!completed_checksum))
                completed_checksum = send_section(checksum_buffer, 2, &pos);
            if(completed_checksum)
                completed = true;
            if(run_callback)
                call_upkeep_callback();
        }

        if(run_callback) {
            uint32_t answer = -1;
            while(answer == -1) {
                call_upkeep_callback();
                answer = get_data_in();
                if(answer == (GBRIDGE_CMD_REPLY_F | cmd))
                    try = false;
                // Checksum failure, retry
                else if(answer == (GBRIDGE_CMD_REPLY_F | (cmd + 1)));
                else
                    answer = -1;
            }
        }
        else
            try = false;
    }
}

bool impl_sock_open(void *user, unsigned conn, enum mobile_socktype type, enum mobile_addrtype addrtype, unsigned bindport)
{
    uint8_t buffer[BUF_SIZE];
    (void)user;
    uint8_t cmd = GBRIDGE_PROT_MA_CMD_OPEN;
    buffer[0] = cmd;
    buffer[1] = conn;
    buffer[2] = type;
    buffer[3] = addrtype;
    buffer[4] = (bindport >> 8) & 0xFF;
    buffer[5] = (bindport >> 0) & 0xFF;
    
    send_x_bytes(buffer, 6, true, true, true);
    
    if(!get_x_bytes(buffer, 3, true, true, BUF_SIZE))
        return false;

    const struct gbridge_data* recv_data = (const struct gbridge_data*)buffer;
    if (recv_data->size != 2)
        return false;
    if (recv_data->cmd != cmd)
        return false;
    bool res = recv_data->buffer[0] != 0;
    return res;
}

void impl_sock_close(void *user, unsigned conn)
{
    uint8_t buffer[BUF_SIZE];
    (void)user;
    uint8_t cmd = GBRIDGE_PROT_MA_CMD_CLOSE;
    buffer[0] = cmd;
    
    send_x_bytes(buffer, 2, true, true, true);
    
    if(!get_x_bytes(buffer, 2, true, true, BUF_SIZE))
        return;

    const struct gbridge_data* recv_data = (const struct gbridge_data*)buffer;
    if (recv_data->size != 1)
        return;
    if (recv_data->cmd != cmd)
        return;
}

int impl_sock_connect(void* user, unsigned conn, const struct mobile_addr *addr)
{
    uint8_t buffer[BUF_SIZE];
    (void)user;
    uint8_t cmd = GBRIDGE_PROT_MA_CMD_CONNECT;
    buffer[0] = cmd;
    buffer[1] = conn;
    unsigned addrlen = address_write(addr, buffer + 2);
    
    send_x_bytes(buffer, addrlen + 2, true, true, true);

    if(!get_x_bytes(buffer, 3, true, true, BUF_SIZE))
        return -1;

    const struct gbridge_data *recv_data = (const struct gbridge_data*)buffer;
    if (recv_data->size != 2)
        return -1;
    if (recv_data->cmd != cmd)
        return -1;

    return (char)recv_data->buffer[0];
}

bool impl_sock_listen(void* user, unsigned conn)
{
    uint8_t buffer[BUF_SIZE];
    (void)user;
    uint8_t cmd = GBRIDGE_PROT_MA_CMD_LISTEN;
    buffer[0] = cmd;
    buffer[1] = conn;

    send_x_bytes(buffer, 2, true, true, true);

    if(!get_x_bytes(buffer, 3, true, true, BUF_SIZE))
        return false;

    const struct gbridge_data* recv_data = (const struct gbridge_data*)buffer;
    if (recv_data->size != 2)
        return false;
    if (recv_data->cmd != cmd)
        return false;

    return recv_data->buffer[0];
}

bool impl_sock_accept(void* user, unsigned conn)
{
    uint8_t buffer[BUF_SIZE];
    (void)user;
    uint8_t cmd = GBRIDGE_PROT_MA_CMD_ACCEPT;
    buffer[0] = cmd;
    buffer[1] = conn;

    send_x_bytes(buffer, 2, true, true, true);

    if(!get_x_bytes(buffer, 3, true, true, BUF_SIZE))
        return false;

    const struct gbridge_data* recv_data = (const struct gbridge_data*)buffer;
    if (recv_data->size != 2)
        return false;
    if (recv_data->cmd != cmd)
        return false;

    return recv_data->buffer[0];
}

int impl_sock_send(void* user, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr)
{
    uint8_t buffer[BUF_SIZE];
    (void)user;
    uint8_t cmd = GBRIDGE_PROT_MA_CMD_SEND;
    buffer[0] = cmd;
    buffer[1] = conn;
    unsigned addrlen = address_write(addr, buffer + 2);
    
    send_x_bytes(buffer, addrlen + 2, true, true, true);

    send_x_bytes(data, size, true, true, false);

    if(!get_x_bytes(buffer, 4, true, true, BUF_SIZE))
        return -1;

    const struct gbridge_data* recv_data = (const struct gbridge_data*)buffer;
    if (recv_data->size != 3)
        return -1;
    if (recv_data->cmd != cmd)
        return -1;

    return (recv_data->buffer[0] << 8) | (recv_data->buffer[1]);
}

int impl_sock_recv(void* user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr)
{
    uint8_t buffer[BUF_SIZE];
    (void)user;
    uint8_t cmd = GBRIDGE_PROT_MA_CMD_RECV;
    buffer[0] = cmd;
    buffer[1] = conn;
    buffer[2] = size >> 8;
    buffer[3] = size & 0xFF;
    
    send_x_bytes(buffer, 4, true, true, true);

    if(!get_x_bytes(buffer, 4, true, true, BUF_SIZE))
        return -1;

    const struct gbridge_data* recv_data = (const struct gbridge_data*)buffer;
    if (recv_data->size < 3)
        return -1;
    if (recv_data->cmd != cmd)
        return -1;

    int16_t sent_size = (recv_data->buffer[0] << 8) | (recv_data->buffer[1]);
    unsigned recv_addrlen = address_read(addr, recv_data->buffer + 2, recv_data->size - 3);
    if (!recv_addrlen)
        return -1;
    if (recv_data->size != recv_addrlen + 3)
        return -1;
    if (sent_size <= 0)
        return -1;
    
    if(size > sent_size)
        size = sent_size;

    if(!get_x_bytes(buffer, size, true, false, size))
        return -1;

    return sent_size;
}
