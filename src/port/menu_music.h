#ifndef SLIPSTREAM5000_MENU_MUSIC_H
#define SLIPSTREAM5000_MENU_MUSIC_H
#include "game_sound.h"
#include <stdbool.h>
struct SlipRaceRacerTable;
bool SlipMenuMusic_Open(const char *archive, SlipGameSoundState *game);
void SlipMenuMusic_Start(void);
void SlipMenuMusic_Stop(void);
void SlipMenuMusic_Resume(void);
void SlipMenuMusic_StopRetainingSong(void);
void SlipMenuMusic_Close(void);
uint64_t SlipMenuMusic_NonzeroFrames(void);
uint32_t SlipMenuMusic_PendingBranch(void);
void SlipMenuMusic_SetSetting(uint16_t setting);
void SlipMenuMusic_Branch(uint32_t selection);
bool SlipMenuMusic_RaceStart(uint32_t selection);
void SlipMenuMusic_RaceStop(void);
void SlipMenuMusic_RaceConfigurationReturn(void);
void SlipMenuMusic_RaceUpdate(uint32_t delta, uint32_t position);
bool SlipMenuMusic_ResultsStart(const struct SlipRaceRacerTable *racers);
void SlipMenuMusic_ResultsStop(void);
bool SlipMenuMusic_StandingsStart(void);
void SlipMenuMusic_StandingsStop(void);
bool SlipMenuMusic_ScriptedStart(uint32_t selection);
void SlipMenuMusic_ScriptedStop(void);
#endif
