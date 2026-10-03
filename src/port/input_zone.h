#ifndef SLIPSTREAM5000_INPUT_ZONE_H
#define SLIPSTREAM5000_INPUT_ZONE_H
#include "input_navigation.h"

/* Zone data starts with a row count and a table of little-endian row offsets.
 * Each row contains selection-byte / right-edge-word spans, ending at UINT16_MAX. */
enum {
	SLIP_INPUT_ZONE_HEADER_BYTES = sizeof(uint16_t),
	SLIP_INPUT_ZONE_ROW_OFFSET_BYTES = sizeof(uint16_t),
	SLIP_INPUT_ZONE_SELECTION_BYTES = sizeof(uint8_t),
	SLIP_INPUT_ZONE_EDGE_BYTES = sizeof(uint16_t),
	SLIP_INPUT_ZONE_SPAN_BYTES = SLIP_INPUT_ZONE_SELECTION_BYTES + SLIP_INPUT_ZONE_EDGE_BYTES
};

typedef struct SlipInputZoneCalls {
	void *context;
	const uint8_t *(*lock)(void *, uint16_t);
	SlipInputPointerPosition (*pointer)(void *);
	void (*unlock)(void *, uint16_t);
} SlipInputZoneCalls;

uint16_t SlipInput_Zone(uint16_t resource, const SlipInputZoneCalls *calls);
#endif
