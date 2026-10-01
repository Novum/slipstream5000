#ifndef SLIPSTREAM5000_TIMED_VALUES_H
#define SLIPSTREAM5000_TIMED_VALUES_H
#include <stdbool.h>
#include <stdint.h>

enum { SLIP_TIMED_VALUE_PRESERVE_OUTPUT = 1u, SLIP_TIMED_VALUE_RECORD_BYTES = 24 };

enum {
	SLIP_TIMED_VALUE_OUTPUT_IDENTITY = 0,
	SLIP_TIMED_VALUE_FRACTION = 4,
	SLIP_TIMED_VALUE_TARGET = 8,
	SLIP_TIMED_VALUE_TICKS = 12,
	SLIP_TIMED_VALUE_STEP_WHOLE = 16,
	SLIP_TIMED_VALUE_STEP_FRACTION = 20
};

typedef struct SlipTimedValues {
	uint16_t resource;
	uint32_t count;
	uint8_t *records;
	int32_t **outputs; /* Host binding for each original output pointer. */
} SlipTimedValues;

typedef struct SlipTimedValueTimerCalls {
	void *context;
	bool (*registerTimer)(void *, uint32_t rate);
	void (*removeTimer)(void *);
} SlipTimedValueTimerCalls;

bool SlipTimedValues_Initialize(SlipTimedValues *, int32_t **outputs, uint32_t rate, uint32_t count,
                                const SlipTimedValueTimerCalls *);
void SlipTimedValues_Shutdown(SlipTimedValues *, const SlipTimedValueTimerCalls *);
void SlipTimedValues_Start(SlipTimedValues *, uint32_t outputIdentity, int32_t *output, int32_t from, int32_t to,
                           uint32_t ticks, uint32_t mode);
void SlipTimedValues_Tick(SlipTimedValues *);
bool SlipTimedValues_Active(const SlipTimedValues *, uint32_t outputIdentity);
#endif
