#ifndef SLIPSTREAM5000_TEXT_FORMAT_H
#define SLIPSTREAM5000_TEXT_FORMAT_H

#include <stdint.h>

enum { SLIP_TEXT_LOWERCASE_DIGITS = 0x80u };

typedef union SlipTextArgument {
	const int16_t *word;
	const uint32_t *dword;
	const char *text;
} SlipTextArgument;

typedef struct SlipTextExpansion {
	char text[100];
	const char *continuation;
	uint32_t byteCount;
} SlipTextExpansion;

char *SlipText_FormatSigned(int32_t value, uint8_t padding, uint8_t signAndCase, uint16_t digits, uint16_t radix,
                            char *destination);
char *SlipText_FormatUnsigned(uint32_t value, uint8_t padding, uint8_t letterCase, uint16_t digits, uint16_t radix,
                              char *destination);
uint32_t SlipText_ReadArgument(const SlipTextArgument **arguments);
const SlipTextExpansion *SlipText_EvaluateControl(const char *format, const SlipTextArgument **arguments);

#endif
