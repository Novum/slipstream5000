#include "race_championship.h"

static const uint16_t finishingPositionPoints[SLIP_RACE_RACER_COUNT] = {10, 6, 4, 3, 2, 1, 0, 0, 0, 0};
static const uint16_t finishingPositionPrize[SLIP_RACE_RACER_COUNT] = {650, 450, 350, 200, 100, 50, 0, 0, 0, 0};

void SlipChampionship_AwardRace(SlipRaceRacerTable *racers) {
	for (uint16_t i = 0; i < racers->racerCount; ++i) {
		SlipRaceRacerState *const racer = &racers->records[i];
		if (racer->finished != 0) {
			const uint16_t position = (uint16_t)(racer->racePosition - 1);
			racer->championshipPoints = (uint16_t)(racer->championshipPoints + finishingPositionPoints[position]);
			racer->bonusScore += finishingPositionPrize[position];
		}
		racer->championshipPosition = UINT16_MAX;
	}
	for (uint16_t rank = 1; (int16_t)rank <= (int16_t)racers->racerCount; ++rank) {
		int16_t highest = -1;
		SlipRaceRacerState *selected = NULL;
		for (uint16_t i = 0; i < racers->racerCount; ++i) {
			SlipRaceRacerState *const racer = &racers->records[i];
			if (racer->championshipPosition == UINT16_MAX && (int16_t)racer->championshipPoints >= highest) {
				highest = (int16_t)racer->championshipPoints;
				selected = racer;
			}
		}
		selected->championshipPosition = rank;
	}
}

SlipChampionshipAction SlipChampionship_ReadInput(uint32_t hoveredButton, bool pressed[SLIP_INPUT_CODE_COUNT]) {
	if (pressed[SLIP_INPUT_SCAN_ESCAPE]) {
		pressed[SLIP_INPUT_SCAN_ESCAPE] = false;
		return SLIP_CHAMPIONSHIP_SAVE;
	}
	if (hoveredButton == 0)
		return SLIP_CHAMPIONSHIP_WAIT;
	if (pressed[SLIP_INPUT_SCAN_ENTER])
		pressed[SLIP_INPUT_SCAN_ENTER] = false;
	else if (pressed[SLIP_INPUT_MOUSE_LEFT])
		pressed[SLIP_INPUT_MOUSE_LEFT] = false;
	else
		return SLIP_CHAMPIONSHIP_WAIT;
	return (SlipChampionshipAction)hoveredButton;
}
