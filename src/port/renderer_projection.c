#include "renderer_projection.h"
#include "raster/raster.h"
#include "renderer_state.h"

void SlipRenderer_SetFlags(SlipRendererState *state, uint16_t flags) { state->projection.renderFlags = flags; }

void SlipRenderer_SetShapeFlags(SlipRendererState *state, uint16_t flags) {
	if (state->projection.auxiliaryClipPlaneEnabled == 0)
		flags &= (uint16_t)~SLIP_SHAPE_CLIP_AUXILIARY;
	state->shapeFlags = flags;
}

void SlipRenderer_DefaultProjection(SlipRendererState *state) {
	SlipRenderer_SelectState(state, 0);
	SlipDraw3D_SetProjectionMode(&state->projection, SLIP_DRAW3D_PROJECTION_PERSPECTIVE);
	SlipRenderer_SetFlags(state, 0);
	SlipRenderer_SetShapeFlags(state, 0);
	SlipDraw3D_SetProjectionScale(&state->projection, SLIP_DRAW3D_SCALE_ONE_Q16);
	SlipDraw3D_SetProjectionScaleFactor(&state->projection, SLIP_DRAW3D_DEFAULT_FOCAL_LENGTH);
	state->projection.modeOneScale = 1;
	SlipDraw3D_SetViewport(&state->projection, 0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1,
	                       SLIPSTREAM_SCREEN_WIDTH / 2, SLIPSTREAM_SCREEN_HEIGHT / 2);
	state->projection.minZ = SLIP_DRAW3D_DEFAULT_NEAR_DEPTH;
	state->projection.maxZ = INT32_MAX;
}
