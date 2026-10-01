#include "shape_primitives.h"
#include "byte_order.h"
#include "shape_prepare.h"

/* Serialized SHP words/offset; the active shape and callback remain typed. */

void SlipShape_DrawUnsorted(SlipActorShapeState *state, const uint8_t *shape, const SlipShapePrimitiveCalls *calls) {
	uint8_t *primitive = state->shape + SlipBytes_ReadLE32(shape + 0x14);
	uint32_t remaining = SlipBytes_ReadLE16(primitive);
	primitive += 2;
	do {

		const int16_t normalZ = (int16_t)SlipBytes_ReadLE16(primitive + 6);
		const uint32_t traversalValue = (remaining & 0xffff0000u) | (uint16_t)normalZ;
		if (calls->planeVisible(calls->context, (int16_t)SlipBytes_ReadLE16(primitive + 2),
		                        (int16_t)SlipBytes_ReadLE16(primitive + 4), normalZ,
		                        SlipBytes_ReadLE16(primitive + 12)))
			state->primitive(calls->context, primitive, traversalValue);
		primitive = SlipShape_NextPrimitive(primitive);
	} while (--remaining != 0);
}
