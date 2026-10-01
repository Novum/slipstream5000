#include "input.h"
#include <stddef.h>
#include <stdint.h>

uint32_t SlipInput_biosMode;

void SlipInput_SetBiosMode(uint32_t mode) { SlipInput_biosMode = mode; }

bool SlipInput_InterceptScan(uint8_t scan, bool pressed[256], bool held[256], const SlipInputBiosQueueCalls *bios) {
	enum { SCAN_RELEASE = 0x80, SCAN_CODE = 0x7f, SCAN_EXTENDED = 0xe0, SCAN_PAUSE_PREFIX = 0xe1 };

	if (SlipInput_biosMode == 0)
		bios->clear(bios->context);
	if (scan != SCAN_EXTENDED && scan != SCAN_PAUSE_PREFIX) {
		const uint8_t code = scan & SCAN_CODE;
		if ((scan & SCAN_RELEASE) != 0) {
			pressed[code] = false;
			held[code] = false;
			return true;
		}
		pressed[code] = true;
		held[code] = true;
	}
	if (scan == SLIP_INPUT_SCAN_LEFT_SHIFT || scan == SLIP_INPUT_SCAN_RIGHT_SHIFT || scan == SLIP_INPUT_SCAN_CONTROL ||
	    scan == SLIP_INPUT_SCAN_ALT)
		return true;
	return SlipInput_biosMode != 0;
}

static const uint8_t unshiftedCharacters[128] = {0,
                                                 0,
                                                 '1',
                                                 '2',
                                                 '3',
                                                 '4',
                                                 '5',
                                                 '6',
                                                 '7',
                                                 '8',
                                                 '9',
                                                 '0',
                                                 '-',
                                                 '=',
                                                 SLIP_CHARACTER_BACKSPACE,
                                                 SLIP_CHARACTER_TAB,
                                                 'q',
                                                 'w',
                                                 'e',
                                                 'r',
                                                 't',
                                                 'y',
                                                 'u',
                                                 'i',
                                                 'o',
                                                 'p',
                                                 '[',
                                                 ']',
                                                 SLIP_CHARACTER_ENTER,
                                                 0,
                                                 'a',
                                                 's',
                                                 'd',
                                                 'f',
                                                 'g',
                                                 'h',
                                                 'j',
                                                 'k',
                                                 'l',
                                                 ';',
                                                 '\'',
                                                 '`',
                                                 0,
                                                 '\\',
                                                 'z',
                                                 'x',
                                                 'c',
                                                 'v',
                                                 'b',
                                                 'n',
                                                 'm',
                                                 ',',
                                                 '.',
                                                 '/',
                                                 0,
                                                 0,
                                                 0,
                                                 ' ',
                                                 0,
                                                 0,
                                                 0,
                                                 0,
                                                 0,
                                                 0,
                                                 0,
                                                 0,
                                                 0,
                                                 0,
                                                 0,
                                                 0,
                                                 0,
                                                 0,
                                                 SLIP_CHARACTER_UP,
                                                 0,
                                                 0,
                                                 SLIP_CHARACTER_LEFT,
                                                 0,
                                                 SLIP_CHARACTER_RIGHT,
                                                 0,
                                                 0,
                                                 SLIP_CHARACTER_DOWN,
                                                 0,
                                                 0,
                                                 SLIP_CHARACTER_DELETE};
static const uint8_t shiftedCharacters[128] = {0,
                                               0,
                                               '!',
                                               '"',
                                               156,
                                               '$',
                                               '%',
                                               '^',
                                               '&',
                                               '*',
                                               '(',
                                               ')',
                                               '_',
                                               '+',
                                               SLIP_CHARACTER_BACKSPACE,
                                               SLIP_CHARACTER_TAB,
                                               'Q',
                                               'W',
                                               'E',
                                               'R',
                                               'T',
                                               'Y',
                                               'U',
                                               'I',
                                               'O',
                                               'P',
                                               '{',
                                               '}',
                                               SLIP_CHARACTER_ENTER,
                                               0,
                                               'A',
                                               'S',
                                               'D',
                                               'F',
                                               'G',
                                               'H',
                                               'J',
                                               'K',
                                               'L',
                                               ':',
                                               '@',
                                               '`',
                                               0,
                                               '|',
                                               'Z',
                                               'X',
                                               'C',
                                               'V',
                                               'B',
                                               'N',
                                               'M',
                                               '<',
                                               '>',
                                               '?',
                                               0,
                                               0,
                                               0,
                                               ' '};

uint8_t SlipInput_ReadCharacter(bool pressed[256], const bool held[256], const SlipInputBiosCalls *bios) {
	if (SlipInput_biosMode != 0) {
		if (!bios->available(bios->context))
			return SLIP_CHARACTER_NONE;
		SlipInputBiosKey key = bios->read(bios->context);
		if (key.character != 0)
			return key.character;
		switch (key.scanCode) {
		case SLIP_INPUT_SCAN_DELETE:
			return SLIP_CHARACTER_DELETE;
		case SLIP_INPUT_SCAN_UP:
			return SLIP_CHARACTER_UP;
		case SLIP_INPUT_SCAN_DOWN:
			return SLIP_CHARACTER_DOWN;
		case SLIP_INPUT_SCAN_LEFT:
			return SLIP_CHARACTER_LEFT;
		case SLIP_INPUT_SCAN_RIGHT:
			return SLIP_CHARACTER_RIGHT;
		default:
			return SLIP_CHARACTER_NONE;
		}
	}
	for (unsigned scanCode = 0; scanCode < 128; ++scanCode) {
		if (pressed[scanCode]) {
			pressed[scanCode] = false;
			if (held[SLIP_INPUT_SCAN_RIGHT_SHIFT] || held[SLIP_INPUT_SCAN_LEFT_SHIFT])
				return shiftedCharacters[scanCode];
			return unshiftedCharacters[scanCode];
		}
	}
	return SLIP_CHARACTER_NONE;
}

typedef struct SlipInputNameEntry {
	uint8_t inputCode;
	const char *displayName;
} SlipInputNameEntry;

static const SlipInputNameEntry inputNames[] = {
    {0x1e, "A"},    {0x30, "B"},    {0x2e, "C"},    {0x20, "D"},    {0x12, "E"},   {0x21, "F"},   {0x22, "G"},
    {0x23, "H"},    {0x17, "I"},    {0x24, "J"},    {0x25, "K"},    {0x26, "L"},   {0x32, "M"},   {0x31, "N"},
    {0x18, "O"},    {0x19, "P"},    {0x10, "Q"},    {0x13, "R"},    {0x1f, "S"},   {0x14, "T"},   {0x16, "U"},
    {0x2f, "V"},    {0x11, "W"},    {0x2d, "X"},    {0x15, "Y"},    {0x2c, "Z"},   {0x02, "1"},   {0x03, "2"},
    {0x04, "3"},    {0x05, "4"},    {0x06, "5"},    {0x07, "6"},    {0x08, "7"},   {0x09, "8"},   {0x0a, "9"},
    {0x0b, "0"},    {0x1d, "Ctrl"}, {0x38, "Alt"},  {0x39, "Spc"},  {0x0f, "Tab"}, {0x1c, "Ent"}, {0x0e, "Bsp"},
    {0x48, "\x18"}, {0x50, "\x19"}, {0x4b, "\x1b"}, {0x4d, "\x1a"}, {0x80, "LMB"}, {0x81, "RMB"}, {0x82, "J1B1"},
    {0x83, "J1B2"}, {0x84, "J2B1"}, {0x85, "J2B2"}, {0xff, NULL}};

const char *SlipInput_Name(uint32_t inputCode) {
	const SlipInputNameEntry *entry = inputNames;
	for (;;) {
		if (entry->inputCode == (uint8_t)inputCode)
			return entry->displayName;
		++entry;
		if (entry->inputCode == 0xff)
			return NULL;
	}
}
