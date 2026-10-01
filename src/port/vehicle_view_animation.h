#ifndef SLIPSTREAM5000_VEHICLE_VIEW_ANIMATION_H
#define SLIPSTREAM5000_VEHICLE_VIEW_ANIMATION_H
#include <stdbool.h>
#include <stdint.h>

typedef struct SlipVehicleViewAnimation {
	int16_t jetAngle;
	uint16_t jetDirection;
	int16_t fanStep;
	int16_t jetStep;
} SlipVehicleViewAnimation;

typedef struct SlipVehicleViewAnimationCalls {
	void *context;
	/* True represents carry clear. Failed lookup leaves the angle unused. */
	bool (*getAngle)(void *, uint16_t object, uint32_t partTag, uint16_t *angle);
	void (*setAngle)(void *, uint16_t object, uint32_t partTag, uint16_t angle);
} SlipVehicleViewAnimationCalls;

void SlipVehicleView_Animate(SlipVehicleViewAnimation *state, uint16_t object,
                             const SlipVehicleViewAnimationCalls *calls);
#endif
