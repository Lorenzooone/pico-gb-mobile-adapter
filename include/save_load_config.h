#pragma once

// If your implementation cannot save,
// removing this define will make it work regardless.
// IMPLEMENTATION-SPECIFIC
#define CAN_SAVE

#ifdef CAN_SAVE
bool ReadConfig(uint8_t * buff, uint32_t size);
void SaveConfig(uint8_t * buff, uint32_t size);
#endif
