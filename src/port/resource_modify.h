#ifndef SLIPSTREAM5000_RESOURCE_MODIFY_H
#define SLIPSTREAM5000_RESOURCE_MODIFY_H
#include "resource_access.h"
#include "resource_handles.h"

typedef enum SlipResourceModifyExit {
	SLIP_RESOURCE_MODIFY_HANDLE,
	SLIP_RESOURCE_MODIFY_RECORD_POINTER,
	SLIP_RESOURCE_MODIFY_DISPLACED_RETURN
} SlipResourceModifyExit;

typedef struct SlipResourceModifyResult {
	SlipResourceModifyExit exit;
	uint32_t handle;
	SlipResourceHandle *record;
} SlipResourceModifyResult;

SlipResourceModifyResult SlipResource_Modify(uint16_t source, const SlipResourceAccessCalls *access,
                                             const SlipResourceHandleCalls *handles);
#endif
