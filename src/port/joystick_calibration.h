#ifndef SLIPSTREAM5000_JOYSTICK_CALIBRATION_H
#define SLIPSTREAM5000_JOYSTICK_CALIBRATION_H
#include "config_controls.h"
typedef bool (*SlipJoystickRead)(void *, uint32_t joystick, uint16_t *x, uint16_t *y);
extern SlipJoystickCalibration SlipJoystick_calibration[2];
extern uint8_t SlipJoystick_present[2];
void SlipJoystick_BindHostRead(SlipJoystickRead read, void *context);
bool SlipJoystick_ReadHost(void *context, uint32_t joystick, uint16_t *x, uint16_t *y);

typedef enum SlipJoystickAxesExit {
	SLIP_JOYSTICK_AXES_RETURN,
	SLIP_JOYSTICK_AXES_DIVIDE_ZERO,
	SLIP_JOYSTICK_AXES_DIVIDE_OVERFLOW
} SlipJoystickAxesExit;

SlipJoystickAxesExit SlipJoystick_ReadAxes(uint32_t joystick, int32_t deadZone, int16_t *x, int16_t *y);
void SlipJoystick_ReportUnsupportedDivide(SlipJoystickAxesExit exit);
bool SlipJoystick_Calibrate(uint32_t operation, uint32_t joystick, SlipJoystickRead, void *);
void SlipJoystick_GetCalibration(uint32_t joystick, SlipJoystickCalibration *);
void SlipJoystick_SetCalibration(uint32_t joystick, const SlipJoystickCalibration *);
#endif
