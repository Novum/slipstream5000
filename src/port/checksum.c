#include "checksum.h"

void SlipChecksum_Initialize(uint16_t table[256]) {
	for (unsigned index = 0; index < 256; ++index) {
		uint16_t value = (uint16_t)index;
		for (unsigned bit = 0; bit < 8; ++bit) {
			const uint16_t carry = value & 1u;
			value >>= 1;
			if (carry)
				value ^= 0x8408u;
		}
		table[index] = value;
	}
}

uint16_t SlipChecksum_Calculate(const uint16_t table[256], const uint8_t *bytes, uint32_t length) {
	uint16_t checksum = 0xffffu;
	for (uint32_t words = length >> 1; words != 0; --words) {
		checksum ^= (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
		bytes += 2;
		uint16_t index = checksum & 0xffu;
		checksum = (uint16_t)((checksum >> 8) ^ table[index]);
		index = checksum & 0xffu;
		checksum = (uint16_t)((checksum >> 8) ^ table[index]);
	}
	if (length & 1u) {
		checksum ^= *bytes;
		const uint16_t index = checksum & 0xffu;
		checksum = (uint16_t)((checksum >> 8) ^ table[index]);
	}
	return checksum;
}
