#include "maths_host.h"
#include "byte_order.h"
#include "maths_lifecycle.h"
#include "resource_host.h"
#include "runtime.h"
static SlipMathsState maths;
static SlipMathsFloatingPointState floatingPoint;
static const SlipMathsCalls mathsCalls;
static SlipMathsCleanup mathsCleanup;
static uint16_t softwareControlWord;

static void SlipMathsHost_InitializeControl(void *context) {
	(void)context;
	softwareControlWord = SLIP_X87_INITIAL_CONTROL;
}

static uint16_t SlipMathsHost_ReadControl(void *context) {
	(void)context;
	return softwareControlWord;
}

static void SlipMathsHost_WriteControl(void *context, uint16_t control) {
	(void)context;
	softwareControlWord = control;
}

static bool SlipMathsHost_Present(void *context) {
	(void)context;
	/* The translated software arithmetic supplies the guest maths boundary. */
	return true;
}

static const SlipMathsFloatingPointCalls floatingPointCalls = {.present = SlipMathsHost_Present,
                                                               .initialize = SlipMathsHost_InitializeControl,
                                                               .readControl = SlipMathsHost_ReadControl,
                                                               .writeControl = SlipMathsHost_WriteControl};

static void SlipMathsHost_FloatingPoint(void *context, uint32_t enabled) {
	(void)context;
	SlipMaths_SetFloatingPoint(&floatingPoint, enabled, &floatingPointCalls);
}

static SlipView3DMaths SlipMathsHost_Lock(void *context, uint16_t handle) {
	const uint8_t *const data = SlipResourceHost_Lock(context, handle);
	SlipResourcePayload payload = SlipResourceHost_Payload(handle);
	return (SlipView3DMaths){.data = (uint8_t *)data,
	                         .size = payload.size,
	                         .sineTableOffset = SlipBytes_ReadLE16(data),
	                         .arcsineTableOffset = SlipBytes_ReadLE16(data + 2),
	                         .arctangentTableOffset = SlipBytes_ReadLE16(data + 4)};
}

static void SlipMathsHost_Close(void) { mathsCleanup(&maths, &mathsCalls); }

static void SlipMathsHost_RegisterExit(void *context, SlipMathsCleanup callback) {
	(void)context;
	mathsCleanup = callback;
	SlipRuntime_RegisterExit(SlipMathsHost_Close);
}

static const SlipMathsCalls mathsCalls = {.find = SlipResourceHost_Find,
                                          .lock = SlipMathsHost_Lock,
                                          .registerExit = SlipMathsHost_RegisterExit,
                                          .floatingPoint = SlipMathsHost_FloatingPoint,
                                          .unlock = SlipResourceHost_Unlock,
                                          .release = SlipResourceHost_Release};

bool SlipMathsHost_Initialize(void) { return SlipMaths_Initialize(&maths, &mathsCalls) == 0; }
