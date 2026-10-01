#ifndef SLIPSTREAM5000_FILE_WRITE_H
#define SLIPSTREAM5000_FILE_WRITE_H
#include <stdbool.h>
#include <stdint.h>

bool SlipFile_Write(const char *path, const uint8_t *data, uint32_t length);
#endif
