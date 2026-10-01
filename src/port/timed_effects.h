#ifndef SLIP_TIMED_EFFECTS_H
#define SLIP_TIMED_EFFECTS_H

#include "view3d.h"
#include <stdint.h>

struct SlipObject;

typedef struct SlipTimedEffectFrames {
	uint16_t count;
	uint16_t delay;
	uint16_t *handles;
} SlipTimedEffectFrames;

typedef struct SlipTimedEffectDescriptor {
	int32_t initialExtent, holdExtent, finalExtent;
	uint32_t growthDuration, holdDuration, finalDuration;
	const SlipTimedEffectFrames *initialFrames;
	const SlipTimedEffectFrames *finalFrames;
	const SlipTimedEffectFrames *attachedFrames;
	int32_t emissionPeriod;
	uint32_t displacementRange;

	uint32_t dosAddress, finalFramesDosAddress;
	uint32_t initialFramesDosAddress;
} SlipTimedEffectDescriptor;

typedef struct SlipTimedEffectObjectState {
	const SlipTimedEffectDescriptor *descriptor;
	uint32_t age;
	int32_t initialExtent, holdExtent;
	uint16_t phase;
	int16_t frameCountdown;
	uint16_t speedLimit;
} SlipTimedEffectObjectState;

typedef struct SlipTimedEffectPhase {
	int32_t step;
	uint32_t age;
	uint32_t extent;
	uint32_t callbackValue;
	bool writeExtent;
	bool freeObject;
} SlipTimedEffectPhase;

extern uint32_t SlipTimedEffects_fraction;
void SlipTimedEffects_Phase(SlipTimedEffectObjectState *state, uint32_t elapsed, SlipTimedEffectPhase *result);

typedef struct SlipTimedEffect {
	struct SlipTimedEffect *next;
	struct SlipTimedEffect *previous;
	uint16_t parentObject;
	int32_t x, y, z;
	int32_t displacementX, displacementY;
	uint16_t currentObject, precedingObject;
	int32_t lifetime;
	uint16_t speedLimit;
	const struct SlipTimedEffectDescriptor *descriptor;
	int32_t periodCountdown;
} SlipTimedEffect;

extern SlipTimedEffect *SlipTimedEffects_active;
extern SlipTimedEffect *SlipTimedEffects_free;
extern uint16_t SlipTimedEffects_count;
extern uint16_t SlipTimedEffects_objectCount;

void SlipTimedEffects_Reset(void);
SlipTimedEffect *SlipTimedEffects_Allocate(void);
void SlipTimedEffects_Release(SlipTimedEffect *entry);
void SlipTimedEffects_RemoveParent(uint16_t parentObject);

SlipTimedEffect *SlipTimedEffects_Create(uint16_t parentObject, int32_t x, int32_t y, int32_t z, int32_t lifetime,
                                         uint16_t speedLimit, const struct SlipTimedEffectDescriptor *descriptor);
void SlipTimedEffects_ObjectDeleted(uint16_t object);

/* False denotes carry set (no eligible object). The table is the bound object pool. */
bool SlipTimedEffects_SelectOldest(const struct SlipObject *objects, size_t objectBytes, uint16_t *selected);
bool SlipTimedEffects_Reclaim(const struct SlipObject *objects, size_t objectBytes, uint32_t eventCode,
                              uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags, uintptr_t dispatchData,
                              uint32_t dispatchFrame);

bool SlipTimedEffects_Position(const SlipTimedEffect *entry, const struct SlipObject *objects, size_t objectBytes,
                               SlipView3DVec32 *position);

void SlipTimedEffects_Displace(SlipTimedEffect *entry, uint32_t verticalDisplacementRange);
uint16_t SlipTimedEffects_SelectFrame(const SlipTimedEffectFrames *frames, uint16_t previous);

bool SlipTimedEffects_UpdateFrame(SlipTimedEffectObjectState *state, int32_t step, struct SlipObject *objects,
                                  size_t objectBytes, uint16_t object, uint32_t descriptorAddress,
                                  uint32_t finalFramesAddress, uint32_t *frameSelectionValue);

extern uint32_t SlipTimedEffects_speedLimit;
bool SlipTimedEffects_UpdateSpeed(const SlipTimedEffectObjectState *state, int32_t step,
                                  uint32_t *speedCalculationValue, struct SlipObject *objects, uint16_t object);

typedef void (*SlipTimedEffectUpdate)(uint16_t object, uint32_t speedResult);
extern SlipTimedEffectUpdate SlipTimedEffects_update;
void SlipTimedEffects_UpdateObject(uint16_t object, uint32_t elapsed, uint32_t callerValue, uint32_t callerFrame);
uint32_t SlipTimedEffects_Event(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                uint16_t object, uintptr_t dispatchData, uint32_t dispatchFrame);

typedef uint32_t (*SlipTimedEffectAttach)(uint16_t object, int32_t radius, const SlipTimedEffect *emitter,
                                          uint32_t callerValue);
extern SlipTimedEffectAttach SlipTimedEffects_attach;
extern uint32_t SlipTimedEffects_initialized;
extern uint16_t SlipTimedEffects_drawMode;
extern uint16_t SlipTimedEffects_objectLimit;
extern uint16_t SlipTimedEffects_interpolationLimit;
bool SlipTimedEffects_Initialize(uint16_t count, uint16_t drawMode, uint16_t objectLimit, uint16_t interpolationLimit,
                                 SlipTimedEffectAttach attach, SlipTimedEffectUpdate update);
void SlipTimedEffects_Cleanup(void);
uint32_t SlipTimedEffects_Notify(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                 uint16_t object, uintptr_t dispatchData, uint32_t dispatchFrame);

bool SlipTimedEffects_CreateObject(const SlipTimedEffect *emitter, SlipView3DVec32 position,
                                   const SlipView3DMatrix *creationTemplate, uint32_t emitterDosAddress,
                                   uint16_t *createdObject, uint32_t *displacementScale);

void SlipTimedEffects_Tick(const SlipView3DMatrix *creationTemplate, uint32_t emitterPoolDosAddress);

#endif
