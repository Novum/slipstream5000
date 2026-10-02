struct VertexIn {
	float4 position : TEXCOORD0;
	float2 uv : TEXCOORD1;
	float4 color : TEXCOORD2;
};

struct VertexOut {
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
	float4 color : TEXCOORD1;
};

VertexOut main(VertexIn input) {
	VertexOut o;
	o.position = input.position;
	o.uv = input.uv;
	o.color = input.color;
	return o;
}
