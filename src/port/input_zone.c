#include "input_zone.h"

uint16_t SlipInput_Zone(uint16_t resource, const SlipInputZoneCalls *calls) {
	const uint8_t *const zones = calls->lock(calls->context, resource);
	SlipInputPointerPosition pointer = calls->pointer(calls->context);
	const uint16_t rows = (uint16_t)(zones[0] | (uint16_t)zones[1] << 8);
	const uint16_t lastRow = (uint16_t)(rows - 1);
	uint16_t selection;
	if ((int16_t)pointer.y > (int16_t)lastRow) {
		selection = 0;
	} else {
		const uint32_t rowEntry = SLIP_INPUT_ZONE_HEADER_BYTES + (uint32_t)pointer.y * SLIP_INPUT_ZONE_ROW_OFFSET_BYTES;
		const uint16_t rowOffset = (uint16_t)(zones[rowEntry] | (uint16_t)zones[rowEntry + 1] << 8);
		const uint8_t *span = zones + rowOffset;
		selection = *span++;
		for (;;) {
			const uint16_t edge = (uint16_t)(span[0] | (uint16_t)span[1] << 8);
			if (edge == UINT16_MAX || (int16_t)pointer.x < (int16_t)edge)
				break;
			selection = span[SLIP_INPUT_ZONE_EDGE_BYTES];
			span += SLIP_INPUT_ZONE_SPAN_BYTES;
		}
	}
	calls->unlock(calls->context, resource);
	return selection;
}
