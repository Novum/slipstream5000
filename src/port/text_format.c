#include "text_format.h"
#include "runtime.h"
#include <stddef.h>

static const char upperDigits[] = "0123456789ABCDEF";
static const char lowerDigits[] = "0123456789abcdef";
static char signedResult[18];
static char unsignedResult[15];
static const char *signedDigits;
static const char *unsignedDigits;
static SlipTextExpansion expansion;
static uint8_t hexadecimalCase;
static uint8_t formatFlag;
static uint8_t longArgument;
static char formatWidth[10];

char *SlipText_FormatSigned(int32_t value, uint8_t padding, uint8_t signAndCase, uint16_t digits, uint16_t radix,
                            char *destination) {
	signedDigits = (signAndCase & SLIP_TEXT_LOWERCASE_DIGITS) != 0 ? lowerDigits : upperDigits;
	if (destination == NULL)
		destination = signedResult;
	char *const start = destination;
	char sign = signAndCase != 0 ? '+' : 0;
	uint32_t magnitude = (uint32_t)value;
	if (value < 0) {
		sign = '-';
		magnitude = 0u - magnitude;
	}
	if (sign != 0) {
		*destination++ = sign;
		--digits;
	}

	uint16_t remainders[65536];
	uint32_t count = digits;
	uint32_t pushed = 0;
	do {
		remainders[pushed++] = (uint16_t)(magnitude % radix);
		magnitude /= radix;
	} while (--count != 0);
	count = digits;
	uint16_t digit;
	for (;;) {
		digit = remainders[--pushed];
		if (count == 1 || (uint8_t)digit != 0)
			break;
		--count;
		if (padding != 0)
			*destination++ = (char)padding;
	}
	for (;;) {
		*destination++ = signedDigits[(uint8_t)digit];
		if (--count == 0)
			break;
		digit = remainders[--pushed];
	}
	*destination = 0;
	return start;
}

char *SlipText_FormatUnsigned(uint32_t value, uint8_t padding, uint8_t letterCase, uint16_t digits, uint16_t radix,
                              char *destination) {
	unsignedDigits = (letterCase & SLIP_TEXT_LOWERCASE_DIGITS) != 0 ? lowerDigits : upperDigits;
	if (destination == NULL)
		destination = unsignedResult;
	char *const start = destination;
	uint16_t remainders[65536];
	uint32_t count = digits;
	uint32_t pushed = 0;
	do {
		remainders[pushed++] = (uint16_t)(value % radix);
		value /= radix;
	} while (--count != 0);
	count = digits;
	uint16_t digit;
	for (;;) {
		digit = remainders[--pushed];
		if (count == 1 || (uint8_t)digit != 0)
			break;
		--count;
		if (padding != 0)
			*destination++ = (char)padding;
	}
	for (;;) {
		*destination++ = unsignedDigits[(uint8_t)digit];
		if (--count == 0)
			break;
		digit = remainders[--pushed];
	}
	*destination = 0;
	return start;
}

uint32_t SlipText_ReadArgument(const SlipTextArgument **arguments) {
	const SlipTextArgument *const argument = (*arguments)++;
	if (longArgument != 0)
		return *argument->dword;
	return (uint32_t)(int32_t)*argument->word;
}

const SlipTextExpansion *SlipText_EvaluateControl(const char *format, const SlipTextArgument **arguments) {
	formatFlag = 0;
	longArgument = 0;
	char *destination = expansion.text;
	char *width = formatWidth;
	char conversion;

	for (;;) {
		conversion = *format++;
		if (conversion == '-')
			formatFlag = 1;
		else if (conversion == '+')
			formatFlag = 2;
		else if (conversion == ' ')
			formatFlag = 3;
		else
			break;
	}
	if (conversion != 0) {
		while (conversion == '.' || (conversion >= '0' && conversion <= '9')) {
			*width++ = conversion;
			if (width == formatWidth + 9)
				SlipRuntime_Fatal("TextEvalControl: Buffer overflow.");
			conversion = *format++;
			if (conversion == 0)
				break;
		}

		if (conversion != 0) {
			*width = 0;
			while (conversion == 'l') {
				longArgument = 1;
				conversion = *format++;
			}
		}
	}
	switch (conversion) {
	case 's': {
		const char *text = (*arguments)++->text;
		char character;
		do {
			character = *text++;
			*destination++ = character;
		} while (character != 0);
		break;
	}
	case '%':
		*destination++ = '%';
		*destination++ = 0;
		break;
	case 'd':
	case 'i':
		SlipText_FormatSigned((int32_t)SlipText_ReadArgument(arguments), 0, 0, 9, 10, destination);
		while (*destination++ != 0) {
		}
		break;
	case 'u':
		SlipText_FormatUnsigned(SlipText_ReadArgument(arguments), '0', 0, 9, 10, destination);
		while (*destination++ != 0) {
		}
		break;
	case 'x':
	case 'X': {
		hexadecimalCase = conversion == 'x' ? SLIP_TEXT_LOWERCASE_DIGITS : 0;
		const uint32_t value = SlipText_ReadArgument(arguments);
		uint8_t padding = 0;
		uint16_t digits = 8;
		const char *specifier = formatWidth;
		if (*specifier != 0) {
			if (*specifier == '0')
				padding = (uint8_t)*specifier++;
			if (*specifier != 0)
				digits = (uint8_t)(*specifier - '0');
		}
		SlipText_FormatUnsigned(value, padding, hexadecimalCase, digits, 16, destination);
		while (*destination++ != 0) {
		}
		break;
	}
	case 'p':
		*destination++ = '*';
		*destination++ = 0;
		break;
	case 'c':
		*destination++ = *((*arguments)++->text);
		*destination++ = 0;
		break;
	default:
		*destination++ = 0;
		--format;
		break;
	}
	expansion.continuation = format;
	expansion.byteCount = (uint32_t)(destination - expansion.text);
	if (expansion.byteCount > 99)
		SlipRuntime_Fatal("TextEvalControl: Buffer overflow.");
	return &expansion;
}
