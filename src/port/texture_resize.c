#include "texture_resize.h"
#include "byte_order.h"

/* These words belong to the serialized SPR header. */
static void SlipTexture_SpriteStoreWord(uint8_t *bytes, uint16_t value) {
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

bool SlipTexture_Resize(SlipTextureResizeState *state, uint16_t resource, uint32_t scale,
                        const SlipTextureResizeCalls *calls) {
	state->resource = resource;
	state->scale = scale;
	if (!calls->resident(calls->context, resource)) {
		if (!calls->load(calls->context, resource))
			return false;
	}
	if (state->scale == 0x40000000)
		return true;
	state->sprite = calls->lock(calls->context, resource);
	state->width = SlipBytes_ReadLE16(state->sprite);
	state->height = SlipBytes_ReadLE16(state->sprite + 2);
	uint32_t width = (uint32_t)(((uint64_t)state->scale * state->width) >> 30);
	if (width == 0)
		width = 1;
	state->resizedWidth = width;
	uint32_t height = (uint32_t)(((uint64_t)state->scale * state->height) >> 30);
	if (height == 0)
		height = 1;
	state->resizedHeight = height;
	state->resizedBytes = state->resizedWidth * state->resizedHeight + 16u;
	SlipTexture_SpriteStoreWord(state->sprite, (uint16_t)state->resizedWidth);
	SlipTexture_SpriteStoreWord(state->sprite + 2, (uint16_t)state->resizedHeight);
	state->stepX = (uint32_t)((UINT64_C(0x4000) << 32) / state->scale);
	state->stepY = state->stepX;
	state->stepX = (uint32_t)(((uint64_t)state->width << 16) / state->resizedWidth);
	state->stepY = (uint32_t)(((uint64_t)(state->height + 1u) << 16) / state->resizedHeight);
	uint32_t destination = 16, sourceY = 0;
	do {
		uint32_t column = 0, sourceX = 0;
		do {
			uint32_t y = sourceY >> 16;
			if (y > state->height)
				y = state->height - 1u;
			uint32_t source = y * state->width;
			uint32_t x = sourceX >> 16;
			if (x > state->width)
				x = state->width - 1u;
			source += x;
			state->sprite[destination++] = state->sprite[source + 16u];
			sourceX += state->stepX;
			++column;
		} while (column != state->resizedWidth);
		sourceY += state->stepY;
	} while (destination != state->resizedBytes);
	const uint16_t resizedResource = state->resource;
	calls->unlock(calls->context, resizedResource);
	(void)calls->resize(calls->context, resizedResource, state->resizedBytes);
	return true;
}
