#ifndef SLIPSTREAM5000_VEHICLE_VIEW_CAMERA_HOST_H
#define SLIPSTREAM5000_VEHICLE_VIEW_CAMERA_HOST_H
#include "vehicle_view_camera.h"
/* Bind the original callees to the resident math tables and typed object pool.
 * The viewer must have initialized the pool before invoking these calls. */
SlipVehicleViewCameraCalls SlipVehicleViewCamera_NativeCalls(SlipView3DMaths *maths);
#endif
