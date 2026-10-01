#include "actor_shape.h"
#include "byte_order.h"
#include "renderer_flags.h"
#include "runtime.h"
#include "shape3d.h"
#include "shape_format.h"

typedef struct SlipActorShapeSortBinding {
	SlipActorShapeSortNode callback;
	SlipActorShapeState *state;
	const SlipActorShapeCalls *calls;
} SlipActorShapeSortBinding;

static void SlipActorShape_ActorSortNode(void *context, uint32_t index, uint16_t type, int32_t classification,
                                         const uint8_t *node) {
	SlipActorShapeSortBinding *const binding = context;
	binding->callback(binding->state, index, type, classification, node, binding->calls);
}

void SlipActorShape_Traverse(SlipShapeSortState *sortState, const uint8_t *sort, SlipActorShapeSortNode callback,
                             SlipActorShapeState *state, const SlipActorShapeCalls *calls,
                             const SlipShapeSortCalls *sortCalls) {
	SlipActorShapeSortBinding binding = {callback, state, calls};
	SlipShapeSort_Run(sortState, sort, SlipActorShape_ActorSortNode, &binding, sortCalls);
}

/* SHP header and sort-node fields are serialized data, not native records. */

void SlipShape_Draw(SlipActorShapeState *state, uint16_t resource, const SlipView3DMatrix *world,
                    const SlipView3DMatrix *draw, const SlipActorShapeCalls *calls) {
	if (SlipShape3D_initialized == 0)
		SlipRuntime_Fatal("ShapesInstall MUST be called befor using ShapeDraw");
	void *const context = calls->context;
	calls->matrices(context, world, draw);
	uint8_t *const shape = calls->lock(context, resource);
	const SlipShape3DHeader *const header = (const SlipShape3DHeader *)shape;
	if ((header->flags & SLIP_SHAPE_MATERIALS_PREPARED) == 0)
		calls->prepare(context, shape);
	const uint16_t savedFlags = calls->getShapeFlags(context);
	const uint32_t flags = calls->classify(context, state->viewPosition, (int32_t)header->radius);
	calls->setShapeFlags(context, (uint16_t)flags);
	calls->bounds(context, (SlipView3DVec32){header->minimumX, header->minimumY, header->minimumZ},
	              (SlipView3DVec32){header->maximumX, header->maximumY, header->maximumZ});
	const SlipActorShapeBounds bounds = calls->projectBounds(context, state->viewPosition, &state->drawMatrix);
	if (bounds == SLIP_ACTOR_SHAPE_OUTSIDE) {
		calls->setShapeFlags(context, savedFlags);
		calls->unlock(context, resource);
		return;
	}
	if (bounds == SLIP_ACTOR_SHAPE_INSIDE)
		calls->setShapeFlags(context, calls->getShapeFlags(context) | SLIP_SHAPE_INSIDE_VIEW);
	calls->vertices(context, shape);
	if (header->sortList == 0) {
		if (header->bspOffset == 0)
			SlipRuntime_Fatal("ShapeDraw - this shape has no sort data. Set NOSORT, or create sort data.");
		state->shape = shape;
		calls->traverse(context, shape + header->bspOffset, SlipShape_SortNode, state, calls);
	} else {
		calls->unsorted(context, shape);
	}
	calls->setShapeFlags(context, savedFlags);
	calls->unlock(context, resource);
	calls->restoreVertexCursor(context);
}

void SlipShape_SortNode(SlipActorShapeState *state, uint32_t index, uint16_t type, int32_t classification,
                        const uint8_t *node, const SlipActorShapeCalls *calls) {
	(void)node;
	if (classification >= 0 && type == 0) {
		const SlipShape3DHeader *const header = (const SlipShape3DHeader *)state->shape;
		state->primitive(calls->context, state->shape + header->primitiveOffset + index, (uint32_t)classification);
	}
}

void SlipActorShape_DrawPart(SlipActorShapeState *state, SlipActorPartRecord *part, const SlipActorShapeCalls *calls) {
	if (!part->hasDrawShape)
		return;
	void *const context = calls->context;
	SlipView3DVec32 world = {
	    (int32_t)((uint32_t)part->worldPosition.x + (uint32_t)state->actor->objectWorldPosition.x),
	    (int32_t)((uint32_t)part->worldPosition.y + (uint32_t)state->actor->objectWorldPosition.y),
	    (int32_t)((uint32_t)part->worldPosition.z + (uint32_t)state->actor->objectWorldPosition.z)};
	calls->setup(context, part->drawPosition, world);
	const uint16_t resource = part->drawShape;
	calls->matrices(context, &part->worldMatrix, &part->drawMatrix);
	uint8_t *const shape = calls->lock(context, resource);
	if ((SlipBytes_ReadLE16(shape + SLIP_SHAPE_FLAGS_OFFSET) & SLIP_SHAPE_MATERIALS_PREPARED) == 0)
		calls->prepare(context, shape);
	const uint16_t savedFlags = calls->getShapeFlags(context);
	const uint32_t flags = calls->classify(context, state->viewPosition, (int32_t)SlipBytes_ReadLE32(shape + 0x1c));
	calls->setShapeFlags(context, (uint16_t)flags);
	SlipView3DVec32 minimum = {(int32_t)SlipBytes_ReadLE32(shape + 0x20), (int32_t)SlipBytes_ReadLE32(shape + 0x28),
	                           (int32_t)SlipBytes_ReadLE32(shape + 0x30)};
	SlipView3DVec32 maximum = {(int32_t)SlipBytes_ReadLE32(shape + 0x24), (int32_t)SlipBytes_ReadLE32(shape + 0x2c),
	                           (int32_t)SlipBytes_ReadLE32(shape + 0x34)};
	calls->bounds(context, minimum, maximum);
	const SlipActorShapeBounds classification = calls->projectBounds(context, state->viewPosition, &state->drawMatrix);
	if (classification == SLIP_ACTOR_SHAPE_OUTSIDE) {
		calls->setShapeFlags(context, savedFlags);
		calls->unlock(context, resource);
		return;
	}
	if (classification == SLIP_ACTOR_SHAPE_INSIDE) {
		const uint16_t currentFlags = calls->getShapeFlags(context);
		calls->setShapeFlags(context, currentFlags | SLIP_SHAPE_INSIDE_VIEW);
	}
	calls->vertices(context, shape);
	if (SlipBytes_ReadLE16(shape + 0x38) == 0) {
		if (SlipBytes_ReadLE32(shape + 0x0c) == 0)
			SlipRuntime_Fatal("ArticSlotDraw - this shape has no sort data. Set NOSORT, or create sort data.");
		state->shape = shape;
		const uint8_t *const sort = shape + SlipBytes_ReadLE32(shape + 0x0c);
		calls->traverse(context, sort, SlipActorShape_SortNode, state, calls);
	} else {
		calls->unsorted(context, shape);
	}
	calls->setShapeFlags(context, savedFlags);
	calls->unlock(context, resource);
	calls->restoreVertexCursor(context);
}

void SlipActorShape_SortNode(SlipActorShapeState *state, uint32_t index, uint16_t type, int32_t classification,
                             const uint8_t *node, const SlipActorShapeCalls *calls) {
	if (type == 1) {
		if (state->actor->childrenInSortTree == 0)
			return;
		if ((SlipBytes_ReadLE32(node) | SlipBytes_ReadLE32(node + 4)) != 0)
			SlipRuntime_Fatal("ArticShapeDrawSortNode - child problem");
		const uint32_t rendererStateIndex = calls->getRendererStateIndex(calls->context);
		calls->selectRendererStateIndex(calls->context, rendererStateIndex + 1u);
		uint8_t *const savedShape = state->shape;
		SlipView3DVec32 savedPosition = state->viewPosition;
		SlipView3DMatrix savedMatrix = state->drawMatrix;
		SlipActorPartRecord *const part = state->actor->actor->parts[index];
		SlipActorShape_DrawPart(state, part, calls);
		state->drawMatrix = savedMatrix;
		state->viewPosition = savedPosition;
		state->shape = savedShape;
		calls->selectRendererStateIndex(calls->context, rendererStateIndex);
	} else if (type == 0 && classification >= 0) {
		const uint8_t *const primitive = state->shape + SlipBytes_ReadLE32(state->shape + 0x14) + index;
		state->primitive(calls->context, primitive, (uint32_t)classification);
	}
}
