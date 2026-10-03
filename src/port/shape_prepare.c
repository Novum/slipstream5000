#include "shape_prepare.h"
#include "byte_order.h"
#include "shape_format.h"

/* Serialized SHP header, primitive and material-list fields. */
static void SlipShape_StoreWord(uint8_t *bytes, uint16_t value) {
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

uint8_t *SlipDraw3D_NextPrimitive(uint8_t *primitive) {
	const uint16_t count = SlipBytes_ReadLE16(primitive);
	uint32_t bytes;
	if ((count & SLIP_PRIMITIVE_EXTENDED) != 0) {
		uint16_t stride = SLIP_SERIALIZED_INDEX_BYTES;
		if ((count & SLIP_PRIMITIVE_VERTEX_NORMALS) != 0)
			stride += SLIP_SERIALIZED_NORMAL_BYTES;
		if ((count & SLIP_PRIMITIVE_TEXTURE_COORDINATES) != 0)
			stride += SLIP_SERIALIZED_TEXTURE_COORDINATE_BYTES;
		bytes = (uint16_t)((count & SLIP_PRIMITIVE_VERTEX_COUNT_MASK) * stride);
	} else {
		bytes = (uint32_t)(count & SLIP_PRIMITIVE_VERTEX_COUNT_MASK) * SLIP_SERIALIZED_INDEX_BYTES;
	}
	return primitive + bytes + SLIP_PRIMITIVE_HEADER_BYTES;
}

uint8_t *SlipShape_NextPrimitive(uint8_t *primitive) { return SlipDraw3D_NextPrimitive(primitive); }

bool SlipShape_MaterialName(uint8_t *shape, uint16_t material, char **name) {
	const uint32_t materialOffset = SlipBytes_ReadLE32(shape + SLIP_SHAPE_MATERIAL_TABLE_OFFSET);
	if (materialOffset == 0)
		return false;
	uint8_t *entry = shape + materialOffset;
	const uint32_t materialCount = SlipBytes_ReadLE16(entry);
	if (materialCount == 0)
		return false;
	entry += SLIP_SHAPE_TABLE_COUNT_BYTES;
	for (uint32_t materialIndex = 0; materialIndex < materialCount; ++materialIndex) {
		if (SlipBytes_ReadLE16(entry + SLIP_SHAPE_MATERIAL_ID_OFFSET) == material) {
			for (unsigned i = 0; i < SLIP_SHAPE_MATERIAL_NAME_BYTES; ++i) {
				uint8_t character = entry[i];
				if (character >= 'a' && character <= 'z')
					character -= 'a' - 'A';
				entry[i] = character;
			}
			*name = (char *)entry;
			return true;
		}
		entry += SLIP_SHAPE_MATERIAL_ENTRY_BYTES;
	}
	return false;
}

void SlipShape_Prepare(SlipActorShapeState *state, uint8_t *shape, const SlipShapePrepareCalls *calls) {
	state->shape = shape;
	if (SlipBytes_ReadLE32(shape + SLIP_SHAPE_MATERIAL_TABLE_OFFSET) != 0 &&
	    SlipBytes_ReadLE32(shape + SLIP_SHAPE_PRIMITIVE_TABLE_OFFSET) != 0) {
		uint8_t *primitive = shape + SlipBytes_ReadLE32(shape + SLIP_SHAPE_PRIMITIVE_TABLE_OFFSET);
		uint32_t remaining = SlipBytes_ReadLE16(primitive);
		SlipShape_StoreWord(shape + SLIP_SHAPE_FLAGS_OFFSET,
		                    SlipBytes_ReadLE16(shape + SLIP_SHAPE_FLAGS_OFFSET) | SLIP_SHAPE_MATERIALS_PREPARED);
		primitive += SLIP_SHAPE_TABLE_COUNT_BYTES;
		do {
			uint16_t material = SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_MATERIAL_OFFSET);
			char *name;
			if (!SlipShape_MaterialName(shape, material, &name) ||
			    !calls->findMaterialByName(calls->context, name, &material))
				material = 0;
			SlipShape_StoreWord(primitive + SLIP_PRIMITIVE_MATERIAL_OFFSET, material);
			primitive = SlipShape_NextPrimitive(primitive);
		} while (--remaining != 0);
		uint8_t *entry = shape + SlipBytes_ReadLE32(shape + SLIP_SHAPE_MATERIAL_TABLE_OFFSET);
		remaining = SlipBytes_ReadLE16(entry);
		entry += SLIP_SHAPE_TABLE_COUNT_BYTES;
		do {
			uint16_t material;
			if (calls->findMaterialByName(calls->context, (const char *)entry, &material))
				SlipShape_StoreWord(entry + SLIP_SHAPE_MATERIAL_ID_OFFSET, material);
			entry += SLIP_SHAPE_MATERIAL_ENTRY_BYTES;
		} while (--remaining != 0);
	}
	SlipShape_StoreWord(shape + SLIP_SHAPE_FLAGS_OFFSET,
	                    SlipBytes_ReadLE16(shape + SLIP_SHAPE_FLAGS_OFFSET) | SLIP_SHAPE_MATERIALS_PREPARED);
}
