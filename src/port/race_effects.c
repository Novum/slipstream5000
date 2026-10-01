#include "race_effects.h"
#include "cross_effects.h"
#include "frame_timer.h"
#include "race.h"
#include "race_collision.h"
#include "race_player.h"
#include "race_session.h"
#include "runtime.h"
#include "shape_effects.h"
#include "track_world.h"
#include <string.h>

const SlipView3DMatrix SlipRaceEffects_creationTemplate = {
    {(int16_t)0x8360, (int16_t)0x9c3d, 0x0274, 0, (int16_t)0x850f, 0x0093, 0, 0x05c7, 0x749c}};

void SlipRaceEffects_EmitSmoke(uint16_t object, SlipView3DVec32 localPosition, int32_t lifetime,
                               const SlipTimedEffectDescriptor *descriptor) {
	(void)SlipTimedEffects_Create(object, localPosition.x, localPosition.y, localPosition.z, lifetime, 0, descriptor);
}

void SlipRaceEffects_EmitWorldSmoke(SlipView3DVec32 position, int32_t lifetime,
                                    const SlipTimedEffectDescriptor *descriptor) {
	enum { WORLD_SMOKE_SPEED = 0x6fb8, NO_PARENT_OBJECT = 0 };

	(void)SlipTimedEffects_Create(NO_PARENT_OBJECT, position.x, position.y, position.z, lifetime, WORLD_SMOKE_SPEED,
	                              descriptor);
}

uint16_t SlipRaceEffects_explosionHandles[6];
uint16_t SlipRaceEffects_finalExplosionHandles[6];
static const SlipAnimatedFrames initialExplosionFrames = {6, 100, SlipRaceEffects_explosionHandles};
static const SlipAnimatedFrames finalExplosionFrames = {6, 100, SlipRaceEffects_finalExplosionHandles};
static const SlipAnimatedDescriptor collisionDescriptor = {&initialExplosionFrames, &finalExplosionFrames, 0x800,
                                                           0x4f272};

static const SlipAnimatedDescriptor wreckDescriptor = {&initialExplosionFrames, &finalExplosionFrames, 0x800, 0x4f268};

uint32_t SlipRaceEffects_WreckEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                    uint16_t object, uintptr_t dispatchData, uint32_t dispatchFrame) {
	SlipRaceWreckState *const state = &SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE].wreckEffect;
	if ((uint16_t)eventCode == SLIP_OBJECT_EVENT_UPDATE) {
		const uint16_t elapsed = (uint16_t)SlipFrameTimer_Values().deltaMilliseconds;
		if (state->remainingLifetime < elapsed) {
			SlipObjectEventCallbackWriteResult installed;
			(void)SlipObject_SetEventCallback(SlipObject_table, (size_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE,
			                                  object, state->nextEvent, &installed);
			return 0;
		}
		state->remainingLifetime = (uint16_t)(state->remainingLifetime - elapsed);
		if (state->maximumSpeed != 0) {
			const uint16_t step = (uint16_t)SlipFrameTimer_Step();
			const int16_t increment = (int16_t)(uint16_t)(((uint32_t)(uint16_t)state->maximumSpeed * step) >> 14);
			int32_t speed =
			    (int32_t)((uint32_t)SlipObject_Speed(SlipObject_table, object) + (uint32_t)(int32_t)increment);
			if (speed > state->maximumSpeed)
				speed = state->maximumSpeed;
			SlipObject_SetSpeed(SlipObject_table, object, speed);
		}
		state->explosionCountdown =
		    (int32_t)((uint32_t)state->explosionCountdown - SlipFrameTimer_Values().deltaMilliseconds);
		if (state->explosionCountdown < 0) {
			state->explosionCountdown = 200;
			static SlipView3DVec32 displacement;
			const int16_t range = (int16_t)state->explosionRange;
			displacement.x = ((int32_t)(int16_t)SlipRandom_Next() * range) >> 16;

			displacement.y = ((int32_t)(int16_t)SlipRandom_Next() * range) >> 16;
			displacement.z = ((int32_t)(int16_t)SlipRandom_Next() * range) >> 16;
			SlipObjectPosition position;
			(void)SlipObject_Position(SlipObject_table, (size_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE, object,
			                          &position);
			SlipView3DVec32 explosion = {(int32_t)(position.positionX + (uint32_t)displacement.x),
			                             (int32_t)(position.positionY + (uint32_t)displacement.y),
			                             (int32_t)(position.positionZ + (uint32_t)displacement.z)};
			uint16_t created;
			(void)SlipAnimatedEffects_Create(explosion, 0x16e0, 400, 0, &wreckDescriptor, &created);
		}
		return 0;
	}
	if ((uint16_t)eventCode == SLIP_OBJECT_EVENT_COLLISION_BOUNCE) {
		(void)SlipObject_Stop(object, eventCode, eventPayload, eventValue, eventFlags, dispatchData, dispatchFrame);
		state->explosionRange = 0x2250;
		state->maximumSpeed = 0;
		return 0;
	}
	if ((uint16_t)eventCode == SLIP_OBJECT_EVENT_COLLISION_STOP)
		SlipRuntime_Fatal("RaceBangSlotControl - slot-slot collision should not happen!");
	return UINT32_MAX;
}

void SlipRaceEffects_Collision(SlipView3DVec32 position, int32_t radius, uint16_t duration) {
	uint16_t object;
	(void)SlipAnimatedEffects_Create(position, radius, duration, 0, &collisionDescriptor, &object);
}

SlipView3DVec32 SlipRaceEffects_velocity;

void SlipRaceEffects_Debris(uint32_t count, uint16_t source, uint32_t drawPrefixOrImpulseScale) {
	count = (uint16_t)count;
	if (count > SlipRace_debrisBudget)
		count = SlipRace_debrisBudget;
	SlipRace_debrisBudget -= count;
	if (count != 0)
		SlipRaceSession_CreateDebris(count, 0, source, drawPrefixOrImpulseScale);
}

void SlipRaceEffects_ReadVelocity(uint16_t object) {
	SlipRaceEffects_velocity = SlipObject_Velocity(SlipObject_table, object);
}

void SlipRaceEffects_WriteVelocity(uint16_t object) {
	SlipObjectSetDirection result;
	(void)SlipObject_SetDirection(SlipObject_table, (size_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE, object,
	                              SlipRaceEffects_velocity.x, SlipRaceEffects_velocity.y, SlipRaceEffects_velocity.z,
	                              &result);
}

void SlipRaceEffects_ContactFragments(SlipRacePlayerHostBindings *context, uint32_t count, uint16_t object,
                                      const SlipRaceCollisionBounceEvent *contact) {
	static SlipCrossEffectEmission emission;
	static SlipView3DVec32 position;
	static uint16_t material;
	static uint32_t savedCount;
	savedCount = count;
	material = contact->material;
	emission.direction = (SlipView3DVec16){contact->normalX, contact->normalY, contact->normalZ};
	position = contact->contactPosition;
	SlipView3DVec32 offset =
	    SlipView3D_ScaleVector(emission.direction.x, emission.direction.y, emission.direction.z, 0x1e8);
	position.x = (int32_t)((uint32_t)position.x + (uint32_t)offset.x);
	position.y = (int32_t)((uint32_t)position.y + (uint32_t)offset.y);
	position.z = (int32_t)((uint32_t)position.z + (uint32_t)offset.z);
	SlipView3DVec32 reflected = SlipRacePlayer_CollisionVector(
	    context, (uint16_t)emission.direction.x, (uint16_t)emission.direction.y, (uint16_t)emission.direction.z, 0x100);
	const int32_t speed = SlipObject_Speed(context->objectTable, object);
	SlipView3DVec32 velocity =
	    SlipView3D_ScaleVector((int16_t)reflected.x, (int16_t)reflected.y, (int16_t)reflected.z, speed);
	SlipView3DVec32 normalVelocity =
	    SlipView3D_ScaleVector(emission.direction.x, emission.direction.y, emission.direction.z, 0x45d3);
	velocity.x = (int32_t)((uint32_t)velocity.x + (uint32_t)normalVelocity.x);
	velocity.y = (int32_t)((uint32_t)velocity.y + (uint32_t)normalVelocity.y);
	velocity.z = (int32_t)((uint32_t)velocity.z + (uint32_t)normalVelocity.z);
	SlipView3DNormalizeVector3D normalized;
	SlipView3D_NormalizeVector3D((uint32_t)velocity.x, (uint32_t)velocity.y, (uint32_t)velocity.z, &normalized);
	emission.direction =
	    (SlipView3DVec16){(int16_t)normalized.unitXQ14, (int16_t)normalized.unitYQ14, (int16_t)normalized.unitZQ14};
	emission.speed = SlipView3D_VectorLength(velocity.x, velocity.y, velocity.z);
	emission.radius = 0xf4;
	SlipCrossEffects_Create(position, savedCount, &emission, material, SlipRaceSession_UpdateCrossEffect,
	                        context->maths, context->materialTable, context->materialTableBytes);
}

static uint16_t sparkMaterial;
static uint16_t splashMaterial;
static uint16_t fragmentBackMaterial;

void SlipRaceEffects_InitializeMaterials(const uint8_t *materials, size_t materialBytes, uint16_t materialGlobal) {
	SlipDraw3DMaterialNumber result;
	if (!SlipDraw3D_GetMaterialNumber(materials, materialBytes, materialGlobal, (const uint8_t *)"SPARK",
	                                  sizeof("SPARK"), &result) ||
	    result.carryOut) {
		SlipRuntime_Fatal("RaceBangInstall - could not find some of the required materials");
	}
	sparkMaterial = result.materialIndex;
	if (!SlipDraw3D_GetMaterialNumber(materials, materialBytes, materialGlobal, (const uint8_t *)"SPLASH",
	                                  sizeof("SPLASH"), &result) ||
	    result.carryOut) {
		SlipRuntime_Fatal("RaceBangInstall - could not find some of the required materials");
	}
	splashMaterial = result.materialIndex;
	if (!SlipDraw3D_GetMaterialNumber(materials, materialBytes, materialGlobal, (const uint8_t *)"FRAGBACK",
	                                  sizeof("FRAGBACK"), &result) ||
	    result.carryOut) {
		SlipRuntime_Fatal("RaceBangInstall - could not find some of the required materials");
	}
	fragmentBackMaterial = result.materialIndex;
}

void SlipRaceEffects_SparkSplash(SlipRacePlayerHostBindings *context, uint32_t count, uint16_t object,
                                 const SlipRaceCollisionBounceEvent *contact) {
	static SlipCrossEffectEmission emission;
	static SlipView3DVec32 position;
	static uint16_t material;
	static uint32_t savedCount;
	savedCount = count;
	material = contact->material;
	emission.direction = (SlipView3DVec16){contact->normalX, contact->normalY, contact->normalZ};
	position = contact->contactPosition;
	SlipView3DVec32 offset =
	    SlipView3D_ScaleVector(emission.direction.x, emission.direction.y, emission.direction.z, 0x1e8);
	position.x = (int32_t)((uint32_t)position.x + (uint32_t)offset.x);
	position.y = (int32_t)((uint32_t)position.y + (uint32_t)offset.y);
	position.z = (int32_t)((uint32_t)position.z + (uint32_t)offset.z);
	SlipView3DVec32 reflected = SlipRacePlayer_CollisionVector(
	    context, (uint16_t)emission.direction.x, (uint16_t)emission.direction.y, (uint16_t)emission.direction.z, 0x100);
	const int32_t speed = SlipObject_Speed(context->objectTable, object);
	SlipView3DVec32 velocity =
	    SlipView3D_ScaleVector((int16_t)reflected.x, (int16_t)reflected.y, (int16_t)reflected.z, speed);
	SlipView3DVec32 normalVelocity =
	    SlipView3D_ScaleVector(emission.direction.x, emission.direction.y, emission.direction.z, 0x45d3);
	velocity.x = (int32_t)((uint32_t)velocity.x + (uint32_t)normalVelocity.x);
	velocity.y = (int32_t)((uint32_t)velocity.y + (uint32_t)normalVelocity.y);
	velocity.z = (int32_t)((uint32_t)velocity.z + (uint32_t)normalVelocity.z);
	SlipView3DNormalizeVector3D normalized;
	SlipView3D_NormalizeVector3D((uint32_t)velocity.x, (uint32_t)velocity.y, (uint32_t)velocity.z, &normalized);
	emission.direction =
	    (SlipView3DVec16){(int16_t)normalized.unitXQ14, (int16_t)normalized.unitYQ14, (int16_t)normalized.unitZQ14};
	emission.speed = SlipView3D_VectorLength(velocity.x, velocity.y, velocity.z);
	const uint8_t *const name =
	    SlipDraw3D_GetMaterialName(context->materialTable, context->materialTableBytes, material);
	if (name == NULL) {
		SlipRuntime_Fatal("Contact material lookup translation failed (0004fe90)");
	}
	if (memcmp(name, "WATE", 4) != 0) {
		emission.radius = 0x3d0;
		SlipCrossEffects_Create(position, 0x20, &emission, sparkMaterial, SlipRaceSession_UpdateCrossEffect,
		                        context->maths, context->materialTable, context->materialTableBytes);
	} else {
		emission.radius = 1;
		SlipCrossEffects_Create(position, savedCount, &emission, splashMaterial, SlipRaceSession_UpdateCrossEffect,
		                        context->maths, context->materialTable, context->materialTableBytes);
	}
}

bool SlipRaceEffects_Reclaim(const SlipObject *objects, size_t objectBytes, uint32_t eventCode, uint32_t eventPayload,
                             uint32_t eventValue, uint32_t eventFlags, uintptr_t dispatchData, uint32_t dispatchFrame) {
	if (SlipTimedEffects_Reclaim(objects, objectBytes, eventCode, eventPayload, eventValue, eventFlags, dispatchData,
	                             dispatchFrame))
		return true;
	if (SlipCrossEffects_Reclaim(objects, objectBytes, eventCode, eventPayload, eventValue, eventFlags, dispatchData,
	                             dispatchFrame))
		return true;
	if (SlipShapeEffects_Reclaim(objects, objectBytes, eventCode, eventPayload, eventValue, eventFlags, dispatchData,
	                             dispatchFrame))
		return true;
	return SlipAnimatedEffects_Reclaim(objects, objectBytes, eventCode, eventPayload, eventValue, eventFlags,
	                                   dispatchData, dispatchFrame);
}
