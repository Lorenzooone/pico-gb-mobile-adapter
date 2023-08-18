#ifndef _GBRIDGE_H_
#define _GBRIDGE_H_

struct gbridge_data {
    unsigned char size;
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

#endif /* _GBRIDGE_H_ */
