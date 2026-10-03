#include "animated_effects.h"
#include "fixed_point.h"
#include "frame_timer.h"
#include "runtime.h"
#include "track_view_render.h"
#include "track_world.h"

enum {
	SLIP_ANIMATED_DEFAULT_JITTER = 488,
	SLIP_ANIMATED_INITIAL_RADIUS_SHIFT = 2,
	SLIP_ANIMATED_FINAL_DURATION_SHIFT = 2,
	SLIP_ANIMATED_VALUE_UPPER_WORD_MASK = 0xffff0000u,
	SLIP_ANIMATED_FRAME_SELECTION_ATTEMPTS = 4,
	SLIP_ANIMATED_FRAME_HANDLE_BYTES = sizeof(uint16_t),
	SLIP_ANIMATED_FRAME_OFFSET_SHIFT = 1,
	SLIP_ANIMATED_FRAME_OFFSET_MASK = UINT16_MAX - (SLIP_ANIMATED_FRAME_HANDLE_BYTES - 1)
};

SlipAnimatedUpdate SlipAnimatedEffects_update;
int16_t SlipAnimatedEffects_jitter = SLIP_ANIMATED_DEFAULT_JITTER;

void SlipAnimatedEffects_UpdateAttachment(uint16_t object) {
	SlipAnimatedState *const state = SlipObject_AnimatedState(object);
	if (state->parent == 0)
		return;
	const int32_t jitterZ =
	    ((int32_t)(int16_t)SlipRandom_Next() * SlipAnimatedEffects_jitter) >> SLIP_RANDOM_SAMPLE_BITS;
	const int32_t jitterY =
	    ((int32_t)(int16_t)SlipRandom_Next() * SlipAnimatedEffects_jitter) >> SLIP_RANDOM_SAMPLE_BITS;
	const int32_t jitterX =
	    ((int32_t)(int16_t)SlipRandom_Next() * SlipAnimatedEffects_jitter) >> SLIP_RANDOM_SAMPLE_BITS;
	SlipView3DVec32 attachmentOffset = {(int32_t)((uint32_t)jitterX + (uint32_t)state->offset.x),
	                                    (int32_t)((uint32_t)jitterY + (uint32_t)state->offset.y),
	                                    (int32_t)((uint32_t)jitterZ + (uint32_t)state->offset.z)};
	const size_t objectBytes = (size_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE;
	SlipView3DMatrix matrix;
	SlipObjectMatrixCopy matrixCopy;
	SlipObjectPosition parent;
	SlipObjectSetPosition positioned;
	(void)SlipObject_MatrixCopy(SlipObject_table, objectBytes, state->parent, &matrix, &matrixCopy);
	attachmentOffset = SlipView3D_TransformPositionByColumns(&matrix, attachmentOffset);
	(void)SlipObject_Position(SlipObject_table, objectBytes, state->parent, &parent);
	(void)SlipObject_SetPosition(SlipObject_table, objectBytes, object, (uint32_t)attachmentOffset.x + parent.positionX,
	                             (uint32_t)attachmentOffset.y + parent.positionY,
	                             (uint32_t)attachmentOffset.z + parent.positionZ, &positioned);
}

uint32_t SlipAnimatedEffects_Event(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                   uint16_t object, uintptr_t dispatchData, uint32_t dispatchFrame) {
	(void)eventPayload;
	(void)eventValue;
	(void)dispatchData;
	(void)dispatchFrame;
	if ((uint16_t)eventCode == SLIP_OBJECT_EVENT_HANDLE_ACTION) {
		SlipAnimatedEffects_UpdateAttachment(object);
		if (SlipAnimatedEffects_update != NULL)
			(void)SlipAnimatedEffects_update(object);
		return 0;
	}
	if ((uint16_t)eventCode != SLIP_OBJECT_EVENT_UPDATE)
		return UINT32_MAX;

	SlipAnimatedState *const state = SlipObject_AnimatedState(object);
	SlipFrameTimerValues timer = SlipFrameTimer_Values();
	const uint16_t deltaMilliseconds = (uint16_t)timer.deltaMilliseconds;
	state->age = (uint16_t)(state->age + deltaMilliseconds);
	const SlipAnimatedDescriptor *const descriptor = state->descriptor;
	const uint16_t duration = (uint16_t)(state->initialDuration + state->finalDuration);
	if (state->age >= duration) {
		SlipObject_FreeImmediate(
		    object, (timer.deltaMilliseconds & SLIP_ANIMATED_VALUE_UPPER_WORD_MASK) | state->age,
		    (timer.stepQ14 & SLIP_ANIMATED_VALUE_UPPER_WORD_MASK) | duration, timer.deltaMilliseconds,
		    (eventFlags & SLIP_ANIMATED_VALUE_UPPER_WORD_MASK) | state->age, (uintptr_t)state, descriptor->dosAddress);
		return 0;
	}

	uint32_t lifetimeFractionQ14 = ((uint32_t)state->age << SLIP_Q14_FRACTION_BITS) / duration;
	const int32_t radiusDifference = (int32_t)((uint32_t)state->finalRadius - (uint32_t)state->initialRadius);
	uint32_t radiusFrameSpeedOrCallbackResult =
	    (uint32_t)(((int64_t)lifetimeFractionQ14 * radiusDifference) >> SLIP_Q14_FRACTION_BITS) +
	    (uint32_t)state->initialRadius;
	const size_t objectBytes = (size_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE;
	SlipObjectExtentWriteResult radiusWrite;
	(void)SlipObject_SetDrawExtent(SlipObject_table, objectBytes, object, radiusFrameSpeedOrCallbackResult,
	                               &radiusWrite);

	if (state->age <= state->initialDuration) {
		const SlipAnimatedFrames *const frames = descriptor->initialFrames;
		state->frameElapsed = (uint16_t)(state->frameElapsed + deltaMilliseconds);
		radiusFrameSpeedOrCallbackResult =
		    (radiusFrameSpeedOrCallbackResult & SLIP_ANIMATED_VALUE_UPPER_WORD_MASK) | state->frameElapsed;
		if (state->frameElapsed >= frames->frameDelay) {
			state->frameElapsed = 0;
			SlipObjectSlotDataReadResult current;
			(void)SlipObject_GetDrawData(SlipObject_table, objectBytes, object, &current);

			for (unsigned attempt = 0; attempt < SLIP_ANIMATED_FRAME_SELECTION_ATTEMPTS; ++attempt) {
				const uint32_t product = (uint32_t)(uint16_t)SlipRandom_Next() * frames->frameCount;
				const uint16_t offset =
				    (uint16_t)((product >> SLIP_RANDOM_SAMPLE_BITS) << SLIP_ANIMATED_FRAME_OFFSET_SHIFT);
				radiusFrameSpeedOrCallbackResult = frames->frameHandles[offset / SLIP_ANIMATED_FRAME_HANDLE_BYTES];
				if ((uint16_t)radiusFrameSpeedOrCallbackResult != (uint16_t)current.drawData)
					break;
			}
			SlipObjectSlotDataWriteResult frameWrite;
			(void)SlipObject_SetDrawData(SlipObject_table, objectBytes, object, radiusFrameSpeedOrCallbackResult,
			                             &frameWrite);
		}
	} else {
		const uint16_t finalPhaseElapsed = (uint16_t)(state->age - state->initialDuration);
		lifetimeFractionQ14 = ((uint32_t)finalPhaseElapsed << SLIP_Q14_FRACTION_BITS) / state->finalDuration;
		const SlipAnimatedFrames *const frames = descriptor->finalFrames;
		const uint16_t offset = (uint16_t)((lifetimeFractionQ14 * frames->frameCount) >>
		                                   (SLIP_Q14_FRACTION_BITS - SLIP_ANIMATED_FRAME_OFFSET_SHIFT)) &
		                        SLIP_ANIMATED_FRAME_OFFSET_MASK;
		radiusFrameSpeedOrCallbackResult = frames->frameHandles[offset / SLIP_ANIMATED_FRAME_HANDLE_BYTES];
		SlipObjectSlotDataWriteResult frameWrite;
		(void)SlipObject_SetDrawData(SlipObject_table, objectBytes, object, radiusFrameSpeedOrCallbackResult,
		                             &frameWrite);
	}

	const uint32_t speed = (uint32_t)SlipObject_Speed(SlipObject_table, object);
	if (speed != 0) {
		const uint16_t step = (uint16_t)SlipFrameTimer_Step();
		const uint16_t damping = (uint16_t)(((uint32_t)descriptor->damping * step) >> SLIP_Q14_FRACTION_BITS);
		const uint16_t remainingSpeedFractionQ14 = (uint16_t)(SLIP_Q14_ONE - damping);
		radiusFrameSpeedOrCallbackResult =
		    (uint32_t)(((uint64_t)remainingSpeedFractionQ14 * speed) >> SLIP_Q14_FRACTION_BITS);
		SlipObject_SetSpeed(SlipObject_table, object, radiusFrameSpeedOrCallbackResult);
	}
	if (SlipAnimatedEffects_update != NULL)
		radiusFrameSpeedOrCallbackResult = SlipAnimatedEffects_update(object);

	return radiusFrameSpeedOrCallbackResult & SLIP_ANIMATED_VALUE_UPPER_WORD_MASK;
}

uint32_t SlipAnimatedEffects_initialized;
uint32_t SlipAnimatedEffects_drawMode;
SlipAnimatedAttach SlipAnimatedEffects_attach;
static SlipView3DVec32 SlipAnimatedEffects_offset;
static int32_t SlipAnimatedEffects_radius;
static uint16_t SlipAnimatedEffects_parent;
static uint16_t SlipAnimatedEffects_duration;
static const SlipView3DMatrix SlipAnimatedEffects_identity = {
    {SLIP_Q14_ONE, 0, 0, 0, SLIP_Q14_ONE, 0, 0, 0, SLIP_Q14_ONE}};

void SlipAnimatedEffects_Initialize(uint32_t drawMode, SlipAnimatedAttach attach, SlipAnimatedUpdate update) {
	if (SlipAnimatedEffects_initialized == 0) {
		SlipAnimatedEffects_initialized = UINT32_MAX;
		SlipAnimatedEffects_drawMode = drawMode;
		SlipAnimatedEffects_attach = attach;
		SlipAnimatedEffects_update = update;
		SlipRuntime_RegisterExit(SlipAnimatedEffects_Cleanup);
		SlipObject_SetServer(SLIP_OBJECT_RELEASE_SERVER_ID, SlipAnimatedEffects_Notify);
	}
}

void SlipAnimatedEffects_Cleanup(void) {
	if (SlipAnimatedEffects_initialized != 0)
		SlipAnimatedEffects_initialized = 0;
}

uint32_t SlipAnimatedEffects_Notify(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                    uint16_t object, uintptr_t dispatchData, uint32_t dispatchFrame) {
	(void)dispatchData;
	if (object != 0 && (eventCode & SLIP_OBJECT_SERVER_EVENT_FREE) != 0 && SlipAnimatedEffects_initialized != 0)
		SlipAnimatedEffects_RemoveParent(object, eventCode, eventPayload, eventValue, eventFlags, dispatchFrame);
	return eventCode;
}

void SlipAnimatedEffects_RemoveParent(uint16_t parent, uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                      uint32_t eventFlags, uint32_t dispatchFrame) {
	uint16_t object = SlipObject_Next(UINT16_MAX);
	const size_t objectBytes = (size_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE;
	while (object != UINT16_MAX) {
		SlipObjectDrawCallbackReadResult draw;
		(void)SlipObject_GetSlotDrawCallback(SlipObject_table, objectBytes, object, &draw);
		if (draw.slotDrawCallback == TrackView_DrawAnimatedEffect ||
		    draw.slotDrawCallback == TrackView_QueueAnimatedEffect) {
			SlipAnimatedState *const state = SlipObject_AnimatedState(object);
			eventCode = (eventCode & SLIP_ANIMATED_VALUE_UPPER_WORD_MASK) | state->parent;
			if (state->parent == parent) {
				SlipObject_FreeImmediate(object, eventCode, eventPayload, eventValue, eventFlags, (uintptr_t)state,
				                         dispatchFrame);
				object = SlipObject_Next(UINT16_MAX);
				continue;
			}
		}
		object = SlipObject_Next(object);
	}
}

bool SlipAnimatedEffects_Create(SlipView3DVec32 position, int32_t radius, uint16_t duration, uint16_t parent,
                                const SlipAnimatedDescriptor *descriptor, uint16_t *createdObject) {
	SlipAnimatedEffects_radius = radius;
	SlipAnimatedEffects_duration = duration;
	SlipAnimatedEffects_parent = parent;
	if (parent != 0) {
		SlipAnimatedEffects_RemoveParent(parent, (uint32_t)position.x, (uint32_t)position.y, (uint32_t)position.z,
		                                 (uint32_t)radius, duration);
		SlipAnimatedEffects_offset = position;
	}
	SlipObjectSlotFill fill;
	const SlipObjectDrawCallback draw =
	    SlipAnimatedEffects_drawMode != 0 ? TrackView_QueueAnimatedEffect : TrackView_DrawAnimatedEffect;
	if (!SlipObject_SlotFill(&SlipAnimatedEffects_identity, (uint32_t)position.x, (uint32_t)position.y,
	                         (uint32_t)position.z, draw, UINT32_MAX, SlipAnimatedEffects_Event, &fill) ||
	    fill.carryOut)
		return false;
	const uint16_t object = (uint16_t)fill.objectOffset;
	const size_t objectBytes = (size_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE;
	SlipAnimatedState *const state = SlipObject_AnimatedState(object);
	state->descriptor = descriptor;
	SlipObjectSlotDataWriteResult frameWrite;
	(void)SlipObject_SetDrawData(SlipObject_table, objectBytes, object, descriptor->initialFrames->frameHandles[0],
	                             &frameWrite);
	state->age = 0;
	state->frameElapsed = 0;
	state->finalRadius = SlipAnimatedEffects_radius;
	const int32_t initialRadius = SlipAnimatedEffects_radius >> SLIP_ANIMATED_INITIAL_RADIUS_SHIFT;
	SlipObjectExtentWriteResult radiusWrite;
	(void)SlipObject_SetDrawExtent(SlipObject_table, objectBytes, object, (uint32_t)initialRadius, &radiusWrite);
	state->initialRadius = initialRadius;
	const uint16_t finalDuration =
	    (uint16_t)((int16_t)SlipAnimatedEffects_duration >> SLIP_ANIMATED_FINAL_DURATION_SHIFT);
	state->finalDuration = finalDuration;
	state->initialDuration = (uint16_t)(SlipAnimatedEffects_duration - finalDuration);
	state->offset = SlipAnimatedEffects_offset;
	state->parent = SlipAnimatedEffects_parent;
	SlipAnimatedEffects_UpdateAttachment(object);
	if (SlipAnimatedEffects_attach != NULL) {
		SlipAnimatedEffects_attach(object, SlipAnimatedEffects_radius);
		if (!SlipObject_IsLive(object))
			return false;
	}
	*createdObject = object;
	return true;
}

bool SlipAnimatedEffects_SelectOldest(const SlipObject *objects, size_t objectBytes, uint16_t *selected) {
	uint32_t greatestAge = 0;
	*selected = UINT16_MAX;
	for (uint16_t object = SlipObject_Next(UINT16_MAX); object != UINT16_MAX; object = SlipObject_Next(object)) {
		SlipObjectDrawCallbackReadResult draw;
		if (!SlipObject_GetSlotDrawCallback(objects, objectBytes, object, &draw))
			return false;
		if (draw.slotDrawCallback != TrackView_DrawAnimatedEffect &&
		    draw.slotDrawCallback != TrackView_QueueAnimatedEffect)
			continue;
		const uint32_t age = SlipObject_AnimatedState(object)->age;
		if (age >= greatestAge) {
			*selected = object;
			greatestAge = age;
		}
	}
	return *selected != UINT16_MAX;
}

bool SlipAnimatedEffects_Reclaim(const SlipObject *objects, size_t objectBytes, uint32_t eventCode,
                                 uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                 uintptr_t dispatchData, uint32_t dispatchFrame) {
	uint16_t selected;
	if (!SlipAnimatedEffects_SelectOldest(objects, objectBytes, &selected))
		return false;
	SlipObject_FreeImmediate(selected, eventCode, eventPayload, eventValue, eventFlags, dispatchData, dispatchFrame);
	return true;
}
