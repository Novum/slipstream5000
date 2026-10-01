#include "vehicle_select.h"
#include "frame_timer.h"

void SlipVehicleSelection_Update(SlipVehicleSelection *state, uint32_t gameMode, uint16_t excludedVehicle) {
	if (gameMode == 1) {
		const uint16_t elapsed = (uint16_t)SlipFrameTimer_Values().deltaMilliseconds;
		state->titleDelay = (uint16_t)(state->titleDelay - elapsed);
		if ((int16_t)state->titleDelay < 0) {
			state->titleDelay = 1500;
			uint32_t title = 0x5449544c; /* TITL */
			if (state->titleTag == title) {
				title = 0x504c5931; /* PLY1 */
				if (excludedVehicle != 0)
					++title; /* PLY2 */
			}
			state->titleTag = title;
			state->backgroundRedraws = 2;
		}
	}
	for (;;) {
		const uint16_t elapsed = (uint16_t)SlipFrameTimer_Values().deltaMilliseconds;
		state->doorElapsed = (uint16_t)(state->doorElapsed + elapsed);
		if (state->doorElapsed < 100)
			return;
		state->doorElapsed = (uint16_t)(state->doorElapsed - 100);
		uint16_t selection = state->selectedVehicle;
		for (unsigned i = 0; i < 10; ++i) {
			SlipVehicleDoor *const door = &state->doors[i];
			selection = (uint16_t)(selection - 1);
			if (selection == 0) {
				if (door->frame != 4)
					++door->frame;
				door->redrawPasses = 2;
			} else if (door->frame != 0) {
				--door->frame;
				door->redrawPasses = 2;
			}
		}
	}
}
