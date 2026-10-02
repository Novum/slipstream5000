#include <metal_stdlib>
using namespace metal;

struct VertexIn {
	float4 position [[attribute(0)]];
	float2 uv [[attribute(1)]];
	float4 color [[attribute(2)]];
};

struct FragmentIn {
	float4 position [[position]];
	float2 uv;
	float4 color;
};

vertex FragmentIn main0(VertexIn input [[stage_in]]) { return {input.position, input.uv, input.color}; }
