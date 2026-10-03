#include "shape_primitives.h"
#include "byte_order.h"
#include "shape_format.h"
#include "shape_prepare.h"

/* Keep the traversal counter in the upper word and replace its low word with normal Z. */
static const uint32_t SLIP_SHAPE_TRAVERSAL_UPPER_WORD_MASK = UINT32_MAX ^ UINT16_MAX;

void SlipShape_DrawUnsorted(SlipActorShapeState *state, const uint8_t *shape, const SlipShapePrimitiveCalls *calls) {
	uint8_t *primitive = state->shape + SlipBytes_ReadLE32(shape + SLIP_SHAPE_PRIMITIVE_TABLE_OFFSET);
	uint32_t remaining = SlipBytes_ReadLE16(primitive);
	primitive += SLIP_SHAPE_TABLE_COUNT_BYTES;
	do {

		const int16_t normalZ = (int16_t)SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_NORMAL_Z_OFFSET);
		const uint32_t traversalValue = (remaining & SLIP_SHAPE_TRAVERSAL_UPPER_WORD_MASK) | (uint16_t)normalZ;
		if (calls->planeVisible(calls->context, (int16_t)SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_NORMAL_X_OFFSET),
		                        (int16_t)SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_NORMAL_Y_OFFSET), normalZ,
		                        SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_HEADER_BYTES)))
			state->primitive(calls->context, primitive, traversalValue);
		primitive = SlipShape_NextPrimitive(primitive);
	} while (--remaining != 0);
}
