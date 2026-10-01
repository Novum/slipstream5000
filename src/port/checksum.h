#ifndef SLIPSTREAM5000_CHECKSUM_H
#define SLIPSTREAM5000_CHECKSUM_H
#include <stdint.h>

void SlipChecksum_Initialize(uint16_t table[256]);
/* Bytes are serialized file data, not native game-state storage. */
uint16_t SlipChecksum_Calculate(const uint16_t table[256], const uint8_t *bytes, uint32_t length);
#endif
