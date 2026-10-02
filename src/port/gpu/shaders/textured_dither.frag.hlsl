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
	if (input.color.a > 1.5) {
		uint noise = uint(input.position.x) * 1973u + uint(input.position.y) * 9277u;
		noise = (noise ^ (noise >> 8)) * 26699u;
		uint index = (uint(input.color.r) + (noise & uint(input.color.g))) & 255u;
		return palette.SampleLevel(paletteSampler, float2((index + 0.5) / 256.0, 0.5), 0);
	}
	float4 color = image.Sample(imageSampler, input.uv) * input.color;
	if (color.a < 0.5)
		discard;
	return color;
}
