#ifndef SLIPSTREAM5000_RACE_DRONE_H
#define SLIPSTREAM5000_RACE_DRONE_H

#include "artic_slot.h"
#include "race_player.h"
#include "resource.h"
#include "track_view_render.h"

typedef struct SlipRaceDroneHostBindings {
	SlipRacePlayerHostBindings *playerBindings;
	SlipArticSlotPool *articPool;
	SlipArticSlotFindResource findResource;
	void *findResourceUser;
	const uint8_t *artPayload;
	size_t artPayloadBytes;
	uint16_t artResourceHandle;
	uint8_t *slotDrawBase;
	size_t slotDrawBytes;
	uint32_t slotDrawBaseAddress;
	uint32_t slotDrawFreeListAddress;
	uint32_t slotListFreeListAddress;
} SlipRaceDroneHostBindings;

void SlipRaceDrone_BindHostContext(SlipRaceDroneHostBindings *bindings);
bool SlipRaceDrone_Initialize(const char *const *archives, size_t archiveCount,
                              TrackViewResourceHandleRegistry *resourceRegistry, SlipResourcePayload *artPayload,
                              uint16_t *artHandle);
void SlipRaceDrone_Shutdown(uint16_t artHandle, SlipResourcePayload *artPayload,
                            TrackViewResourceHandleRegistry *registry);
void SlipRaceDrone_Update(void);
uint32_t SlipRaceDrone_Event(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                             uint16_t objectOffset, uintptr_t dispatchData, uint32_t dispatchFrame);

void SlipRaceDrone_Move(uint16_t object, uint32_t initializeOrientation, uint32_t move);
#endif
