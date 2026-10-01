#ifndef SLIPSTREAM5000_INPUT_ZONE_H
#define SLIPSTREAM5000_INPUT_ZONE_H
#include "input_navigation.h"

typedef struct SlipInputZoneCalls {
	void *context;
	const uint8_t *(*lock)(void *, uint16_t);
	SlipInputPointerPosition (*pointer)(void *);
	void (*unlock)(void *, uint16_t);
} SlipInputZoneCalls;

uint16_t SlipInput_Zone(uint16_t resource, const SlipInputZoneCalls *calls);
#endif
