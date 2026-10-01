#include "renderer_lighting.h"

uint32_t SlipRenderer_VertexColour(SlipRendererState *state, const SlipDraw3DMaterialRecord *material,
                                   SlipDraw3DVertexRecord *vertex, int16_t normalX, int16_t normalY, int16_t normalZ,
                                   void *sourceContext) {
	uint16_t specular = 0;
	if ((state->projection.renderFlags & SLIP_RENDER_DISABLE_SPECULAR) == 0 && material->specularCoefficient != 0) {
		SlipView3DVec32 source = state->activeSource(vertex->sourceX, vertex->sourceY, vertex->sourceZ, sourceContext);
		SlipDraw3DVec32 relative = {(int32_t)((uint32_t)source.x - (uint32_t)state->origin.x),
		                            (int32_t)((uint32_t)source.y - (uint32_t)state->origin.y),
		                            (int32_t)((uint32_t)source.z - (uint32_t)state->origin.z)};
		specular = SlipRenderer_Specular(state, relative, normalX, normalY, normalZ);
	}
	uint16_t diffuse = 0;
	if (SlipDraw3D_directLight != 0) {
		SlipDraw3DVec32 light = state->currentState->light;
		const int64_t dot = (int64_t)normalX * (int16_t)light.x + (int64_t)normalY * (int16_t)light.y +
		                    (int64_t)normalZ * (int16_t)light.z;
		const int32_t projected = -(int32_t)(int16_t)((uint64_t)dot >> SLIP_NORMAL_FRACTION_BITS);
		if (projected >= 0)
			diffuse = (uint16_t)(((uint32_t)(uint16_t)projected * (uint16_t)SlipDraw3D_directLight) >>
			                     SLIP_NORMAL_FRACTION_BITS);
	}
	if (SlipDraw3D_fadeStart != 0) {
		if ((vertex->flags & SLIP_VERTEX_DEPTH_BLEND_CACHED) == 0) {
			vertex->flags |= SLIP_VERTEX_DEPTH_BLEND_CACHED;
			SlipDraw3DLightDepthBlend blend;
			SlipDraw3D_LightDepthBlend((uint32_t)vertex->world.z, SlipDraw3D_fadeStart, SlipDraw3D_fadeEnd,
			                           SlipDraw3D_fadeRange, &blend);
			vertex->depthFadeBlend = blend.fadeBlendQ14;
		}
	} else {
		vertex->depthFadeBlend = 0;
		vertex->flags |= SLIP_VERTEX_DEPTH_BLEND_CACHED;
	}
	SlipDraw3DLightingMaterial lighting;
	SlipDraw3D_LightingMaterial(material, sizeof(*material), vertex->depthFadeBlend, diffuse, specular,
	                            SlipDraw3D_directLight, SlipDraw3D_ambientLight, SlipDraw3D_fadeColour,
	                            SlipDraw3D_fadeStart, &lighting);
	uint32_t start, end;
	if (state->overrideRamp == 0) {
		start = material->rampStart;
		end = material->rampEnd;
	} else {
		start = state->rampStart;
		end = state->rampEnd;
	}
	const uint32_t difference = end - start;
	const uint32_t product = (uint32_t)(uint16_t)difference * (uint16_t)lighting.shade;
	return ((difference & ~(uint32_t)UINT16_MAX) | (uint16_t)(product >> SLIP_NORMAL_FRACTION_BITS)) + start;
}

uint16_t SlipRenderer_Specular(const SlipRendererState *state, SlipDraw3DVec32 relative, int16_t normalX,
                               int16_t normalY, int16_t normalZ) {
	if (SlipDraw3D_directLight == 0)
		return 0;
	const uint16_t invertedX = (uint16_t)(0u - (uint16_t)normalX);
	const uint16_t invertedY = (uint16_t)(0u - (uint16_t)normalY);
	const uint16_t invertedZ = (uint16_t)(0u - (uint16_t)normalZ);
	SlipDraw3DVec32 light = state->currentState->light;
	const int32_t x = (int32_t)((uint32_t)relative.x + (uint32_t)light.x);
	const int32_t y = (int32_t)((uint32_t)relative.y + (uint32_t)light.y);
	const int32_t z = (int32_t)((uint32_t)relative.z + (uint32_t)light.z);
	SlipView3DDotProduct32By16 dot;
	SlipView3D_DotProduct32By16((uint32_t)x, (uint32_t)y, (uint32_t)z, invertedX, invertedY, invertedZ, &dot);
	const uint16_t high = (uint16_t)dot.dotProductHigh;
	if ((int16_t)high < 0)
		return 0;
	const uint32_t length = SlipView3D_ApproximateLength(x, y, z);
	uint32_t ratio;
	if (high >= length) {
		ratio = SLIP_LIGHT_UNIT;
	} else {
		ratio = (uint32_t)(((uint64_t)high << 32 | dot.dotProductLow) / length);
		if (ratio > SLIP_LIGHT_UNIT)
			ratio = SLIP_LIGHT_UNIT;
	}
	uint32_t index = ratio - state->specularThreshold;
	if ((int32_t)index < 0)
		return 0;
	index &= SLIP_SPECULAR_WORD_INDEX_MASK;
	const uint32_t product = (uint32_t)state->specularTable[index] * (uint16_t)SlipDraw3D_directLight;
	return (uint16_t)(product >> SLIP_NORMAL_FRACTION_BITS);
}
