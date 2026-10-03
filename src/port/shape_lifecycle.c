#include "draw3d.h"
#include "resource_host.h"
#include "runtime.h"
#include "shape3d.h"
#include "shape_format.h"

static void SlipShape3D_InvalidateShape(const char *name, uint8_t *payload) {
	(void)name;
	SlipShape3D_InvalidateMaterials((SlipShape3DHeader *)payload);
}

void SlipShape3D_Initialize(void) {
	if (SlipShape3D_initialized == 0) {
		SlipShape3D_initialized = UINT32_MAX;
		SlipResource_RegisterCallback(SLIP_SHAPE_RESOURCE_EXTENSION_TAG, SlipShape3D_Loaded);
		SlipDraw3D_RegisterMaterialCallback(SlipShape3D_InvalidateResident);
		SlipRuntime_RegisterExit(SlipShape3D_Shutdown);
	}
}

void SlipShape3D_InvalidateResident(void) {
	if (SlipShape3D_initialized != 0)
		SlipResourceHost_VisitResident(SLIP_SHAPE_RESOURCE_EXTENSION_TAG, SlipShape3D_InvalidateShape);
}
