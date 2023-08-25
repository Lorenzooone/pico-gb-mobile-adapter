#pragma once

#include "hardware/flash.h"

#define USE_FLASH

#define KEY_CONFIG "CONFIG"

void FormatFlashConfig();
bool ReadFlashConfig(uint8_t * buff, uint32_t size);
void SaveFlashConfig(uint8_t * buff, uint32_t size);
