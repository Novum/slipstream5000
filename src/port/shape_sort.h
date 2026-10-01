#ifndef SLIPSTREAM5000_SHAPE_SORT_H
#define SLIPSTREAM5000_SHAPE_SORT_H
#include <stdbool.h>
#include <stdint.h>

typedef void (*SlipShapeSortCallback)(void *context, uint32_t index, uint16_t type, int32_t classification,
                                      const uint8_t *node);

typedef struct SlipShapeSortState {
	const uint8_t *sortBlock;
	SlipShapeSortCallback callback;
	void *callbackContext; /* Native context paired with the typed callback. */
} SlipShapeSortState;

typedef struct SlipShapeSortCalls {
	void *context;

	bool (*classifyPlane)(void *, int16_t x, int16_t y, int16_t z, uint16_t vertex);
} SlipShapeSortCalls;

void SlipShapeSort_Run(SlipShapeSortState *, const uint8_t *sort, SlipShapeSortCallback, void *callbackContext,
                       const SlipShapeSortCalls *);
void SlipShapeSort_Node(SlipShapeSortState *, const uint8_t *node, const SlipShapeSortCalls *);
#endif
