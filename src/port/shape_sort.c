#include "shape_sort.h"
#include "byte_order.h"

/* Offsets and normal words belong to the serialized SHP sort block. */

void SlipShapeSort_Run(SlipShapeSortState *state, const uint8_t *sort, SlipShapeSortCallback callback,
                       void *callbackContext, const SlipShapeSortCalls *calls) {
	const uint8_t *const savedBase = state->sortBlock;
	const SlipShapeSortCallback savedCallback = state->callback;
	void *const savedContext = state->callbackContext;
	state->sortBlock = sort;
	state->callback = callback;
	state->callbackContext = callbackContext;
	SlipShapeSort_Node(state, sort + 2, calls);
	state->callback = savedCallback;
	state->callbackContext = savedContext;
	state->sortBlock = savedBase;
}

void SlipShapeSort_Node(SlipShapeSortState *state, const uint8_t *node, const SlipShapeSortCalls *calls) {
	const uint16_t vertex = SlipBytes_ReadLE16(node + 0x10);
	if (vertex == UINT16_MAX) {
		state->callback(state->callbackContext, SlipBytes_ReadLE32(node + 8), SlipBytes_ReadLE16(node + 0x0c), 0, node);
	} else if (calls->classifyPlane(calls->context, (int16_t)SlipBytes_ReadLE16(node + 0x12),
	                                (int16_t)SlipBytes_ReadLE16(node + 0x14), (int16_t)SlipBytes_ReadLE16(node + 0x16),
	                                vertex)) {
		uint32_t child = SlipBytes_ReadLE32(node);
		if (child != 0)
			SlipShapeSort_Node(state, state->sortBlock + child, calls);
		const uint32_t index = SlipBytes_ReadLE32(node + 8);
		if (index != 0)
			state->callback(state->callbackContext, index, SlipBytes_ReadLE16(node + 0x0c), -1, node);
		child = SlipBytes_ReadLE32(node + 4);
		if (child != 0)
			SlipShapeSort_Node(state, state->sortBlock + child, calls);
	} else {
		uint32_t child = SlipBytes_ReadLE32(node + 4);
		if (child != 0)
			SlipShapeSort_Node(state, state->sortBlock + child, calls);
		const uint32_t index = SlipBytes_ReadLE32(node + 8);
		if (index != 0)
			state->callback(state->callbackContext, index, SlipBytes_ReadLE16(node + 0x0c), 1, node);
		child = SlipBytes_ReadLE32(node);
		if (child != 0)
			SlipShapeSort_Node(state, state->sortBlock + child, calls);
	}
}
