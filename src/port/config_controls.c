#include "config_controls.h"
#include <string.h>

SlipConfigControlsState SlipConfigControls_state;
uint8_t SlipConfigControls_usedInputs[256];

void SlipConfigControls_Run(SlipConfigControlsState *state, uint32_t player, uint16_t configurationBackground,
                            const SlipConfigControlsCalls *calls) {
	const SlipConfigMenuCalls *const menu = calls->menu;
	const SlipConfigMenuDrawCalls *const draw = calls->draw;
	state->player = player;
	state->controlBinding = &SlipRace_controlBindings[0];
	if (player != 1)
		state->controlBinding = &SlipRace_controlBindings[1];
	SlipInputCode *const fields[] = {&state->controlBinding->up,        &state->controlBinding->left,
	                                 &state->controlBinding->right,     &state->controlBinding->down,
	                                 &state->controlBinding->select,    &state->controlBinding->fire,
	                                 &state->controlBinding->accelerate};
	menu->navigation(menu->context, SLIP_CONFIG_BINDINGS_TABLE);
	uint16_t handle;
	if (!menu->load(menu->context, "CONTROLS.SPR", &handle))
		menu->resourceError(menu->context);
	state->background = handle;
	if (!calls->copy(calls->context, &handle))
		menu->resourceError(menu->context);
	state->workingSprite = handle;
	if (!menu->load(menu->context, "CONBLOCK.SPR", &handle))
		menu->resourceError(menu->context);
	state->movementBlocker = handle;
	draw->clip(draw->context, draw->surface(draw->context));
	state->pendingBinding = NULL;
	menu->resetTimer(menu->context);
	for (;;) {
		menu->updateTimer(menu->context);
		bool readPointer = true;
		if (state->pendingBinding != NULL) {
			const int32_t input = calls->nextInput(calls->context);
			if (input < 0)
				readPointer = false;
			else if (calls->inputName(calls->context, (uint32_t)input) == NULL)
				readPointer = false;
			else {
				*state->pendingBinding = (SlipInputCode)(uint16_t)input;
				calls->showPointer(calls->context);
				state->pendingBinding = NULL;
			}
		}
		if (readPointer) {
			SlipConfigMenuPoint point = menu->pointer(menu->context);
			const uint32_t selected = menu->hitTest(menu->context, SLIP_CONFIG_BINDINGS_TABLE, point);
			state->selection = selected;
			if (menu->pressed(menu->context, SLIP_INPUT_SCAN_ENTER) ||
			    menu->pressed(menu->context, SLIP_INPUT_MOUSE_LEFT)) {
				if (selected != 0) {
					if (selected == 10)
						break;
					if (selected == 1 || selected == 2)
						calls->cycleMovement(calls->context, state->player);
					else {
						state->pendingBinding = fields[selected - 3];
						calls->hidePointer(calls->context);
					}
				}
			}
		}
		calls->drawBindings(calls->context, state);
		draw->clippedSprite(draw->context, configurationBackground, 0, 0);
		draw->clippedSprite(draw->context, state->workingSprite, 39, 53);
		menu->present(menu->context);
		menu->poll(menu->context);
		if (menu->pressed(menu->context, SLIP_INPUT_SCAN_ESCAPE)) {
			if (state->pendingBinding != NULL) {
				state->pendingBinding = NULL;
				calls->showPointer(calls->context);
			} else
				break;
		}
	}
	menu->release(menu->context, state->workingSprite);
	menu->release(menu->context, state->background);
	menu->release(menu->context, state->movementBlocker);
}

bool SlipConfigControls_Conflict(void) {
	memset(SlipConfigControls_usedInputs, 0, sizeof(SlipConfigControls_usedInputs));
	const SlipRaceControlBinding *const first = &SlipRace_controlBindings[0];
	const SlipRaceControlBinding *const second = &SlipRace_controlBindings[1];
	if (first->movementControl != 0 && second->movementControl != 0 &&
	    first->movementControl == second->movementControl)
		return true;
	const SlipInputCode *const firstKeys[] = {&first->left,       &first->right, &first->up,    &first->down,
	                                          &first->accelerate, &first->fire,  &first->select};
	const SlipInputCode *const secondKeys[] = {&second->left,       &second->right, &second->up,    &second->down,
	                                           &second->accelerate, &second->fire,  &second->select};
	unsigned remaining = 7, index = 0;
	if (first->movementControl != 0) {
		index += 4;
		remaining -= 4;
	}
	do {
		SlipConfigControls_usedInputs[(uint16_t)*firstKeys[index++]] = 1;
	} while (--remaining != 0);
	remaining = 7;
	index = 0;
	if (second->movementControl != 0) {
		index += 4;
		remaining -= 4;
	}
	do {
		if (SlipConfigControls_usedInputs[(uint16_t)*secondKeys[index++]] != 0)
			return true;
	} while (--remaining != 0);
	return false;
}

SlipConfigConflictState SlipConfigConflict_state;
SlipConfigCalibrationState SlipConfigCalibration_state;
uint8_t SlipConfigControls_calibrated[2];
SlipJoystickCalibration SlipConfigControls_calibration[2];

void SlipConfigControls_Warn(SlipConfigConflictState *state, uint16_t configurationBackground,
                             const SlipConfigDialogCalls *calls) {
	if (!SlipConfigControls_Conflict())
		return;
	const SlipConfigControlsCalls *const controls = calls->controls;
	const SlipConfigMenuCalls *const menu = controls->menu;
	const SlipConfigMenuDrawCalls *const draw = controls->draw;
	menu->navigation(menu->context, SLIP_CONFIG_CONFLICT_TABLE);
	uint16_t handle;
	if (!menu->load(menu->context, "JOYCAL.SPR", &handle))
		menu->resourceError(menu->context);
	state->background = handle;
	if (!controls->copy(controls->context, &handle))
		menu->resourceError(menu->context);
	state->workingSprite = handle;
	if (!menu->load(menu->context, "JOYCALD.SPR", &handle))
		menu->resourceError(menu->context);
	state->inactiveBackground = handle;
	draw->clip(draw->context, draw->surface(draw->context));
	menu->resetTimer(menu->context);
	for (;;) {
		menu->updateTimer(menu->context);
		SlipConfigMenuPoint point = menu->pointer(menu->context);
		point.x = (int16_t)(point.x - 39);
		point.y = (int16_t)(point.y - 53);
		const uint32_t selection = menu->hitTest(menu->context, SLIP_CONFIG_CONFLICT_TABLE, point);
		state->selection = selection;
		if (menu->pressed(menu->context, SLIP_INPUT_SCAN_ENTER) ||
		    menu->pressed(menu->context, SLIP_INPUT_MOUSE_LEFT)) {
			if (selection != 0)
				break;
		}
		calls->drawConflict(calls->context, state);
		draw->clippedSprite(draw->context, configurationBackground, 0, 0);
		draw->clippedSprite(draw->context, state->workingSprite, 39, 53);
		menu->present(menu->context);
		menu->poll(menu->context);
		if (menu->pressed(menu->context, SLIP_INPUT_SCAN_ESCAPE))
			break;
	}
	menu->release(menu->context, state->workingSprite);
	menu->release(menu->context, state->background);
	menu->release(menu->context, state->inactiveBackground);
}

void SlipConfigControls_Calibrate(SlipConfigCalibrationState *state, uint32_t joystick,
                                  uint16_t configurationBackground, const SlipConfigDialogCalls *calls) {
	const SlipConfigControlsCalls *const controls = calls->controls;
	const SlipConfigMenuCalls *const menu = controls->menu;
	const SlipConfigMenuDrawCalls *const draw = controls->draw;
	state->joystick = joystick;
	menu->navigation(menu->context, SLIP_CONFIG_CALIBRATION_TABLE);
	state->detected = 0;
	if (calls->calibration(calls->context, 0, state->joystick)) {
		state->detected = 1;
		calls->calibration(calls->context, 1, state->joystick);
	}
	uint16_t handle;
	if (!menu->load(menu->context, "JOYCAL.SPR", &handle))
		menu->resourceError(menu->context);
	state->background = handle;
	if (!controls->copy(controls->context, &handle))
		menu->resourceError(menu->context);
	state->workingSprite = handle;
	if (!menu->load(menu->context, "JOYCALD.SPR", &handle))
		menu->resourceError(menu->context);
	state->inactiveBackground = handle;
	draw->clip(draw->context, draw->surface(draw->context));
	menu->resetTimer(menu->context);
	for (;;) {
		menu->updateTimer(menu->context);
		SlipConfigMenuPoint point = menu->pointer(menu->context);
		point.x = (int16_t)(point.x - 39);
		point.y = (int16_t)(point.y - 53);
		const uint32_t selection = menu->hitTest(menu->context, SLIP_CONFIG_CALIBRATION_TABLE, point);
		state->selection = selection;
		if (menu->pressed(menu->context, SLIP_INPUT_SCAN_ENTER) ||
		    menu->pressed(menu->context, SLIP_INPUT_MOUSE_LEFT)) {
			if (selection != 0) {
				if (state->detected == 1) {
					if (!calls->calibration(calls->context, 3, state->joystick)) {
						state->detected = 0;
						continue;
					}
					const unsigned index = state->joystick == 0 ? 0 : 1;
					SlipConfigControls_calibrated[index] = 1;
					calls->readCalibration(calls->context, state->joystick, &SlipConfigControls_calibration[index]);
				}
				break;
			}
		}
		if (state->detected == 1)
			calls->calibration(calls->context, 2, state->joystick);
		calls->drawCalibration(calls->context, state);
		draw->clippedSprite(draw->context, configurationBackground, 0, 0);
		draw->clippedSprite(draw->context, state->workingSprite, 39, 53);
		menu->present(menu->context);
		menu->poll(menu->context);
		if (menu->pressed(menu->context, SLIP_INPUT_SCAN_ESCAPE))
			break;
	}
	menu->release(menu->context, state->workingSprite);
	menu->release(menu->context, state->background);
	menu->release(menu->context, state->inactiveBackground);
}

void SlipConfigControls_CycleMovement(uint32_t player, SlipConfigMousePresent mousePresent, void *context) {
	uint16_t limit = 3;
	if (mousePresent(context) != 0)
		limit = 4;
	if ((uint16_t)player != 2) {
		uint16_t mode = (uint16_t)(SlipRace_controlBindings[0].movementControl + 1u);
		if ((int16_t)mode >= (int16_t)limit)
			mode = 0;
		SlipRace_controlBindings[0].movementControl = mode;
	} else {
		uint16_t mode = (uint16_t)(SlipRace_controlBindings[1].movementControl + 1u);
		if ((int16_t)mode >= (int16_t)limit)
			mode = 0;
		SlipRace_controlBindings[1].movementControl = mode;
	}
}
