#ifndef SLIPSTREAM5000_SHAPE_PREPARE_H
#define SLIPSTREAM5000_SHAPE_PREPARE_H
#include "actor_shape.h"

typedef struct SlipShapePrepareCalls {
	void *context;
	bool (*findMaterialByName)(void *, const char *, uint16_t *);
} SlipShapePrepareCalls;

uint8_t *SlipDraw3D_NextPrimitive(uint8_t *);
uint8_t *SlipShape_NextPrimitive(uint8_t *);
bool SlipShape_MaterialName(uint8_t *, uint16_t, char **);
void SlipShape_Prepare(SlipActorShapeState *, uint8_t *, const SlipShapePrepareCalls *);
extern const SlipShapePrepareCalls SlipShapeHost_prepareCalls;
#endif
