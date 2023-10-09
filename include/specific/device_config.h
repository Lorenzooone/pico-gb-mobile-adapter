#ifndef _DEVICE_CONFIG_H_
#define _DEVICE_CONFIG_H_

// Hardware specific defines!
// Change this file to suit your needs!
// IMPLEMENTATION-SPECIFIC

// Output to PC, change these three defines for your implementation/version
#define IMPLEMENTATION_NAME "PICO-USB-SM"

#define IMPLEMENTATION_VERSION_SIZE 4
#define IMPLEMENTATION_VERSION_MAJOR 1
#define IMPLEMENTATION_VERSION_MINOR 0
#define IMPLEMENTATION_VERSION_PATCH 0

#define IMPLEMENTATION_VERSION ((IMPLEMENTATION_VERSION_MAJOR << 16) | (IMPLEMENTATION_VERSION_MINOR << 8) | IMPLEMENTATION_VERSION_PATCH)

// If your implementation cannot save,
// removing this define will make it work regardless.
#define CAN_SAVE

#endif /* _DEVICE_CONFIG_H_ */
