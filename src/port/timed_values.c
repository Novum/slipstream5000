#include "timed_values.h"
#include "byte_order.h"
#include "resource_host.h"
#include "runtime.h"
#define SLIP_TIMED_VALUE_FRACTION_SCALE UINT64_C(0x100000000)

void SlipTimedValues_Shutdown(SlipTimedValues *state, const SlipTimedValueTimerCalls *timer) {
	if (state->count != 0) {
		timer->removeTimer(timer->context);
		state->count = 0;
		SlipResourceHost_Unlock(NULL, state->resource);
		SlipResourceHost_Release(NULL, state->resource);
	}
}

bool SlipTimedValues_Initialize(SlipTimedValues *state, int32_t **outputs, uint32_t rate, uint32_t count,
                                const SlipTimedValueTimerCalls *timer) {
	SlipTimedValues_Shutdown(state, timer);
	state->count = count;
	if (!SlipResourceHost_Allocate(NULL, count * SLIP_TIMED_VALUE_RECORD_BYTES, 0, &state->resource)) {
		state->count = 0;
		return false;
	}
	state->records = SlipResourceHost_LockWritable(NULL, state->resource);
	state->outputs = outputs;
	for (uint32_t i = 0; i < count; ++i)
		SlipBytes_WriteLE32(state->records + i * SLIP_TIMED_VALUE_RECORD_BYTES + SLIP_TIMED_VALUE_OUTPUT_IDENTITY, 0);
	if (timer->registerTimer(timer->context, rate))
		return true;
	SlipResourceHost_Unlock(NULL, state->resource);
	SlipResourceHost_Release(NULL, state->resource);
	state->count = 0;
	return false;
}

void SlipTimedValues_Start(SlipTimedValues *state, uint32_t identity, int32_t *output, int32_t from, int32_t to,
                           uint32_t ticks, uint32_t mode) {
	uint32_t index;
	for (index = 0; index < state->count; ++index)
		if (SlipBytes_ReadLE32(state->records + index * SLIP_TIMED_VALUE_RECORD_BYTES +
		                       SLIP_TIMED_VALUE_OUTPUT_IDENTITY) == identity)
			break;
	bool existing = index != state->count;
	if (!existing) {
		for (index = 0; index < state->count; ++index)
			if (SlipBytes_ReadLE32(state->records + index * SLIP_TIMED_VALUE_RECORD_BYTES +
			                       SLIP_TIMED_VALUE_OUTPUT_IDENTITY) == 0)
				break;
		if (index == state->count)
			SlipRuntime_Fatal("EventStart: Too many events.");
	}
	uint8_t *const record = state->records + index * SLIP_TIMED_VALUE_RECORD_BYTES;
	if (existing)
		SlipBytes_WriteLE32(record + SLIP_TIMED_VALUE_OUTPUT_IDENTITY, 0);
	if ((mode & SLIP_TIMED_VALUE_PRESERVE_OUTPUT) == 0)
		*output = from;
	SlipBytes_WriteLE32(record + SLIP_TIMED_VALUE_FRACTION, 0);
	SlipBytes_WriteLE32(record + SLIP_TIMED_VALUE_TARGET, (uint32_t)to);
	SlipBytes_WriteLE32(record + SLIP_TIMED_VALUE_TICKS, ticks);
	const int32_t difference = (int32_t)((uint32_t)to - (uint32_t)from);

	const uint32_t magnitude = difference < 0 ? 0u - (uint32_t)difference : (uint32_t)difference;
	const uint32_t denominator = (int32_t)ticks < 0 ? 0u - ticks : ticks;
	uint64_t step = (uint64_t)magnitude * SLIP_TIMED_VALUE_FRACTION_SCALE / denominator;
	if ((difference < 0) != ((int32_t)ticks < 0))
		step = 0u - step;
	SlipBytes_WriteLE32(record + SLIP_TIMED_VALUE_STEP_WHOLE, (uint32_t)((uint64_t)step >> 32));
	SlipBytes_WriteLE32(record + SLIP_TIMED_VALUE_STEP_FRACTION, (uint32_t)step);
	state->outputs[index] = output;
	SlipBytes_WriteLE32(record + SLIP_TIMED_VALUE_OUTPUT_IDENTITY, identity);
}

void SlipTimedValues_Tick(SlipTimedValues *state) {
	for (uint32_t i = 0; i < state->count; ++i) {
		uint8_t *const record = state->records + i * SLIP_TIMED_VALUE_RECORD_BYTES;
		if (SlipBytes_ReadLE32(record + SLIP_TIMED_VALUE_OUTPUT_IDENTITY) != 0) {
			int32_t *const output = state->outputs[i];
			const uint32_t target = SlipBytes_ReadLE32(record + SLIP_TIMED_VALUE_TARGET);
			const uint32_t before = target - (uint32_t)*output;
			const uint32_t fraction = SlipBytes_ReadLE32(record + SLIP_TIMED_VALUE_FRACTION);
			const uint32_t nextFraction = fraction + SlipBytes_ReadLE32(record + SLIP_TIMED_VALUE_STEP_FRACTION);
			SlipBytes_WriteLE32(record + SLIP_TIMED_VALUE_FRACTION, nextFraction);
			*output = (int32_t)((uint32_t)*output + SlipBytes_ReadLE32(record + SLIP_TIMED_VALUE_STEP_WHOLE) +
			                    (nextFraction < fraction));
			const uint32_t after = target - (uint32_t)*output;
			if (after == 0) {
				SlipBytes_WriteLE32(record + SLIP_TIMED_VALUE_OUTPUT_IDENTITY, 0);
			} else if ((int32_t)(after ^ before) < 0) {
				*output = (int32_t)target;
				SlipBytes_WriteLE32(record + SLIP_TIMED_VALUE_OUTPUT_IDENTITY, 0);
			} else {
				const uint32_t remaining = SlipBytes_ReadLE32(record + SLIP_TIMED_VALUE_TICKS) - 1;
				SlipBytes_WriteLE32(record + SLIP_TIMED_VALUE_TICKS, remaining);
				if (remaining == 0) {
					*output = (int32_t)target;
					SlipBytes_WriteLE32(record + SLIP_TIMED_VALUE_OUTPUT_IDENTITY, 0);
				}
			}
		}
	}
}

bool SlipTimedValues_Active(const SlipTimedValues *state, uint32_t identity) {
	for (uint32_t i = 0; i < state->count; ++i)
		if (SlipBytes_ReadLE32(state->records + i * SLIP_TIMED_VALUE_RECORD_BYTES + SLIP_TIMED_VALUE_OUTPUT_IDENTITY) ==
		    identity)
			return true;
	return false;
}
