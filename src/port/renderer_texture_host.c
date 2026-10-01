#include "raster_affine.h"
#include "raster_perspective.h"
#include "renderer_host.h"
#include "resource_host.h"

static SlipSprite textureSprite;
static SlipResourcePayload texturePayload;
static const uint8_t *textureRows[UINT16_MAX + 1u];
static RasterAffineDrawState affineState;
static RasterOpaqueAffineState opaqueAffineState;
static RasterPerspectiveDrawState perspectiveState;
static RasterOpaquePerspectiveState opaquePerspectiveState;

static const SlipSprite *SlipRendererHost_LockTexture(void *context, uint16_t resource) {
	SlipResourceHost_Lock(context, resource);
	texturePayload = SlipResourceHost_Payload(resource);
	(void)SlipSprite_FromPayload(&texturePayload, &textureSprite);
	return &textureSprite;
}

static void SlipRendererHost_BuildTextureRows(void *context, const SlipSprite *texture) {
	(void)context;
	RasterTextureRowTable result;
	(void)Raster_BuildTextureRowTable(texture->data, texturePayload.size, Raster_textureRowScroll, textureRows,
	                                  UINT16_MAX + 1u, &result);
}

static void SlipRendererHost_DrawAffineSpan(void *context, const RasterAffineDrawState *state, int32_t scanline) {
	(void)context;
	/* Keep the complete transparency word from the serialized texture header. */
	const uint16_t transparent = (uint16_t)((uint16_t)state->texture->data[8] | (uint16_t)state->texture->data[9] << 8);
	RasterTexturedSpanCoreState span = {.screenRow = g_screenRowPtrs[scanline],
	                                    .screenRowBytes = (size_t)g_screenPitch,
	                                    .spanLeftX = state->left.x,
	                                    .spanRightX = state->right.x,
	                                    .startTexU = state->left.u,
	                                    .startTexV = state->left.v,
	                                    .endTexU = state->right.u,
	                                    .endTexV = state->right.v,
	                                    .transparentWord = transparent,
	                                    .textureRows = textureRows,
	                                    .textureRowCount = state->texture->height,
	                                    .textureRowBytes = state->texture->width};
	RasterTexturedSpanCore result;
	(void)Raster_DrawTexturedSpanCore(&span, &result);
}

static const RasterAffineDrawCalls affineCalls = {.lock = SlipRendererHost_LockTexture,
                                                  .textureRows = SlipRendererHost_BuildTextureRows,
                                                  .span = SlipRendererHost_DrawAffineSpan,
                                                  .unlock = SlipResourceHost_Unlock};

void SlipRendererHost_DrawAffine(void *context, RasterTexturedPoint *points, uint32_t count, uint32_t texture) {
	(void)context;
	Raster_DrawAffine(&affineState, (uint16_t)texture, points, count, &affineCalls);
}

void SlipRendererHost_DrawOpaqueAffine(void *context, RasterTexturedPoint *points, uint32_t count, uint32_t texture) {
	(void)context;
	Raster_DrawOpaqueAffine(&affineState, &opaqueAffineState, (uint16_t)texture, points, count, g_screenRowPtrs,
	                        &affineCalls);
}

static void SlipRendererHost_DrawPerspectiveSpan(void *context, const RasterPerspectiveDrawState *state,
                                                 int32_t scanline) {
	(void)context;
	const uint16_t transparent = (uint16_t)((uint16_t)state->texture->data[8] | (uint16_t)state->texture->data[9] << 8);
	RasterTexturedSpanDispatchState span = {.screenRow = g_screenRowPtrs[scanline],
	                                        .screenRowBytes = (size_t)g_screenPitch,
	                                        .scanlineIndex = (uint32_t)scanline,
	                                        .spanLeftX = state->left.x,
	                                        .spanRightX = state->right.x,
	                                        .startTexU = state->leftU,
	                                        .startTexV = state->leftV,
	                                        .endTexU = state->rightU,
	                                        .endTexV = state->rightV,
	                                        .startDepth = state->leftDepth,
	                                        .endDepth = state->rightDepth,
	                                        .transparentWord = transparent,
	                                        .textureRows = textureRows,
	                                        .textureRowCount = state->texture->height,
	                                        .textureRowBytes = state->texture->width};
	RasterTexturedSpanDispatch result;
	(void)Raster_DispatchTexturedSpan(&span, &result);
}

static const RasterPerspectiveDrawCalls perspectiveCalls = {.lock = SlipRendererHost_LockTexture,
                                                            .textureRows = SlipRendererHost_BuildTextureRows,
                                                            .span = SlipRendererHost_DrawPerspectiveSpan,
                                                            .unlock = SlipResourceHost_Unlock};

void SlipRendererHost_DrawPerspective(void *context, RasterTexturedPoint *points, uint32_t count, uint32_t texture) {
	(void)context;
	Raster_DrawPerspective(&perspectiveState, (uint16_t)texture, points, count, &perspectiveCalls);
}

void SlipRendererHost_DrawOpaquePerspective(void *context, RasterTexturedPoint *points, uint32_t count,
                                            uint32_t texture) {
	(void)context;
	Raster_DrawOpaquePerspective(&perspectiveState, &opaquePerspectiveState, (uint16_t)texture, points, count,
	                             g_screenRowPtrs, &perspectiveCalls);
}

const SlipRendererRasterCalls SlipRendererHost_rasterCalls = {.line = SlipRendererHost_DrawLine,
                                                              .rectangle = SlipRendererHost_DrawRectangle,
                                                              .flat = SlipRendererHost_DrawFlat,
                                                              .dithered = SlipRendererHost_DrawDithered,
                                                              .shaded = SlipRendererHost_DrawShaded,
                                                              .opaqueAffine = SlipRendererHost_DrawOpaqueAffine,
                                                              .maskedAffine = SlipRendererHost_DrawAffine,
                                                              .opaquePerspective =
                                                                  SlipRendererHost_DrawOpaquePerspective,
                                                              .maskedPerspective = SlipRendererHost_DrawPerspective};
