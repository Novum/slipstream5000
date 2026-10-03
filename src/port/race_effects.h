#ifndef SLIP_RACE_EFFECTS_H
#define SLIP_RACE_EFFECTS_H
#include "animated_effects.h"
#include "timed_effects.h"

enum {
	SLIP_RACE_EXPLOSION_FRAME_COUNT = 6,
	SLIP_RACE_SMOKE_INITIAL_FRAME_COUNT = 6,
	SLIP_RACE_SMOKE_FINAL_FRAME_COUNT = 9,
	SLIP_RACE_FIRE_FRAME_COUNT = 4
};

typedef struct SlipRaceDebrisState {
	uint16_t elapsed;
	int16_t rotationRateX, rotationRateY, rotationRateZ;
} SlipRaceDebrisState;

extern uint16_t SlipRaceEffects_blackSmokeHandles[SLIP_RACE_SMOKE_INITIAL_FRAME_COUNT];
extern uint16_t SlipRaceEffects_finalBlackSmokeHandles[SLIP_RACE_SMOKE_FINAL_FRAME_COUNT];
extern uint16_t SlipRaceEffects_graySmokeHandles[SLIP_RACE_SMOKE_INITIAL_FRAME_COUNT];
extern uint16_t SlipRaceEffects_finalGraySmokeHandles[SLIP_RACE_SMOKE_FINAL_FRAME_COUNT];
extern uint16_t SlipRaceEffects_fireHandles[SLIP_RACE_FIRE_FRAME_COUNT];
extern const SlipTimedEffectDescriptor SlipRaceEffects_projectileTrail;
extern const SlipTimedEffectDescriptor SlipRaceEffects_damageSmoke;
extern const SlipTimedEffectDescriptor SlipRaceEffects_weaponSmoke;
extern const SlipView3DMatrix SlipRaceEffects_creationTemplate;
void SlipRaceEffects_EmitSmoke(uint16_t object, SlipView3DVec32 localPosition, int32_t lifetime,
                               const SlipTimedEffectDescriptor *descriptor);

void SlipRaceEffects_EmitWorldSmoke(SlipView3DVec32 position, int32_t lifetime,
                                    const SlipTimedEffectDescriptor *descriptor);
extern uint16_t SlipRaceEffects_explosionHandles[SLIP_RACE_EXPLOSION_FRAME_COUNT];
extern uint16_t SlipRaceEffects_finalExplosionHandles[SLIP_RACE_EXPLOSION_FRAME_COUNT];
void SlipRaceEffects_Collision(SlipView3DVec32 position, int32_t radius, uint16_t duration);
uint32_t SlipRaceEffects_WreckEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                    uint16_t object, uintptr_t dispatchData, uint32_t dispatchFrame);
extern SlipView3DVec32 SlipRaceEffects_velocity;
void SlipRaceEffects_Debris(uint32_t count, uint16_t source, uint32_t drawPrefixOrImpulseScale);
void SlipRaceEffects_ReadVelocity(uint16_t object);
void SlipRaceEffects_WriteVelocity(uint16_t object);
struct SlipRacePlayerHostBindings;
struct SlipRaceCollisionBounceEvent;
void SlipRaceEffects_ContactFragments(struct SlipRacePlayerHostBindings *context, uint32_t count, uint16_t object,
                                      const struct SlipRaceCollisionBounceEvent *contact);
void SlipRaceEffects_InitializeMaterials(const uint8_t *materials, size_t materialBytes, uint16_t materialGlobal);
void SlipRaceEffects_SparkSplash(struct SlipRacePlayerHostBindings *context, uint32_t count, uint16_t object,
                                 const struct SlipRaceCollisionBounceEvent *contact);
bool SlipRaceEffects_Reclaim(const struct SlipObject *objects, size_t objectBytes, uint32_t eventCode,
                             uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags, uintptr_t dispatchData,
                             uint32_t dispatchFrame);
#endif
