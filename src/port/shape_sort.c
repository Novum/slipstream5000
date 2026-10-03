#include "shape_sort.h"
#include "byte_order.h"
#include "shape_format.h"

/* Offsets and normal words belong to the serialized SHP sort block. */

void SlipShapeSort_Run(SlipShapeSortState *state, const uint8_t *sort, SlipShapeSortCallback callback,
                       void *callbackContext, const SlipShapeSortCalls *calls) {
	const uint8_t *const savedBase = state->sortBlock;
	const SlipShapeSortCallback savedCallback = state->callback;
	void *const savedContext = state->callbackContext;
	state->sortBlock = sort;
	state->callback = callback;
	state->callbackContext = callbackContext;
	SlipShapeSort_Node(state, sort + SLIP_SHAPE_SORT_HEADER_BYTES, calls);
	state->callback = savedCallback;
	state->callbackContext = savedContext;
	state->sortBlock = savedBase;
}

void SlipShapeSort_Node(SlipShapeSortState *state, const uint8_t *node, const SlipShapeSortCalls *calls) {
	const uint16_t vertex = SlipBytes_ReadLE16(node + SLIP_SHAPE_SORT_PLANE_VERTEX_OFFSET);
	if (vertex == UINT16_MAX) {
		state->callback(state->callbackContext, SlipBytes_ReadLE32(node + SLIP_SHAPE_SORT_INDEX_OFFSET),
		                SlipBytes_ReadLE16(node + SLIP_SHAPE_SORT_TYPE_OFFSET), 0, node);
	} else if (calls->classifyPlane(calls->context, (int16_t)SlipBytes_ReadLE16(node + SLIP_SHAPE_SORT_NORMAL_X_OFFSET),
	                                (int16_t)SlipBytes_ReadLE16(node + SLIP_SHAPE_SORT_NORMAL_Y_OFFSET),
	                                (int16_t)SlipBytes_ReadLE16(node + SLIP_SHAPE_SORT_NORMAL_Z_OFFSET), vertex)) {
		uint32_t child = SlipBytes_ReadLE32(node + SLIP_SHAPE_SORT_CHILD_0_OFFSET);
		if (child != 0)
			SlipShapeSort_Node(state, state->sortBlock + child, calls);
		const uint32_t index = SlipBytes_ReadLE32(node + SLIP_SHAPE_SORT_INDEX_OFFSET);
		if (index != 0)
			state->callback(state->callbackContext, index, SlipBytes_ReadLE16(node + SLIP_SHAPE_SORT_TYPE_OFFSET), -1,
			                node);
		child = SlipBytes_ReadLE32(node + SLIP_SHAPE_SORT_CHILD_1_OFFSET);
		if (child != 0)
			SlipShapeSort_Node(state, state->sortBlock + child, calls);
	} else {
		uint32_t child = SlipBytes_ReadLE32(node + SLIP_SHAPE_SORT_CHILD_1_OFFSET);
		if (child != 0)
			SlipShapeSort_Node(state, state->sortBlock + child, calls);
		const uint32_t index = SlipBytes_ReadLE32(node + SLIP_SHAPE_SORT_INDEX_OFFSET);
		if (index != 0)
			state->callback(state->callbackContext, index, SlipBytes_ReadLE16(node + SLIP_SHAPE_SORT_TYPE_OFFSET), 1,
			                node);
		child = SlipBytes_ReadLE32(node + SLIP_SHAPE_SORT_CHILD_0_OFFSET);
		if (child != 0)
			SlipShapeSort_Node(state, state->sortBlock + child, calls);
	}
}
