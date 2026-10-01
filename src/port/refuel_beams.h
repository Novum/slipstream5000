#ifndef SLIPSTREAM5000_REFUEL_BEAMS_H
#define SLIPSTREAM5000_REFUEL_BEAMS_H
#include "track_world.h"

typedef struct SlipRefuelBeamState {
	uint32_t section;
	uint32_t active;
	uint32_t built;
} SlipRefuelBeamState;

typedef struct SlipRefuelBeamCalls {
	void *context;
	const SlipTrackSlotRecord *(*slot)(void *, uint16_t object);
	void (*position)(void *, uint16_t object, uint32_t partTag, uint32_t pointTag, SlipView3DVec32 *);
	const SlipView3DMatrix *(*objectMatrix)(void *, uint16_t object);
	uint16_t (*random)(void *);
	void (*clip)(void *, SlipView3DVec32 start, SlipView3DVec32 *end);
	SlipTrackBeamRecord *(*allocate)(void *);
} SlipRefuelBeamCalls;

void SlipRefuel_BuildBeams(SlipRefuelBeamState *, uint32_t section, uint16_t firstDrawOffset,
                           const SlipTrackDrawRecord *drawRecords, uint32_t drawRecordsAddress, const SlipView3DMaths *,
                           uint32_t incomingX, uint32_t incomingY, const SlipRefuelBeamCalls *);
#endif
