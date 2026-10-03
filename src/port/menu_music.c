#include "menu_music.h"
#include "config_settings.h"
#include "game_errors.h"
#include "game_music.h"
#include "hmi_opl_output.h"
#include "resource.h"
#include "resource_host.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

enum {
	SLIP_MENU_MUSIC_TIMER_RATE_HZ = 1000,
	SLIP_MENU_MUSIC_CALLBACK_RATE_HZ = 120,
	/* Each track has an eight-bit branch count. */
	SLIP_MENU_MUSIC_BRANCH_VIEW_CAPACITY = HMI_MUSIC_TRACK_COUNT * UINT8_MAX
};

static struct {
	HmiMusicState music;
	HmiA002State driver;
	HmiTimerState timer;
	SlipGameTimerState gameTimer;
	HmiOplOutput output;
	SlipGameSoundState *game;
	SlipResourcePayload melodic, percussion;
	uint16_t songResource;
	uint16_t melodicHandle, percussionHandle;
	HmiMusicBranchRecord *branches;
	const char *archive;
	uint32_t playbackHandle;
	SlipRaceMusicState race;
	uint16_t raceResource;
	HmiMusicBranchRecord *raceBranches;
	uint16_t resultsResource;
	HmiMusicBranchRecord *resultsBranches;
	uint32_t resultsHandle;
	uint16_t standingsResource;
	HmiMusicBranchRecord *standingsBranches;
	uint32_t standingsHandle;
	uint16_t scriptedResource;
	HmiMusicBranchRecord *scriptedBranches;
	uint32_t scriptedHandle;
	bool ready;
} menuMusic;

/* No BIOS runs on the host. This acknowledges the modeled PIC boundary. */
static void SlipMenuMusic_BiosIrq(SlipGameTimerState *timer) {
	timer->picEndOfInterruptCommand = HMI_TIMER_PIC_END_OF_INTERRUPT;
}

bool SlipMenuMusic_Open(const char *archive, SlipGameSoundState *game) {
	uint32_t i, error;
	menuMusic.archive = archive;
	menuMusic.game = game;
	HmiMusic_Construct(&menuMusic.music);
	HmiA002_ConstructStatic(&menuMusic.driver);
	HmiTimer_Construct(&menuMusic.timer);
	HmiTimer_Initialize(&menuMusic.timer, 0, HMI_TIMER_SKIP_HARDWARE);
	menuMusic.gameTimer = (SlipGameTimerState){0};
	menuMusic.music.gameSound = game;
	menuMusic.gameTimer.gameSound = game;
	menuMusic.gameTimer.musicTimer = &menuMusic.timer;
	menuMusic.gameTimer.rate = SLIP_MENU_MUSIC_TIMER_RATE_HZ;
	menuMusic.gameTimer.divisor = HMI_TIMER_PIT_CLOCK_HZ / SLIP_MENU_MUSIC_TIMER_RATE_HZ;
	menuMusic.gameTimer.savedDivisor = HMI_TIMER_MAXIMUM_DIVISOR;
	menuMusic.gameTimer.biosCountdown = HMI_TIMER_MAXIMUM_DIVISOR;
	menuMusic.gameTimer.savedVector = SlipMenuMusic_BiosIrq;
	HmiOplOutput_Construct(&menuMusic.output, &menuMusic.driver, &menuMusic.gameTimer);
	HmiA002_BindNativeFunctions(&menuMusic.music, 0, &menuMusic.driver);
	menuMusic.music.driverIds[0] = HMI_MUSIC_DRIVER_A002;
	error = menuMusic.music.driverInit[0](&menuMusic.music, 0, HMI_OPL_ADDRESS_PORT);
	assert(error == 0);
	if (error != 0)
		goto failure;
	for (i = 0; i < HMI_MIDI_CHANNEL_COUNT; ++i)
		HmiA002_Reset(&menuMusic.driver, i);

	if (!SlipResourceHost_Load(NULL, "MELODIC.BNK", &menuMusic.melodicHandle))
		menuMusic.melodicHandle = 0;
	if (!SlipResourceHost_Load(NULL, "DRUM.BNK", &menuMusic.percussionHandle))
		menuMusic.percussionHandle = 0;
	if (menuMusic.melodicHandle != 0) {
		uint32_t size;
		SlipResourceHost_Size(NULL, menuMusic.melodicHandle, &size);
		uint8_t *const data = SlipResourceHost_LockWritable(NULL, menuMusic.melodicHandle);
		menuMusic.melodic = SlipResourceHost_Payload(menuMusic.melodicHandle);
		HmiMusic_SetBank(&menuMusic.music, 0, size, data);
	}
	if (menuMusic.percussionHandle != 0) {
		uint32_t size;
		SlipResourceHost_Size(NULL, menuMusic.percussionHandle, &size);
		uint8_t *const data = SlipResourceHost_LockWritable(NULL, menuMusic.percussionHandle);
		menuMusic.percussion = SlipResourceHost_Payload(menuMusic.percussionHandle);
		HmiMusic_SetBank(&menuMusic.music, 0, size, data);
	}

	error = SlipGameTimer_Register(&menuMusic.gameTimer, SLIP_MENU_MUSIC_CALLBACK_RATE_HZ, SlipGameMusic_TimerCallback);
	assert(error == 0);
	if (error != 0)
		goto failure;
	if (!HmiOplOutput_Open(&menuMusic.output))
		goto failure;
	game->musicCard = HMI_MUSIC_DRIVER_A002;
	game->musicVolumeSetting = 1;
	menuMusic.ready = true;
	if (!HmiOplOutput_Resume(&menuMusic.output))
		goto failure;
	return true;
failure:
	SlipMenuMusic_Close();
	return false;
}

static void SlipMenuMusic_ResumeLocked(void) {
	if (menuMusic.playbackHandle == 0 && menuMusic.songResource != 0) {
		uint8_t *const song = SlipResourceHost_LockWritable(NULL, menuMusic.songResource);
		if (menuMusic.branches == NULL) {
			menuMusic.branches = calloc(SLIP_MENU_MUSIC_BRANCH_VIEW_CAPACITY, sizeof(*menuMusic.branches));
			if (menuMusic.branches == NULL) {
				fprintf(stderr, "Unable to allocate native HMI branch pointer views.\n");
				return; /* Host pointer-view execution limitation. */
			}
		}
		menuMusic.playbackHandle =
		    SlipGameMusic_Start(menuMusic.game, &menuMusic.music, &menuMusic.timer, song, menuMusic.branches);
		if (menuMusic.playbackHandle == 0)
			SlipResourceHost_Unlock(NULL, menuMusic.songResource);
	}
}

void SlipMenuMusic_Start(void) {
	if (!menuMusic.ready)
		return;
	SDL_LockAudioStream(menuMusic.output.stream);
	if (menuMusic.game->musicCard != 0) {
		if (menuMusic.songResource != 0) {
			if (menuMusic.playbackHandle == 0)
				SlipMenuMusic_ResumeLocked();
		} else {
			if (!SlipResourceHost_Load(NULL, "INTRO.HMP", &menuMusic.songResource))
				SlipGame_ResourceFailure();
			SlipMenuMusic_ResumeLocked();
		}
	}
	SDL_UnlockAudioStream(menuMusic.output.stream);
}

void SlipMenuMusic_Stop(void) {
	if (!menuMusic.ready)
		return;
	SDL_LockAudioStream(menuMusic.output.stream);
	if (menuMusic.game->musicCard != 0) {
		if (menuMusic.playbackHandle != 0) {
			SlipGameMusic_Stop(menuMusic.game, &menuMusic.music, &menuMusic.timer, menuMusic.playbackHandle);
			SlipResourceHost_Unlock(NULL, menuMusic.songResource);
			menuMusic.playbackHandle = 0;
		}
		if (menuMusic.songResource != 0) {
			SlipResourceHost_Release(NULL, menuMusic.songResource);
			menuMusic.songResource = 0;
		}
		free(menuMusic.branches);
		menuMusic.branches = NULL;
	}
	SDL_UnlockAudioStream(menuMusic.output.stream);
}

void SlipMenuMusic_Resume(void) {
	if (!menuMusic.ready)
		return;
	SDL_LockAudioStream(menuMusic.output.stream);
	if (menuMusic.game->musicCard != 0)
		SlipMenuMusic_ResumeLocked();
	SDL_UnlockAudioStream(menuMusic.output.stream);
}

void SlipMenuMusic_StopRetainingSong(void) {
	if (!menuMusic.ready)
		return;
	SDL_LockAudioStream(menuMusic.output.stream);
	if (menuMusic.game->musicCard != 0 && menuMusic.playbackHandle != 0) {
		SlipGameMusic_Stop(menuMusic.game, &menuMusic.music, &menuMusic.timer, menuMusic.playbackHandle);
		menuMusic.playbackHandle = 0;
		SlipResourceHost_Unlock(NULL, menuMusic.songResource);
	}
	SDL_UnlockAudioStream(menuMusic.output.stream);
}

void SlipMenuMusic_Branch(uint32_t selection) {
	if (!menuMusic.ready)
		return;
	SDL_LockAudioStream(menuMusic.output.stream);
	SlipGameMusic_MenuBranch(menuMusic.game, menuMusic.playbackHandle, selection);
	SDL_UnlockAudioStream(menuMusic.output.stream);
}

void SlipMenuMusic_Close(void) {

	if (menuMusic.game == NULL || menuMusic.game->initialized == 0)
		return;
	HmiOplOutput_Close(&menuMusic.output);
	HmiTimer_Shutdown(&menuMusic.timer, 0);
	SlipGameTimer_Remove(&menuMusic.gameTimer, SlipGameMusic_TimerCallback);
	if (menuMusic.driver.portWrite != NULL)
		HmiA002_ShutdownEntry(&menuMusic.driver);
	menuMusic.ready = false;
	if (menuMusic.game != NULL)
		menuMusic.game->musicCard = 0;

	if (menuMusic.melodicHandle != 0) {
		SlipResourceHost_Unlock(NULL, menuMusic.melodicHandle);
		SlipResourceHost_Release(NULL, menuMusic.melodicHandle);
	}
	if (menuMusic.percussionHandle != 0) {
		SlipResourceHost_Unlock(NULL, menuMusic.percussionHandle);
		SlipResourceHost_Release(NULL, menuMusic.percussionHandle);
	}
	--menuMusic.game->initialized;
}

uint64_t SlipMenuMusic_NonzeroFrames(void) {
	uint64_t result;
	if (!menuMusic.ready)
		return 0;
	SDL_LockAudioStream(menuMusic.output.stream);
	result = menuMusic.output.nonzeroFrames;
	SDL_UnlockAudioStream(menuMusic.output.stream);
	return result;
}

void SlipMenuMusic_SetSetting(uint16_t setting) {
	if (!menuMusic.ready)
		return;
	assert(setting < SLIP_CONFIG_MUSIC_SETTING_COUNT);
	SDL_LockAudioStream(menuMusic.output.stream);
	menuMusic.game->musicVolumeSetting = setting;
	SlipGameMusic_ApplySetting(menuMusic.game, &menuMusic.music);
	SDL_UnlockAudioStream(menuMusic.output.stream);
}

/* Read-only diagnostic at the host synchronization boundary. */
uint32_t SlipMenuMusic_PendingBranch(void) {
	uint32_t result;
	if (!menuMusic.ready)
		return 0;
	SDL_LockAudioStream(menuMusic.output.stream);
	result = menuMusic.game->branchRequest;
	SDL_UnlockAudioStream(menuMusic.output.stream);
	return result;
}

bool SlipMenuMusic_RaceStart(uint32_t selection) {
	if (!menuMusic.ready)
		return false;
	assert(selection < SLIP_RACE_MUSIC_SONG_COUNT);
	SDL_LockAudioStream(menuMusic.output.stream);
	assert(menuMusic.raceResource == 0);
	menuMusic.race.selection = selection;
	if (menuMusic.game->musicCard != 0) {
		if (!SlipResourceHost_Load(NULL, SlipRaceMusic_names[selection], &menuMusic.raceResource))
			SlipGame_ResourceFailure();
		menuMusic.race.song = SlipResourceHost_LockWritable(NULL, menuMusic.raceResource);
		menuMusic.raceBranches = calloc(SLIP_MENU_MUSIC_BRANCH_VIEW_CAPACITY, sizeof(*menuMusic.raceBranches));
		if (menuMusic.raceBranches == NULL) {
			SlipResourceHost_Unlock(NULL, menuMusic.raceResource);
			SlipResourceHost_Release(NULL, menuMusic.raceResource);
			menuMusic.raceResource = 0;
			SDL_UnlockAudioStream(menuMusic.output.stream);
			return false;
		}
		menuMusic.race.handle = SlipGameMusic_Start(menuMusic.game, &menuMusic.music, &menuMusic.timer,
		                                            menuMusic.race.song, menuMusic.raceBranches);
		SlipRaceMusic_InitializeBranches(&menuMusic.race);
	} else {
		menuMusic.race.handle = 0;
	}
	SDL_UnlockAudioStream(menuMusic.output.stream);
	return true;
}

void SlipMenuMusic_RaceStop(void) {
	if (!menuMusic.ready)
		return;
	SDL_LockAudioStream(menuMusic.output.stream);
	if (menuMusic.game->musicCard != 0) {
		SlipGameMusic_Stop(menuMusic.game, &menuMusic.music, &menuMusic.timer, menuMusic.race.handle);
		SlipResourceHost_Unlock(NULL, menuMusic.raceResource);
		SlipResourceHost_Release(NULL, menuMusic.raceResource);
		menuMusic.raceResource = 0;
		free(menuMusic.raceBranches);
		menuMusic.raceBranches = NULL;
		/* Native close may run after race teardown; retire its ownership. */
		menuMusic.race.handle = 0;
		menuMusic.race.song = NULL;
	}
	SDL_UnlockAudioStream(menuMusic.output.stream);
}

void SlipMenuMusic_RaceConfigurationReturn(void) {
	if (!menuMusic.ready)
		return;
	SDL_LockAudioStream(menuMusic.output.stream);
	SlipRaceMusic_ConfigurationReturn(&menuMusic.race, menuMusic.game, &menuMusic.music, &menuMusic.timer,
	                                  menuMusic.raceBranches);
	SDL_UnlockAudioStream(menuMusic.output.stream);
}

void SlipMenuMusic_RaceUpdate(uint32_t delta, uint32_t position) {
	if (!menuMusic.ready)
		return;
	SDL_LockAudioStream(menuMusic.output.stream);
	SlipRaceMusic_Update(&menuMusic.race, menuMusic.game, delta, position);
	SDL_UnlockAudioStream(menuMusic.output.stream);
}

bool SlipMenuMusic_ResultsStart(const struct SlipRaceRacerTable *racers) {
	if (!menuMusic.ready)
		return false;
	SDL_LockAudioStream(menuMusic.output.stream);
	if (menuMusic.game->musicCard != 0) {
		const char *const name = SlipResultMusic_Select(racers);
		assert(menuMusic.resultsResource == 0);
		if (!SlipResourceHost_Load(NULL, name, &menuMusic.resultsResource))
			SlipGame_ResourceFailure();
		uint8_t *const song = SlipResourceHost_LockWritable(NULL, menuMusic.resultsResource);
		menuMusic.resultsBranches = calloc(SLIP_MENU_MUSIC_BRANCH_VIEW_CAPACITY, sizeof(*menuMusic.resultsBranches));
		if (menuMusic.resultsBranches == NULL) {
			SlipResourceHost_Unlock(NULL, menuMusic.resultsResource);
			SlipResourceHost_Release(NULL, menuMusic.resultsResource);
			menuMusic.resultsResource = 0;
			SDL_UnlockAudioStream(menuMusic.output.stream);
			return false;
		}
		menuMusic.resultsHandle =
		    SlipGameMusic_Start(menuMusic.game, &menuMusic.music, &menuMusic.timer, song, menuMusic.resultsBranches);
	}
	SDL_UnlockAudioStream(menuMusic.output.stream);
	return true;
}

void SlipMenuMusic_ResultsStop(void) {
	if (!menuMusic.ready)
		return;
	SDL_LockAudioStream(menuMusic.output.stream);
	if (menuMusic.game->musicCard != 0) {
		SlipGameMusic_Stop(menuMusic.game, &menuMusic.music, &menuMusic.timer, menuMusic.resultsHandle);
		SlipResourceHost_Unlock(NULL, menuMusic.resultsResource);
		SlipResourceHost_Release(NULL, menuMusic.resultsResource);
		menuMusic.resultsResource = 0;
		free(menuMusic.resultsBranches);
		menuMusic.resultsBranches = NULL;
		/* Retire native ownership so final host shutdown may follow teardown. */
		menuMusic.resultsHandle = 0;
	}
	SDL_UnlockAudioStream(menuMusic.output.stream);
}

bool SlipMenuMusic_StandingsStart(void) {
	if (!menuMusic.ready)
		return false;
	SDL_LockAudioStream(menuMusic.output.stream);
	if (menuMusic.game->musicCard != 0) {
		assert(menuMusic.standingsResource == 0);
		if (!SlipResourceHost_Load(NULL, "WIN.HMP", &menuMusic.standingsResource))
			SlipGame_ResourceFailure();
		uint8_t *const song = SlipResourceHost_LockWritable(NULL, menuMusic.standingsResource);
		menuMusic.standingsBranches =
		    calloc(SLIP_MENU_MUSIC_BRANCH_VIEW_CAPACITY, sizeof(*menuMusic.standingsBranches));
		if (menuMusic.standingsBranches == NULL) {
			SlipResourceHost_Unlock(NULL, menuMusic.standingsResource);
			SlipResourceHost_Release(NULL, menuMusic.standingsResource);
			menuMusic.standingsResource = 0;
			SDL_UnlockAudioStream(menuMusic.output.stream);
			return false;
		}
		menuMusic.standingsHandle =
		    SlipGameMusic_Start(menuMusic.game, &menuMusic.music, &menuMusic.timer, song, menuMusic.standingsBranches);
	}
	SDL_UnlockAudioStream(menuMusic.output.stream);
	return true;
}

void SlipMenuMusic_StandingsStop(void) {
	if (!menuMusic.ready)
		return;
	SDL_LockAudioStream(menuMusic.output.stream);
	if (menuMusic.game->musicCard != 0) {
		SlipGameMusic_Stop(menuMusic.game, &menuMusic.music, &menuMusic.timer, menuMusic.standingsHandle);
		SlipResourceHost_Unlock(NULL, menuMusic.standingsResource);
		SlipResourceHost_Release(NULL, menuMusic.standingsResource);
		menuMusic.standingsResource = 0;
		free(menuMusic.standingsBranches);
		menuMusic.standingsBranches = NULL;
		menuMusic.standingsHandle = 0; /* Retire native ownership for final close. */
	}
	SDL_UnlockAudioStream(menuMusic.output.stream);
}

bool SlipMenuMusic_ScriptedStart(uint32_t selection) {
	static const char *const names[SLIP_RACE_MUSIC_SONG_COUNT] = {"INGAME2.HMP", "INGAME3.HMP", "INGAME4.HMP",
	                                                              "INGAME6.HMP"};
	if (!menuMusic.ready)
		return false;
	assert(selection < SLIP_RACE_MUSIC_SONG_COUNT);
	SDL_LockAudioStream(menuMusic.output.stream);
	if (menuMusic.game->musicCard != 0) {
		assert(menuMusic.scriptedResource == 0);
		if (!SlipResourceHost_Load(NULL, names[selection], &menuMusic.scriptedResource))
			SlipGame_ResourceFailure();
		uint8_t *const song = SlipResourceHost_LockWritable(NULL, menuMusic.scriptedResource);
		menuMusic.scriptedBranches = calloc(SLIP_MENU_MUSIC_BRANCH_VIEW_CAPACITY, sizeof(*menuMusic.scriptedBranches));
		if (menuMusic.scriptedBranches == NULL) {
			SDL_UnlockAudioStream(menuMusic.output.stream);
			return false; /* Native HMI pointer view allocation failure. */
		}

		menuMusic.scriptedHandle =
		    SlipGameMusic_Start(menuMusic.game, &menuMusic.music, &menuMusic.timer, song, menuMusic.scriptedBranches);
	} else {
		menuMusic.scriptedHandle = 0;
	}
	SDL_UnlockAudioStream(menuMusic.output.stream);
	return true;
}

void SlipMenuMusic_ScriptedStop(void) {
	if (!menuMusic.ready)
		return;
	SDL_LockAudioStream(menuMusic.output.stream);
	if (menuMusic.game->musicCard != 0) {
		SlipGameMusic_Stop(menuMusic.game, &menuMusic.music, &menuMusic.timer, menuMusic.scriptedHandle);
		SlipResourceHost_Unlock(NULL, menuMusic.scriptedResource);
		SlipResourceHost_Release(NULL, menuMusic.scriptedResource);
		menuMusic.scriptedResource = 0;
		free(menuMusic.scriptedBranches);
		menuMusic.scriptedBranches = NULL;
		menuMusic.scriptedHandle = 0; /* Retire native ownership for final close. */
	}
	SDL_UnlockAudioStream(menuMusic.output.stream);
}
