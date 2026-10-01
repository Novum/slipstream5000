#include "renderer_host.h"
#include "material_host.h"
#include "renderer_allocation.h"
#include "renderer_interpolation.h"
#include "renderer_projection.h"
#include "renderer_state.h"
#include "resource_host.h"
#include "runtime.h"

SlipRendererState SlipRendererHost_state = {.materials = &SlipMaterialHost_residency};
static SlipRendererCleanup exitCallback;

static void SlipRendererHost_Vertices(void *context, uint32_t capacity) {
	SlipRenderer_AllocateVertices(context, capacity, &SlipRendererHost_allocationCalls);
}

static bool SlipRendererHost_Polygons(void *context) {
	return SlipRenderer_AllocatePolygons(context, &SlipRendererHost_allocationCalls);
}

static void SlipRendererHost_Specular(void *context) {
	SlipRenderer_AllocateSpecular(context, &SlipRendererHost_allocationCalls);
	const SlipRendererState *const state = context;
	SlipDraw3D_BindSpecularTable(state->specularTable, state->specularThreshold);
}

static void SlipRendererHost_Projection(void *context) {
	SlipRendererState *const state = context;
	SlipResourcePayload points = SlipResourceHost_Payload(state->pointResource);
	SlipDraw3D_BindPointBuffer(points.data);
	SlipRenderer_DefaultProjection(context);
}

static void SlipRendererHost_Background(void *context) { SlipRenderer_AdvanceBackground(context); }

static void SlipRendererHost_ResetState(void *context) { SlipRenderer_ResetState(context); }

static void SlipRendererHost_Lighting(void *context) {
	(void)context;
	SlipDraw3D_ResetLighting();
}

static void SlipRendererHost_ShutdownRenderer(void) {
	exitCallback(&SlipRendererHost_state, &SlipRendererHost_lifecycleCalls);
}

static void SlipRendererHost_RegisterExit(void *context, SlipRendererCleanup callback) {
	(void)context;
	exitCallback = callback;
	SlipRuntime_RegisterExit(SlipRendererHost_ShutdownRenderer);
}

static void SlipRendererHost_FreeSpecular(void *context) {
	SlipDraw3D_BindPointBuffer(NULL);
	SlipDraw3D_BindSpecularTable(NULL, 0);
	SlipRenderer_FreeSpecular(context, &SlipRendererHost_allocationCalls);
}

static void SlipRendererHost_FreeMaterials(void *context) {
	SlipRendererState *const state = context;
	SlipMaterial_Shutdown(state->materials, &SlipMaterialHost_residencyCalls);
}

const SlipRendererLifecycleCalls SlipRendererHost_lifecycleCalls = {.context = &SlipRendererHost_state,
                                                                    .initializeVertices = SlipRendererHost_Vertices,
                                                                    .initializePolygons = SlipRendererHost_Polygons,
                                                                    .initializeSpecular = SlipRendererHost_Specular,
                                                                    .allocate = SlipResourceHost_Allocate,
                                                                    .lockStates = SlipResourceHost_LockDrawStates,
                                                                    .lockReserved = SlipResourceHost_LockReserved,
                                                                    .lockPoints = SlipResourceHost_LockPoints,
                                                                    .initializeProjection = SlipRendererHost_Projection,
                                                                    .advanceBackground = SlipRendererHost_Background,
                                                                    .resetDrawState = SlipRendererHost_ResetState,
                                                                    .resetLighting = SlipRendererHost_Lighting,
                                                                    .registerExit = SlipRendererHost_RegisterExit,
                                                                    .freeSpecular = SlipRendererHost_FreeSpecular,
                                                                    .unlock = SlipResourceHost_Unlock,
                                                                    .release = SlipResourceHost_Release,
                                                                    .freeMaterials = SlipRendererHost_FreeMaterials};

static uint32_t SlipRendererHost_ProjectLinePoint(void *context, SlipRendererPolygonPoint *point,
                                                  SlipDraw3DVec32 world) {
	return SlipRenderer_ProjectPoint(context, point, world);
}

static bool SlipRendererHost_ClipLine(void *context, uint32_t allClipMask, uint32_t anyClipMask) {
	(void)allClipMask;
	return SlipRenderer_ClipLine(context, anyClipMask);
}

const SlipRendererLineCalls SlipRendererHost_lineCalls = {&SlipRendererHost_state, SlipRendererHost_ProjectLinePoint,
                                                          SlipRendererHost_ClipLine};

static bool SlipRendererHost_PrepareClipEdges(void *context, uint32_t mask, SlipRendererClipEdges *edges) {
	return SlipRenderer_PrepareClipEdges(context, mask, edges);
}

static void SlipRendererHost_InterpolateAuxiliary(void *context, SlipRendererPolygon *target,
                                                  SlipRendererPolygon *inside) {
	SlipRenderer_InterpolateAuxiliary(context, target, inside);
}

static void SlipRendererHost_InterpolateDepth(void *context, SlipRendererPolygon *target, SlipRendererPolygon *inside,
                                              int32_t limit) {
	SlipRenderer_InterpolateDepth(context, target, inside, limit);
}

static void SlipRendererHost_InterpolateHorizontal(void *context, SlipRendererPolygon *target,
                                                   SlipRendererPolygon *inside, int32_t limit) {
	SlipRenderer_InterpolateHorizontal(context, target, inside, limit);
}

static void SlipRendererHost_InterpolateVertical(void *context, SlipRendererPolygon *target,
                                                 SlipRendererPolygon *inside, int32_t limit) {
	SlipRenderer_InterpolateVertical(context, target, inside, limit);
}

static bool SlipRendererHost_ClipPostPlanes(void *context) { return SlipRenderer_ClipPostPlanes(context); }

const SlipRendererDepthClipCalls SlipRendererHost_depthClipCalls = {.context = &SlipRendererHost_state,
                                                                    .edges = SlipRendererHost_PrepareClipEdges,
                                                                    .auxiliary = SlipRendererHost_InterpolateAuxiliary,
                                                                    .depth = SlipRendererHost_InterpolateDepth};
const SlipRendererScreenClipCalls SlipRendererHost_screenClipCalls = {.context = &SlipRendererHost_state,
                                                                      .edges = SlipRendererHost_PrepareClipEdges,
                                                                      .horizontal =
                                                                          SlipRendererHost_InterpolateHorizontal,
                                                                      .vertical = SlipRendererHost_InterpolateVertical,
                                                                      .postPlanes = SlipRendererHost_ClipPostPlanes};
