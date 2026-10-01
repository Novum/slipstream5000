#include "frame_timer.h"
#include "resource_host.h"
#include "runtime.h"
#include "timed_effects.h"
#include "track_view_render.h"
#include "track_world.h"
#include <stdlib.h>

bool SlipTimedEffects_SelectOldest(const SlipObject *objects, size_t objectBytes, uint16_t *selected) {
	uint32_t greatestAge = 0;
	*selected = UINT16_MAX;
	for (uint16_t object = SlipObject_Next(UINT16_MAX); object != UINT16_MAX; object = SlipObject_Next(object)) {
		SlipObjectDrawCallbackReadResult draw;
		if (!SlipObject_GetSlotDrawCallback(objects, objectBytes, object, &draw))
			return false;
		if (draw.slotDrawCallback != TrackView_DrawTimedEffect && draw.slotDrawCallback != TrackView_QueueTimedEffect)
			continue;
		const SlipTimedEffectObjectState *const state = SlipObject_TimedEffectState(object);
		if (state->age >= greatestAge) {
			*selected = object;
			greatestAge = state->age;
		}
	}
	return *selected != UINT16_MAX;
}

bool SlipTimedEffects_Reclaim(const SlipObject *objects, size_t objectBytes, uint32_t eventCode, uint32_t eventPayload,
                              uint32_t eventValue, uint32_t eventFlags, uintptr_t dispatchData,
                              uint32_t dispatchFrame) {
	uint16_t selected;
	if (!SlipTimedEffects_SelectOldest(objects, objectBytes, &selected))
		return false;
	SlipObject_FreeImmediate(selected, eventCode, eventPayload, eventValue, eventFlags, dispatchData, dispatchFrame);
	return true;
}

bool SlipTimedEffects_Position(const SlipTimedEffect *entry, const SlipObject *objects, size_t objectBytes,
                               SlipView3DVec32 *position) {
	SlipView3DVec32 local = {entry->x, entry->y, entry->z};
	if (entry->parentObject != 0) {
		SlipObjectPosition parent;
		SlipObjectMatrixCopy copy;
		SlipView3DMatrix matrix;
		if (!SlipObject_Position(objects, objectBytes, entry->parentObject, &parent)) {
			return false;
		}
		local.x = (int32_t)((uint32_t)local.x + (uint32_t)entry->displacementX);
		local.y = (int32_t)((uint32_t)local.y + (uint32_t)entry->displacementY);
		if (!SlipObject_MatrixCopy(objects, objectBytes, entry->parentObject, &matrix, &copy)) {
			return false;
		}
		local = SlipView3D_TransformPositionByColumns(&matrix, local);
		local.z = (int32_t)((uint32_t)local.z + parent.positionZ);
		local.y = (int32_t)((uint32_t)local.y + parent.positionY);
		local.x = (int32_t)((uint32_t)local.x + parent.positionX);
	}
	*position = local;
	return true;
}

void SlipTimedEffects_Displace(SlipTimedEffect *entry, uint32_t verticalDisplacementRange) {
	const uint32_t range = entry->descriptor->displacementRange;
	if (range != 0) {
		uint32_t random = SlipRandom_Next() & 0x3fffu;
		uint32_t displacement = (uint32_t)(((uint64_t)random * range) >> 14);
		if ((SlipRandom_Next() & 2u) != 0) {
			displacement = 0u - displacement;
		}
		entry->displacementX = (int32_t)displacement;
		random = SlipRandom_Next() & 0x3fffu;
		displacement = (uint32_t)(((uint64_t)random * verticalDisplacementRange) >> 14);
		if ((SlipRandom_Next() & 2u) != 0) {
			displacement = 0u - displacement;
		}
		entry->displacementY = (int32_t)displacement;
	}
}

uint16_t SlipTimedEffects_SelectFrame(const SlipTimedEffectFrames *frames, uint16_t previous) {
	uint16_t selected = previous;
	for (unsigned attempt = 0; attempt < 4; ++attempt) {
		const uint32_t product = (uint32_t)(uint16_t)SlipRandom_Next() * frames->count;
		const uint16_t offset = (uint16_t)((product >> 16) << 1);
		selected = frames->handles[offset / 2u];
		if (selected != previous) {
			break;
		}
	}
	return selected;
}

bool SlipTimedEffects_UpdateFrame(SlipTimedEffectObjectState *state, int32_t step, SlipObject *objects,
                                  size_t objectBytes, uint16_t object, uint32_t descriptorAddress,
                                  uint32_t finalFramesAddress, uint32_t *frameSelectionValue) {
	uint16_t selected;
	if (state->phase != 2) {
		const SlipTimedEffectFrames *const frames = state->descriptor->initialFrames;
		state->frameCountdown = (int16_t)((uint16_t)state->frameCountdown - (uint16_t)step);
		*frameSelectionValue = (*frameSelectionValue & 0xffff0000u) | (uint16_t)state->frameCountdown;
		if (state->frameCountdown >= 0) {
			return true;
		}
		state->frameCountdown = (int16_t)frames->delay;
		SlipObjectSlotDataReadResult current;
		if (!SlipObject_GetDrawData(objects, objectBytes, object, &current)) {
			return false;
		}

		selected = SlipTimedEffects_SelectFrame(frames, (uint16_t)current.drawData);

		*frameSelectionValue = selected;
	} else {
		const SlipTimedEffectFrames *const frames = state->descriptor->finalFrames;
		const uint64_t product = (uint64_t)frames->count * SlipTimedEffects_fraction;
		const uint16_t offset = (uint16_t)(product >> 15) & 0xfffeu;
		selected = frames->handles[offset / 2u];
		*frameSelectionValue = ((uint32_t)(product >> 15) & 0xffff0000u) | selected;
	}
	SlipObjectSlotDataWriteResult write;

	const uint32_t address = state->phase == 2 ? finalFramesAddress : descriptorAddress;
	return SlipObject_SetDrawData(objects, objectBytes, object, (address & 0xffff0000u) | selected, &write);
}

uint32_t SlipTimedEffects_speedLimit;

bool SlipTimedEffects_UpdateSpeed(const SlipTimedEffectObjectState *state, int32_t step,
                                  uint32_t *speedCalculationValue, SlipObject *objects, uint16_t object) {
	*speedCalculationValue = (*speedCalculationValue & 0xffff0000u) | state->speedLimit;
	if (state->speedLimit != 0) {
		SlipTimedEffects_speedLimit = (SlipTimedEffects_speedLimit & 0xffff0000u) | state->speedLimit;
		const uint32_t dividend = (uint32_t)(uint16_t)step << 14;
		const uint32_t quotient = dividend / 1000u;
		if (quotient > UINT16_MAX) {
			return false;
		}
		const uint32_t factor = (*speedCalculationValue & 0xffff0000u) | quotient;
		const uint32_t increment = (uint32_t)(((uint64_t)factor * SlipTimedEffects_speedLimit) >> 14);
		*speedCalculationValue = increment;
		uint32_t speed = (uint32_t)SlipObject_Speed(objects, object) + increment;
		if ((int32_t)speed >= (int32_t)SlipTimedEffects_speedLimit) {
			speed = SlipTimedEffects_speedLimit;
		}
		SlipObject_SetSpeed(objects, object, speed);
	}
	return true;
}

SlipTimedEffectUpdate SlipTimedEffects_update;

void SlipTimedEffects_UpdateObject(uint16_t object, uint32_t elapsed, uint32_t callerValue, uint32_t callerFrame) {
	SlipTimedEffectObjectState *const state = SlipObject_TimedEffectState(object);
	const SlipTimedEffectDescriptor *const descriptor = state->descriptor;
	SlipTimedEffectPhase phase;
	SlipTimedEffects_Phase(state, elapsed, &phase);
	if (phase.freeObject) {
		SlipObject_FreeImmediate(object, phase.callbackValue, descriptor->finalDuration, callerValue,
		                         phase.age - descriptor->holdDuration - descriptor->growthDuration,
		                         (uintptr_t)descriptor, callerFrame);
		return;
	}
	const size_t objectBytes = (size_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE;
	if (phase.writeExtent) {
		SlipObjectExtentWriteResult scalarWrite;
		(void)SlipObject_SetDrawExtent(SlipObject_table, objectBytes, object, phase.extent, &scalarWrite);
	}
	uint32_t speedResult = phase.callbackValue;
	(void)SlipTimedEffects_UpdateFrame(state, phase.step, SlipObject_table, objectBytes, object, descriptor->dosAddress,
	                                   descriptor->finalFramesDosAddress, &speedResult);
	if (!SlipTimedEffects_UpdateSpeed(state, phase.step, &speedResult, SlipObject_table, object))
		SlipRuntime_Fatal("DOS timed effect DIV overflow (00027be5)");
	if (SlipTimedEffects_update != NULL)
		SlipTimedEffects_update(object, speedResult);
}

uint32_t SlipTimedEffects_Event(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                uint16_t object, uintptr_t dispatchData, uint32_t dispatchFrame) {
	(void)eventPayload;
	(void)eventValue;
	(void)eventFlags;
	(void)dispatchData;
	if ((uint16_t)eventCode != SLIP_OBJECT_EVENT_UPDATE)
		return UINT32_MAX;
	SlipFrameTimerValues timer = SlipFrameTimer_Values();
	SlipTimedEffects_UpdateObject(object, timer.deltaMilliseconds, timer.frameRateHz, dispatchFrame);
	return 0;
}

uint32_t SlipTimedEffects_initialized;
uint16_t SlipTimedEffects_drawMode;
uint16_t SlipTimedEffects_objectLimit;
uint16_t SlipTimedEffects_interpolationLimit;
SlipTimedEffectAttach SlipTimedEffects_attach;
static SlipTimedEffect *emitterStorage;
static uint16_t emitterResource;

bool SlipTimedEffects_Initialize(uint16_t count, uint16_t drawMode, uint16_t objectLimit, uint16_t interpolationLimit,
                                 SlipTimedEffectAttach attach, SlipTimedEffectUpdate update) {
	if (SlipTimedEffects_initialized != 0)
		return true;
	SlipTimedEffects_initialized = UINT32_MAX;
	SlipTimedEffects_objectLimit = objectLimit;
	SlipTimedEffects_drawMode = drawMode;
	SlipTimedEffects_attach = attach;
	SlipTimedEffects_update = update;
	SlipTimedEffects_interpolationLimit = interpolationLimit;
	SlipTimedEffects_count = count;

	if (count == 0 || count >= UINT16_MAX - 1u)
		SlipRuntime_Fatal("DOS timed emitter allocation/reset overflow (000274e3/000278b5)");

	enum { DOS_EMITTER_BYTES = 0x30, EMITTER_SENTINELS = 2 };

	const uint32_t resourceBytes = (uint32_t)(uint16_t)(count + EMITTER_SENTINELS) * DOS_EMITTER_BYTES;
	if (!SlipResourceHost_Allocate(NULL, resourceBytes, 0, &emitterResource))
		return false;
	(void)SlipResourceHost_LockReserved(NULL, emitterResource);
	emitterStorage = malloc((size_t)(uint16_t)(count + 2u) * sizeof(*emitterStorage));
	if (emitterStorage == NULL)
		SlipRuntime_Fatal("Cannot allocate native timed emitter records");
	SlipTimedEffects_active = emitterStorage;
	SlipTimedEffects_free = emitterStorage + 1;
	SlipTimedEffects_Reset();
	SlipTimedEffects_objectCount = 0;
	SlipObject_SetServer(1, SlipTimedEffects_Notify);
	SlipRuntime_RegisterExit(SlipTimedEffects_Cleanup);
	return true;
}

void SlipTimedEffects_Cleanup(void) {
	if (SlipTimedEffects_initialized != 0) {
		SlipTimedEffects_initialized = 0;
		SlipResourceHost_Unlock(NULL, emitterResource);
		SlipResourceHost_Release(NULL, emitterResource);
		emitterResource = 0;
		free(emitterStorage);
		emitterStorage = NULL;
	}
}

uint32_t SlipTimedEffects_Notify(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                 uint16_t object, uintptr_t dispatchData, uint32_t dispatchFrame) {
	(void)eventPayload;
	(void)eventValue;
	(void)eventFlags;
	(void)dispatchData;
	(void)dispatchFrame;
	if (object == 0 || (eventCode & 1u) == 0 || SlipTimedEffects_initialized == 0)
		return eventCode;
	SlipTimedEffects_RemoveParent(object);
	SlipTimedEffect *entry = SlipTimedEffects_active->next;
	while (entry != SlipTimedEffects_active) {
		SlipTimedEffect *const next = entry->next;
		if (entry->currentObject == object)
			entry->currentObject = 0;
		if (entry->precedingObject == object)
			entry->precedingObject = 0;
		entry = next;
	}
	if (SlipObject_Callback(object) == SlipTimedEffects_Event)
		SlipTimedEffects_ObjectDeleted(object);
	return eventCode;
}

bool SlipTimedEffects_CreateObject(const SlipTimedEffect *emitter, SlipView3DVec32 position,
                                   const SlipView3DMatrix *creationTemplate, uint32_t emitterDosAddress,
                                   uint16_t *createdObject, uint32_t *displacementScale) {
	*displacementScale = (uint32_t)position.y;
	uint16_t object;
	bool reuse = SlipTimedEffects_objectCount == SlipTimedEffects_objectLimit;
	const size_t objectBytes = (size_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE;
	if (!reuse) {
		SlipObjectSlotFill fill;
		const SlipObjectDrawCallback draw =
		    SlipTimedEffects_drawMode != 0 ? TrackView_QueueTimedEffect : TrackView_DrawTimedEffect;
		if (!SlipObject_SlotFill(creationTemplate, (uint32_t)position.x, (uint32_t)position.y, (uint32_t)position.z,
		                         draw, UINT32_MAX, SlipTimedEffects_Event, &fill))
			SlipRuntime_Fatal("Timed effect slot creation translation failed (00027c6d)");
		reuse = fill.carryOut;
		if (!reuse) {
			object = (uint16_t)fill.objectOffset;
			++SlipTimedEffects_objectCount;
		}
	}
	if (reuse && !SlipTimedEffects_SelectOldest(SlipObject_table, objectBytes, &object))
		return false;
	SlipTimedEffectObjectState *const state = SlipObject_TimedEffectState(object);
	state->speedLimit = emitter->speedLimit;
	const SlipTimedEffectDescriptor *const descriptor = emitter->descriptor;
	int32_t factor = ((int16_t)SlipRandom_Next() >> 4) + 0x4000;
	state->holdExtent = (int32_t)((uint64_t)((int64_t)factor * descriptor->holdExtent) >> 14);
	factor = ((int16_t)SlipRandom_Next() >> 5) + 0x4000;
	state->initialExtent = (int32_t)((uint64_t)((int64_t)factor * descriptor->initialExtent) >> 14);
	SlipObjectExtentWriteResult scalarWrite;
	(void)SlipObject_SetDrawExtent(SlipObject_table, objectBytes, object, (uint32_t)state->initialExtent, &scalarWrite);
	state->descriptor = descriptor;
	const uint16_t random = (uint16_t)SlipRandom_Next();
	const SlipTimedEffectFrames *const frames = descriptor->initialFrames;
	const uint16_t frameOffset = (uint16_t)((((uint32_t)random * frames->count) >> 16) << 1);
	SlipObjectSlotDataWriteResult frameWrite;
	(void)SlipObject_SetDrawData(SlipObject_table, objectBytes, object,
	                             (emitterDosAddress & 0xffff0000u) | frames->handles[frameOffset / 2u], &frameWrite);
	state->age = 0;
	state->frameCountdown = 0;
	state->phase = 0;
	SlipObject_SetSpeed(SlipObject_table, object, 0);
	SlipObject_SetDirectionQ14(SlipObject_table, object, 0, 0x4000, 0);

	*displacementScale = ((descriptor->initialFramesDosAddress + frameOffset) & 0xffff0000u) | 0x4000u;
	if (SlipTimedEffects_attach != NULL) {
		*displacementScale =
		    SlipTimedEffects_attach(object, state->holdExtent, emitter, (uint32_t)position.z & 0xffff0000u);
		if (!SlipObject_IsLive(object))
			return false;
	}
	*createdObject = object;
	return true;
}

void SlipTimedEffects_Tick(const SlipView3DMatrix *creationTemplate, uint32_t emitterPoolDosAddress) {
	SlipFrameTimerValues timer = SlipFrameTimer_Values();
	const uint32_t elapsed = timer.deltaMilliseconds;
	uint32_t displacementScale = timer.stepQ14;
	const size_t objectBytes = (size_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE;
	for (SlipTimedEffect *entry = SlipTimedEffects_active->next; entry != SlipTimedEffects_active;
	     entry = entry->next) {
		entry->lifetime = (int32_t)((uint32_t)entry->lifetime - elapsed);
		if (entry->lifetime < 0) {
			SlipTimedEffect *const previous = entry->previous;
			SlipTimedEffects_Release(entry);
			entry = previous;
			continue;
		}
		const uint32_t entryAddress = emitterPoolDosAddress + (uint32_t)(entry - SlipTimedEffects_active) * 0x30u;
		const SlipTimedEffectDescriptor *const descriptor = entry->descriptor;
		bool emit = true;
		if (descriptor->emissionPeriod != -1) {
			entry->periodCountdown = (int32_t)((uint32_t)entry->periodCountdown - elapsed);
			emit = entry->periodCountdown < 0;
			if (emit)
				entry->periodCountdown = descriptor->emissionPeriod;
		} else {
			SlipView3DVec32 origin;
			(void)SlipTimedEffects_Position(entry, SlipObject_table, objectBytes, &origin);
			displacementScale = (uint32_t)origin.y;
			if (entry->precedingObject != 0) {
				SlipObjectPosition preceding;
				(void)SlipObject_Position(SlipObject_table, objectBytes, entry->precedingObject, &preceding);
				SlipView3DVec32 difference = {(int32_t)(preceding.positionX - (uint32_t)origin.x),
				                              (int32_t)(preceding.positionY - (uint32_t)origin.y),
				                              (int32_t)(preceding.positionZ - (uint32_t)origin.z)};
				const uint32_t distance = SlipView3D_ApproximateLength(difference.x, difference.y, difference.z);
				displacementScale = distance;
				const uint32_t spacing =
				    (uint32_t)(((uint64_t)((uint32_t)descriptor->initialExtent << 1) * 0x3f00u) >> 14);
				emit = (int32_t)distance >= (int32_t)spacing;
				if (emit) {
					if (spacing == 0)
						SlipRuntime_Fatal("DOS timed emitter division by zero (000276bc)");
					uint32_t quotient = distance / spacing;
					if ((int16_t)quotient > (int16_t)SlipTimedEffects_interpolationLimit)
						quotient = (quotient & 0xffff0000u) | SlipTimedEffects_interpolationLimit;
					uint16_t remaining = (uint16_t)quotient;
					const uint16_t divisor = (uint16_t)(quotient + 1u);
					if (divisor == 0)
						SlipRuntime_Fatal("DOS timed emitter division by zero (000276e0)");
					const uint32_t weightStep = 0x4000u / divisor;
					uint32_t weight = ((quotient + 1u) & 0xffff0000u) | (uint16_t)(0x4000u - weightStep);
					const uint32_t precedingAge = SlipObject_TimedEffectState(entry->precedingObject)->age;
					do {
						const uint32_t fraction = (uint16_t)weight;
						SlipView3DVec32 position = {
						    (int32_t)((uint32_t)origin.x + (uint32_t)(((int64_t)difference.x * fraction) >> 14)),
						    (int32_t)((uint32_t)origin.y + (uint32_t)(((int64_t)difference.y * fraction) >> 14)),
						    (int32_t)((uint32_t)origin.z + (uint32_t)(((int64_t)difference.z * fraction) >> 14))};
						uint16_t created;
						if (!SlipTimedEffects_CreateObject(entry, position, creationTemplate, entryAddress, &created,
						                                   &displacementScale)) {
							displacementScale = weight;
							emit = false;
							break;
						}
						entry->currentObject = created;
						const uint32_t age = (uint32_t)(((uint64_t)weight * precedingAge) >> 14);
						SlipTimedEffects_UpdateObject(created, age, (uint32_t)position.z & 0xffff0000u,
						                              descriptor->dosAddress);
						displacementScale = weight;
						if (!SlipObject_IsLive(created))
							break;
						weight -= weightStep;
						displacementScale = weight;
					} while (--remaining != 0);
				}
			}
		}
		if (emit) {
			if (entry->currentObject == 0) {
				SlipTimedEffects_Displace(entry, displacementScale);
				SlipView3DVec32 position;
				(void)SlipTimedEffects_Position(entry, SlipObject_table, objectBytes, &position);
				uint16_t created;
				if (!SlipTimedEffects_CreateObject(entry, position, creationTemplate, entryAddress, &created,
				                                   &displacementScale))
					continue;
				entry->currentObject = created;
			}
			SlipTimedEffects_Displace(entry, displacementScale);
			SlipView3DVec32 position;
			(void)SlipTimedEffects_Position(entry, SlipObject_table, objectBytes, &position);
			uint16_t created;
			if (!SlipTimedEffects_CreateObject(entry, position, creationTemplate, entryAddress, &created,
			                                   &displacementScale))
				continue;
			entry->precedingObject = entry->currentObject;
			entry->currentObject = created;
		}
		SlipView3DVec32 position;
		(void)SlipTimedEffects_Position(entry, SlipObject_table, objectBytes, &position);
		displacementScale = (uint32_t)position.y;
		if (entry->currentObject != 0) {
			SlipObjectSetPosition positioned;
			(void)SlipObject_SetPosition(SlipObject_table, objectBytes, entry->currentObject, (uint32_t)position.x,
			                             (uint32_t)position.y, (uint32_t)position.z, &positioned);
			displacementScale = descriptor->dosAddress;
			if (descriptor->attachedFrames != NULL) {
				SlipObjectSlotDataReadResult current;
				(void)SlipObject_GetDrawData(SlipObject_table, objectBytes, entry->currentObject, &current);
				displacementScale = (displacementScale & 0xffff0000u) | (uint16_t)current.drawData;
				const uint16_t selected =
				    SlipTimedEffects_SelectFrame(descriptor->attachedFrames, (uint16_t)current.drawData);
				SlipObjectSlotDataWriteResult frameWrite;
				(void)SlipObject_SetDrawData(SlipObject_table, objectBytes, entry->currentObject,
				                             (entryAddress & 0xffff0000u) | selected, &frameWrite);
			}
		}
	}
}
