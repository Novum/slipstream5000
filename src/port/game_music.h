#ifndef SLIPSTREAM5000_GAME_MUSIC_H
#define SLIPSTREAM5000_GAME_MUSIC_H

#include "game_sound.h"
#include "game_timer.h"
#include "hmi_music.h"

struct SlipRaceRacerTable;

typedef struct SlipRaceMusicState {
	uint32_t handle;
	uint8_t *song;
	uint32_t branch;
	uint32_t previousBranch;
	uint32_t countdown;
	uint32_t selection;
} SlipRaceMusicState;

extern const char *const SlipRaceMusic_names[4];
void SlipGameMusic_MenuBranch(SlipGameSoundState *game, uint32_t handle, uint32_t selection);

const char *SlipResultMusic_Select(const struct SlipRaceRacerTable *racers);

uint32_t SlipRaceMusic_Select(uint32_t (*memoryQuery)(void));
void SlipRaceMusic_InitializeBranches(SlipRaceMusicState *);
void SlipRaceMusic_ConfigurationReturn(SlipRaceMusicState *, SlipGameSoundState *, HmiMusicState *, HmiTimerState *,
                                       HmiMusicBranchRecord *);

void SlipRaceMusic_Update(SlipRaceMusicState *, SlipGameSoundState *, uint32_t delta, uint32_t position);

void SlipGameMusic_RequestBranch(SlipGameSoundState *, uint32_t handle, uint32_t request);
uint32_t SlipGameMusic_Trigger(SlipGameSoundState *, HmiMusicState *, uint32_t song, uint8_t id);
void SlipGameMusic_TriggerCallback(HmiMusicState *, uint32_t song, uint8_t track, uint8_t id);
void SlipGameMusic_Timer(SlipGameSoundState *, HmiTimerState *);
void SlipGameMusic_TimerCallback(SlipGameTimerState *);
void SlipGameMusic_SetVolume(SlipGameSoundState *, HmiMusicState *, uint32_t volume);
void SlipGameMusic_ApplySetting(SlipGameSoundState *, HmiMusicState *);
void SlipGameMusic_Stop(SlipGameSoundState *, HmiMusicState *, HmiTimerState *, uint32_t handle);
uint32_t SlipGameMusic_Start(SlipGameSoundState *, HmiMusicState *, HmiTimerState *, uint8_t *song,
                             HmiMusicBranchRecord *branchStorage);

#endif
