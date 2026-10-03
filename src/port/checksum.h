#ifndef SLIPSTREAM5000_CHECKSUM_H
#define SLIPSTREAM5000_CHECKSUM_H
#include <stdint.h>

enum { SLIP_CHECKSUM_TABLE_COUNT = UINT8_MAX + 1 };

void SlipChecksum_Initialize(uint16_t table[SLIP_CHECKSUM_TABLE_COUNT]);
/* Bytes are serialized file data, not native game-state storage. */
uint16_t SlipChecksum_Calculate(const uint16_t table[SLIP_CHECKSUM_TABLE_COUNT], const uint8_t *bytes, uint32_t length);
#endif
