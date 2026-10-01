#include "config_menu.h"
#include "config_controls.h"
#include "race_player.h"

SlipConfigMenuState SlipConfigMenu_state = {.allowDifficulty = 1};

void SlipConfigMenu_Acquire(SlipConfigMenuState *state, const SlipConfigMenuCalls *calls) {
	void *const context = calls->context;
	if (state->references == 0) {
		if (!calls->load(context, "CNFFONT.FNT", &state->font))
			calls->resourceError(context);
		calls->selectFont(context, state->font);
		calls->textColor(context, UINT16_MAX);
		if (!calls->load(context, "GARAGEA.SPR", &state->background))
			calls->resourceError(context);
		calls->palette(context, state->background);
		if (!calls->load(context, "GARAGEB.SPR", &state->inactiveBackground))
			calls->resourceError(context);
		if (!calls->load(context, "CNFBLOCK.SPR", &state->blocker))
			calls->resourceError(context);
	}
	state->references = (uint16_t)(state->references + 1);
}

void SlipConfigMenu_Release(SlipConfigMenuState *state, const SlipConfigMenuCalls *calls) {
	state->references = (uint16_t)(state->references - 1);
	if (state->references == 0) {
		calls->release(calls->context, state->font);
		calls->release(calls->context, state->background);
		calls->release(calls->context, state->inactiveBackground);
		calls->release(calls->context, state->blocker);
	}
}

void SlipConfigMenu_Run(SlipConfigMenuState *state, const SlipConfigMenuCalls *calls) {
	void *const context = calls->context;
	if (state->allowDifficulty != 0) {
		SlipConfig_mode = -1;
		SlipConfig_damageOverride = -1;
	}
	calls->language(context);
	if (!calls->strings(context, "CONFIG  ", &state->mainStrings))
		calls->resourceError(context);
	SlipConfigMenu_Acquire(state, calls);
	calls->resetTimer(context);
	calls->navigation(context, SLIP_CONFIG_MAIN_TABLE);
	for (;;) {
		calls->updateTimer(context);
		SlipConfigMenuPoint point = calls->pointer(context);
		const uint32_t selection = calls->hitTest(context, SLIP_CONFIG_MAIN_TABLE, point);
		if (selection != 1) {
			state->mainSelection = selection;
			if (calls->pressed(context, SLIP_INPUT_SCAN_ENTER) || calls->pressed(context, SLIP_INPUT_MOUSE_LEFT)) {
				if (selection != 0) {
					bool returnedFromSubmenu = true;
					if (selection == 2) {
						SlipConfigMenu_General(state, calls);
						calls->releaseStrings(context, state->mainStrings);
						calls->language(context);
						if (!calls->strings(context, "CONFIG  ", &state->mainStrings))
							calls->resourceError(context);
					} else if (selection == 3) {
						SlipConfigMenu_Controls(state, calls);
					} else if (selection == 7) {
						SlipConfigMenu_Sound(state, calls);
					} else if (selection == 4) {
						SlipConfigMenu_Detail(state, calls);
					} else if (selection == 6 && state->allowDifficulty != 0) {
						SlipConfigMenu_Difficulty(state, calls);
					} else {
						returnedFromSubmenu = false;
						if (selection == 5)
							break;
					}
					if (returnedFromSubmenu) {
						calls->palette(context, state->background);
						state->mainSelection = 0;
						calls->navigation(context, SLIP_CONFIG_MAIN_TABLE);
						calls->textColor(context, UINT16_MAX);
					}
				}
			}
		}
		calls->drawMain(context, state);
		calls->present(context);
		calls->poll(context);
		if (calls->pressed(context, SLIP_INPUT_SCAN_ESCAPE))
			break;
	}
	calls->clearNavigation(context);
	SlipConfigMenu_Release(state, calls);
	calls->releaseStrings(context, state->mainStrings);
	calls->save(context);
}

void SlipConfigMenu_Difficulty(SlipConfigMenuState *state, const SlipConfigMenuCalls *calls) {
	void *const context = calls->context;
	const uint16_t savedFont = calls->currentFont(context);
	SlipConfigMenu_Acquire(state, calls);
	calls->language(context);
	if (!calls->strings(context, "DIFF    ", &state->difficultyStrings))
		calls->resourceError(context);
	calls->navigation(context, SLIP_CONFIG_DIFFICULTY_TABLE);
	calls->resetTimer(context);
	void (*const options[5])(void *) = {calls->cycleDifficulty, calls->toggleDamage, NULL, NULL, NULL};
	for (;;) {
		calls->updateTimer(context);
		SlipConfigMenuPoint point = calls->pointer(context);
		const uint32_t selection = calls->hitTest(context, SLIP_CONFIG_DIFFICULTY_TABLE, point);
		if (selection != 4) {
			state->difficultySelection = selection;
			if (calls->pressed(context, SLIP_INPUT_SCAN_ENTER) || calls->pressed(context, SLIP_INPUT_MOUSE_LEFT)) {
				if (selection != 0) {
					if (selection == 3)
						break;
					if (options[selection - 1] != NULL)
						options[selection - 1](context);
				}
			}
		}
		calls->drawDifficulty(context, state);
		calls->present(context);
		calls->poll(context);
		if (calls->pressed(context, SLIP_INPUT_SCAN_ESCAPE))
			break;
	}
	calls->clearNavigation(context);
	SlipConfigMenu_Release(state, calls);
	calls->releaseStrings(context, state->difficultyStrings);
	calls->selectFont(context, savedFont);
}

void SlipConfigMenu_CycleLanguage(SlipConfigMenuState *state, const SlipConfigMenuCalls *calls) {
	void *const context = calls->context;
	calls->cycleLanguage(context);
	calls->releaseStrings(context, state->generalStrings);
	calls->language(context);
	if (!calls->strings(context, "GENERAL ", &state->generalStrings))
		calls->resourceError(context);
}

void SlipConfigMenu_CycleMusic(const SlipConfigMenuCalls *calls) {
	calls->cycleMusic(calls->context);
	calls->applyMusic(calls->context);
}

void SlipConfigMenu_General(SlipConfigMenuState *state, const SlipConfigMenuCalls *calls) {
	void *const context = calls->context;
	const uint16_t savedFont = calls->currentFont(context);
	SlipConfigMenu_Acquire(state, calls);
	calls->language(context);
	if (!calls->strings(context, "GENERAL ", &state->generalStrings))
		calls->resourceError(context);
	calls->navigation(context, SLIP_CONFIG_GENERAL_TABLE);
	calls->resetTimer(context);
	for (;;) {
		calls->updateTimer(context);
		SlipConfigMenuPoint point = calls->pointer(context);
		const uint32_t selection = calls->hitTest(context, SLIP_CONFIG_GENERAL_TABLE, point);
		if (selection != 7) {
			state->generalSelection = selection;
			if (calls->pressed(context, SLIP_INPUT_SCAN_ENTER) || calls->pressed(context, SLIP_INPUT_MOUSE_LEFT)) {
				if (selection != 0) {
					if (selection == 6)
						break;
					calls->generalOptions[selection - 1](context);
				}
			}
		}
		calls->drawGeneral(context, state);
		calls->present(context);
		calls->poll(context);
		if (calls->pressed(context, SLIP_INPUT_SCAN_ESCAPE))
			break;
	}
	calls->clearNavigation(context);
	SlipConfigMenu_Release(state, calls);
	calls->releaseStrings(context, state->generalStrings);
	calls->selectFont(context, savedFont);
}

void SlipConfigMenu_Detail(SlipConfigMenuState *state, const SlipConfigMenuCalls *calls) {
	void *const context = calls->context;
	const uint16_t savedFont = calls->currentFont(context);
	SlipConfigMenu_Acquire(state, calls);
	calls->language(context);
	if (!calls->strings(context, "DETAIL  ", &state->detailStrings))
		calls->resourceError(context);
	calls->navigation(context, SLIP_CONFIG_DETAIL_TABLE);
	calls->resetTimer(context);
	for (;;) {
		calls->updateTimer(context);
		SlipConfigMenuPoint point = calls->pointer(context);
		const uint32_t selection = calls->hitTest(context, SLIP_CONFIG_DETAIL_TABLE, point);
		if (selection != 8) {
			state->detailSelection = selection;
			if (calls->pressed(context, SLIP_INPUT_SCAN_ENTER) || calls->pressed(context, SLIP_INPUT_MOUSE_LEFT)) {
				if (selection != 0) {
					if (selection == 7)
						break;
					calls->detailOptions[selection - 1](context);
				}
			}
		}
		calls->drawDetail(context, state);
		calls->present(context);
		calls->poll(context);
		if (calls->pressed(context, SLIP_INPUT_SCAN_ESCAPE))
			break;
	}
	calls->clearNavigation(context);
	SlipConfigMenu_Release(state, calls);
	calls->releaseStrings(context, state->detailStrings);
	calls->selectFont(context, savedFont);
}

void SlipConfigMenu_Sound(SlipConfigMenuState *state, const SlipConfigMenuCalls *calls) {
	void *const context = calls->context;
	const uint16_t savedFont = calls->currentFont(context);
	SlipConfigMenu_Acquire(state, calls);
	calls->language(context);
	if (!calls->strings(context, "SOUND   ", &state->soundStrings))
		calls->resourceError(context);
	calls->navigation(context, SLIP_CONFIG_SOUND_TABLE);
	calls->resetTimer(context);
	for (;;) {
		calls->updateTimer(context);
		SlipConfigMenuPoint point = calls->pointer(context);
		const uint32_t selection = calls->hitTest(context, SLIP_CONFIG_SOUND_TABLE, point);
		if (selection != 6) {
			state->soundSelection = selection;
			if (calls->pressed(context, SLIP_INPUT_SCAN_ENTER) || calls->pressed(context, SLIP_INPUT_MOUSE_LEFT)) {
				if (selection != 0) {
					if (selection == 5)
						break;
					if (calls->soundOptions[selection - 1] != NULL)
						calls->soundOptions[selection - 1](context);
				}
			}
		}
		calls->drawSound(context, state);
		calls->present(context);
		calls->poll(context);
		if (calls->pressed(context, SLIP_INPUT_SCAN_ESCAPE))
			break;
	}
	calls->clearNavigation(context);
	SlipConfigMenu_Release(state, calls);
	calls->releaseStrings(context, state->soundStrings);
	calls->selectFont(context, savedFont);
}

void SlipConfigMenu_Controls(SlipConfigMenuState *state, const SlipConfigMenuCalls *calls) {
	void *const context = calls->context;
	SlipConfigMenu_Acquire(state, calls);
	calls->language(context);
	if (!calls->strings(context, "CONTROLS", &state->controlsStrings))
		calls->resourceError(context);
	calls->navigation(context, SLIP_CONFIG_CONTROLS_TABLE);
	calls->resetTimer(context);
	for (;;) {
		calls->updateTimer(context);
		SlipConfigMenuPoint point = calls->pointer(context);
		const uint32_t selection = calls->hitTest(context, SLIP_CONFIG_CONTROLS_TABLE, point);
		if (selection != 7) {
			state->controlsSelection = selection;
			if (calls->pressed(context, SLIP_INPUT_SCAN_ENTER) || calls->pressed(context, SLIP_INPUT_MOUSE_LEFT)) {
				if (selection != 0) {
					if (selection == 6)
						break;
					if (selection == 3) {
						SlipConfigControls_Calibrate(&SlipConfigCalibration_state, 0, state->background,
						                             calls->controlDialogs);
						calls->navigation(context, SLIP_CONFIG_CONTROLS_TABLE);
					} else if (selection == 4) {
						SlipConfigControls_Calibrate(&SlipConfigCalibration_state, 1, state->background,
						                             calls->controlDialogs);
						calls->navigation(context, SLIP_CONFIG_CONTROLS_TABLE);
					} else if (selection == 1) {
						SlipConfigControls_Run(&SlipConfigControls_state, 1, state->background,
						                       calls->controlDialogs->controls);
						calls->navigation(context, SLIP_CONFIG_CONTROLS_TABLE);
					} else if (selection == 2) {
						SlipConfigControls_Run(&SlipConfigControls_state, 2, state->background,
						                       calls->controlDialogs->controls);
						calls->navigation(context, SLIP_CONFIG_CONTROLS_TABLE);
					} else if (selection == 5) {
						calls->reverseAccelerator(context);
					}
				}
			}
		}
		calls->drawControls(context, state);
		calls->present(context);
		calls->poll(context);
		if (calls->pressed(context, SLIP_INPUT_SCAN_ESCAPE))
			break;
	}
	calls->clearNavigation(context);
	SlipConfigControls_Warn(&SlipConfigConflict_state, state->background, calls->controlDialogs);
	SlipConfigMenu_Release(state, calls);
	calls->releaseStrings(context, state->controlsStrings);
}
