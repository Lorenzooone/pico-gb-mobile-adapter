#include "io_buffer.h"
#include "gbridge.h"
#include "pico_mobile_adapter.h"
#include "bridge_debug_commands.h"

#define MAX_DEBUG_COMMAND_SIZE 63

enum bridge_debug_command_id {
    SEND_EEPROM_CMD = 1
};

void interpret_debug_command(uint8_t* src, uint8_t size) {
    if(!size)
        return;
    if(!src)
        return;
    struct mobile_user* mobile = get_mobile_user();

    switch(src[0]) {
        case SEND_EEPROM_CMD:
            debug_send(mobile->config_eeprom, EEPROM_SIZE, GBRIDGE_CMD_DEBUG_CFG);
            break;
        default:
            break;
    }
}
