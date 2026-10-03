#include "controller_sdl.h"
#include "input_navigation.h"
#include "joystick_calibration.h"

enum {
	CONTROLLER_SLOTS = 2,
	BUTTONS_PER_CONTROLLER = 2,
	SDL_AXIS_TO_GAMEPORT_SHIFT = 2,
	SDL_AXIS_UNSIGNED_BIAS = 1 << 15
};

typedef struct SlipControllerSdlDevice {
	SDL_JoystickID id;
	SDL_Joystick *joystick;
	SDL_Gamepad *gamepad;
} SlipControllerSdlDevice;

static SlipControllerSdlDevice devices[CONTROLLER_SLOTS];

static void SlipControllerSdl_Close(unsigned slot) {
	SlipControllerSdlDevice *const device = &devices[slot];
	if (device->gamepad)
		SDL_CloseGamepad(device->gamepad);
	else if (device->joystick)
		SDL_CloseJoystick(device->joystick);
	*device = (SlipControllerSdlDevice){0};
	SlipJoystick_present[slot] = 0;
}

bool SlipControllerSdl_Read(void *context, uint32_t joystick, uint16_t *x, uint16_t *y) {
	(void)context;
	SlipControllerSdlDevice *const device = &devices[joystick];
	if (!device->joystick || !SDL_JoystickConnected(device->joystick))
		return false;
	const Sint16 axisX = device->gamepad ? SDL_GetGamepadAxis(device->gamepad, SDL_GAMEPAD_AXIS_LEFTX)
	                                     : SDL_GetJoystickAxis(device->joystick, 0);
	const Sint16 axisY = device->gamepad ? SDL_GetGamepadAxis(device->gamepad, SDL_GAMEPAD_AXIS_LEFTY)
	                                     : SDL_GetJoystickAxis(device->joystick, 1);

	*x = (uint16_t)((((int32_t)axisX + SDL_AXIS_UNSIGNED_BIAS) >> SDL_AXIS_TO_GAMEPORT_SHIFT) + 1);
	*y = (uint16_t)((((int32_t)axisY + SDL_AXIS_UNSIGNED_BIAS) >> SDL_AXIS_TO_GAMEPORT_SHIFT) + 1);
	return true;
}

static void SlipControllerSdl_Discover(void) {
	for (unsigned slot = 0; slot < CONTROLLER_SLOTS; ++slot)
		if (devices[slot].joystick && !SDL_JoystickConnected(devices[slot].joystick))
			SlipControllerSdl_Close(slot);
	int count;
	SDL_JoystickID *const ids = SDL_GetJoysticks(&count);
	if (!ids)
		return;
	for (int index = 0; index < count; ++index) {
		if (ids[index] == devices[0].id || ids[index] == devices[1].id)
			continue;
		unsigned slot;
		for (slot = 0; slot < CONTROLLER_SLOTS && devices[slot].joystick; ++slot) {
		}
		if (slot == CONTROLLER_SLOTS)
			break;
		SlipControllerSdlDevice *const device = &devices[slot];
		if (SDL_IsGamepad(ids[index])) {
			device->gamepad = SDL_OpenGamepad(ids[index]);
			if (device->gamepad)
				device->joystick = SDL_GetGamepadJoystick(device->gamepad);
		} else {
			device->joystick = SDL_OpenJoystick(ids[index]);
		}
		if (!device->joystick)
			continue;
		if (!device->gamepad && SDL_GetNumJoystickAxes(device->joystick) < 2) {
			SlipControllerSdl_Close(slot);
			continue;
		}
		device->id = ids[index];

		if (SlipJoystick_Calibrate(SLIP_JOYSTICK_CALIBRATION_INITIALIZE, slot, SlipControllerSdl_Read, NULL))
			SlipJoystick_present[slot] = 1;
	}
	SDL_free(ids);
}

void SlipControllerSdl_Initialize(void) {
	SlipJoystick_BindHostRead(SlipControllerSdl_Read, NULL);
	SlipControllerSdl_Discover();
}

void SlipControllerSdl_Poll(void) {
	SDL_UpdateJoysticks();
	SlipControllerSdl_Discover();
	for (unsigned slot = 0; slot < CONTROLLER_SLOTS; ++slot) {
		SlipControllerSdlDevice *const device = &devices[slot];
		for (unsigned button = 0; button < BUTTONS_PER_CONTROLLER; ++button) {
			const unsigned code = SLIP_INPUT_JOYSTICK_1_BUTTON_1 + slot * BUTTONS_PER_CONTROLLER + button;
			bool down = false;
			if (device->gamepad)
				down = SDL_GetGamepadButton(device->gamepad,
				                            button == 0 ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST);
			else if (device->joystick && (int)button < SDL_GetNumJoystickButtons(device->joystick))
				down = SDL_GetJoystickButton(device->joystick, (int)button);

			if (down && !SlipInput_held[code])
				SlipInput_pressed[code] = true;
			else if (!down && SlipInput_held[code])
				SlipInput_pressed[code] = false;
			SlipInput_held[code] = down;
		}
	}
}

uint16_t SlipControllerSdl_Presence(void) {
	return (uint16_t)(SlipJoystick_present[0] | ((uint16_t)SlipJoystick_present[1] << 8));
}

void SlipControllerSdl_ObserveEvent(const SDL_Event *event) {
	if (event->type == SDL_EVENT_MOUSE_MOTION) {
		SlipInput_motion.x = (int16_t)(uint16_t)((uint16_t)SlipInput_motion.x + (uint32_t)(int32_t)event->motion.xrel);
		SlipInput_motion.y = (int16_t)(uint16_t)((uint16_t)SlipInput_motion.y + (uint32_t)(int32_t)event->motion.yrel);
	}
}

void SlipControllerSdl_Shutdown(void) {
	for (unsigned slot = 0; slot < CONTROLLER_SLOTS; ++slot)
		SlipControllerSdl_Close(slot);
	SlipJoystick_BindHostRead(NULL, NULL);
}
