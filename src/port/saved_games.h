#ifndef SLIP_SAVED_GAMES_H
#define SLIP_SAVED_GAMES_H
#include "race.h"
void SlipSavedGamesHost_ImportLegacySave(const char *resourcePath);
bool SlipSavedGames_Run(const char *resourcePath, SlipRaceRacerTable *racers, uint32_t *stage);
bool SlipSavedGames_Save(const char *resourcePath, const SlipRaceRacerTable *racers, uint32_t stage);
#endif
