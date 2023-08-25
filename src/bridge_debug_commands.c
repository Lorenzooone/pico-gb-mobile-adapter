#include <stdio.h>
#include <mobile.h>
#include <mobile_data.h>
#include "io_buffer.h"
#include "gbridge.h"
#include "pico_mobile_adapter.h"
#include "bridge_debug_commands.h"
#include "flash_eeprom.h"
#include "utils.h"

#define MAX_DEBUG_COMMAND_SIZE 0x3F
#define DEBUG_COMMAND_ID_SIZE 1
#define MAX_NEEDED_DEBUG_SIZE EEPROM_SIZE

enum bridge_debug_command_id {
    SEND_EEPROM_CMD = 1,
    UPDATE_EEPROM_CMD = 2,
    UPDATE_RELAY_CMD = 3,
    UPDATE_RELAY_TOKEN_CMD = 4,
    UPDATE_DNS1_CMD = 5,
    UPDATE_DNS2_CMD = 6,
    UPDATE_P2P_PORT_CMD = 7,
    UPDATE_DEVICE_CMD = 8,
    SEND_NAME_INFO_CMD = 9,
    SEND_OTHER_INFO_CMD = 10,
    STOP_CMD = 11,
    START_CMD = 12
};

void interpret_debug_command(const uint8_t* src, uint8_t size, uint8_t real_size, bool is_in_mobile_loop) {
    if(real_size <= GBRIDGE_CHECKSUM_SIZE)
        return;
    if(size > (real_size - GBRIDGE_CHECKSUM_SIZE))
        size = real_size - GBRIDGE_CHECKSUM_SIZE;
    if(!size)
        return;
    if(size > (MAX_DEBUG_COMMAND_SIZE - GBRIDGE_CHECKSUM_SIZE))
        size = MAX_DEBUG_COMMAND_SIZE - GBRIDGE_CHECKSUM_SIZE;
    if(!src)
        return;
    
    if(is_in_mobile_loop)
        return;

    struct mobile_user* mobile = get_mobile_user();
    struct mobile_addr target_addr;
    struct mobile_addr other_addr;
    uint8_t data_out[MAX_NEEDED_DEBUG_SIZE];
    unsigned data_out_len;
    unsigned addrsize;

    uint8_t cmd = src[0];
    const uint8_t* data = src + DEBUG_COMMAND_ID_SIZE;
    const uint8_t* end_of_data = src + size;

    if(!check_checksum(src, size, end_of_data))
        return;

    size -= DEBUG_COMMAND_ID_SIZE;    

    switch(cmd) {
        case SEND_EEPROM_CMD:
#ifdef USE_FLASH
            ReadFlashConfig(data_out, EEPROM_SIZE);
            debug_send(data_out, EEPROM_SIZE, GBRIDGE_CMD_DEBUG_CFG);
#else
            debug_send(mobile->config_eeprom, EEPROM_SIZE, GBRIDGE_CMD_DEBUG_CFG);
#endif
            break;
        case SEND_NAME_INFO_CMD:
            data_out_len = snprintf(data_out, MAX_NEEDED_DEBUG_SIZE, IMPLEMENTATION_NAME);
            debug_send(data_out, data_out_len, GBRIDGE_CMD_DEBUG_INFO_NAME);
            break;
        case SEND_OTHER_INFO_CMD:
            data_out_len = snprintf(data_out, MAX_NEEDED_DEBUG_SIZE, LIBMOBILE_VERSION " :: " IMPLEMENTATION_VERSION);
            debug_send(data_out, data_out_len, GBRIDGE_CMD_DEBUG_INFO_OTHER);
            break;
        case UPDATE_EEPROM_CMD:
            if(size < 2)
                return;

            if(mobile->adapter->global.start)
                return;

            unsigned offset = (data[0] << 8) | data[1];
            size -= 2;
            
            if(offset > EEPROM_SIZE)
                offset = EEPROM_SIZE;
            
            if((size + offset) > EEPROM_SIZE)
                size = EEPROM_SIZE - offset;
            
            impl_config_write(mobile, data + 2, offset, size);
            mobile_init(mobile->adapter, mobile);
            debug_send_ack();

            break;
        case STOP_CMD:
            mobile_stop(mobile->adapter);
            debug_send_ack();

            break;
        case START_CMD:
            mobile_start(mobile->adapter);
            debug_send_ack();

            break;
        case UPDATE_DEVICE_CMD:
            if(size < 1)
                return;

            bool unmetered = data[0] & 0x80;
            enum mobile_adapter_device device = data[0] & 0x7F;
            
            if((device != MOBILE_ADAPTER_BLUE) && (device != MOBILE_ADAPTER_RED) && (device != MOBILE_ADAPTER_YELLOW) && (device != MOBILE_ADAPTER_GREEN))
                return;
            
            mobile_config_set_device(mobile->adapter, device, unmetered);
            debug_send_ack();

            break;
        case UPDATE_P2P_PORT_CMD:
            if(size < 2)
                return;

            uint16_t port = (data[0] << 8) | data[1];

            mobile_config_set_p2p_port(mobile->adapter, port);
            debug_send_ack();

            break;
        case UPDATE_RELAY_CMD:
            if(size < 1)
                return;

            addrsize = address_read(&target_addr, data, size);
            if(!addrsize)
                return;

            mobile_config_set_relay(mobile->adapter, &target_addr);
            debug_send_ack();

            break;
        case UPDATE_DNS1_CMD:
            if(size < 1)
                return;

            mobile_config_get_dns(mobile->adapter, &target_addr, &other_addr);
            addrsize = address_read(&target_addr, data, size);
            if(!addrsize)
                return;

            mobile_config_set_dns(mobile->adapter, &target_addr, &other_addr);
            debug_send_ack();

            break;
        case UPDATE_DNS2_CMD:
            if(size < 1)
                return;

            mobile_config_get_dns(mobile->adapter, &other_addr, &target_addr);
            addrsize = address_read(&target_addr, data, size);
            if(!addrsize)
                return;

            mobile_config_set_dns(mobile->adapter, &other_addr, &target_addr);
            debug_send_ack();

            break;
        case UPDATE_RELAY_TOKEN_CMD:
            if(size < MOBILE_RELAY_TOKEN_SIZE)
                return;

            mobile_config_set_relay_token(mobile->adapter, data);
            debug_send_ack();

            break;
        default:
            break;
    }
}
