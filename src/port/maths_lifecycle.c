#include "maths_lifecycle.h"

uint32_t SlipMaths_Initialize(SlipMathsState *state, const SlipMathsCalls *calls) {
	if (state->initialized == 0) {
		state->initialized = UINT32_MAX;
		uint16_t resource;
		if (!calls->find(calls->context, "MATHS.BIN", &resource))
			return UINT32_MAX;
		state->resourceHandle = resource;

		state->tables = calls->lock(calls->context, resource);
		calls->registerExit(calls->context, SlipMaths_Close);
		calls->floatingPoint(calls->context, UINT32_MAX);
	}
	return 0;
}

void SlipMaths_Close(SlipMathsState *state, const SlipMathsCalls *calls) {
	if (state->initialized != 0) {
		state->initialized = 0;
		calls->unlock(calls->context, state->resourceHandle);
		calls->release(calls->context, state->resourceHandle);
	}
}

void SlipMaths_SetFloatingPoint(SlipMathsFloatingPointState *state, uint32_t enabled,
                                const SlipMathsFloatingPointCalls *calls) {
	state->enabled = 0;
	if (enabled == 0)
		return;
	if (!calls->present(calls->context))
		return;
	calls->initialize(calls->context);
	state->controlWord = calls->readControl(calls->context);
	uint16_t control = state->controlWord;
	control |= SLIP_X87_ROUND_TOWARD_ZERO;
	control &= (uint16_t)~SLIP_X87_PRECISION_MASK;
	control |= SLIP_X87_EXTENDED_PRECISION;
	state->controlWord = control;
	calls->writeControl(calls->context, control);
	state->enabled = UINT32_MAX;
}
