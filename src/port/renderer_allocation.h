#ifndef SLIPSTREAM5000_RENDERER_ALLOCATION_H
#define SLIPSTREAM5000_RENDERER_ALLOCATION_H
#include "renderer_lifecycle.h"

typedef struct SlipRendererAllocationCalls {
	void *context;
	bool (*allocate)(void *, uint32_t bytes, uint32_t flags, uint16_t *resource);
	SlipDraw3DVertexRecord *(*lockVertices)(void *, uint16_t resource);
	uint16_t *(*lockSpecular)(void *, uint16_t resource);
	void (*unlock)(void *, uint16_t resource);
	void (*release)(void *, uint16_t resource);
	SlipRendererPolygon *(*lockPolygons)(void *, uint16_t resource);
} SlipRendererAllocationCalls;

void SlipRenderer_AllocateVertices(SlipRendererState *, uint32_t capacity, const SlipRendererAllocationCalls *);
void SlipRenderer_GrowVertices(SlipRendererState *, uint32_t capacity, const SlipRendererAllocationCalls *);
void SlipRenderer_AllocateSpecular(SlipRendererState *, const SlipRendererAllocationCalls *);
void SlipRenderer_FreeSpecular(SlipRendererState *, const SlipRendererAllocationCalls *);
extern const SlipRendererAllocationCalls SlipRendererHost_allocationCalls;
bool SlipRenderer_AllocatePolygons(SlipRendererState *, const SlipRendererAllocationCalls *);
#endif
