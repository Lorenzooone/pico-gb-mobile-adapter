#ifndef _GBRIDGE_H_
#define _GBRIDGE_H_

#define GBRIDGE_HANDSHAKE {0x99, 0x66, 'G', 'B'}

#define GBRIDGE_TIMEOUT_US 1000000
#define GBRIDGE_TIMEOUT_MS (GBRIDGE_TIMEOUT_US / 1000)

#define GBRIDGE_MAX_DATA_SIZE 0x80

#define GBRIDGE_CHECKSUM_SIZE 2

// Flag set when replying to a message
#define GBRIDGE_CMD_REPLY_F 0x80

enum gbridge_cmd {
    GBRIDGE_CMD_NONE = 0,

    // to PC
    GBRIDGE_CMD_PING = 0x01,
    GBRIDGE_CMD_DEBUG_LINE = 0x02,
    GBRIDGE_CMD_DEBUG_CHAR = 0x03,
    GBRIDGE_CMD_DEBUG_INFO = 0x04,
    GBRIDGE_CMD_DEBUG_LOG = 0x05,
    GBRIDGE_CMD_DEBUG_ACK = 0x08,
    GBRIDGE_CMD_DATA = 0x0A,
    GBRIDGE_CMD_DATA_FAIL = 0x0B,  // Checksum failure, retry
    GBRIDGE_CMD_STREAM = 0x0C,
    GBRIDGE_CMD_STREAM_FAIL = 0x0D,  // Checksum failure, retry

    // from PC
    GBRIDGE_CMD_PROG_STOP = 0x41,
    GBRIDGE_CMD_PROG_START = 0x42,
    GBRIDGE_CMD_DATA_PC = 0x4A,
    GBRIDGE_CMD_DATA_FAIL_PC = 0x4B,  // Checksum failure, retry
    GBRIDGE_CMD_STREAM_PC = 0x4C,
    GBRIDGE_CMD_STREAM_FAIL_PC = 0x4D,  // Checksum failure, retry
    GBRIDGE_CMD_RESET = 0x4F,
};

enum log_cmd {
    CMD_DEBUG_LOG_IN = 0x01,
    CMD_DEBUG_LOG_OUT = 0x02,
    CMD_DEBUG_LOG_TIME_TR = 0x03,
    CMD_DEBUG_LOG_TIME_AC = 0x04,
    CMD_DEBUG_LOG_TIME_IR = 0x05,
};

struct gbridge_data {
    unsigned char cmd;
    unsigned char *buffer;
};

enum gbma_prot_cmd {
    GBRIDGE_PROT_MA_CMD_OPEN,
    GBRIDGE_PROT_MA_CMD_CLOSE,
    GBRIDGE_PROT_MA_CMD_CONNECT,
    GBRIDGE_PROT_MA_CMD_LISTEN,
    GBRIDGE_PROT_MA_CMD_ACCEPT,
    GBRIDGE_PROT_MA_CMD_SEND,
    GBRIDGE_PROT_MA_CMD_RECV
};

bool get_x_bytes(uint8_t* buffer, uint32_t size, bool run_callback, bool expected_data, uint32_t limit, uint32_t* read_size);
bool send_x_bytes(const uint8_t* buffer, uint32_t size, bool run_callback, bool send_checksum, bool is_data);
bool debug_send(uint8_t* buffer, uint32_t size, enum gbridge_cmd cmd);
bool debug_send_ack(uint8_t command);

#endif /* _GBRIDGE_H_ */
