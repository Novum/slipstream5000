#include "runtime.h"
#include "shape3d.h"

void SlipShape3D_InvalidateMaterials(SlipShape3DHeader *header) { header->flags &= 0xfffeu; }

void SlipShape3D_Loaded(SlipShape3DHeader *header) {
	header->flags &= 0xfffeu;
	if (header->version == 12) {
		SlipShape3D_ClearPrepared(header);
		return;
	}
	SlipRuntime_Fatal("ShapeLoad - Wrong ShapeVersion");
}

void SlipShape3D_ClearPrepared(SlipShape3DHeader *header) {
	if (header->version == 12)
		header->flags &= 0xfffeu;
}
