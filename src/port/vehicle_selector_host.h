#ifndef SLIPSTREAM5000_VEHICLE_SELECTOR_HOST_H
#define SLIPSTREAM5000_VEHICLE_SELECTOR_HOST_H
#include "raster/raster.h"
#include "vehicle_selector.h"

/* Host views needed by the original surface/font callees. The caller supplies
 * modify000249a0, viewVehicle00046a94 and the synchronized sound bindings.
 * BindNativeCalls preserves those entries. calls->context must point to this
 * host record; the remaining bindings must use the same context contract. */
typedef struct SlipVehicleSelectorHost {
	uint16_t smallFontHandle, boundSpriteHandle;
	RasterSurfaceBinding previousScreenSurface;
} SlipVehicleSelectorHost;

void SlipVehicleSelector_BindNativeCalls(SlipVehicleSelectorCalls *calls);
#endif
