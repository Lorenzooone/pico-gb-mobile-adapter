#include "hardware/flash.h"
#include "save_load_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/sync.h"
#include "pico_mobile_adapter.h"
#include "gbridge.h"
#include "utils.h"

#define KEY_STR_SIZE 16
#define CHECKSUM_SIZE 2
#define PROGRESSIVE_SIZE 2
#define KEY_CONFIG_INDEX 0

#define NON_FLASH_RELATED_SIZE (EEPROM_SIZE)
#define FINAL_FLASH_MIN_SIZE (KEY_STR_SIZE + NON_FLASH_RELATED_SIZE + PROGRESSIVE_SIZE + CHECKSUM_SIZE)

#define FLASH_PAGE_NEEDED ((FINAL_FLASH_MIN_SIZE + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE)
#define FLASH_DATA_SIZE (FLASH_PAGE_SIZE * FLASH_PAGE_NEEDED)
#define FLASH_SECTOR_NEEDED ((FLASH_DATA_SIZE + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE)
#define FLASH_DATA_SECTOR_SIZE (FLASH_SECTOR_SIZE * FLASH_SECTOR_NEEDED)
#define NUM_MIRRORS 2
#define MIRROR_SAVE_REGIONS (FLASH_DATA_SECTOR_SIZE / FLASH_DATA_SIZE)
#define TOTAL_SAVE_REGIONS (NUM_MIRRORS * MIRROR_SAVE_REGIONS)
#define FLASH_BASE_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - (NUM_MIRRORS * FLASH_DATA_SECTOR_SIZE))

struct saved_device_data {
    uint8_t config_key[KEY_STR_SIZE];
    uint8_t config_eeprom[EEPROM_SIZE];
    uint8_t unused[FLASH_DATA_SIZE - FINAL_FLASH_MIN_SIZE];
    uint8_t progressive_number[PROGRESSIVE_SIZE];
    uint8_t checksum[CHECKSUM_SIZE];
};

const char* save_key_strings[] = {"CONFIG"};
const uint8_t *flash_target_contents = (const uint8_t *) (XIP_BASE + FLASH_BASE_TARGET_OFFSET);

uint8_t save_mirror = 0;
uint16_t save_position = 0;
uint16_t save_progressive_value = 0;
uint8_t unused_data[FLASH_DATA_SIZE - FINAL_FLASH_MIN_SIZE];

// Is this save valid?
static bool is_saved_data_valid(const struct saved_device_data* data) {
    if(!check_checksum((const uint8_t*)data, sizeof(struct saved_device_data) - CHECKSUM_SIZE, (const uint8_t*)data->checksum))
        return false;
    if(strlen(save_key_strings[KEY_CONFIG_INDEX]) >= KEY_STR_SIZE)
        return false;
    if(strncmp(save_key_strings[KEY_CONFIG_INDEX], data->config_key, KEY_STR_SIZE) != 0)
        return false;
   return true;
}

// The sector erasure function
static void FormatFlashConfig(uint8_t mirror) {
    if(mirror >= NUM_MIRRORS)
        mirror = NUM_MIRRORS - 1;
    DEBUG_PRINT_FUNCTION("Erasing target region...");
    uint32_t irqs = save_and_disable_interrupts();
    flash_range_erase(FLASH_BASE_TARGET_OFFSET + (mirror * FLASH_DATA_SECTOR_SIZE), FLASH_DATA_SECTOR_SIZE);
    restore_interrupts(irqs);
}

// Prepares a single unit (Key String + Data)
static void prepare_data_unit(uint8_t* src, uint8_t* dst, size_t size, const char* key_string, uint8_t* str_dst) {
    for(size_t i = 0; i < KEY_STR_SIZE; i++)
        str_dst[i] = 0;
    memset(dst, 0, size);

    size_t str_len = strlen(key_string);
    if(str_len >= KEY_STR_SIZE)
        return;
    for(size_t i = 0; i < str_len; i++)
        str_dst[i] = key_string[i];

    if(src != NULL)
        memcpy(dst, src, size);
}

// Tries to save the config to the proper place. Returns true if successfull, false if not!
static bool save_single_config(uint8_t mirror, uint16_t position, uint16_t progressive_number, struct saved_data_pointers* save_ptrs) {
    struct saved_device_data tmp_data;

    // Place other data saving here...
    if(save_ptrs != NULL) {
        prepare_data_unit(save_ptrs->eeprom, tmp_data.config_eeprom, EEPROM_SIZE, save_key_strings[KEY_CONFIG_INDEX], tmp_data.config_key);
    }
    else {
        prepare_data_unit(NULL, tmp_data.config_eeprom, EEPROM_SIZE, save_key_strings[KEY_CONFIG_INDEX], tmp_data.config_key);
    }
    
    memcpy(tmp_data.unused, unused_data, FLASH_DATA_SIZE - FINAL_FLASH_MIN_SIZE);
    write_big_endian(tmp_data.progressive_number, progressive_number, PROGRESSIVE_SIZE);
    set_checksum((const uint8_t*)&tmp_data, sizeof(struct saved_device_data) - CHECKSUM_SIZE, tmp_data.checksum);

    uint32_t irqs = save_and_disable_interrupts();
    flash_range_program(FLASH_BASE_TARGET_OFFSET + (mirror * FLASH_DATA_SECTOR_SIZE) + (position * FLASH_DATA_SIZE), (const uint8_t*)&tmp_data, FLASH_DATA_SIZE);
    restore_interrupts(irqs);

    for(size_t i = 0; i < sizeof(struct saved_device_data); i++) {
        if((flash_target_contents + (mirror * FLASH_DATA_SECTOR_SIZE) + (position * FLASH_DATA_SIZE))[i] != ((uint8_t*)&tmp_data)[i])
            return false;
    }
    return true;
}

// Read flash memory and prepare the values to access the saved data
void InitSave(void) {
    DEBUG_PRINT_FUNCTION("Reading the target region... ");
    uint16_t valid_progressive_numbers[TOTAL_SAVE_REGIONS];
    bool valid_regions[TOTAL_SAVE_REGIONS];
    bool found_one = false;
    for(int i = 0; i < TOTAL_SAVE_REGIONS; i++)
        valid_regions[i] = false;

    // Parse various possible save locations to find valid ones
    struct saved_device_data tmp_data;
    for(int i = 0; i < NUM_MIRRORS; i++) {
        for(int j = 0; j < MIRROR_SAVE_REGIONS; j++) {
            memcpy(&tmp_data, flash_target_contents + (i * FLASH_DATA_SECTOR_SIZE) + (j * FLASH_DATA_SIZE), sizeof(struct saved_device_data));
            if(is_saved_data_valid(&tmp_data)) {
                found_one = true;
                valid_regions[(i * MIRROR_SAVE_REGIONS) + j] = true;
                valid_progressive_numbers[(i * MIRROR_SAVE_REGIONS) + j] = read_big_endian(tmp_data.progressive_number, PROGRESSIVE_SIZE);
            }
        }
    }

    // Check if the Flash is already formatted 
    if(!found_one) {
        DEBUG_PRINT_FUNCTION("The flash memory is not formatted. Formatting now...");
        FormatFlashConfig(0);
        memset(unused_data, 0, FLASH_DATA_SIZE - FINAL_FLASH_MIN_SIZE);
        save_mirror = 0;
        save_position = 0;
        save_progressive_value = 0;
        save_single_config(save_mirror, save_position, save_progressive_value, NULL);
        return;
    }

    // Get the latest valid save
    bool found_first = false;
    for(int i = 0; i < NUM_MIRRORS; i++) {
        for(int j = 0; j < MIRROR_SAVE_REGIONS; j++) {
            if(valid_regions[(i * MIRROR_SAVE_REGIONS) + j]) {
                bool update = false;
                if(!found_first) {
                    found_first = true;
                    update = true;
                }
                else {
                    if(((int16_t)(save_progressive_value - valid_progressive_numbers[(i * MIRROR_SAVE_REGIONS) + j])) < 0)
                        update = true;
                }
                if(update) {
                    save_position = j;
                    save_mirror = i;
                    save_progressive_value = valid_progressive_numbers[(i * MIRROR_SAVE_REGIONS) + j];
                }
            }
        }
    }
}

void InitSavedPointers(struct saved_data_pointers* save_ptrs, struct mobile_user* mobile) {
    // NULL all other pointers here...
    save_ptrs->eeprom = NULL;

    if(mobile != NULL) {
        // Connect all other pointers here...
        save_ptrs->eeprom = mobile->config_eeprom;
    }
}

// Read saved eeprom data
void ReadEeprom(uint8_t* buffer) {
    struct saved_data_pointers ptrs;
    InitSavedPointers(&ptrs, NULL);

    ptrs.eeprom = buffer;

    ReadConfig(&ptrs);
}

// Read saved data and set the configs
void ReadConfig(struct saved_data_pointers* save_ptrs) {
    struct saved_device_data tmp_data;
    memcpy(&tmp_data, flash_target_contents + (save_mirror * FLASH_DATA_SECTOR_SIZE) + (save_position * FLASH_DATA_SIZE), sizeof(struct saved_device_data));
    
    // Place other data loading here...
    if(save_ptrs->eeprom != NULL)
        memcpy(save_ptrs->eeprom, tmp_data.config_eeprom, EEPROM_SIZE);
    
    memcpy(unused_data, tmp_data.unused, FLASH_DATA_SIZE - FINAL_FLASH_MIN_SIZE);
    DEBUG_PRINT_FUNCTION("Done.\n");
}

// Setup the save in the proper position, with the logic to choose where to save
void SaveConfig(struct saved_data_pointers* save_ptrs) {
    uint8_t new_mirror = (save_mirror + 1) % NUM_MIRRORS;
    uint8_t position_increase = 0;
    if(new_mirror == 0)
        position_increase = 1;
    uint16_t new_position = (save_position + position_increase) % MIRROR_SAVE_REGIONS;
    uint16_t new_progressive_value = save_progressive_value + 1;
    bool success = false;

    if(new_position == 0)
        FormatFlashConfig(new_mirror);
    DEBUG_PRINT_FUNCTION("Programming target region...");
    if(!save_single_config(new_mirror, new_position, new_progressive_value, save_ptrs)) {
        DEBUG_PRINT_FUNCTION("Failure!");
        FormatFlashConfig(new_mirror);
        DEBUG_PRINT_FUNCTION("Retrying programming on target region...");
        if(!save_single_config(new_mirror, new_position, new_progressive_value, save_ptrs))
            DEBUG_PRINT_FUNCTION("Double failure!!! Saving seems impossible!");
        else
            success = true;
    }
    else
        success = true;

    if(success) {
        DEBUG_PRINT_FUNCTION("Done.\n");
        
        // Update data position
        save_mirror = new_mirror;
        save_position = new_position;
        save_progressive_value = new_progressive_value;
    }
}
