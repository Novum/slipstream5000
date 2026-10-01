#include "cross_effects.h"
#include "frame_timer.h"
#include "runtime.h"
#include "track_view_render.h"
#include "track_world.h"

uint32_t SlipCrossEffects_drawMode;
SlipCrossEffectAttach SlipCrossEffects_attach;
uint32_t SlipCrossEffects_initialized;
uint16_t SlipCrossEffects_paletteStart;
uint16_t SlipCrossEffects_paletteCount;

void SlipCrossEffects_Initialize(uint32_t drawMode, SlipCrossEffectAttach attach) {
	if (SlipCrossEffects_initialized == 0) {
		SlipCrossEffects_initialized = UINT32_MAX;
		SlipCrossEffects_drawMode = drawMode;
		SlipCrossEffects_attach = attach;
		SlipRuntime_RegisterExit(SlipCrossEffects_Cleanup);
	}
}

void SlipCrossEffects_Cleanup(void) {
	if (SlipCrossEffects_initialized != 0)
		SlipCrossEffects_initialized = 0;
}

bool SlipCrossEffects_Color(SlipObject *objects, size_t objectBytes, uint16_t object, uint32_t *colorPayloadBits) {
	const uint16_t endpoint = (uint16_t)(SlipCrossEffects_paletteStart + SlipCrossEffects_paletteCount);
	*colorPayloadBits = ((*colorPayloadBits & 0xffff0000u) | endpoint) - 1u;
	SlipObjectSlotDataReadResult current;
	if (!SlipObject_GetDrawData(objects, objectBytes, object, &current))
		return false;
	const uint32_t drawDataWithColor = (current.drawData & 0xffffu) | (*colorPayloadBits << 16);
	SlipObjectSlotDataWriteResult write;
	return SlipObject_SetDrawData(objects, objectBytes, object, drawDataWithColor, &write);
}

uint32_t SlipCrossEffects_Event(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                uint16_t object, uintptr_t dispatchData, uint32_t dispatchFrame) {
	(void)eventPayload;
	(void)eventValue;
	(void)dispatchData;
	if ((uint16_t)eventCode != SLIP_OBJECT_EVENT_UPDATE)
		return UINT32_MAX;

	SlipCrossEffectState *const state = SlipObject_CrossEffectState(object);
	SlipFrameTimerValues timer = SlipFrameTimer_Values();
	state->remainingLifetime = (uint16_t)(state->remainingLifetime - (uint16_t)timer.deltaMilliseconds);
	if ((int16_t)state->remainingLifetime < 0) {
		SlipObject_FreeImmediate(object, timer.deltaMilliseconds, timer.stepQ14, timer.frameRateHz, eventFlags,
		                         (uintptr_t)state, dispatchFrame);
		return timer.deltaMilliseconds;
	}

	const int32_t updateStepProduct = (int32_t)(int16_t)timer.stepQ14 * state->updateStepMultiplier;
	uint32_t colorPayloadBits = (eventFlags & 0xffff0000u) | ((uint32_t)updateStepProduct >> 16);

	const size_t objectBytes = (size_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE;
	SlipObjectSlotDataReadResult current;
	SlipObjectSlotDataWriteResult write;
	(void)SlipObject_GetDrawData(SlipObject_table, objectBytes, object, &current);
	(void)SlipObject_SetDrawData(SlipObject_table, objectBytes, object, current.drawData, &write);
	(void)SlipCrossEffects_Color(SlipObject_table, objectBytes, object, &colorPayloadBits);
	return state->update(object);
}

bool SlipCrossEffects_Select(const SlipObject *objects, size_t objectBytes, uint16_t *selected) {
	uint32_t greatestLifetime = 0;
	*selected = UINT16_MAX;
	for (uint16_t object = SlipObject_Next(UINT16_MAX); object != UINT16_MAX; object = SlipObject_Next(object)) {
		SlipObjectDrawCallbackReadResult draw;
		if (!SlipObject_GetSlotDrawCallback(objects, objectBytes, object, &draw))
			return false;
		if (draw.slotDrawCallback != TrackView_DrawCrossEffect && draw.slotDrawCallback != TrackView_QueueCrossEffect)
			continue;
		const SlipCrossEffectState *const state = SlipObject_CrossEffectState(object);
		if (state->remainingLifetime >= greatestLifetime) {
			*selected = object;
			greatestLifetime = state->remainingLifetime;
		}
	}
	return *selected != UINT16_MAX;
}

bool SlipCrossEffects_Reclaim(const SlipObject *objects, size_t objectBytes, uint32_t eventCode, uint32_t eventPayload,
                              uint32_t eventValue, uint32_t eventFlags, uintptr_t dispatchData,
                              uint32_t dispatchFrame) {
	uint16_t selected;
	if (!SlipCrossEffects_Select(objects, objectBytes, &selected))
		return false;
	SlipObject_FreeImmediate(selected, eventCode, eventPayload, eventValue, eventFlags, dispatchData, dispatchFrame);
	return true;
}

void SlipCrossEffects_Create(SlipView3DVec32 position, uint32_t count, const SlipCrossEffectEmission *emission,
                             uint16_t material, SlipCrossEffectUpdate update, const SlipView3DMaths *maths,
                             const uint8_t *materialTable, size_t materialBytes) {
	static SlipView3DVec32 savedPosition;
	static SlipCrossEffectEmission savedEmission;
	static uint16_t savedMaterial;
	static SlipCrossEffectUpdate savedUpdate;
	static SlipView3DMatrix directionMatrix;
	static const SlipView3DMatrix identity = {{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}};
	savedPosition = position;
	savedEmission = *emission;
	savedMaterial = material;
	savedUpdate = update;
	do {
		SlipObjectSlotFill fill;
		const SlipObjectDrawCallback draw =
		    SlipCrossEffects_drawMode != 0 ? TrackView_QueueCrossEffect : TrackView_DrawCrossEffect;
		if (!SlipObject_SlotFill(&identity, (uint32_t)savedPosition.x, (uint32_t)savedPosition.y,
		                         (uint32_t)savedPosition.z, draw, UINT32_MAX, SlipCrossEffects_Event, &fill) ||
		    fill.carryOut)
			return;
		const uint16_t object = (uint16_t)fill.objectOffset;
		if (SlipCrossEffects_attach != NULL) {
			SlipCrossEffects_attach(object);
			if (!SlipObject_IsLive(object))
				continue;
		}
		SlipCrossEffectState *const state = SlipObject_CrossEffectState(object);
		const uint16_t random = (uint16_t)SlipRandom_Next();
		state->remainingLifetime = (uint16_t)((((uint32_t)(random >> 2) * 1500u) >> 16) + 1500u);
		const uint32_t radiusFactor = ((uint16_t)SlipRandom_Next() & 0x1fffu) + 0x2000u;
		const uint32_t radius = (uint32_t)(((uint64_t)radiusFactor * savedEmission.radius) >> 14);
		const size_t objectBytes = (size_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE;
		SlipObjectExtentWriteResult radiusWrite;
		(void)SlipObject_SetDrawExtent(SlipObject_table, objectBytes, object, radius, &radiusWrite);
		state->update = savedUpdate;
		const uint32_t updateStepFactor = (SlipRandom_Next() & 0x1fffu) + 0x2000u;
		state->updateStepMultiplier = (int16_t)updateStepFactor;
		SlipObjectSlotDataReadResult current;
		SlipObjectSlotDataWriteResult slotWrite;
		(void)SlipObject_GetDrawData(SlipObject_table, objectBytes, object, &current);
		(void)SlipObject_SetDrawData(SlipObject_table, objectBytes, object,
		                             (current.drawData & 0xffff0000u) | (uint16_t)(updateStepFactor ^ 0x3456u),
		                             &slotWrite);
		(void)SlipView3D_BuildMatrixFromVector(&directionMatrix, savedEmission.direction.x, savedEmission.direction.y,
		                                       savedEmission.direction.z);
		int32_t rotationJitterProduct = (int32_t)(int16_t)SlipRandom_Next() * 0xa00;
		SlipView3D_ApplyRow0Row2Rotation(maths, (int16_t)((uint32_t)rotationJitterProduct >> 16), &directionMatrix);
		rotationJitterProduct = (int32_t)(int16_t)SlipRandom_Next() * 0xa00;
		SlipView3D_ApplyPitchMatrix(maths, (int16_t)((uint32_t)rotationJitterProduct >> 16), &directionMatrix);
		SlipObject_SetDirectionQ14(SlipObject_table, object, (uint16_t)directionMatrix.m[6],
		                           (uint16_t)directionMatrix.m[7], (uint16_t)directionMatrix.m[8]);
		SlipObject_SetSpeed(SlipObject_table, object, savedEmission.speed);
		uint32_t firstColor, lastColor;
		if (!SlipDraw3D_GetMaterialValues(materialTable, materialBytes, savedMaterial, &firstColor, &lastColor))
			SlipRuntime_Fatal("Cross effect material lookup failed");
		const uint16_t halfRange = (uint16_t)((uint16_t)(lastColor - firstColor) >> 1);
		SlipCrossEffects_paletteStart = (uint16_t)(lastColor - halfRange);
		SlipCrossEffects_paletteCount = halfRange;
		uint32_t colorPayloadBits = savedMaterial;
		(void)SlipCrossEffects_Color(SlipObject_table, objectBytes, object, &colorPayloadBits);
	} while (--count != 0);
}
