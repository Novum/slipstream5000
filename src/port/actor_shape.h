#ifndef SLIPSTREAM5000_ACTOR_SHAPE_H
#define SLIPSTREAM5000_ACTOR_SHAPE_H
#include "actor_render.h"
#include "shape_sort.h"

typedef struct SlipActorShapeState SlipActorShapeState;
typedef struct SlipActorShapeCalls SlipActorShapeCalls;
typedef void (*SlipActorShapePrimitive)(void *, const uint8_t *primitive, uint32_t traversalValue);
typedef void (*SlipActorShapeSortNode)(SlipActorShapeState *, uint32_t index, uint16_t type, int32_t classification,
                                       const uint8_t *node, const SlipActorShapeCalls *);

struct SlipActorShapeState {
	SlipActorRenderState *actor;
	SlipView3DVec32 viewPosition;
	SlipView3DVec32 worldPosition;
	SlipActorShapePrimitive primitive;
	uint8_t *shape;
	uint32_t sourceShift, transformShift;
	SlipView3DMatrix drawMatrix;
};

typedef enum SlipActorShapeBounds {
	SLIP_ACTOR_SHAPE_OUTSIDE = -1,
	SLIP_ACTOR_SHAPE_INSIDE = 0,    /* SF clear, ZF set */
	SLIP_ACTOR_SHAPE_INTERSECTS = 1 /* SF clear, ZF clear */
} SlipActorShapeBounds;

struct SlipActorShapeCalls {
	void *context;
	void (*setup)(void *, SlipView3DVec32 viewPosition, SlipView3DVec32 worldPosition);
	void (*matrices)(void *, const SlipView3DMatrix *world, const SlipView3DMatrix *draw);
	uint8_t *(*lock)(void *, uint16_t resource);
	void (*prepare)(void *, uint8_t *shape);
	uint16_t (*getShapeFlags)(void *);
	void (*setShapeFlags)(void *, uint16_t flags);
	uint32_t (*classify)(void *, SlipView3DVec32 center, int32_t radius);
	void (*bounds)(void *, SlipView3DVec32 minimum, SlipView3DVec32 maximum);
	SlipActorShapeBounds (*projectBounds)(void *, SlipView3DVec32 center, SlipView3DMatrix *);
	void (*vertices)(void *, uint8_t *shape);
	void (*traverse)(void *, const uint8_t *sort, SlipActorShapeSortNode, SlipActorShapeState *,
	                 const SlipActorShapeCalls *);
	void (*unsorted)(void *, uint8_t *shape);
	void (*unlock)(void *, uint16_t resource);
	void (*restoreVertexCursor)(void *);
	uint32_t (*getRendererStateIndex)(void *);
	void (*selectRendererStateIndex)(void *, uint32_t stateIndex);
};

void SlipActorShape_DrawPart(SlipActorShapeState *, SlipActorPartRecord *, const SlipActorShapeCalls *);
/* Standalone SHP entry shares the original shape/vertex globals with ART parts. */
void SlipShape_Draw(SlipActorShapeState *, uint16_t resource, const SlipView3DMatrix *world,
                    const SlipView3DMatrix *draw, const SlipActorShapeCalls *);
void SlipShape_SortNode(SlipActorShapeState *, uint32_t index, uint16_t type, int32_t classification,
                        const uint8_t *node, const SlipActorShapeCalls *);
void SlipActorShape_SortNode(SlipActorShapeState *, uint32_t index, uint16_t type, int32_t classification,
                             const uint8_t *node, const SlipActorShapeCalls *);

void SlipActorShape_Traverse(SlipShapeSortState *, const uint8_t *, SlipActorShapeSortNode, SlipActorShapeState *,
                             const SlipActorShapeCalls *, const SlipShapeSortCalls *);
#endif
