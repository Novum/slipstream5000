#include "shape_effects.h"
#include "runtime.h"
#include "track_view_render.h"
#include "track_world.h"

enum {
	SLIP_SHAPE_EFFECT_SERVER_SLOT = SLIP_OBJECT_RELEASE_SERVER_ID,
	SLIP_SHAPE_EFFECT_NOTIFICATION_BIT = SLIP_OBJECT_SERVER_EVENT_FREE,
	SLIP_SHAPE_EFFECT_DRAW_PREFIX_MASK = 0xffff0000u
};

uint32_t SlipShapeEffects_initialized;
static SlipView3DVec32 SlipShapeEffects_position;
static const SlipView3DMatrix *SlipShapeEffects_matrix;
static uint16_t SlipShapeEffects_object;
static uint16_t SlipShapeEffects_shape;

void SlipShapeEffects_Initialize(void) {
	if (SlipShapeEffects_initialized == 0) {
		SlipShapeEffects_initialized = UINT32_MAX;
		SlipObject_SetServer(SLIP_SHAPE_EFFECT_SERVER_SLOT, SlipShapeEffects_Notify);
		SlipRuntime_RegisterExit(SlipShapeEffects_Cleanup);
	}
}

void SlipShapeEffects_Cleanup(void) {
	if (SlipShapeEffects_initialized != 0)
		SlipShapeEffects_initialized = 0;
}

uint32_t SlipShapeEffects_Notify(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                 uint16_t object, uintptr_t dispatchData, uint32_t dispatchFrame) {
	(void)eventPayload;
	(void)eventValue;
	(void)eventFlags;
	(void)dispatchData;
	(void)dispatchFrame;
	if (object != 0 && (eventCode & SLIP_SHAPE_EFFECT_NOTIFICATION_BIT) != 0 && SlipShapeEffects_initialized != 0) {
		SlipObjectDrawCallbackReadResult draw;
		(void)SlipObject_GetSlotDrawCallback(SlipObject_table, (size_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE,
		                                     object, &draw);
		if (draw.slotDrawCallback == TrackView_DrawShapeEffect)
			return eventCode;
		if (draw.slotDrawCallback != TrackView_QueueShapeEffect)
			return eventCode;
	}
	return eventCode;
}

bool SlipShapeEffects_Create(SlipView3DVec32 position, const SlipView3DMatrix *matrix, uint16_t shape,
                             uint32_t drawDataPrefix, uint16_t *createdObject) {
	SlipShapeEffects_position = position;
	SlipShapeEffects_matrix = matrix;
	SlipShapeEffects_shape = shape;
	SlipObjectSlotFill fill;
	if (!SlipObject_SlotFill(
	        SlipShapeEffects_matrix, (uint32_t)SlipShapeEffects_position.x, (uint32_t)SlipShapeEffects_position.y,
	        (uint32_t)SlipShapeEffects_position.z, TrackView_DrawShapeEffect,
	        (drawDataPrefix & SLIP_SHAPE_EFFECT_DRAW_PREFIX_MASK) | SlipShapeEffects_shape, NULL, &fill) ||
	    fill.carryOut)
		return false;
	SlipShapeEffects_object = (uint16_t)fill.objectOffset;
	*createdObject = SlipShapeEffects_object;
	return true;
}

bool SlipShapeEffects_SelectOldest(const SlipObject *objects, size_t objectBytes, uint16_t *selected) {
	uint32_t greatestAge = 0;
	*selected = UINT16_MAX;
	for (uint16_t object = SlipObject_Next(UINT16_MAX); object != UINT16_MAX; object = SlipObject_Next(object)) {
		SlipObjectDrawCallbackReadResult draw;
		if (!SlipObject_GetSlotDrawCallback(objects, objectBytes, object, &draw))
			return false;
		if (draw.slotDrawCallback != TrackView_DrawShapeEffect && draw.slotDrawCallback != TrackView_QueueShapeEffect)
			continue;
		const uint32_t age = objects[object / SLIP_OBJECT_DOS_STRIDE].debrisEffect.elapsed;
		if (age >= greatestAge) {
			*selected = object;
			greatestAge = age;
		}
	}
	return *selected != UINT16_MAX;
}

bool SlipShapeEffects_Reclaim(const SlipObject *objects, size_t objectBytes, uint32_t eventCode, uint32_t eventPayload,
                              uint32_t eventValue, uint32_t eventFlags, uintptr_t dispatchData,
                              uint32_t dispatchFrame) {
	uint16_t selected;
	if (!SlipShapeEffects_SelectOldest(objects, objectBytes, &selected))
		return false;
	SlipObject_FreeImmediate(selected, eventCode, eventPayload, eventValue, eventFlags, dispatchData, dispatchFrame);
	return true;
}
