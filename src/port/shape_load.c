#include "runtime.h"
#include "shape3d.h"
#include "shape_format.h"

enum { SLIP_SHAPE_CLEAR_PREPARED_MASK = UINT16_MAX ^ SLIP_SHAPE_MATERIALS_PREPARED };

void SlipShape3D_InvalidateMaterials(SlipShape3DHeader *header) { header->flags &= SLIP_SHAPE_CLEAR_PREPARED_MASK; }

void SlipShape3D_Loaded(SlipShape3DHeader *header) {
	header->flags &= SLIP_SHAPE_CLEAR_PREPARED_MASK;
	if (header->version == SLIP_SHAPE_VERSION) {
		SlipShape3D_ClearPrepared(header);
		return;
	}
	SlipRuntime_Fatal("ShapeLoad - Wrong ShapeVersion");
}

void SlipShape3D_ClearPrepared(SlipShape3DHeader *header) {
	if (header->version == SLIP_SHAPE_VERSION)
		header->flags &= SLIP_SHAPE_CLEAR_PREPARED_MASK;
}
