#include "shape_dispatch.h"
#include "byte_order.h"
#include "shape_format.h"

int32_t SlipShape_textureDispatchThreshold = SLIP_DEFAULT_TEXTURE_DISPATCH_THRESHOLD;

void SlipShape_DispatchPrimitive(const uint8_t *primitive, uint32_t traversalValue,
                                 const SlipShapeDispatchCalls *calls) {
	if ((SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_RENDER_FLAGS_OFFSET) & SLIP_PRIMITIVE_SKIP_RENDER) != 0)
		return;
	const uint16_t countAndFlags = SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_COUNT_FLAGS_OFFSET);
	const int16_t normalX = (int16_t)SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_NORMAL_X_OFFSET);
	const int16_t normalY = (int16_t)SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_NORMAL_Y_OFFSET);
	const uint16_t normalZ = SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_NORMAL_Z_OFFSET);
	const uint16_t material = SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_MATERIAL_OFFSET);
	const uint8_t *const stream = primitive + SLIP_PRIMITIVE_HEADER_BYTES;
	if ((countAndFlags & SLIP_PRIMITIVE_TEXTURE_COORDINATES) == 0) {
		calls->solid(calls->context, countAndFlags, normalX, normalY, (int16_t)normalZ, material, stream);
	} else {
		(void)calls->polygonDepth(calls->context, countAndFlags, stream);
		uint32_t flags;
		if ((int32_t)((traversalValue & ~(uint32_t)UINT16_MAX) | normalZ) > SlipShape_textureDispatchThreshold)
			flags = calls->renderFlags(calls->context) | SLIP_RENDER_ALTERNATE_TEXTURE_RASTER;
		else
			flags = calls->renderFlags(calls->context) & ~SLIP_RENDER_ALTERNATE_TEXTURE_RASTER;
		calls->setRenderFlags(calls->context, (uint16_t)flags);
		calls->textured(calls->context, countAndFlags, normalX, normalY, (int16_t)normalZ, material, stream);
	}
}
