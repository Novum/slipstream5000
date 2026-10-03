#version 450

// Integer hash parameters keep dithering stable at each screen pixel.
const uint DITHER_HASH_X_MULTIPLIER = 1973u;
const uint DITHER_HASH_Y_MULTIPLIER = 9277u;
const uint DITHER_HASH_MIX_MULTIPLIER = 26699u;
const int DITHER_HASH_MIX_SHIFT = 8;
const uint PALETTE_INDEX_MASK = 255u;
const float PALETTE_COLOUR_COUNT = 256.0;
// Alpha above this threshold selects indexed flat shading.
const float INDEXED_SHADE_ALPHA_THRESHOLD = 1.5;
const float ALPHA_TEST_THRESHOLD = 0.5;

layout(set = 2, binding = 0) uniform sampler2D image;
layout(set = 2, binding = 1) uniform sampler2D palette;
layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 outputColor;

void main() {
	if (color.a > INDEXED_SHADE_ALPHA_THRESHOLD) {
		uint noise = uint(gl_FragCoord.x) * DITHER_HASH_X_MULTIPLIER + uint(gl_FragCoord.y) * DITHER_HASH_Y_MULTIPLIER;
		noise = (noise ^ (noise >> DITHER_HASH_MIX_SHIFT)) * DITHER_HASH_MIX_MULTIPLIER;
		uint index = (uint(color.r) + (noise & uint(color.g))) & PALETTE_INDEX_MASK;
		outputColor = textureLod(palette, vec2((index + 0.5) / PALETTE_COLOUR_COUNT, 0.5), 0);
	} else {
		outputColor = texture(image, uv) * color;
		if (outputColor.a < ALPHA_TEST_THRESHOLD)
			discard;
	}
}
