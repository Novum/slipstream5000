#include "vehicle_view_camera.h"
#include "frame_timer.h"

void SlipVehicleView_CameraMatrix(SlipVehicleViewer *viewer, const SlipVehicleViewCameraCalls *calls) {
	const int16_t step = (int16_t)SlipFrameTimer_Step();
	viewer->cameraStep = (int16_t)(((int32_t)0x4000 * step) >> 14);
	calls->yaw(calls->context, (int16_t)viewer->yaw, &viewer->cameraMatrix);
	calls->pitch(calls->context, viewer->parameters->pitch, &viewer->cameraMatrix);
	calls->normalize(calls->context, &viewer->cameraMatrix);
}

uint32_t SlipVehicleView_ObjectEvent(uint32_t event, uint16_t object, const SlipVehicleViewCameraCalls *calls) {
	if (event == 0x104) {
		const int16_t step = (int16_t)SlipFrameTimer_Step();
		const int16_t angle = (int16_t)(((int32_t)0x4000 * step) >> 14);
		calls->rotate(calls->context, object, 0, 0, angle, 0);
		return 0;
	}
	return event;
}

void SlipVehicleView_CameraPosition(const SlipVehicleViewer *viewer, uint16_t object,
                                    const SlipVehicleViewCameraCalls *calls) {
	SlipView3DVec32 position = calls->getPosition(calls->context, object);
	calls->matrix(calls->context, 0, &viewer->cameraMatrix);
	SlipView3DVec32 offset = {0, 0, (int32_t)(0u - viewer->parameters->distance)};
	offset = calls->transform(calls->context, &viewer->cameraMatrix, offset);
	offset.z = (int32_t)((uint32_t)offset.z + (uint32_t)position.z);
	offset.y = (int32_t)((uint32_t)offset.y + (uint32_t)position.y);
	offset.x = (int32_t)((uint32_t)offset.x + (uint32_t)position.x);
	calls->setPosition(calls->context, 0, offset);
}
