#pragma once

#include "device_config.h"
#include "pico_mobile_adapter.h"

struct saved_data_pointers {
    uint8_t* eeprom;
};

#ifdef CAN_SAVE
void InitSave(void);
void InitSavedPointers(struct saved_data_pointers* saved_ptrs, struct mobile_user* mobile);
void ReadEeprom(uint8_t* buffer);
void ReadConfig(struct saved_data_pointers* save_ptrs);
void SaveConfig(struct saved_data_pointers* save_ptrs);
#endif
