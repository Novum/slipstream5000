#ifndef SLIPSTREAM5000_CONFIG_FILE_H
#define SLIPSTREAM5000_CONFIG_FILE_H
#include <stdint.h>

enum { SLIP_CONFIG_PAYLOAD_BYTES = 0x64d, SLIP_CONFIG_FILE_BYTES = 0x64f };

void SlipConfig_SaveImage(const char *path, uint8_t image[SLIP_CONFIG_FILE_BYTES]);
#endif
