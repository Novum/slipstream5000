#ifndef SLIP_ANIMATED_EFFECTS_H
#define SLIP_ANIMATED_EFFECTS_H

#include "view3d.h"
#include <stdint.h>

typedef struct SlipAnimatedFrames {
	uint16_t frameCount;
	uint16_t frameDelay;
	const uint16_t *frameHandles;
} SlipAnimatedFrames;

typedef struct SlipAnimatedDescriptor {
	const SlipAnimatedFrames *initialFrames;
	const SlipAnimatedFrames *finalFrames;
	uint16_t damping;

	uint32_t dosAddress;
} SlipAnimatedDescriptor;

typedef struct SlipAnimatedState {
	const SlipAnimatedDescriptor *descriptor;
	int32_t initialRadius;
	int32_t finalRadius;
	SlipView3DVec32 offset;
	uint16_t age;
	uint16_t frameElapsed;
	uint16_t initialDuration;
	uint16_t finalDuration;
	uint16_t parent;
} SlipAnimatedState;

typedef void (*SlipAnimatedAttach)(uint16_t object, int32_t radius);
extern uint32_t SlipAnimatedEffects_initialized;
extern uint32_t SlipAnimatedEffects_drawMode;
extern SlipAnimatedAttach SlipAnimatedEffects_attach;

typedef uint32_t (*SlipAnimatedUpdate)(uint16_t object);
extern SlipAnimatedUpdate SlipAnimatedEffects_update;
extern int16_t SlipAnimatedEffects_jitter;
void SlipAnimatedEffects_UpdateAttachment(uint16_t object);
uint32_t SlipAnimatedEffects_Event(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                   uint16_t object, uintptr_t dispatchData, uint32_t dispatchFrame);

void SlipAnimatedEffects_Initialize(uint32_t drawMode, SlipAnimatedAttach attach, SlipAnimatedUpdate update);
void SlipAnimatedEffects_Cleanup(void);
uint32_t SlipAnimatedEffects_Notify(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                    uint16_t object, uintptr_t dispatchData, uint32_t dispatchFrame);
void SlipAnimatedEffects_RemoveParent(uint16_t parent, uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                      uint32_t eventFlags, uint32_t dispatchFrame);
bool SlipAnimatedEffects_Create(SlipView3DVec32 position, int32_t radius, uint16_t duration, uint16_t parent,
                                const SlipAnimatedDescriptor *descriptor, uint16_t *createdObject);

struct SlipObject;
bool SlipAnimatedEffects_SelectOldest(const struct SlipObject *objects, size_t objectBytes, uint16_t *selected);
bool SlipAnimatedEffects_Reclaim(const struct SlipObject *objects, size_t objectBytes, uint32_t eventCode,
                                 uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                 uintptr_t dispatchData, uint32_t dispatchFrame);

#endif
