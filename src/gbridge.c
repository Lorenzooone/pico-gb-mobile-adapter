#include "io_buffer.h"
#include "pico_mobile_adapter.h"
#include "gbridge.h"
#include "gbridge_timeout.h"
#include "utils.h"

static bool get_section(uint8_t* buffer, uint32_t size, bool run_callback, bool is_cmd) {
    uint32_t pos = 0;
    bool found = true;
    while(pos < size) {
        if(found)
            prepare_timeout();
        else if(!timeout_can_try_again()) {
            reset_data_out();
            reset_data_in();
            return false;
        }
        found = false;
        if(run_callback)
            call_upkeep_callback();
        for(; pos < size; pos++) {
            uint32_t data_read = get_data_in();
            if(data_read == -1)
                    break;
            buffer[pos] = data_read;
            found = true;
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

    prepare_failure();
    bool try = true;
    while(try) {
        if(!failed_can_try_again())
            return false;
        if(!get_section(cmd_data, size_length + 1, run_callback, true))
            continue;
        if(wanted_cmd != cmd_data[0])
            continue;
        uint32_t cmd_size = 0;
        for(int i = 0; i < size_length; i++)
            cmd_size = (cmd_size << 8) | cmd_data[i + 1];
        if(cmd_size > limit)
            cmd_size = limit;
        if(size > cmd_size)
            cmd_size = size;
        *read_size = cmd_size;
        if(!get_section(buffer, cmd_size, run_callback, false))
            continue;
        if(!get_section(checksum_data, GBRIDGE_CHECKSUM_SIZE, run_callback, false))
            continue;
        if(check_checksum(buffer, cmd_size, checksum_data))
            try = false;
        uint8_t buffer_send[] = {GBRIDGE_CMD_REPLY_F | wanted_cmd};
        if(try)
            buffer_send[0] += 1;
        if(run_callback) {
            prepare_timeout();
            uint32_t pos = 0;
            while(!pos) {
                if(!timeout_can_try_again()) {
                    reset_data_out();
                    reset_data_in();
                    break;
                }
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
    uint32_t base_pos = *pos;
    if(!is_debug)
        *pos = set_data_out(buffer, size, *pos);
    else {
        *pos = set_data_out_debug(buffer, size, *pos);
    }
    if((*pos) == size) {
        prepare_timeout();
        *pos = 0;
        return true;
    }
    else {
        if((*pos) != base_pos)
            prepare_timeout();
    }
    return false;
}

static bool _send_x_bytes(const uint8_t* buffer, uint32_t size, uint8_t cmd, uint8_t size_length, bool run_callback, bool send_checksum, bool expect_recieve, bool is_debug) {
    uint8_t checksum_buffer[GBRIDGE_CHECKSUM_SIZE];
    uint8_t command_buffer[5];
    command_buffer[0] = cmd;
    if(size_length > 4)
        size_length = 4;
    for(int i = 0; i < size_length; i++)
        command_buffer[i + 1] = (size >> (8 * (size_length - (i + 1)))) & 0xFF;
    set_checksum(buffer, size, checksum_buffer);

    prepare_failure();
    bool try = true;
    while(try) {
        if(!failed_can_try_again())
            return false;
        uint32_t pos = 0;
        uint32_t total_length = size_length + 1 + size + 2;
        if(is_debug && !are_available(total_length, is_debug))
            return false;
        prepare_timeout();
        bool completed = false;
        bool completed_cmd = false;
        bool completed_data = false;
        bool completed_checksum = true;
        if(send_checksum)
            completed_checksum = false;
        while(!completed) {
            if(!timeout_can_try_again()) {
                if(!is_debug) {
                    reset_data_out();
                    reset_data_in();
                }
                break;
            }
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
        if(!completed)
            continue;

        if(expect_recieve && run_callback) {
            prepare_timeout();
            uint32_t answer = -1;
            while(answer == -1) {
                if(!timeout_can_try_again()) {
                    if(!is_debug) {
                        reset_data_out();
                        reset_data_in();
                    }
                    try = false;
                    break;
                }
                call_upkeep_callback();
                answer = get_data_in();
                if(answer == (GBRIDGE_CMD_REPLY_F | cmd))
                    try = false;
                // Checksum failure, retry
                else if(answer == (GBRIDGE_CMD_REPLY_F | (cmd + 1)));
                else if(answer != -1) {
                    prepare_timeout();
                    answer = -1;
                }
            }
        }
        else
            try = false;
    }
    return true;
}

bool send_x_bytes(const uint8_t* buffer, uint32_t size, bool run_callback, bool send_checksum, bool is_data) {
    uint8_t cmd = GBRIDGE_CMD_STREAM;
    uint8_t size_length = 2;
    if(is_data) {
        cmd = GBRIDGE_CMD_DATA;
        size_length = 1;
    }
    return _send_x_bytes(buffer, size, cmd, size_length, run_callback, send_checksum, true, false);
}

bool debug_send(uint8_t* buffer, uint32_t size, enum gbridge_cmd cmd)
{
    return _send_x_bytes(buffer, size, cmd, 2, false, true, false, true);
}

bool debug_send_ack(uint8_t command)
{
    return _send_x_bytes(&command, 1, GBRIDGE_CMD_DEBUG_ACK, 0, false, true, false, true);
}

