#include <metal_stdlib>
using namespace metal;

struct FragmentIn {
	float4 position [[position]];
	float2 uv;
	float4 color;
};

fragment float4 main0(FragmentIn input [[stage_in]], texture2d<float> image [[texture(0)]],
                      sampler imageSampler [[sampler(0)]], texture2d<float> palette [[texture(1)]],
                      sampler paletteSampler [[sampler(1)]]) {
	if (input.color.a > 1.5) {
		uint noise = uint(input.position.x) * 1973u + uint(input.position.y) * 9277u;
		noise = (noise ^ (noise >> 8)) * 26699u;
		uint index = (uint(input.color.r) + (noise & uint(input.color.g))) & 255u;
		return palette.sample(paletteSampler, float2((index + 0.5) / 256.0, 0.5), level(0));
	}
	float4 color = image.sample(imageSampler, input.uv) * input.color;
	if (color.a < 0.5)
		discard_fragment();
	return color;
}
