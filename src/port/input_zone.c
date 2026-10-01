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
		const uint32_t rowEntry = ((uint32_t)pointer.y + 1) * 2;
		const uint16_t rowOffset = (uint16_t)(zones[rowEntry] | (uint16_t)zones[rowEntry + 1] << 8);
		const uint8_t *span = zones + rowOffset;
		selection = *span++;
		for (;;) {
			const uint16_t edge = (uint16_t)(span[0] | (uint16_t)span[1] << 8);
			if (edge == UINT16_MAX || (int16_t)pointer.x < (int16_t)edge)
				break;
			selection = span[2];
			span += 3;
		}
	}
	calls->unlock(calls->context, resource);
	return selection;
}
