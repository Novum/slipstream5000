#include "renderer_culling.h"
#include "renderer_host.h"
#include "renderer_lighting.h"

static void SlipRendererHost_ProjectPrimary(SlipDraw3DVec32 world, int32_t *x, int32_t *y, void *context) {
	(void)context;
	SlipDraw3DProjectState *const projection = &SlipRendererHost_state.projection;
	projection->projectPrimary(world, x, y, projection);
}

static void SlipRendererHost_ProjectSecondary(SlipDraw3DVec32 world, int32_t *x, int32_t *y, void *context) {
	(void)context;
	SlipDraw3DProjectState *const projection = &SlipRendererHost_state.projection;
	projection->projectSecondary(world, x, y, projection);
}

static uint32_t SlipRendererHost_ProjectVertex(void *context, SlipDraw3DVertexRecord *vertex) {
	SlipRendererState *const state = &SlipRendererHost_state;

	SlipDraw3DProjectState projection = state->projection;
	projection.renderFlags = state->shapeFlags;
	projection.depthOrigin = projection.auxiliaryClipPlaneOrigin;
	projection.depthNormal = projection.auxiliaryClipPlaneNormal;
	return SlipDraw3D_ProjectVertex(vertex, &projection, state->activeTransform, SlipRendererHost_ProjectPrimary,
	                                SlipRendererHost_ProjectSecondary, context);
}

static RasterPoint SlipRendererHost_ProjectUnclipped(void *context, SlipDraw3DVertexRecord *vertex) {
	SlipRendererState *const state = &SlipRendererHost_state;
	return SlipDraw3D_ProjectUnclippedVertex(vertex, &state->projection, state->activeTransform,
	                                         SlipRendererHost_ProjectPrimary, context);
}

static uint32_t SlipRendererHost_ShadeVertex(void *context, SlipDraw3DMaterialRecord *material,
                                             SlipDraw3DVertexRecord *vertex, int16_t x, int16_t y, int16_t z) {
	return SlipRenderer_VertexColour(&SlipRendererHost_state, material, vertex, x, y, z, context);
}

static bool SlipRendererHost_ClipDepth(void *context, uint32_t all, uint32_t *any) {
	(void)context;
	return SlipRenderer_ClipDepth(&SlipRendererHost_state, all, any, &SlipRendererHost_depthClipCalls);
}

static bool SlipRendererHost_ClipScreen(void *context, uint32_t any) {
	(void)context;
	return SlipRenderer_ClipScreen(&SlipRendererHost_state, any, &SlipRendererHost_screenClipCalls);
}

static uint32_t SlipRendererHost_PolygonDepth(void *context, uint16_t countAndFlags, const uint8_t *indices) {
	return (uint32_t)SlipRenderer_PolygonDepth(&SlipRendererHost_state, countAndFlags, indices, context);
}

static uint32_t SlipRendererHost_DepthBlend(void *context, uint32_t depth) {
	(void)context;
	SlipDraw3DLightDepthBlend result;
	(void)SlipDraw3D_LightDepthBlend(depth, SlipDraw3D_fadeStart, SlipDraw3D_fadeEnd, SlipDraw3D_fadeRange, &result);
	return result.fadeBlendQ14;
}

static uint32_t SlipRendererHost_LightingMaterial(void *context, SlipDraw3DMaterialRecord *material, uint32_t blend,
                                                  uint16_t diffuse, uint16_t specular) {
	(void)context;
	SlipDraw3DLightingMaterial result;
	(void)SlipDraw3D_LightingMaterial(material, sizeof(*material), blend, diffuse, specular, SlipDraw3D_directLight,
	                                  SlipDraw3D_ambientLight, SlipDraw3D_fadeColour, SlipDraw3D_fadeStart, &result);
	return result.shade;
}

static SlipRendererPolygonColour SlipRendererHost_PolygonColour(void *context, SlipDraw3DMaterialRecord *material,
                                                                int16_t x, int16_t y, uint32_t count,
                                                                uint32_t countAndFlags, const uint8_t *indices) {
	SlipRendererState *const state = &SlipRendererHost_state;
	SlipDraw3DVertexLighting lighting = {.light = state->currentState->light,
	                                     .direct = SlipDraw3D_directLight,
	                                     .fadeStart = SlipDraw3D_fadeStart,
	                                     .overrideRamp = state->overrideRamp,
	                                     .rampStart = state->rampStart,
	                                     .rampEnd = state->rampEnd};
	SlipRendererFlatColourCalls calls = {context, SlipRendererHost_PolygonDepth, SlipRendererHost_DepthBlend,
	                                     SlipRendererHost_LightingMaterial};
	return SlipRenderer_FlatColour(material, x, y, (int16_t)count, countAndFlags, indices, &lighting, &calls);
}

static bool SlipRendererHost_BuildSolid(void *context, uint32_t countAndFlags, int16_t x, int16_t y, int16_t z,
                                        uint16_t material, const uint8_t *indices) {
	(void)z;
	SlipRendererSolidBuilderCalls calls = {.ring = {context, SlipRendererHost_ProjectVertex, SlipRendererHost_ClipDepth,
	                                                SlipRendererHost_ClipScreen, SlipRendererHost_ShadeVertex},
	                                       .colour = SlipRendererHost_PolygonColour};
	return SlipRenderer_BuildSolidPolygon(&SlipRendererHost_state, countAndFlags, x, y, material, indices, &calls);
}

static void SlipRendererHost_DrawPolygon(void *context) {
	(void)context;
	SlipRenderer_DrawPolygon(&SlipRendererHost_state, &SlipRendererHost_rasterCalls);
}

static void SlipRendererHost_DrawUnclipped(void *context, uint16_t countAndFlags, int16_t x, int16_t y, int16_t z,
                                           uint16_t material, const uint8_t *indices) {
	(void)z;
	SlipRendererUnclippedCalls calls = {context, SlipRendererHost_ProjectUnclipped, SlipRendererHost_ShadeVertex,
	                                    SlipRendererHost_PolygonColour, SlipRendererHost_rasterCalls};
	SlipRenderer_DrawUnclipped(&SlipRendererHost_state, countAndFlags, x, y, material, indices, &calls);
}

static void SlipRendererHost_SelectTextureRaster(void *context, int16_t x, int16_t y, int16_t z) {
	(void)context;
	SlipRenderer_SelectTextureRaster(&SlipRendererHost_state, x, y, z);
}

static bool SlipRendererHost_RejectPolygon(void *context, uint16_t countAndFlags, const uint8_t *indices) {
	return SlipRenderer_ClassifyPolygon(&SlipRendererHost_state, countAndFlags, indices, context) < 0;
}

static bool SlipRendererHost_BuildTextured(void *context, uint32_t countAndFlags, int16_t x, int16_t y, int16_t z,
                                           uint32_t texture, const uint8_t *indices) {
	(void)x;
	(void)y;
	(void)z;
	SlipRendererTexturedRingCalls calls = {.ring = {context, SlipRendererHost_ProjectVertex, SlipRendererHost_ClipDepth,
	                                                SlipRendererHost_ClipScreen, SlipRendererHost_ShadeVertex},
	                                       .reject = SlipRendererHost_RejectPolygon};
	return SlipRenderer_BuildTexturedRing(&SlipRendererHost_state, (uint16_t)countAndFlags, texture, indices, &calls);
}

bool SlipRendererHost_SubmitSolid(void *context, uint16_t countAndFlags, int16_t x, int16_t y, int16_t z,
                                  uint16_t material, const uint8_t *indices) {
	SlipRendererSubmitCalls calls = {context, SlipRendererHost_BuildSolid, SlipRendererHost_DrawPolygon,
	                                 SlipRendererHost_DrawUnclipped};
	return SlipRenderer_SubmitSolid(&SlipRendererHost_state, countAndFlags, x, y, z, material, indices, &calls);
}

bool SlipRendererHost_SubmitTextured(void *context, uint16_t countAndFlags, int16_t x, int16_t y, int16_t z,
                                     uint16_t material, const uint8_t *indices) {
	SlipRendererTexturedSubmitCalls calls = {context, SlipRendererHost_SelectTextureRaster,
	                                         SlipRendererHost_BuildTextured, SlipRendererHost_BuildSolid,
	                                         SlipRendererHost_DrawPolygon};
	return SlipRenderer_SubmitTextured(&SlipRendererHost_state, countAndFlags, x, y, z, material, indices, &calls);
}
