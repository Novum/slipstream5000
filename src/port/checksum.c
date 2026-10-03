#include "checksum.h"
#include <limits.h>

enum { SLIP_CHECKSUM_REFLECTED_POLYNOMIAL = 0x8408u };

void SlipChecksum_Initialize(uint16_t table[SLIP_CHECKSUM_TABLE_COUNT]) {
	for (unsigned index = 0; index < SLIP_CHECKSUM_TABLE_COUNT; ++index) {
		uint16_t value = (uint16_t)index;
		for (unsigned bit = 0; bit < CHAR_BIT; ++bit) {
			const uint16_t carry = value & 1u;
			value >>= 1;
			if (carry)
				value ^= SLIP_CHECKSUM_REFLECTED_POLYNOMIAL;
		}
		table[index] = value;
	}
}

uint16_t SlipChecksum_Calculate(const uint16_t table[SLIP_CHECKSUM_TABLE_COUNT], const uint8_t *bytes,
                                uint32_t length) {
	uint16_t checksum = UINT16_MAX;
	for (uint32_t words = length >> 1; words != 0; --words) {
		checksum ^= (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
		bytes += 2;
		uint16_t index = checksum & UINT8_MAX;
		checksum = (uint16_t)((checksum >> 8) ^ table[index]);
		index = checksum & UINT8_MAX;
		checksum = (uint16_t)((checksum >> 8) ^ table[index]);
	}
	if (length & 1u) {
		checksum ^= *bytes;
		const uint16_t index = checksum & UINT8_MAX;
		checksum = (uint16_t)((checksum >> 8) ^ table[index]);
	}
	return checksum;
}
