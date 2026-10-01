#include "joystick_calibration.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

SlipJoystickCalibration SlipJoystick_calibration[2];
uint8_t SlipJoystick_present[2];
static SlipJoystickRead hostRead;
static void *hostReadContext;

void SlipJoystick_BindHostRead(SlipJoystickRead read, void *context) {
	hostRead = read;
	hostReadContext = context;
}

bool SlipJoystick_ReadHost(void *context, uint32_t joystick, uint16_t *x, uint16_t *y) {
	(void)context;
	return hostRead != NULL && hostRead(hostReadContext, joystick, x, y);
}

static SlipJoystickAxesExit SlipJoystick_NormalizeAxis(uint16_t value, uint16_t center, int16_t minimum,
                                                       int16_t maximum, int16_t *output) {
	int16_t delta = (int16_t)(uint16_t)(value - center);
	int16_t divisor;
	if (delta >= 0) {
		divisor = maximum;
		if (delta > divisor)
			delta = divisor;
	} else {
		divisor = minimum;
		if (delta < divisor)
			delta = divisor;
		divisor = (int16_t)(uint16_t)(0u - (uint16_t)divisor);
	}
	/* A malformed calibration raises guest #DE; do not invent neutral input. */
	if (divisor == 0)
		return SLIP_JOYSTICK_AXES_DIVIDE_ZERO;
	const int32_t quotient = ((int32_t)delta * SLIP_CONTROL_AXIS_LIMIT) / divisor;
	if (quotient < INT16_MIN || quotient > INT16_MAX)
		return SLIP_JOYSTICK_AXES_DIVIDE_OVERFLOW;
	*output = (int16_t)quotient;
	return SLIP_JOYSTICK_AXES_RETURN;
}

void SlipJoystick_ReportUnsupportedDivide(SlipJoystickAxesExit exit) {
	fprintf(stderr, "Host cannot execute DOS joystick #DE (%s) at 00020812/0002084b\n",
	        exit == SLIP_JOYSTICK_AXES_DIVIDE_ZERO ? "zero divisor" : "quotient overflow");
	abort();
}

SlipJoystickAxesExit SlipJoystick_ReadAxes(uint32_t joystick, int32_t deadZone, int16_t *x, int16_t *y) {
	uint16_t rawX, rawY;
	*x = *y = 0;
	if (SlipJoystick_present[joystick] == 0 || !SlipJoystick_ReadHost(NULL, joystick, &rawX, &rawY))
		return SLIP_JOYSTICK_AXES_RETURN;
	const SlipJoystickCalibration *const state = &SlipJoystick_calibration[joystick];
	SlipJoystickAxesExit exit = SlipJoystick_NormalizeAxis(rawY, state->centerY, state->minimumY, state->maximumY, y);
	if (exit != SLIP_JOYSTICK_AXES_RETURN)
		return exit;
	exit = SlipJoystick_NormalizeAxis(rawX, state->centerX, state->minimumX, state->maximumX, x);
	if (exit != SLIP_JOYSTICK_AXES_RETURN)
		return exit;
	if (deadZone != 0) {
		if ((int32_t)*x <= deadZone && (int32_t)*x >= -deadZone)
			*x = 0;
		if ((int32_t)*y <= deadZone && (int32_t)*y >= -deadZone)
			*y = 0;
	}
	return SLIP_JOYSTICK_AXES_RETURN;
}

bool SlipJoystick_Calibrate(uint32_t operation, uint32_t joystick, SlipJoystickRead read, void *context) {
	SlipJoystickCalibration *const state = &SlipJoystick_calibration[joystick];
	uint16_t x, y;
	switch ((uint16_t)operation) {
	case 0:
		if (!read(context, joystick, &x, &y))
			return false;
		state->centerX = x;
		state->centerY = y;
		state->maximumX = (int16_t)x;
		state->minimumX = (int16_t)(0u - x);
		state->maximumY = (int16_t)y;
		state->minimumY = (int16_t)(0u - y);
		return true;
	case 1:
		state->minimumX = 0x4000;
		state->maximumX = 0;
		state->minimumY = 0x4000;
		state->maximumY = 0;
		return true;
	case 2:
		if (!read(context, joystick, &x, &y))
			return false;
		if ((int16_t)x < state->minimumX)
			state->minimumX = (int16_t)x;
		if ((int16_t)x > state->maximumX)
			state->maximumX = (int16_t)x;
		if ((int16_t)y < state->minimumY)
			state->minimumY = (int16_t)y;
		if ((int16_t)y > state->maximumY)
			state->maximumY = (int16_t)y;
		return true;
	case 3:
		if (!read(context, joystick, &x, &y))
			return false;
		state->centerX = x;
		state->centerY = y;
		state->minimumX = (int16_t)((uint16_t)state->minimumX - x);
		state->maximumX = (int16_t)((uint16_t)state->maximumX - x);
		state->minimumY = (int16_t)((uint16_t)state->minimumY - y);
		state->maximumY = (int16_t)((uint16_t)state->maximumY - y);
		if (state->minimumX == 0 || state->maximumX == 0 || state->minimumY == 0 || state->maximumY == 0)
			return false;
		if (state->minimumX >= state->maximumX || state->minimumY >= state->maximumY)
			return false;
		return true;
	default:
		return false;
	}
}

void SlipJoystick_GetCalibration(uint32_t joystick, SlipJoystickCalibration *record) {
	*record = SlipJoystick_calibration[joystick];
}

void SlipJoystick_SetCalibration(uint32_t joystick, const SlipJoystickCalibration *record) {
	SlipJoystick_calibration[joystick] = *record;
}
