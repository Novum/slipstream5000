#include "input.h"
#include <stddef.h>
#include <stdint.h>

enum {
	SCAN_RELEASE = 0x80,
	SCAN_CODE = SCAN_RELEASE - 1,
	SCAN_CODE_COUNT = SCAN_CODE + 1,
	SCAN_EXTENDED = 0xe0,
	SCAN_PAUSE_PREFIX = 0xe1
};

uint32_t SlipInput_biosMode;

void SlipInput_SetBiosMode(uint32_t mode) { SlipInput_biosMode = mode; }

bool SlipInput_InterceptScan(uint8_t scan, bool pressed[SLIP_INPUT_CODE_COUNT], bool held[SLIP_INPUT_CODE_COUNT],
                             const SlipInputBiosQueueCalls *bios) {
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

static const uint8_t unshiftedCharacters[SCAN_CODE_COUNT] = {0,
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
static const uint8_t shiftedCharacters[SCAN_CODE_COUNT] = {0,
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

uint8_t SlipInput_ReadCharacter(bool pressed[SLIP_INPUT_CODE_COUNT], const bool held[SLIP_INPUT_CODE_COUNT],
                                const SlipInputBiosCalls *bios) {
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
	for (unsigned scanCode = 0; scanCode < SCAN_CODE_COUNT; ++scanCode) {
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

static const SlipInputNameEntry inputNames[] = {{SLIP_INPUT_SCAN_A, "A"},
                                                {SLIP_INPUT_SCAN_B, "B"},
                                                {SLIP_INPUT_SCAN_C, "C"},
                                                {SLIP_INPUT_SCAN_D, "D"},
                                                {SLIP_INPUT_SCAN_E, "E"},
                                                {SLIP_INPUT_SCAN_F, "F"},
                                                {SLIP_INPUT_SCAN_G, "G"},
                                                {SLIP_INPUT_SCAN_H, "H"},
                                                {SLIP_INPUT_SCAN_I, "I"},
                                                {SLIP_INPUT_SCAN_J, "J"},
                                                {SLIP_INPUT_SCAN_K, "K"},
                                                {SLIP_INPUT_SCAN_L, "L"},
                                                {SLIP_INPUT_SCAN_M, "M"},
                                                {SLIP_INPUT_SCAN_N, "N"},
                                                {SLIP_INPUT_SCAN_O, "O"},
                                                {SLIP_INPUT_SCAN_P, "P"},
                                                {SLIP_INPUT_SCAN_Q, "Q"},
                                                {SLIP_INPUT_SCAN_R, "R"},
                                                {SLIP_INPUT_SCAN_S, "S"},
                                                {SLIP_INPUT_SCAN_T, "T"},
                                                {SLIP_INPUT_SCAN_U, "U"},
                                                {SLIP_INPUT_SCAN_V, "V"},
                                                {SLIP_INPUT_SCAN_W, "W"},
                                                {SLIP_INPUT_SCAN_X, "X"},
                                                {SLIP_INPUT_SCAN_Y, "Y"},
                                                {SLIP_INPUT_SCAN_Z, "Z"},
                                                {SLIP_INPUT_SCAN_1, "1"},
                                                {SLIP_INPUT_SCAN_2, "2"},
                                                {SLIP_INPUT_SCAN_3, "3"},
                                                {SLIP_INPUT_SCAN_4, "4"},
                                                {SLIP_INPUT_SCAN_5, "5"},
                                                {SLIP_INPUT_SCAN_6, "6"},
                                                {SLIP_INPUT_SCAN_7, "7"},
                                                {SLIP_INPUT_SCAN_8, "8"},
                                                {SLIP_INPUT_SCAN_9, "9"},
                                                {SLIP_INPUT_SCAN_0, "0"},
                                                {SLIP_INPUT_SCAN_CONTROL, "Ctrl"},
                                                {SLIP_INPUT_SCAN_ALT, "Alt"},
                                                {SLIP_INPUT_SCAN_SPACE, "Spc"},
                                                {SLIP_INPUT_SCAN_TAB, "Tab"},
                                                {SLIP_INPUT_SCAN_ENTER, "Ent"},
                                                {SLIP_INPUT_SCAN_BACKSPACE, "Bsp"},
                                                {SLIP_INPUT_SCAN_UP, "\x18"},
                                                {SLIP_INPUT_SCAN_DOWN, "\x19"},
                                                {SLIP_INPUT_SCAN_LEFT, "\x1b"},
                                                {SLIP_INPUT_SCAN_RIGHT, "\x1a"},
                                                {SLIP_INPUT_MOUSE_LEFT, "LMB"},
                                                {SLIP_INPUT_MOUSE_RIGHT, "RMB"},
                                                {SLIP_INPUT_JOYSTICK_1_BUTTON_1, "J1B1"},
                                                {SLIP_INPUT_JOYSTICK_1_BUTTON_2, "J1B2"},
                                                {SLIP_INPUT_JOYSTICK_2_BUTTON_1, "J2B1"},
                                                {SLIP_INPUT_JOYSTICK_2_BUTTON_2, "J2B2"},
                                                {UINT8_MAX, NULL}};

const char *SlipInput_Name(uint32_t inputCode) {
	const SlipInputNameEntry *entry = inputNames;
	for (;;) {
		if (entry->inputCode == (uint8_t)inputCode)
			return entry->displayName;
		++entry;
		if (entry->inputCode == UINT8_MAX)
			return NULL;
	}
}
