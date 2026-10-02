#version 450
layout(set = 2, binding = 0) uniform sampler2D image;
layout(set = 2, binding = 1) uniform sampler2D palette;
layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 outputColor;

void main() {
	if (color.a > 1.5) {
		uint noise = uint(gl_FragCoord.x) * 1973u + uint(gl_FragCoord.y) * 9277u;
		noise = (noise ^ (noise >> 8)) * 26699u;
		uint index = (uint(color.r) + (noise & uint(color.g))) & 255u;
		outputColor = textureLod(palette, vec2((index + 0.5) / 256.0, 0.5), 0);
	} else {
		outputColor = texture(image, uv) * color;
		if (outputColor.a < 0.5)
			discard;
	}
}
