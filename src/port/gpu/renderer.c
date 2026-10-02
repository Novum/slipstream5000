#include "renderer.h"
#include "byte_order.h"
#include "draw.h"
#include "port_app_bridge.h"
#include "shaders.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "../../third_party/stb_image_resize2.h"

typedef struct Vertex {
	float position[4], uv[2], color[4];
} Vertex;

typedef struct Draw {
	uint32_t first, count;
	SDL_GPUTexture *texture;
	SDL_Rect clip;
} Draw;

typedef struct Texture {
	uint64_t hash, frame;
	const uint8_t *payload;
	SDL_GPUTexture *texture;
	struct Texture *next;
} Texture;

static SDL_Renderer *renderer;
static SDL_GPUDevice *device;
static SDL_GPUGraphicsPipeline *pipeline;
static SDL_GPUSampler *sampler;
static SDL_GPUTexture *white, *palette;
static SDL_Texture *overlayTexture;
static bool overlayUpdated;
static SDL_Texture *target;
static SDL_GPUBuffer *vertexBuffer;
static SDL_GPUTransferBuffer *vertexTransfer;
static uint32_t bufferBytes;
static bool mapActive;
static size_t mapFirstVertex;
static Vertex *vertices;
static Draw *draws;
static size_t vertexCount, vertexCapacity, drawCount, drawCapacity;
static Texture *textures;
static int width, height, lineWidth;
static bool active, failed;
static uint32_t lastPalette[256];
static uint64_t frame;

static void Fail(void) {
	if (!failed)
		fprintf(stderr, "High Res GPU: %s\n", SDL_GetError());
	failed = true;
}

static SDL_GPUTexture *Upload(const uint8_t *rgba, int w, int h, bool mips) {
	uint32_t levels = 1;
	int mw = w, mh = h;
	if (mips)
		while (mw > 1 || mh > 1) {
			mw = mw > 1 ? mw / 2 : 1;
			mh = mh > 1 ? mh / 2 : 1;
			++levels;
		}
	SDL_GPUTextureCreateInfo info = {.type = SDL_GPU_TEXTURETYPE_2D,
	                                 .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
	                                 .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
	                                 .width = w,
	                                 .height = h,
	                                 .layer_count_or_depth = 1,
	                                 .num_levels = levels};
	SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &info);
	if (!texture) {
		Fail();
		return NULL;
	}
	size_t total = 0;
	mw = w;
	mh = h;
	for (uint32_t l = 0; l < levels; l++) {
		total += (size_t)mw * mh * 4;
		mw = mw > 1 ? mw / 2 : 1;
		mh = mh > 1 ? mh / 2 : 1;
	}
	SDL_GPUTransferBufferCreateInfo ti = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = (uint32_t)total};
	SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &ti);
	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
	uint8_t *data = transfer ? SDL_MapGPUTransferBuffer(device, transfer, false) : NULL;
	if (!data || !cmd) {
		if (data)
			SDL_UnmapGPUTransferBuffer(device, transfer);
		if (cmd)
			SDL_CancelGPUCommandBuffer(cmd);
		if (transfer)
			SDL_ReleaseGPUTransferBuffer(device, transfer);
		SDL_ReleaseGPUTexture(device, texture);
		Fail();
		return NULL;
	}
	memcpy(data, rgba, (size_t)w * h * 4);
	size_t offset = 0;
	mw = w;
	mh = h;
	for (uint32_t l = 1; l < levels; l++) {
		int nw = mw > 1 ? mw / 2 : 1, nh = mh > 1 ? mh / 2 : 1;
		size_t next = offset + (size_t)mw * mh * 4;
		if (!stbir_resize_uint8_linear(data + offset, mw, mh, 0, data + next, nw, nh, 0, STBIR_RGBA)) {
			SDL_UnmapGPUTransferBuffer(device, transfer);
			SDL_CancelGPUCommandBuffer(cmd);
			SDL_ReleaseGPUTransferBuffer(device, transfer);
			SDL_ReleaseGPUTexture(device, texture);
			SDL_SetError("mipmap allocation failed");
			Fail();
			return NULL;
		}
		offset = next;
		mw = nw;
		mh = nh;
	}
	SDL_UnmapGPUTransferBuffer(device, transfer);
	SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
	offset = 0;
	mw = w;
	mh = h;
	for (uint32_t l = 0; l < levels; l++) {
		SDL_GPUTextureTransferInfo src = {
		    .transfer_buffer = transfer, .offset = (uint32_t)offset, .pixels_per_row = mw, .rows_per_layer = mh};
		SDL_GPUTextureRegion dst = {.texture = texture, .mip_level = l, .w = mw, .h = mh, .d = 1};
		SDL_UploadToGPUTexture(copy, &src, &dst, false);
		offset += (size_t)mw * mh * 4;
		mw = mw > 1 ? mw / 2 : 1;
		mh = mh > 1 ? mh / 2 : 1;
	}
	SDL_EndGPUCopyPass(copy);
	if (!SDL_SubmitGPUCommandBuffer(cmd)) {
		SDL_ReleaseGPUTexture(device, texture);
		texture = NULL;
		Fail();
	}
	SDL_ReleaseGPUTransferBuffer(device, transfer);
	return texture;
}

static void ClearTextures(void) {
	while (textures) {
		Texture *next = textures->next;
		SDL_ReleaseGPUTexture(device, textures->texture);
		free(textures);
		textures = next;
	}
}

static SDL_GPUShader *Shader(SDL_GPUShaderStage stage) {
	SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device), format;
	const unsigned char *code;
	size_t bytes;
	const char *entrypoint = "main";
	bool vertex = stage == SDL_GPU_SHADERSTAGE_VERTEX;
	if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
		format = SDL_GPU_SHADERFORMAT_DXIL;
		code = vertex ? passthrough_vert_dxil : textured_dither_frag_dxil;
		bytes = vertex ? sizeof(passthrough_vert_dxil) : sizeof(textured_dither_frag_dxil);
	} else if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
		format = SDL_GPU_SHADERFORMAT_SPIRV;
		code = vertex ? passthrough_vert_spv : textured_dither_frag_spv;
		bytes = vertex ? sizeof(passthrough_vert_spv) : sizeof(textured_dither_frag_spv);
	} else if (formats & SDL_GPU_SHADERFORMAT_MSL) {
		format = SDL_GPU_SHADERFORMAT_MSL;
		code = vertex ? passthrough_vert_metal : textured_dither_frag_metal;
		bytes = vertex ? sizeof(passthrough_vert_metal) : sizeof(textured_dither_frag_metal);
		entrypoint = "main0";
	} else {
		SDL_SetError("No supported race GPU shader format");
		Fail();
		return NULL;
	}
	SDL_GPUShaderCreateInfo info = {.code = code,
	                                .code_size = bytes,
	                                .entrypoint = entrypoint,
	                                .format = format,
	                                .stage = stage,
	                                .num_samplers = vertex ? 0 : 2};
	SDL_GPUShader *shader = SDL_CreateGPUShader(device, &info);
	if (!shader)
		Fail();
	return shader;
}

bool SlipRaceGpu_Initialize(SDL_Renderer *r) {
	failed = false;
	renderer = r;
	device = SDL_GetGPURendererDevice(r);
	if (!device)
		return false;
	SDL_GPUShader *vs = Shader(SDL_GPU_SHADERSTAGE_VERTEX), *fs = Shader(SDL_GPU_SHADERSTAGE_FRAGMENT);
	if (!vs || !fs) {
		if (vs)
			SDL_ReleaseGPUShader(device, vs);
		if (fs)
			SDL_ReleaseGPUShader(device, fs);
		return false;
	}
	SDL_GPUVertexBufferDescription buffer = {
	    .slot = 0, .pitch = sizeof(Vertex), .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX};
	SDL_GPUVertexAttribute attributes[] = {{.location = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 0},
	                                       {.location = 1, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 16},
	                                       {.location = 2, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 24}};
	SDL_GPUColorTargetDescription color = {.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM};
	SDL_GPUGraphicsPipelineCreateInfo info = {
	    .vertex_shader = vs,
	    .fragment_shader = fs,
	    .vertex_input_state = {.vertex_buffer_descriptions = &buffer,
	                           .num_vertex_buffers = 1,
	                           .vertex_attributes = attributes,
	                           .num_vertex_attributes = 3},
	    .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
	    .target_info = {.color_target_descriptions = &color, .num_color_targets = 1}};
	pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
	SDL_ReleaseGPUShader(device, vs);
	SDL_ReleaseGPUShader(device, fs);
	SDL_GPUSamplerCreateInfo si = {.min_filter = SDL_GPU_FILTER_NEAREST,
	                               .mag_filter = SDL_GPU_FILTER_NEAREST,
	                               .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
	                               .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
	                               .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
	                               .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
	                               .max_lod = 32};
	sampler = SDL_CreateGPUSampler(device, &si);
	const uint8_t pixel[] = {255, 255, 255, 255};
	white = Upload(pixel, 1, 1, false);
	if (!pipeline || !sampler || !white) {
		Fail();
		return false;
	}
	return true;
}

bool SlipRaceGpu_Available(void) { return pipeline && sampler && white && !failed; }

bool SlipRaceGpu_Active(void) { return active; }

void SlipRaceGpu_BeginFrame(int w, int h) {
	Raster_SetDrawBackend(NULL);
	vertexCount = drawCount = 0;
	++frame;
	Texture **link = &textures;
	while (*link) {
		Texture *t = *link;
		if (frame - t->frame > 120) {
			*link = t->next;
			SDL_ReleaseGPUTexture(device, t->texture);
			free(t);
		} else
			link = &t->next;
	}
	active = false;
	overlayUpdated = false;
	if (memcmp(lastPalette, g_palette, sizeof(lastPalette))) {
		ClearTextures();
		memcpy(lastPalette, g_palette, sizeof(lastPalette));
		if (palette)
			SDL_ReleaseGPUTexture(device, palette);
		palette = NULL;
	}
	if (!palette) {
		uint8_t rgba[256 * 4];
		for (int i = 0; i < 256; i++) {
			uint32_t c = g_palette[i];
			rgba[i * 4] = (uint8_t)(c >> 16);
			rgba[i * 4 + 1] = (uint8_t)(c >> 8);
			rgba[i * 4 + 2] = (uint8_t)c;
			rgba[i * 4 + 3] = 255;
		}
		palette = Upload(rgba, 256, 1, false);
	}
	if (w != width || h != height) {
		if (target)
			SDL_DestroyTexture(target);
		target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
		if (!target) {
			Fail();
			return;
		}
		SDL_SetTextureScaleMode(target, SDL_SCALEMODE_NEAREST);
		width = w;
		height = h;
	}
}

void SlipRaceGpu_BeginWorld(int thickness) {
	Raster_SetDrawBackend(&RasterGpu_backend);
	active = true;
	lineWidth = thickness;
}

void SlipRaceGpu_EndWorld(void) {
	active = false;
	Raster_SetDrawBackend(NULL);
}

void SlipRaceGpu_BeginMap(void) {
	mapActive = true;
	mapFirstVertex = vertexCount;
	SlipRaceGpu_BeginWorld((height + 100) / 200 > 1 ? (height + 100) / 200 : 1);
}

void SlipRaceGpu_EndMap(void) {
	if (vertexCount > mapFirstVertex) {
		float left = (float)width, top = (float)height;
		for (size_t i = mapFirstVertex; i < vertexCount; ++i) {
			float x = (vertices[i].position[0] + 1) * width / 2;
			float y = (1 - vertices[i].position[1]) * height / 2;
			if (x < left)
				left = x;
			if (y < top)
				top = y;
		}
		float dx = 2 * (height * 8.0f / 200 - left) / width;
		float dy = -2 * (height * 44.0f / 200 - top) / height;
		for (size_t i = mapFirstVertex; i < vertexCount; ++i) {
			vertices[i].position[0] += dx;
			vertices[i].position[1] += dy;
		}
	}
	mapActive = false;
	SlipRaceGpu_EndWorld();
}

static bool Reserve(size_t count) {
	if (vertexCount + count > vertexCapacity) {
		size_t capacity = (vertexCount + count) * 2;
		Vertex *p = realloc(vertices, capacity * sizeof(*p));
		if (!p) {
			SDL_SetError("GPU vertex allocation failed");
			Fail();
			return false;
		}
		vertices = p;
		vertexCapacity = capacity;
	}
	if (drawCount == drawCapacity) {
		size_t capacity = drawCapacity ? drawCapacity * 2 : 1024;
		Draw *p = realloc(draws, capacity * sizeof(*p));
		if (!p) {
			SDL_SetError("GPU draw allocation failed");
			Fail();
			return false;
		}
		draws = p;
		drawCapacity = capacity;
	}
	return true;
}

static Vertex Point(float x, float y, float depth, float u, float v, uint8_t color) {
	if (mapActive) {
		x *= (float)height / 240;
		y *= (float)height / 200;
	}
	float z = depth > 0 ? depth : 1;
	uint32_t c = g_palette[color];
	Vertex p = {{(2 * x / width - 1) * z, (1 - 2 * y / height) * z, 0, z},
	            {u, v},
	            {((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, 1}};
	return p;
}

/* Native world polygons triangulate in source space, before any GPU clipping.
 * Screen-space overlays retain their projected triangulation. */
static float Area(const float a[2], const float b[2], const float c[2]) {
	return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}

static void Polygon(const Vertex *p, uint32_t count, SDL_GPUTexture *texture, const float (*sourceXY)[2]) {
	if (count < 3 || !texture || failed || !Reserve((count - 2) * 3))
		return;
	float xy[128][2], winding = 0;
	uint32_t ring[128], triangles[126 * 3], triangleCount = 0;
	for (uint32_t i = 0; i < count; i++) {
		xy[i][0] = sourceXY ? sourceXY[i][0] : p[i].position[0] / p[i].position[3];
		xy[i][1] = sourceXY ? sourceXY[i][1] : p[i].position[1] / p[i].position[3];
		ring[i] = i;
	}
	for (uint32_t i = 0; i < count; i++) {
		uint32_t j = (i + 1) % count;
		winding += xy[i][0] * xy[j][1] - xy[j][0] * xy[i][1];
	}
	if (!winding)
		return;
	float sign = winding > 0 ? 1.f : -1.f;
	bool convex = true;
	for (uint32_t i = 0; i < count; i++)
		if (Area(xy[i], xy[(i + 1) % count], xy[(i + 2) % count]) * sign < 0) {
			convex = false;
			break;
		}
	if (convex) {
		for (uint32_t i = 1; i + 1 < count; i++) {
			triangles[triangleCount++] = 0;
			triangles[triangleCount++] = i;
			triangles[triangleCount++] = i + 1;
		}
	} else {
		uint32_t remaining = count;
		while (remaining > 3) {
			bool removed = false;
			for (uint32_t i = 0; i < remaining; i++) {
				uint32_t previous = (i + remaining - 1) % remaining, next = (i + 1) % remaining, a = ring[previous],
				         b = ring[i], c = ring[next];
				float area = Area(xy[a], xy[b], xy[c]) * sign;
				if (area < 0)
					continue;
				bool occupied = false;
				for (uint32_t j = 0; j < remaining && !occupied; j++) {
					uint32_t d = ring[j];
					if (d == a || d == b || d == c)
						continue;
					occupied = Area(xy[a], xy[b], xy[d]) * sign > 0 && Area(xy[b], xy[c], xy[d]) * sign > 0 &&
					           Area(xy[c], xy[a], xy[d]) * sign > 0;
				}
				if (occupied)
					continue;
				triangles[triangleCount++] = a;
				triangles[triangleCount++] = b;
				triangles[triangleCount++] = c;
				memmove(ring + i, ring + i + 1, (remaining - i - 1) * sizeof(*ring));
				--remaining;
				removed = true;
				break;
			}
			if (!removed) {
				/* Integer DOS projection can fold very thin rings onto themselves. */
				triangleCount = 0;
				for (uint32_t i = 1; i + 1 < count; i++) {
					triangles[triangleCount++] = 0;
					triangles[triangleCount++] = i;
					triangles[triangleCount++] = i + 1;
				}
				remaining = 0;
				break;
			}
		}
		if (remaining == 3) {
			triangles[triangleCount++] = ring[0];
			triangles[triangleCount++] = ring[1];
			triangles[triangleCount++] = ring[2];
		}
	}
	SDL_Rect clip = mapActive
	                    ? (SDL_Rect){0, 0, width, height}
	                    : (SDL_Rect){g_clipMinX, g_clipMinY, g_clipMaxX - g_clipMinX + 1, g_clipMaxY - g_clipMinY + 1};
	if (drawCount && draws[drawCount - 1].texture == texture &&
	    memcmp(&draws[drawCount - 1].clip, &clip, sizeof(clip)) == 0)
		draws[drawCount - 1].count += triangleCount;
	else {
		Draw *draw = &draws[drawCount++];
		*draw = (Draw){(uint32_t)vertexCount, triangleCount, texture, clip};
	}
	for (uint32_t i = 0; i < triangleCount; i++)
		vertices[vertexCount++] = p[triangles[i]];
}

void SlipRaceGpu_Flat(const RasterPoint *p, uint32_t n, uint8_t c, uint8_t dither) {
	if (n > 128) {
		SDL_SetError("GPU polygon exceeds point capacity");
		Fail();
		return;
	}
	Vertex v[128];
	for (uint32_t i = 0; i < n; i++) {
		v[i] = Point((float)p[i].x, (float)p[i].y, 1, 0, 0, c);
		v[i].color[0] = c;
		v[i].color[1] = (float)(uint8_t)((1u << (dither & 31u)) - 1u);
		v[i].color[3] = 2;
	}
	Polygon(v, n, white, NULL);
}

void SlipRaceGpu_Shaded(const RasterShadedPoint *p, uint32_t n) {
	if (n > 128) {
		SDL_SetError("GPU polygon exceeds point capacity");
		Fail();
		return;
	}
	Vertex v[128];
	for (uint32_t i = 0; i < n; i++) {
		v[i] = Point((float)p[i].x, (float)p[i].y, 1, 0, 0, 0);
		v[i].color[0] = p[i].shade / 256.f;
		v[i].color[1] = 0;
		v[i].color[3] = 2;
	}
	Polygon(v, n, white, NULL);
}

void SlipRaceGpu_MapMarker(SDL_FPoint point, uint8_t color) {
	if (!mapActive)
		return;
	Vertex v[] = {Point(point.x - 1, point.y - 1, 1, 0, 0, color), Point(point.x + 2, point.y - 1, 1, 0, 0, color),
	              Point(point.x + 2, point.y + 2, 1, 0, 0, color), Point(point.x - 1, point.y + 2, 1, 0, 0, color)};
	Polygon(v, 4, white, NULL);
}

/* Shared cross sections make the map a continuous strip, including its closing join. */
void SlipRaceGpu_MapRibbon(const SDL_FPoint *points, uint32_t count, bool closed, uint8_t color) {
	if (!mapActive || count < 2)
		return;
	SDL_FPoint *p = malloc(count * sizeof(*p));
	Vertex *pairs = malloc(count * 2 * sizeof(*pairs));
	if (!p || !pairs) {
		free(p);
		free(pairs);
		SDL_SetError("GPU map ribbon allocation failed");
		Fail();
		return;
	}
	float sx = (float)height / 240, sy = (float)height / 200;
	uint32_t n = 0;
	for (uint32_t i = 0; i < count; i++) {
		SDL_FPoint v = {points[i].x * sx, points[i].y * sy};
		if (!n || hypotf(v.x - p[n - 1].x, v.y - p[n - 1].y) > 0.0001f)
			p[n++] = v;
	}
	if (closed && n > 1 && hypotf(p[0].x - p[n - 1].x, p[0].y - p[n - 1].y) < 0.0001f)
		--n;
	if (n < 2) {
		free(p);
		free(pairs);
		return;
	}
	for (uint32_t i = 0; i < n; i++) {
		SDL_FPoint a = p[(i + n - 1) % n], b = p[i], c = p[(i + 1) % n];
		float dx0 = b.x - a.x, dy0 = b.y - a.y, dx1 = c.x - b.x, dy1 = c.y - b.y;
		float l0 = hypotf(dx0, dy0), l1 = hypotf(dx1, dy1);
		float nx0 = -dy0 / l0, ny0 = dx0 / l0, nx1 = -dy1 / l1, ny1 = dx1 / l1;
		if (!closed && i == 0) {
			nx0 = nx1;
			ny0 = ny1;
		}
		if (!closed && i == n - 1) {
			nx1 = nx0;
			ny1 = ny0;
		}
		float mx = nx0 + nx1, my = ny0 + ny1, length = hypotf(mx, my);
		if (length < 0.0001f) {
			mx = nx1;
			my = ny1;
		} else {
			mx /= length;
			my /= length;
		}
		float dot = mx * nx1 + my * ny1;
		float distance = lineWidth * .5f / fmaxf(dot, .25f);
		pairs[i * 2] = Point((b.x + mx * distance) / sx, (b.y + my * distance) / sy, 1, 0, 0, color);
		pairs[i * 2 + 1] = Point((b.x - mx * distance) / sx, (b.y - my * distance) / sy, 1, 0, 0, color);
	}
	for (uint32_t i = 0; i < (closed ? n : n - 1); i++) {
		uint32_t j = (i + 1) % n;
		Vertex quad[] = {pairs[i * 2], pairs[j * 2], pairs[j * 2 + 1], pairs[i * 2 + 1]};
		Polygon(quad, 4, white, NULL);
	}
	free(p);
	free(pairs);
}

void SlipRaceGpu_Line(uint8_t c, int x0, int y0, int x1, int y1) {
	float dx = (float)(x1 - x0), dy = (float)(y1 - y0), len = sqrtf(dx * dx + dy * dy);
	if (!len) {
		if (mapActive)
			return;
		RasterPoint p[] = {{x0 - lineWidth / 2, y0 - lineWidth / 2},
		                   {x0 + (lineWidth + 1) / 2, y0 - lineWidth / 2},
		                   {x0 + (lineWidth + 1) / 2, y0 + (lineWidth + 1) / 2},
		                   {x0 - lineWidth / 2, y0 + (lineWidth + 1) / 2}};
		SlipRaceGpu_Flat(p, 4, c, 0);
		return;
	}
	float ox = dy / len * lineWidth * .5f, oy = -dx / len * lineWidth * .5f;
	if (mapActive) {
		float sx = (float)height / 240, sy = (float)height / 200;
		float nativeLength = sqrtf(dx * dx * sx * sx + dy * dy * sy * sy);
		ox = dy * sy / nativeLength * lineWidth * .5f / sx;
		oy = -dx * sx / nativeLength * lineWidth * .5f / sy;
	}
	Vertex v[] = {Point(x0 + .5f + ox, y0 + .5f + oy, 1, 0, 0, c), Point(x1 + .5f + ox, y1 + .5f + oy, 1, 0, 0, c),
	              Point(x1 + .5f - ox, y1 + .5f - oy, 1, 0, 0, c), Point(x0 + .5f - ox, y0 + .5f - oy, 1, 0, 0, c)};
	Polygon(v, 4, white, NULL);
}

static void TexturePolygon(const uint8_t *payload, size_t bytes, uint32_t scroll, const RasterTexturedPoint *p,
                           const SlipRaceGpuWorldPoint *world, uint32_t n, const SlipDraw3DProjectState *projection) {
	if (bytes < 16 || n > 128) {
		SDL_SetError("Invalid GPU texture polygon");
		Fail();
		return;
	}
	int w = SlipBytes_ReadLE16(payload), h = SlipBytes_ReadLE16(payload + 2);
	if (!w || !h || (size_t)w * h > bytes - 16) {
		SDL_SetError("Invalid GPU texture dimensions");
		Fail();
		return;
	}
	Texture *t = textures;
	for (; t && !(t->payload == payload && t->frame == frame); t = t->next) {
	}
	uint64_t hash = 1469598103934665603ull;
	if (!t) {
		for (size_t i = 0; i < (size_t)w * h + 16; i++)
			hash = (hash ^ payload[i]) * 1099511628211ull;
		t = textures;
		for (; t && t->hash != hash; t = t->next) {
		}
	}
	if (!t) {
		uint8_t *rgba = malloc((size_t)w * h * 4);
		if (!rgba) {
			SDL_SetError("GPU texture allocation failed");
			Fail();
			return;
		}
		/* All DOS texture entry points honor the key when the texture has one. */
		uint16_t transparent = SlipBytes_ReadLE16(payload + 8);
		for (int i = 0; i < w * h; i++) {
			uint8_t index = payload[16 + i];
			uint32_t c = g_palette[index];
			rgba[i * 4] = (uint8_t)(c >> 16);
			rgba[i * 4 + 1] = (uint8_t)(c >> 8);
			rgba[i * 4 + 2] = (uint8_t)c;
			rgba[i * 4 + 3] = transparent != 0xffff && index == (uint8_t)transparent ? 0 : 255;
		}
		SDL_GPUTexture *image = Upload(rgba, w, h, true);
		free(rgba);
		if (!image)
			return;
		t = malloc(sizeof(*t));
		if (!t) {
			SDL_ReleaseGPUTexture(device, image);
			SDL_SetError("GPU texture cache allocation failed");
			Fail();
			return;
		}
		*t = (Texture){.hash = hash, .texture = image, .next = textures};
		textures = t;
	}
	t->payload = payload;
	t->frame = frame;
	Vertex v[128];
	float sourceXY[128][2];
	int axis = 2;
	if (world) {
		double normal[3] = {0};
		for (uint32_t i = 0; i < n; i++) {
			SlipDraw3DVec32 a = world[i].source, b = world[(i + 1) % n].source;
			normal[0] += ((double)a.y - b.y) * ((double)a.z + b.z);
			normal[1] += ((double)a.z - b.z) * ((double)a.x + b.x);
			normal[2] += ((double)a.x - b.x) * ((double)a.y + b.y);
		}
		axis = fabs(normal[0]) > fabs(normal[1]) ? 0 : 1;
		if (fabs(normal[2]) > fabs(normal[axis]))
			axis = 2;
	}
	for (uint32_t i = 0; i < n; i++) {
		float u = (world ? world[i].u : p[i].u) / 16384.f;
		float uv = (world ? world[i].v : p[i].v) / 16384.f;
		/* Original row scrolling wraps texture rows, rather than changing geometry. */
		uv = SDL_clamp(uv, 0.f, 1.f) * (1.f - 1.f / (65536.f * h)) + (float)(((uint64_t)scroll * h >> 14) % h) / h;
		if (world) {
			const SlipDraw3DVec32 a = world[i].source, c = world[i].view;
			sourceXY[i][0] = axis == 0 ? (float)a.y : (float)a.x;
			sourceXY[i][1] = axis == 2 ? (float)a.y : (float)a.z;
			float z = (float)c.z;
			float nearZ = (float)projection->minZ, farZ = (float)projection->maxZ;
			float depthScale = farZ / (farZ - nearZ);
			float scaleX =
			    (float)(projection->squarePixels ? projection->projectionScale * 5 / 6 : projection->projectionScale);
			v[i] = (Vertex){
			    {(2.f * projection->centerX / width - 1) * z + 2.f * scaleX * c.x / width,
			     (1 - 2.f * projection->centerY / height) * z + 2.f * projection->projectionScale * c.y / height,
			     depthScale * (z - nearZ), z},
			    {u, uv},
			    {1, 1, 1, 1}};
		} else
			v[i] = Point((float)p[i].x, (float)p[i].y, (float)p[i].depth, u, uv, 0);
		v[i].color[0] = v[i].color[1] = v[i].color[2] = 1;
	}
	Polygon(v, n, t->texture, world ? sourceXY : NULL);
}

void SlipRaceGpu_Texture(const uint8_t *payload, size_t bytes, uint32_t scroll, const RasterTexturedPoint *points,
                         uint32_t count) {
	TexturePolygon(payload, bytes, scroll, points, NULL, count, NULL);
}

void SlipRaceGpu_WorldTexture(const uint8_t *payload, size_t bytes, uint32_t scroll,
                              const SlipRaceGpuWorldPoint *points, uint32_t count,
                              const SlipDraw3DProjectState *projection) {
	TexturePolygon(payload, bytes, scroll, NULL, points, count, projection);
}

void SlipRaceGpu_Sprite(const uint8_t *p, size_t bytes, int left, int top, int right, int bottom) {
	RasterTexturedPoint v[4] = {{.x = left, .y = top, .depth = 1},
	                            {.x = right + 1, .y = top, .u = 16384, .depth = 1},
	                            {.x = right + 1, .y = bottom + 1, .u = 16384, .v = 16384, .depth = 1},
	                            {.x = left, .y = bottom + 1, .v = 16384, .depth = 1}};
	SlipRaceGpu_Texture(p, bytes, 0, v, 4);
}

bool SlipRaceGpu_Present(void) {
	if (failed || !target)
		return false;
	if (!SDL_FlushRenderer(renderer)) {
		Fail();
		return false;
	}
	SDL_GPUTexture *image =
	    SDL_GetPointerProperty(SDL_GetTextureProperties(target), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, NULL);
	if (!image) {
		Fail();
		return false;
	}
	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
	if (!cmd) {
		Fail();
		return false;
	}
	if (vertexCount) {
		uint32_t bytes = (uint32_t)(vertexCount * sizeof(Vertex));
		if (bytes > bufferBytes) {
			if (vertexBuffer)
				SDL_ReleaseGPUBuffer(device, vertexBuffer);
			if (vertexTransfer)
				SDL_ReleaseGPUTransferBuffer(device, vertexTransfer);
			bufferBytes = bytes * 2;
			SDL_GPUBufferCreateInfo bi = {.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = bufferBytes};
			vertexBuffer = SDL_CreateGPUBuffer(device, &bi);
			SDL_GPUTransferBufferCreateInfo ti = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = bufferBytes};
			vertexTransfer = SDL_CreateGPUTransferBuffer(device, &ti);
		}
		void *mapped = vertexTransfer ? SDL_MapGPUTransferBuffer(device, vertexTransfer, true) : NULL;
		if (!mapped || !vertexBuffer) {
			if (mapped)
				SDL_UnmapGPUTransferBuffer(device, vertexTransfer);
			SDL_CancelGPUCommandBuffer(cmd);
			Fail();
			return false;
		}
		memcpy(mapped, vertices, bytes);
		SDL_UnmapGPUTransferBuffer(device, vertexTransfer);
		SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
		SDL_GPUTransferBufferLocation source = {vertexTransfer, 0};
		SDL_GPUBufferRegion dst = {vertexBuffer, 0, bytes};
		SDL_UploadToGPUBuffer(copy, &source, &dst, true);
		SDL_EndGPUCopyPass(copy);
	}
	uint32_t clear = g_palette[0];
	SDL_GPUColorTargetInfo ct = {
	    .texture = image,
	    .clear_color = {((clear >> 16) & 255) / 255.f, ((clear >> 8) & 255) / 255.f, (clear & 255) / 255.f, 1},
	    .load_op = SDL_GPU_LOADOP_CLEAR,
	    .store_op = SDL_GPU_STOREOP_STORE};
	SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
	SDL_BindGPUGraphicsPipeline(pass, pipeline);
	if (vertexCount) {
		SDL_GPUBufferBinding binding = {vertexBuffer, 0};
		SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
		for (size_t i = 0; i < drawCount; i++) {
			Draw *d = &draws[i];
			SDL_SetGPUScissor(pass, &d->clip);
			SDL_GPUTextureSamplerBinding texture[] = {{d->texture, sampler}, {palette, sampler}};
			SDL_BindGPUFragmentSamplers(pass, 0, texture, 2);
			SDL_DrawGPUPrimitives(pass, d->count, 1, d->first, 0);
		}
	}
	SDL_EndGPURenderPass(pass);
	if (!SDL_SubmitGPUCommandBuffer(cmd)) {
		Fail();
		return false;
	}
	return SDL_RenderTexture(renderer, target, NULL, NULL);
}

void SlipRaceGpu_Overlay(const uint8_t *pixels, const uint8_t *mask, int left, int top, int right, int bottom,
                         SDL_FRect destination) {
	/* DOS draw pages alternate; the HUD texture persists across draw pages. */
	SDL_Texture *texture = overlayTexture;
	if (!texture) {
		texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 320, 200);
		if (!texture) {
			Fail();
			return;
		}
		overlayTexture = texture;
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
		SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
	}
	if (!overlayUpdated) {
		uint32_t rgba[64000];
		for (int i = 0; i < 64000; i++)
			rgba[i] = mask[i] ? g_palette[pixels[i]] : 0;
		SDL_UpdateTexture(texture, NULL, rgba, 320 * 4);
		overlayUpdated = true;
	}
	SDL_FRect source = {(float)left, (float)top, (float)(right - left), (float)(bottom - top)};
	SDL_RenderTexture(renderer, texture, &source, &destination);
}

void SlipRaceGpu_Shutdown(void) {
	Raster_SetDrawBackend(NULL);
	active = false;
	if (!device)
		return;
	SDL_WaitForGPUIdle(device);
	ClearTextures();
	if (target)
		SDL_DestroyTexture(target);
	if (overlayTexture)
		SDL_DestroyTexture(overlayTexture);
	overlayTexture = NULL;
	if (palette)
		SDL_ReleaseGPUTexture(device, palette);
	palette = NULL;
	if (white)
		SDL_ReleaseGPUTexture(device, white);
	if (sampler)
		SDL_ReleaseGPUSampler(device, sampler);
	if (pipeline)
		SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
	if (vertexBuffer)
		SDL_ReleaseGPUBuffer(device, vertexBuffer);
	if (vertexTransfer)
		SDL_ReleaseGPUTransferBuffer(device, vertexTransfer);
	free(vertices);
	free(draws);
	target = NULL;
	pipeline = NULL;
	sampler = NULL;
	white = NULL;
	vertexBuffer = NULL;
	vertexTransfer = NULL;
	vertices = NULL;
	draws = NULL;
	vertexCount = drawCount = vertexCapacity = drawCapacity = bufferBytes = 0;
	device = NULL;
	renderer = NULL;
	width = height = 0;
	memset(lastPalette, 0, sizeof(lastPalette));
}
