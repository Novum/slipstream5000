#include "game_music.h"
#include "race.h"
#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void SlipGameMusic_MenuBranch(SlipGameSoundState *game, uint32_t handle, uint32_t selection) {
	static const uint32_t branches[10] = {56, 59, 55, 58, 62, 53, 60, 57, 61, 54};
	if (game->musicCard == 0)
		return;

	assert(selection <= 10);
	SlipGameMusic_RequestBranch(game, handle, selection == 0 ? 63 : branches[selection - 1]);
}

const char *const SlipRaceMusic_names[4] = {"INGAME2.HMP", "INGAME3.HMP", "INGAME4.HMP", "INGAME6.HMP"};

const char *SlipResultMusic_Select(const struct SlipRaceRacerTable *racers) {
	uint32_t count = racers->racerCount;
	const SlipRaceRacerState *racer = racers->records;
	do {
		if (racer->racerType == 0) {
			const int16_t position = (int16_t)racer->racePosition;
			return position <= 3 ? "WIN.HMP" : "LOSE.HMP";
		}
		++racer;
	} while (--count != 0);
	return "WIN.HMP";
}

static const uint32_t raceMusicBranches[4][4] = {
    {62, 60, 59, 61}, {62, 60, 61, 59}, {63, 60, 62, 61}, {62, 61, 60, 59}};

uint32_t SlipRaceMusic_Select(uint32_t (*memoryQuery)(void)) {
	uint32_t selection = SlipRandom_Range(3);
	if (selection == 1) {
		const uint32_t bytes = memoryQuery();

		if ((int32_t)bytes < 0x80000)
			++selection;
	}
	return selection;
}

void SlipRaceMusic_InitializeBranches(SlipRaceMusicState *race) {
	race->branch = raceMusicBranches[race->selection][1];
	race->previousBranch = UINT32_MAX;
	race->countdown = 10000;
}

void SlipRaceMusic_ConfigurationReturn(SlipRaceMusicState *race, SlipGameSoundState *game, HmiMusicState *music,
                                       HmiTimerState *timer, HmiMusicBranchRecord *branchStorage) {
	if (game->musicCard == 0)
		return;
	if (game->musicVolumeSetting == 0) {
		if (race->handle == 0)
			return;
		SlipGameMusic_Stop(game, music, timer, race->handle);
		race->handle = 0;
	} else {
		if (race->handle != 0)
			return;
		race->handle = SlipGameMusic_Start(game, music, timer, race->song, branchStorage);
	}
}

void SlipRaceMusic_Update(SlipRaceMusicState *race, SlipGameSoundState *game, uint32_t delta, uint32_t position) {

	static const uint32_t positionBranches[10] = {3, 2, 2, 3, 3, 2, 2, 3, 3, 0};
	if (game->musicCard == 0)
		return;
	if (race->countdown != 0) {
		race->countdown -= delta;
		if (race->countdown != 0) {
			if ((race->countdown & 0x80000000u) == 0)
				return;
			race->countdown = 0;
		}
	}
	if ((position & 0x8000u) != 0 || position == 0) {
		race->branch = raceMusicBranches[race->selection][1];
	} else {
		position &= 0x7fffu;
		race->branch = raceMusicBranches[race->selection][positionBranches[position - 1]];
	}
	if (race->branch == race->previousBranch)
		return;
	race->previousBranch = race->branch;
	SlipGameMusic_RequestBranch(game, race->handle, race->branch);
}

void SlipGameMusic_ApplySetting(SlipGameSoundState *state, HmiMusicState *music) {
	static const uint32_t volumes[3] = {0, 63, 127};
	/* Original setting word is constrained to 0..2 by configuration. */
	SlipGameMusic_SetVolume(state, music, volumes[state->musicVolumeSetting]);
}

uint32_t SlipGameMusic_Start(SlipGameSoundState *state, HmiMusicState *music, HmiTimerState *timer, uint8_t *song,
                             HmiMusicBranchRecord *branchStorage) {
	uint32_t slot, error, trigger;
	if (state->initialized == 0)
		return 0;

	if (state->musicVolumeSetting == 0)
		return 0;
	if (state->musicCard == 0)
		return 0;
	state->branchRequest = 0;
	state->musicDescriptor.completionCallbackSelector = 0;
	state->musicDescriptor.completionCallbackOffset = 0;
	state->musicDescriptor.songData = song;
	error = HmiMusic_Register(music, &state->musicDescriptor, state->musicRouting, &slot, branchStorage, NULL);
	if (error == 0)
		error = HmiMusic_StartSong(music, timer, slot);
	for (trigger = 0; trigger < 128 && error == 0; ++trigger)
		error = HmiMusic_SetTriggerCallback(music, slot, (uint8_t)trigger, SlipGameMusic_TriggerCallback);
	if (error != 0) {
		SlipRuntime_Shutdown();
		printf("ERROR: Couldn't play song: %s.\n", HmiMusic_ErrorString(error));
		exit(1);
	}
	return slot + 0x10000;
}

void SlipGameMusic_Stop(SlipGameSoundState *state, HmiMusicState *music, HmiTimerState *timer, uint32_t handle) {
	uint32_t error;
	if (state->initialized != 0) {
		if (state->musicCard != 0) {
			if (handle != 0) {
				state->branchSong = 0;
				handle &= 0xffff;
				state->branchRequest = 0;
				error = HmiMusic_StopSong(music, timer, handle);
				if (error == 0)
					error = HmiMusic_Unregister(music, handle);
				if (error != 0) {
					SlipRuntime_Shutdown();

					printf("ERROR: Couldn't stop song: %s.\n", HmiMusic_ErrorString(error));
					exit(1);
				}
			}
		}
	}
}

void SlipGameMusic_SetVolume(SlipGameSoundState *state, HmiMusicState *music, uint32_t volume) {
	if (state->initialized != 0) {
		if (state->musicCard != 0)
			HmiMusic_SetMasterVolume(music, (uint8_t)volume);
	}
}

void SlipGameMusic_TimerCallback(SlipGameTimerState *timer) {
	SlipGameMusic_Timer(timer->gameSound, timer->musicTimer);
}

void SlipGameMusic_Timer(SlipGameSoundState *state, HmiTimerState *timer) {
	const uint32_t previous = state->timerGuard;
	state->timerGuard |= 1;
	if ((previous & 1) != 0)
		return;
	HmiTimer_Dispatch(timer);
	state->timerGuard = 0;
}

void SlipGameMusic_TriggerCallback(HmiMusicState *music, uint32_t song, uint8_t track, uint8_t id) {
	(void)track;

	(void)SlipGameMusic_Trigger(music->gameSound, music, song, id);
}

void SlipGameMusic_RequestBranch(SlipGameSoundState *state, uint32_t handle, uint32_t request) {
	if (state->initialized != 0) {
		if (state->musicCard != 0) {
			if (handle != 0) {
				handle &= 0xffff;
				state->branchRequest = request;
				state->branchSong = handle;
			}
		}
	}
}

uint32_t SlipGameMusic_Trigger(SlipGameSoundState *state, HmiMusicState *music, uint32_t song, uint8_t id) {
	id &= 0x7f;
	if (id < 4) {
		if (state->branchRequest != 0) {
			if (song == state->branchSong) {
				HmiMusic_BranchSong(music, song, (uint8_t)state->branchRequest);
				state->branchRequest = 0;
			}
		}
	}
	return 0;
}
