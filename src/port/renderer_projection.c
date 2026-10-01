#include "renderer_projection.h"
#include "renderer_state.h"

void SlipRenderer_SetFlags(SlipRendererState *state, uint16_t flags) { state->projection.renderFlags = flags; }

void SlipRenderer_SetShapeFlags(SlipRendererState *state, uint16_t flags) {
	if (state->projection.auxiliaryClipPlaneEnabled == 0)
		flags &= (uint16_t)~SLIP_SHAPE_CLIP_AUXILIARY;
	state->shapeFlags = flags;
}

void SlipRenderer_DefaultProjection(SlipRendererState *state) {
	SlipRenderer_SelectState(state, 0);
	SlipDraw3D_SetProjectionMode(&state->projection, 0);
	SlipRenderer_SetFlags(state, 0);
	SlipRenderer_SetShapeFlags(state, 0);
	SlipDraw3D_SetProjectionScale(&state->projection, 0x10000);
	SlipDraw3D_SetProjectionScaleFactor(&state->projection, 0x100);
	state->projection.modeOneScale = 1;
	SlipDraw3D_SetViewport(&state->projection, 0, 0, 319, 199, 160, 100);
	state->projection.minZ = 0x40;
	state->projection.maxZ = INT32_MAX;
}
