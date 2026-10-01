#include "shape_host.h"
#include "renderer_bounds.h"
#include "renderer_culling.h"
#include "renderer_host.h"
#include "renderer_projection.h"
#include "renderer_state.h"
#include "renderer_vertices.h"
#include "resource_host.h"
#include "shape_dispatch.h"
#include "shape_prepare.h"
#include "shape_primitives.h"
#include "shape_vertices.h"

static void SlipShapeHost_DispatchPrimitive(void *, const uint8_t *, uint32_t);

SlipActorShapeState SlipShapeHost_state = {.primitive = SlipShapeHost_DispatchPrimitive};
static SlipShapeSortState sortState;

static int32_t SlipShapeHost_PolygonDepth(void *context, uint16_t countAndFlags, const uint8_t *indices) {
	return SlipRenderer_PolygonDepth(&SlipRendererHost_state, countAndFlags, indices, context);
}

static uint32_t SlipShapeHost_DrawFlags(void *context) {
	(void)context;
	return SlipRendererHost_state.projection.renderFlags;
}

static void SlipShapeHost_SetDrawFlags(void *context, uint16_t flags) {
	(void)context;
	SlipRenderer_SetFlags(&SlipRendererHost_state, flags);
}

static void SlipShapeHost_Solid(void *context, uint16_t countAndFlags, int16_t x, int16_t y, int16_t z,
                                uint16_t material, const uint8_t *stream) {
	(void)SlipRendererHost_SubmitSolid(context, countAndFlags, x, y, z, material, stream);
}

static void SlipShapeHost_Textured(void *context, uint16_t countAndFlags, int16_t x, int16_t y, int16_t z,
                                   uint16_t material, const uint8_t *stream) {
	(void)SlipRendererHost_SubmitTextured(context, countAndFlags, x, y, z, material, stream);
}

static void SlipShapeHost_DispatchPrimitive(void *context, const uint8_t *primitive, uint32_t traversalValue) {
	SlipShapeDispatchCalls calls = {context,
	                                SlipShapeHost_PolygonDepth,
	                                SlipShapeHost_DrawFlags,
	                                SlipShapeHost_SetDrawFlags,
	                                SlipShapeHost_Solid,
	                                SlipShapeHost_Textured};
	SlipShape_DispatchPrimitive(primitive, traversalValue, &calls);
}

static void SlipShapeHost_Origin(void *context, const SlipView3DMatrix *draw, const SlipView3DMatrix *world,
                                 SlipView3DVec32 position) {
	(void)context;
	SlipRenderer_SetOrigin(&SlipRendererHost_state, draw, world, position);
}

static void SlipShapeHost_BuildVertices(void *context, const uint8_t *vertices, uint16_t count, int16_t stride,
                                        SlipDraw3DTransformFn transform, SlipDraw3DSourcePointFn source) {
	(void)context;
	SlipRenderer_BuildVertices(&SlipRendererHost_state, vertices, count, stride, transform, source,
	                           &SlipRendererHost_allocationCalls);
}

static const SlipShapeVertexCalls vertexCalls = {
    .context = &SlipShapeHost_state, .setOrigin = SlipShapeHost_Origin, .buildVertices = SlipShapeHost_BuildVertices};

static void SlipShapeHost_Setup(void *context, SlipView3DVec32 view, SlipView3DVec32 world) {
	SlipShapeVertices_Setup(context, view, world);
}

static void SlipShapeHost_Matrices(void *context, const SlipView3DMatrix *world, const SlipView3DMatrix *draw) {
	SlipShapeVertices_Matrices(context, world, draw, &vertexCalls);
}

static void SlipShapeHost_Prepare(void *context, uint8_t *shape) {
	SlipShape_Prepare(context, shape, &SlipShapeHost_prepareCalls);
}

static uint16_t SlipShapeHost_ShapeFlags(void *context) {
	(void)context;
	return SlipRendererHost_state.shapeFlags;
}

static void SlipShapeHost_SetShapeFlags(void *context, uint16_t flags) {
	(void)context;
	SlipRenderer_SetShapeFlags(&SlipRendererHost_state, flags);
}

static uint32_t SlipShapeHost_Classify(void *context, SlipView3DVec32 position, int32_t radius) {
	(void)context;
	return SlipDraw3D_ClassifyShapeBounds((SlipDraw3DVec32){position.x, position.y, position.z}, radius,
	                                      &SlipRendererHost_state.projection);
}

static void SlipShapeHost_Bounds(void *context, SlipView3DVec32 minimum, SlipView3DVec32 maximum) {
	(void)context;
	SlipRenderer_SetBounds(&SlipRendererHost_state, minimum, maximum);
}

static SlipActorShapeBounds SlipShapeHost_ProjectBounds(void *context, SlipView3DVec32 position,
                                                        SlipView3DMatrix *matrix) {
	(void)context;
	return SlipRenderer_ProjectBounds(&SlipRendererHost_state, position, matrix, &SlipRendererHost_state.projection);
}

static void SlipShapeHost_Vertices(void *context, uint8_t *shape) {
	SlipShapeVertices_Initialize(context, shape, &vertexCalls);
}

static bool SlipShapeHost_Visible(void *context, int16_t x, int16_t y, int16_t z, uint16_t vertex) {
	return SlipRenderer_PlaneVisible(&SlipRendererHost_state, x, y, z, vertex, context);
}

static bool SlipShapeHost_ClassifySortPlane(void *context, int16_t x, int16_t y, int16_t z, uint16_t vertex) {
	return !SlipRenderer_PlaneVisible(&SlipRendererHost_state, x, y, z, vertex, context);
}

static void SlipShapeHost_Traverse(void *context, const uint8_t *sort, SlipActorShapeSortNode callback,
                                   SlipActorShapeState *shape, const SlipActorShapeCalls *calls) {
	SlipShapeSortCalls sortCalls = {context, SlipShapeHost_ClassifySortPlane};
	SlipActorShape_Traverse(&sortState, sort, callback, shape, calls, &sortCalls);
}

static void SlipShapeHost_Unsorted(void *context, uint8_t *shape) {
	SlipShapePrimitiveCalls calls = {context, SlipShapeHost_Visible};
	SlipShape_DrawUnsorted(context, shape, &calls);
}

static void SlipShapeHost_RestoreVertices(void *context) {
	(void)context;
	SlipRenderer_RestoreVertices(&SlipRendererHost_state);
}

static uint32_t SlipShapeHost_ClipLevel(void *context) {
	(void)context;
	return SlipRenderer_GetState(&SlipRendererHost_state);
}

static void SlipShapeHost_SetClipLevel(void *context, uint32_t level) {
	(void)context;
	SlipRenderer_SelectState(&SlipRendererHost_state, (uint16_t)level);
}

const SlipActorShapeCalls SlipShapeHost_drawCalls = {.context = &SlipShapeHost_state,
                                                     .setup = SlipShapeHost_Setup,
                                                     .matrices = SlipShapeHost_Matrices,
                                                     .lock = SlipResourceHost_LockWritable,
                                                     .prepare = SlipShapeHost_Prepare,
                                                     .getShapeFlags = SlipShapeHost_ShapeFlags,
                                                     .setShapeFlags = SlipShapeHost_SetShapeFlags,
                                                     .classify = SlipShapeHost_Classify,
                                                     .bounds = SlipShapeHost_Bounds,
                                                     .projectBounds = SlipShapeHost_ProjectBounds,
                                                     .vertices = SlipShapeHost_Vertices,
                                                     .traverse = SlipShapeHost_Traverse,
                                                     .unsorted = SlipShapeHost_Unsorted,
                                                     .unlock = SlipResourceHost_Unlock,
                                                     .restoreVertexCursor = SlipShapeHost_RestoreVertices,
                                                     .getRendererStateIndex = SlipShapeHost_ClipLevel,
                                                     .selectRendererStateIndex = SlipShapeHost_SetClipLevel};
