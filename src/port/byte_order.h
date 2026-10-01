#ifndef SLIPSTREAM5000_BYTE_ORDER_H
#define SLIPSTREAM5000_BYTE_ORDER_H

#include <stdint.h>

/* Read serialized bytes without requiring host alignment or byte order.
 * The caller must provide at least two or four readable bytes. */
static inline uint16_t SlipBytes_ReadLE16(const uint8_t *bytes) {
	return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static inline uint32_t SlipBytes_ReadLE32(const uint8_t *bytes) {
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static inline int16_t SlipBytes_ReadLEI16(const uint8_t *bytes) { return (int16_t)SlipBytes_ReadLE16(bytes); }

static inline int32_t SlipBytes_ReadLEI32(const uint8_t *bytes) { return (int32_t)SlipBytes_ReadLE32(bytes); }

static inline void SlipBytes_WriteLE16(uint8_t *bytes, uint16_t value) {
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static inline void SlipBytes_WriteLE32(uint8_t *bytes, uint32_t value) {
	for (unsigned byte = 0; byte < 4; ++byte)
		bytes[byte] = (uint8_t)(value >> (byte * 8));
}

#endif
