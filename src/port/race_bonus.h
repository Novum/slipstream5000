#ifndef SLIPSTREAM5000_RACE_BONUS_H
#define SLIPSTREAM5000_RACE_BONUS_H

#include "race_player.h"

typedef enum SlipRaceBonusType {
	SLIP_RACE_BONUS_ENGINE_REPAIR,
	SLIP_RACE_BONUS_CONTROL_REPAIR,
	SLIP_RACE_BONUS_POWERUP_RECHARGE,
	SLIP_RACE_BONUS_REVERSE_CONTROLS,
	SLIP_RACE_BONUS_CREDITS,
	SLIP_RACE_BONUS_SPEED_BOOST,
	SLIP_RACE_BONUS_COUNT
} SlipRaceBonusType;

typedef struct SlipRaceBonusSpawn {
	int32_t x;
	int32_t y;
	int32_t z;
	int32_t type;
} SlipRaceBonusSpawn;

typedef struct SlipRaceBonusSpawnTable {
	const SlipRaceBonusSpawn *spawns;
	size_t count;
} SlipRaceBonusSpawnTable;

extern const SlipRaceBonusSpawnTable SlipRaceBonus_trackTables[10];

typedef struct SlipRaceBonusHostBindings {
	SlipRacePlayerHostBindings *playerBindings;
	uint8_t *slotDrawBase;
	size_t slotDrawBytes;
	SlipObjectDrawCallback *slotDrawCallbacks;
	size_t slotDrawCallbackCount;
	uint32_t slotDrawBaseAddress;
	uint32_t slotDrawFreeListAddress;
	uint32_t slotListFreeListAddress;
	uint16_t resourceHandles[SLIP_RACE_BONUS_COUNT];
} SlipRaceBonusHostBindings;

uint32_t SlipRaceBonus_Type(uint16_t objectOffset);

void SlipRaceBonus_BindHostContext(SlipRaceBonusHostBindings *bindings);
uint32_t SlipRaceBonus_Event(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                             uint16_t objectOffset, uintptr_t dispatchData, uint32_t dispatchFrame);
void SlipRaceBonus_Create(SlipView3DVec32 position, int32_t remainingTime, int32_t bonusType);

#endif
