#ifndef SLIPSTREAM5000_INPUT_H
#define SLIPSTREAM5000_INPUT_H
#include <stdbool.h>
#include <stdint.h>

typedef enum SlipInputCode {
	SLIP_INPUT_SCAN_NONE = -1,
	SLIP_INPUT_SCAN_ESCAPE = 0x01,
	SLIP_INPUT_SCAN_1 = 0x02,
	SLIP_INPUT_SCAN_2 = 0x03,
	SLIP_INPUT_SCAN_3 = 0x04,
	SLIP_INPUT_SCAN_4 = 0x05,
	SLIP_INPUT_SCAN_5 = 0x06,
	SLIP_INPUT_SCAN_6 = 0x07,
	SLIP_INPUT_SCAN_7 = 0x08,
	SLIP_INPUT_SCAN_8 = 0x09,
	SLIP_INPUT_SCAN_9 = 0x0a,
	SLIP_INPUT_SCAN_0 = 0x0b,
	SLIP_INPUT_SCAN_BACKSPACE = 0x0e,
	SLIP_INPUT_SCAN_TAB = 0x0f,
	SLIP_INPUT_SCAN_Q = 0x10,
	SLIP_INPUT_SCAN_W = 0x11,
	SLIP_INPUT_SCAN_E = 0x12,
	SLIP_INPUT_SCAN_R = 0x13,
	SLIP_INPUT_SCAN_T = 0x14,
	SLIP_INPUT_SCAN_Y = 0x15,
	SLIP_INPUT_SCAN_U = 0x16,
	SLIP_INPUT_SCAN_I = 0x17,
	SLIP_INPUT_SCAN_O = 0x18,
	SLIP_INPUT_SCAN_P = 0x19,
	SLIP_INPUT_SCAN_ENTER = 0x1c,
	SLIP_INPUT_SCAN_CONTROL = 0x1d,
	SLIP_INPUT_SCAN_A = 0x1e,
	SLIP_INPUT_SCAN_S = 0x1f,
	SLIP_INPUT_SCAN_D = 0x20,
	SLIP_INPUT_SCAN_F = 0x21,
	SLIP_INPUT_SCAN_G = 0x22,
	SLIP_INPUT_SCAN_H = 0x23,
	SLIP_INPUT_SCAN_J = 0x24,
	SLIP_INPUT_SCAN_K = 0x25,
	SLIP_INPUT_SCAN_L = 0x26,
	SLIP_INPUT_SCAN_LEFT_SHIFT = 0x2a,
	SLIP_INPUT_SCAN_Z = 0x2c,
	SLIP_INPUT_SCAN_X = 0x2d,
	SLIP_INPUT_SCAN_C = 0x2e,
	SLIP_INPUT_SCAN_V = 0x2f,
	SLIP_INPUT_SCAN_B = 0x30,
	SLIP_INPUT_SCAN_N = 0x31,
	SLIP_INPUT_SCAN_M = 0x32,
	SLIP_INPUT_SCAN_RIGHT_SHIFT = 0x36,
	SLIP_INPUT_SCAN_ALT = 0x38,
	SLIP_INPUT_SCAN_SPACE = 0x39,
	SLIP_INPUT_SCAN_UP = 0x48,
	SLIP_INPUT_SCAN_LEFT = 0x4b,
	SLIP_INPUT_SCAN_RIGHT = 0x4d,
	SLIP_INPUT_SCAN_DOWN = 0x50,
	SLIP_INPUT_SCAN_DELETE = 0x53,
	SLIP_INPUT_MOUSE_LEFT = 0x80,
	SLIP_INPUT_MOUSE_RIGHT = 0x81,
	SLIP_INPUT_JOYSTICK_1_BUTTON_1 = 0x82,
	SLIP_INPUT_JOYSTICK_1_BUTTON_2 = 0x83,
	SLIP_INPUT_JOYSTICK_2_BUTTON_1 = 0x84,
	SLIP_INPUT_JOYSTICK_2_BUTTON_2 = 0x85
} SlipInputCode;

const char *SlipInput_Name(uint32_t inputCode);

typedef enum SlipInputCharacter {
	SLIP_CHARACTER_NONE = 0,
	SLIP_CHARACTER_UP = 1,
	SLIP_CHARACTER_DOWN = 2,
	SLIP_CHARACTER_LEFT = 3,
	SLIP_CHARACTER_RIGHT = 4,
	SLIP_CHARACTER_DELETE = 5,
	SLIP_CHARACTER_BACKSPACE = 8,
	SLIP_CHARACTER_TAB = 9,
	SLIP_CHARACTER_ENTER = 13
} SlipInputCharacter;

typedef struct SlipInputBiosKey {
	uint8_t character, scanCode;
} SlipInputBiosKey;

typedef struct SlipInputBiosCalls {
	bool (*available)(void *context);
	SlipInputBiosKey (*read)(void *context);
	void *context;
} SlipInputBiosCalls;

extern uint32_t SlipInput_biosMode;
void SlipInput_SetBiosMode(uint32_t mode);
uint8_t SlipInput_ReadCharacter(bool pressed[256], const bool held[256], const SlipInputBiosCalls *bios);

typedef struct SlipInputBiosQueueCalls {
	void *context;
	void (*clear)(void *);
} SlipInputBiosQueueCalls;

bool SlipInput_InterceptScan(uint8_t scan, bool pressed[256], bool held[256], const SlipInputBiosQueueCalls *bios);
#endif
