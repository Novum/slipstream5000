#ifndef SLIPSTREAM5000_RACE_CHAMPIONSHIP_H
#define SLIPSTREAM5000_RACE_CHAMPIONSHIP_H
#include "input.h"
#include "race.h"
void SlipChampionship_AwardRace(SlipRaceRacerTable *racers);

typedef enum SlipChampionshipAction {
	SLIP_CHAMPIONSHIP_WAIT = 0,
	SLIP_CHAMPIONSHIP_SAVE = 1,
	SLIP_CHAMPIONSHIP_CONTINUE = 2
} SlipChampionshipAction;

SlipChampionshipAction SlipChampionship_ReadInput(uint32_t hoveredButton, bool pressed[SLIP_INPUT_CODE_COUNT]);

#endif
