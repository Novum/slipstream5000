// Integer hash parameters keep dithering stable at each screen pixel.
static const uint DITHER_HASH_X_MULTIPLIER = 1973u;
static const uint DITHER_HASH_Y_MULTIPLIER = 9277u;
static const uint DITHER_HASH_MIX_MULTIPLIER = 26699u;
static const int DITHER_HASH_MIX_SHIFT = 8;
static const uint PALETTE_INDEX_MASK = 255u;
static const float PALETTE_COLOUR_COUNT = 256.0;
// Alpha above this threshold selects indexed flat shading.
static const float INDEXED_SHADE_ALPHA_THRESHOLD = 1.5;
static const float ALPHA_TEST_THRESHOLD = 0.5;

Texture2D<float4> image : register(t0, space2);
SamplerState imageSampler : register(s0, space2);
Texture2D<float4> palette : register(t1, space2);
SamplerState paletteSampler : register(s1, space2);

struct FragmentIn {
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
	float4 color : TEXCOORD1;
};

float4 main(FragmentIn input) : SV_Target0 {
	if (input.color.a > INDEXED_SHADE_ALPHA_THRESHOLD) {
		uint noise =
		    uint(input.position.x) * DITHER_HASH_X_MULTIPLIER + uint(input.position.y) * DITHER_HASH_Y_MULTIPLIER;
		noise = (noise ^ (noise >> DITHER_HASH_MIX_SHIFT)) * DITHER_HASH_MIX_MULTIPLIER;
		uint index = (uint(input.color.r) + (noise & uint(input.color.g))) & PALETTE_INDEX_MASK;
		return palette.SampleLevel(paletteSampler, float2((index + 0.5) / PALETTE_COLOUR_COUNT, 0.5), 0);
	}
	float4 color = image.Sample(imageSampler, input.uv) * input.color;
	if (color.a < ALPHA_TEST_THRESHOLD)
		discard;
	return color;
}
