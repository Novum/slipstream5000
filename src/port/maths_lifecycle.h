#ifndef SLIPSTREAM5000_MATHS_LIFECYCLE_H
#define SLIPSTREAM5000_MATHS_LIFECYCLE_H
#include "view3d.h"

typedef struct SlipMathsState {
	uint32_t initialized;
	uint16_t resourceHandle;
	SlipView3DMaths tables;
} SlipMathsState;

struct SlipMathsCalls;
typedef void (*SlipMathsCleanup)(SlipMathsState *, const struct SlipMathsCalls *);

typedef struct SlipMathsCalls {
	void *context;
	bool (*find)(void *, const char *, uint16_t *);
	SlipView3DMaths (*lock)(void *, uint16_t);
	void (*registerExit)(void *, SlipMathsCleanup);
	void (*floatingPoint)(void *, uint32_t enabled);
	void (*unlock)(void *, uint16_t);
	void (*release)(void *, uint16_t);
} SlipMathsCalls;

uint32_t SlipMaths_Initialize(SlipMathsState *, const SlipMathsCalls *);
void SlipMaths_Close(SlipMathsState *, const SlipMathsCalls *);

typedef enum SlipX87Control {
	SLIP_X87_INITIAL_CONTROL = 0x037f, /* FNINIT: masked exceptions, extended precision, nearest rounding. */
	SLIP_X87_ROUND_TOWARD_ZERO = 0x0c00,
	SLIP_X87_PRECISION_MASK = 0x0300,
	SLIP_X87_EXTENDED_PRECISION = 0x0300
} SlipX87Control;

typedef struct SlipMathsFloatingPointState {
	uint32_t enabled;
	uint16_t controlWord;
} SlipMathsFloatingPointState;

typedef struct SlipMathsFloatingPointCalls {
	void *context;
	bool (*present)(void *);
	void (*initialize)(void *);
	uint16_t (*readControl)(void *);
	void (*writeControl)(void *, uint16_t);
} SlipMathsFloatingPointCalls;

void SlipMaths_SetFloatingPoint(SlipMathsFloatingPointState *, uint32_t enabled, const SlipMathsFloatingPointCalls *);
#endif
