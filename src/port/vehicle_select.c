#include "vehicle_select.h"
#include "frame_timer.h"
#include "string_tags.h"

enum { SLIP_VEHICLE_TITLE_DELAY_MS = 1500, SLIP_VEHICLE_DOOR_FRAME_DELAY_MS = 100 };

void SlipVehicleSelection_Update(SlipVehicleSelection *state, uint32_t gameMode, uint16_t excludedVehicle) {
	if (gameMode == SLIP_RACE_GAME_SPLIT_SCREEN) {
		const uint16_t elapsed = (uint16_t)SlipFrameTimer_Values().deltaMilliseconds;
		state->titleDelay = (uint16_t)(state->titleDelay - elapsed);
		if ((int16_t)state->titleDelay < 0) {
			state->titleDelay = SLIP_VEHICLE_TITLE_DELAY_MS;
			uint32_t title = SLIP_STRING_TITLE; /* TITL */
			if (state->titleTag == title) {
				title = SLIP_STRING_FIRST_PLAYER; /* PLY1 */
				if (excludedVehicle != 0)
					++title; /* PLY2 */
			}
			state->titleTag = title;
			state->backgroundRedraws = SLIP_VEHICLE_SELECTION_REDRAW_PASSES;
		}
	}
	for (;;) {
		const uint16_t elapsed = (uint16_t)SlipFrameTimer_Values().deltaMilliseconds;
		state->doorElapsed = (uint16_t)(state->doorElapsed + elapsed);
		if (state->doorElapsed < SLIP_VEHICLE_DOOR_FRAME_DELAY_MS)
			return;
		state->doorElapsed = (uint16_t)(state->doorElapsed - SLIP_VEHICLE_DOOR_FRAME_DELAY_MS);
		uint16_t selection = state->selectedVehicle;
		for (unsigned i = 0; i < SLIP_RACE_RACER_COUNT; ++i) {
			SlipVehicleDoor *const door = &state->doors[i];
			selection = (uint16_t)(selection - 1);
			if (selection == 0) {
				if (door->frame != SLIP_VEHICLE_DOOR_FRAME_COUNT)
					++door->frame;
				door->redrawPasses = SLIP_VEHICLE_SELECTION_REDRAW_PASSES;
			} else if (door->frame != 0) {
				--door->frame;
				door->redrawPasses = SLIP_VEHICLE_SELECTION_REDRAW_PASSES;
			}
		}
	}
}
