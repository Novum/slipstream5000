#include "texture_resize.h"
#include "byte_order.h"
#include "sprite_format.h"

bool SlipTexture_Resize(SlipTextureResizeState *state, uint16_t resource, uint32_t scale,
                        const SlipTextureResizeCalls *calls) {
	state->resource = resource;
	state->scale = scale;
	if (!calls->resident(calls->context, resource)) {
		if (!calls->load(calls->context, resource))
			return false;
	}
	if (state->scale == SLIP_TEXTURE_RESIZE_SCALE_ONE_Q30)
		return true;
	state->sprite = calls->lock(calls->context, resource);
	state->width = SlipBytes_ReadLE16(state->sprite + SLIP_SPRITE_WIDTH_OFFSET);
	state->height = SlipBytes_ReadLE16(state->sprite + SLIP_SPRITE_HEIGHT_OFFSET);
	uint32_t width = (uint32_t)(((uint64_t)state->scale * state->width) >> SLIP_TEXTURE_RESIZE_SCALE_FRACTION_BITS);
	if (width == 0)
		width = 1;
	state->resizedWidth = width;
	uint32_t height = (uint32_t)(((uint64_t)state->scale * state->height) >> SLIP_TEXTURE_RESIZE_SCALE_FRACTION_BITS);
	if (height == 0)
		height = 1;
	state->resizedHeight = height;
	state->resizedBytes = state->resizedWidth * state->resizedHeight + SLIP_SPRITE_HEADER_BYTES;
	SlipBytes_WriteLE16(state->sprite + SLIP_SPRITE_WIDTH_OFFSET, (uint16_t)state->resizedWidth);
	SlipBytes_WriteLE16(state->sprite + SLIP_SPRITE_HEIGHT_OFFSET, (uint16_t)state->resizedHeight);
	state->stepX = (uint32_t)((UINT64_C(1)
	                           << (SLIP_TEXTURE_RESIZE_SCALE_FRACTION_BITS + SLIP_TEXTURE_RESIZE_PIXEL_FRACTION_BITS)) /
	                          state->scale);
	state->stepY = state->stepX;
	state->stepX =
	    (uint32_t)(((uint64_t)state->width << SLIP_TEXTURE_RESIZE_PIXEL_FRACTION_BITS) / state->resizedWidth);
	state->stepY =
	    (uint32_t)(((uint64_t)(state->height + 1u) << SLIP_TEXTURE_RESIZE_PIXEL_FRACTION_BITS) / state->resizedHeight);
	uint32_t destination = SLIP_SPRITE_HEADER_BYTES, sourceY = 0;
	do {
		uint32_t column = 0, sourceX = 0;
		do {
			uint32_t y = sourceY >> SLIP_TEXTURE_RESIZE_PIXEL_FRACTION_BITS;
			if (y > state->height)
				y = state->height - 1u;
			uint32_t source = y * state->width;
			uint32_t x = sourceX >> SLIP_TEXTURE_RESIZE_PIXEL_FRACTION_BITS;
			if (x > state->width)
				x = state->width - 1u;
			source += x;
			state->sprite[destination++] = state->sprite[source + SLIP_SPRITE_HEADER_BYTES];
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
