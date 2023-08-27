#include "io_buffer.h"
#include "pico_mobile_adapter.h"
#include "gbridge.h"
#include "utils.h"

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

static bool _get_x_bytes(uint8_t* buffer, uint32_t size, uint32_t limit, uint32_t* read_size, uint8_t wanted_cmd, uint8_t size_length, bool run_callback) {
    *read_size = 0;
    if(!buffer)
        return true;
    uint8_t cmd_data[5];
    uint8_t checksum_data[GBRIDGE_CHECKSUM_SIZE];
    if(size_length > 4)
        size_length = 4;

    bool try = true;
    while(try) {
        if(!get_section(cmd_data, size_length + 1, run_callback, true))
            return false;
        if(wanted_cmd != cmd_data[0])
            return false;
        uint32_t cmd_size = 0;
        for(int i = 0; i < size_length; i++)
            cmd_size = (cmd_size << 8) | cmd_data[i + 1];
        if(cmd_size > limit)
            cmd_size = limit;
        if(size > cmd_size)
            cmd_size = size;
        *read_size = cmd_size;
        if(!get_section(buffer, cmd_size, run_callback, false))
            return false;
        if(!get_section(checksum_data, GBRIDGE_CHECKSUM_SIZE, run_callback, false))
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

bool get_x_bytes(uint8_t* buffer, uint32_t size, bool run_callback, bool expected_data, uint32_t limit, uint32_t* read_size) {
    uint8_t wanted_cmd = GBRIDGE_CMD_STREAM_PC;
    uint8_t size_length = 2;
    if(expected_data) {
        wanted_cmd = GBRIDGE_CMD_DATA_PC;
        size_length = 1;
    }

    return _get_x_bytes(buffer, size, limit, read_size, wanted_cmd, size_length, run_callback);
}

static bool are_available(uint32_t size, bool is_debug) {
    uint32_t available = 0;
    if(!is_debug)
        available = available_data_out();
    else
        available = available_data_out_debug();
    return size <= available;
}

static bool send_section(const uint8_t* buffer, uint32_t size, uint32_t* pos, bool is_debug) {
    if(!is_debug)
        *pos = set_data_out(buffer, size, *pos);
    else {
        *pos = set_data_out_debug(buffer, size, *pos);
    }
    if((*pos) == size) {
        *pos = 0;
        return true;
    }
    return false;
}

static void _send_x_bytes(const uint8_t* buffer, uint32_t size, uint8_t cmd, uint8_t size_length, bool run_callback, bool send_checksum, bool expect_recieve, bool is_debug) {
    uint8_t checksum_buffer[GBRIDGE_CHECKSUM_SIZE];
    uint8_t command_buffer[5];
    command_buffer[0] = cmd;
    if(size_length > 4)
        size_length = 4;
    for(int i = 0; i < size_length; i++)
        command_buffer[i + 1] = (size >> (8 * (size_length - (i + 1)))) & 0xFF;
    set_checksum(buffer, size, checksum_buffer);
    bool try = true;
    while(try) {
        uint32_t pos = 0;
        uint32_t total_length = size_length + 1 + size + 2;
        if(is_debug && !are_available(total_length, is_debug))
            return;
        bool completed = false;
        bool completed_cmd = false;
        bool completed_data = false;
        bool completed_checksum = true;
        if(send_checksum)
            completed_checksum = false;
        while(!completed) {
            if(!completed_cmd)
                completed_cmd = send_section(command_buffer, size_length + 1, &pos, is_debug);
            if(completed_cmd && (!completed_data))
                completed_data = send_section(buffer, size, &pos, is_debug);
            if(completed_data && (!completed_checksum))
                completed_checksum = send_section(checksum_buffer, GBRIDGE_CHECKSUM_SIZE, &pos, is_debug);
            if(completed_data && completed_checksum)
                completed = true;
            if(run_callback)
                call_upkeep_callback();
        }

        if(expect_recieve && run_callback) {
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

void send_x_bytes(const uint8_t* buffer, uint32_t size, bool run_callback, bool send_checksum, bool is_data) {
    uint8_t cmd = GBRIDGE_CMD_STREAM;
    uint8_t size_length = 2;
    if(is_data) {
        cmd = GBRIDGE_CMD_DATA;
        size_length = 1;
    }
    _send_x_bytes(buffer, size, cmd, size_length, run_callback, send_checksum, true, false);
}

void debug_send(uint8_t* buffer, uint32_t size, enum gbridge_cmd cmd)
{
    _send_x_bytes(buffer, size, cmd, 2, false, true, false, true);
}

void debug_send_ack(uint8_t command)
{
    _send_x_bytes(&command, 1, GBRIDGE_CMD_DEBUG_ACK, 0, false, true, false, true);
}

