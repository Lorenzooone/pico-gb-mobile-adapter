#pragma once

#include "device_config.h"

#ifdef CAN_SAVE
bool ReadConfig(uint8_t * buff, uint32_t size);
void SaveConfig(uint8_t * buff, uint32_t size);
#endif
