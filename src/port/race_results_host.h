#ifndef SLIPSTREAM5000_RACE_RESULTS_HOST_H
#define SLIPSTREAM5000_RACE_RESULTS_HOST_H
#include "game_sound.h"
#include "input_navigation.h"
#include "race_results.h"

typedef struct SlipRaceResultsScreen {
	SlipRaceRacerTable *racers;
	uint16_t track;
	uint32_t hoveredButton;
	SlipRaceResultsAssets assets;
} SlipRaceResultsScreen;

void SlipRaceResults_Begin(SlipRaceResultsScreen *, uint16_t track, SlipRaceRacerTable *, uint16_t playerOne,
                           const char *archive, SlipGameSoundState *);
void SlipRaceResults_BeginDisplay(SlipRaceResultsScreen *, const char *archive, SlipGameSoundState *);
SlipRaceResultsAction SlipRaceResults_Frame(SlipRaceResultsScreen *);
void SlipRaceResults_EndDisplay(SlipRaceResultsScreen *, SlipGameSoundState *);
#endif
