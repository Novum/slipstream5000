#include <metal_stdlib>
using namespace metal;

// Integer hash parameters keep dithering stable at each screen pixel.
constant uint DITHER_HASH_X_MULTIPLIER = 1973u;
constant uint DITHER_HASH_Y_MULTIPLIER = 9277u;
constant uint DITHER_HASH_MIX_MULTIPLIER = 26699u;
constant int DITHER_HASH_MIX_SHIFT = 8;
constant uint PALETTE_INDEX_MASK = 255u;
constant float PALETTE_COLOUR_COUNT = 256.0;
// Alpha above this threshold selects indexed flat shading.
constant float INDEXED_SHADE_ALPHA_THRESHOLD = 1.5;
constant float ALPHA_TEST_THRESHOLD = 0.5;

struct FragmentIn {
	float4 position [[position]];
	float2 uv;
	float4 color;
};

fragment float4 main0(FragmentIn input [[stage_in]], texture2d<float> image [[texture(0)]],
                      sampler imageSampler [[sampler(0)]], texture2d<float> palette [[texture(1)]],
                      sampler paletteSampler [[sampler(1)]]) {
	if (input.color.a > INDEXED_SHADE_ALPHA_THRESHOLD) {
		uint noise =
		    uint(input.position.x) * DITHER_HASH_X_MULTIPLIER + uint(input.position.y) * DITHER_HASH_Y_MULTIPLIER;
		noise = (noise ^ (noise >> DITHER_HASH_MIX_SHIFT)) * DITHER_HASH_MIX_MULTIPLIER;
		uint index = (uint(input.color.r) + (noise & uint(input.color.g))) & PALETTE_INDEX_MASK;
		return palette.sample(paletteSampler, float2((index + 0.5) / PALETTE_COLOUR_COUNT, 0.5), level(0));
	}
	float4 color = image.sample(imageSampler, input.uv) * input.color;
	if (color.a < ALPHA_TEST_THRESHOLD)
		discard_fragment();
	return color;
}
