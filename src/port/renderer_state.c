#include "renderer_state.h"

void SlipRenderer_SetCamera(SlipRendererState *state, SlipView3DVec32 position, const SlipView3DMatrix *matrix) {
	state->camera = position;
	state->cameraLight =
	    SlipView3D_TransformVector(matrix, (SlipView3DVec32){SlipDraw3D_lightX, SlipDraw3D_lightY, SlipDraw3D_lightZ});
}

void SlipRenderer_SelectState(SlipRendererState *state, uint16_t index) {
	state->stateIndex = index;
	SlipRendererDrawState *const selected = state->states + index;
	state->currentState = selected;
	state->activeVertices = selected->vertices;
	state->activeTransform = selected->transform;
	state->activeSource = selected->source;
	state->origin = selected->origin;
}

uint32_t SlipRenderer_GetState(const SlipRendererState *state) { return state->stateIndex; }

void SlipRenderer_ResetState(SlipRendererState *state) { SlipRenderer_SelectState(state, 0); }

void SlipRenderer_RestoreVertices(SlipRendererState *state) {
	state->vertexCursor -= state->vertexCursor - state->currentState->vertices;
}

void SlipRenderer_SetOrigin(SlipRendererState *state, const SlipView3DMatrix *draw, const SlipView3DMatrix *world,
                            SlipView3DVec32 position) {
	SlipRendererDrawState *const selected = state->currentState;
	selected->matrix = *draw;
	if (world != NULL) {
		SlipView3DVec32 delta = {(int32_t)((uint32_t)state->camera.x - (uint32_t)position.x),
		                         (int32_t)((uint32_t)state->camera.y - (uint32_t)position.y),
		                         (int32_t)((uint32_t)state->camera.z - (uint32_t)position.z)};
		SlipView3DVec32 origin = SlipView3D_TransformPositionByRows(world, delta);
		state->origin = (SlipDraw3DVec32){origin.x, origin.y, origin.z};
		selected->origin = state->origin;
		if (SlipDraw3D_directLight != 0) {
			SlipView3DVec32 light = SlipView3D_TransformVector(
			    world, (SlipView3DVec32){SlipDraw3D_lightX, SlipDraw3D_lightY, SlipDraw3D_lightZ});
			selected->light = (SlipDraw3DVec32){light.x, light.y, light.z};
		}
	} else {
		state->origin = (SlipDraw3DVec32){(int32_t)((uint32_t)state->camera.x - (uint32_t)position.x),
		                                  (int32_t)((uint32_t)state->camera.y - (uint32_t)position.y),
		                                  (int32_t)((uint32_t)state->camera.z - (uint32_t)position.z)};
		selected->origin = state->origin;
		if (SlipDraw3D_directLight != 0)
			selected->light = (SlipDraw3DVec32){SlipDraw3D_lightX, SlipDraw3D_lightY, SlipDraw3D_lightZ};
	}
}

void SlipRenderer_AdvanceBackground(SlipRendererState *state) {
	state->backgroundColour = (uint16_t)(state->backgroundColour + 1u);
}
