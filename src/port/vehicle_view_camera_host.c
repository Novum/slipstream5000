#include "vehicle_view_camera_host.h"

static void SlipVehicleViewCamera_Yaw(void *context, int16_t angle, SlipView3DMatrix *matrix) {
	SlipView3D_BuildYawMatrix(context, angle, matrix);
}

static void SlipVehicleViewCamera_Pitch(void *context, int16_t angle, SlipView3DMatrix *matrix) {
	SlipView3D_ApplyPitchMatrix(context, angle, matrix);
}

static void SlipVehicleViewCamera_Normalize(void *context, SlipView3DMatrix *matrix) {
	(void)context;
	SlipView3D_OrthonormalizeForwardBasis(matrix);
}

static void SlipVehicleViewCamera_Rotate(void *context, uint16_t object, int16_t x, int16_t y, int16_t z,
                                         uint32_t flags) {
	SlipObjectRotate result;
	SlipObject_Rotate(SlipObject_table, (size_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE, object, x, y, z,
	                  (int16_t)flags, context, &result);
}

static SlipView3DVec32 SlipVehicleViewCamera_Position(void *context, uint16_t object) {
	(void)context;
	SlipObjectPosition result;
	SlipObject_Position(SlipObject_table, (size_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE, object, &result);
	return (SlipView3DVec32){(int32_t)result.positionX, (int32_t)result.positionY, (int32_t)result.positionZ};
}

static void SlipVehicleViewCamera_Matrix(void *context, uint16_t object, const SlipView3DMatrix *value) {
	(void)context;
	SlipObjectMatrixInstall result;
	SlipObject_MatrixInstall(SlipObject_table, (size_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE, object, value,
	                         &result);
}

static SlipView3DVec32 SlipVehicleViewCamera_Transform(void *context, const SlipView3DMatrix *matrix,
                                                       SlipView3DVec32 value) {
	(void)context;
	return SlipView3D_TransformPositionByColumns(matrix, value);
}

static void SlipVehicleViewCamera_SetPosition(void *context, uint16_t object, SlipView3DVec32 value) {
	(void)context;
	SlipObjectSetPosition result;
	SlipObject_SetPosition(SlipObject_table, (size_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE, object,
	                       (uint32_t)value.x, (uint32_t)value.y, (uint32_t)value.z, &result);
}

SlipVehicleViewCameraCalls SlipVehicleViewCamera_NativeCalls(SlipView3DMaths *maths) {
	return (SlipVehicleViewCameraCalls){maths,
	                                    SlipVehicleViewCamera_Yaw,
	                                    SlipVehicleViewCamera_Pitch,
	                                    SlipVehicleViewCamera_Normalize,
	                                    SlipVehicleViewCamera_Rotate,
	                                    SlipVehicleViewCamera_Position,
	                                    SlipVehicleViewCamera_Matrix,
	                                    SlipVehicleViewCamera_Transform,
	                                    SlipVehicleViewCamera_SetPosition};
}
