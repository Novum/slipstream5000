#ifndef SLIPSTREAM5000_SHAPE_PRIMITIVES_H
#define SLIPSTREAM5000_SHAPE_PRIMITIVES_H
#include "actor_shape.h"

typedef struct SlipShapePrimitiveCalls {
	void *context;
	bool (*planeVisible)(void *, int16_t, int16_t, int16_t, uint16_t);
} SlipShapePrimitiveCalls;

void SlipShape_DrawUnsorted(SlipActorShapeState *, const uint8_t *, const SlipShapePrimitiveCalls *);
#endif
