#ifndef SLIPSTREAM5000_VEHICLE_VIEW_CAMERA_H
#define SLIPSTREAM5000_VEHICLE_VIEW_CAMERA_H
#include "vehicle_viewer.h"

typedef struct SlipVehicleViewCameraCalls {
	void *context;
	void (*yaw)(void *, int16_t, SlipView3DMatrix *);
	void (*pitch)(void *, int16_t, SlipView3DMatrix *);
	void (*normalize)(void *, SlipView3DMatrix *);
	void (*rotate)(void *, uint16_t, int16_t, int16_t, int16_t, uint32_t);
	SlipView3DVec32 (*getPosition)(void *, uint16_t);
	void (*matrix)(void *, uint16_t, const SlipView3DMatrix *);
	SlipView3DVec32 (*transform)(void *, const SlipView3DMatrix *, SlipView3DVec32);
	void (*setPosition)(void *, uint16_t, SlipView3DVec32);
} SlipVehicleViewCameraCalls;

void SlipVehicleView_CameraMatrix(SlipVehicleViewer *, const SlipVehicleViewCameraCalls *);
uint32_t SlipVehicleView_ObjectEvent(uint32_t event, uint16_t object, const SlipVehicleViewCameraCalls *);
void SlipVehicleView_CameraPosition(const SlipVehicleViewer *, uint16_t object, const SlipVehicleViewCameraCalls *);
#endif
