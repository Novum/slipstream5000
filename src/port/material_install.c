#include "material_install.h"
#include "byte_order.h"
#include "material_format.h"
#include "runtime.h"
#include <string.h>

/* Serialized MAT words, not native record fields. */
static const uint32_t SLIP_MATERIAL_COUNTER_UPPER_WORD_MASK = UINT32_MAX ^ UINT16_MAX;

static uint8_t SlipMaterial_Uppercase(uint8_t value) {
	if (value >= 'a' && value <= 'z')
		value -= 'a' - 'A';
	return value;
}

static void SlipMaterial_ExpandRecord(SlipDraw3DMaterialRecord *record, const uint8_t *source) {
	memcpy(record->name, source, SLIP_MAT_NAME_BYTES);
	record->textureTransparency = (int8_t)source[SLIP_MAT_TRANSPARENCY_OFFSET];
	record->skipFlatPolygon = (int8_t)source[SLIP_MAT_SKIP_FLAT_OFFSET];
	record->fixedShade = SlipBytes_ReadLE16(source + SLIP_MAT_FIXED_SHADE_OFFSET);
	record->ambientCoefficient = SlipBytes_ReadLE16(source + SLIP_MAT_AMBIENT_OFFSET);
	record->diffuseCoefficient = SlipBytes_ReadLE16(source + SLIP_MAT_DIFFUSE_OFFSET);
	record->specularCoefficient = SlipBytes_ReadLE16(source + SLIP_MAT_SPECULAR_OFFSET);
	record->vertexShading = (uint32_t)(int32_t)(int8_t)source[SLIP_MAT_VERTEX_SHADING_OFFSET];
	const uint32_t ditherBits = SlipBytes_ReadLE16(source + SLIP_MAT_DITHER_BITS_OFFSET);
	record->ditherBits = ditherBits;
	const uint32_t ditherMask = (1u << (ditherBits & SLIP_MAT_DITHER_SHIFT_MASK)) - 1u;
	const uint32_t first = source[SLIP_MAT_RAMP_START_OFFSET];
	uint32_t range = (uint32_t)source[SLIP_MAT_RAMP_END_OFFSET] - first;
	if (range > SLIP_MAT_MAXIMUM_RAMP_RANGE)
		range = SLIP_MAT_MAXIMUM_RAMP_RANGE;
	record->rampStart = first;
	record->rampEnd = range + first - ditherMask;
	record->importedMaterialByte = source[SLIP_MAT_IMPORTED_BYTE_OFFSET];
	for (unsigned i = 0; i < SLIP_MAT_NAME_BYTES; ++i)
		record->name[i] = (char)SlipMaterial_Uppercase((uint8_t)record->name[i]);
	memcpy(record->textureName, source + SLIP_MAT_TEXTURE_NAME_OFFSET, SLIP_MAT_TEXTURE_NAME_BYTES);
}

void SlipMaterial_Install(SlipMaterialInstallState *state, const uint8_t *asset,
                          const SlipMaterialInstallCalls *calls) {
	state->rampOverrideEnabled = 0;
	uint32_t count = SlipBytes_ReadLE16(asset);
	const uint16_t version = SlipBytes_ReadLE16(asset + SLIP_MAT_VERSION_OFFSET);
	const uint8_t *source = asset + SLIP_MAT_HEADER_BYTES;
	if (version != SLIP_MAT_VERSION)
		SlipRuntime_Fatal("Draw3DSetMaterials - material data wrong version");
	SlipDraw3DMaterialRecord *destination;
	if (state->materials->resource != 0) {
		state->incomingRecords = source;
		state->incomingMaterialCount = count;
		const uint32_t oldCount = state->materials->table->count;
		uint16_t temporaryResource;
		if (!calls->allocate(calls->context,
		                     oldCount * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE +
		                         SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES,
		                     0, &temporaryResource))
			SlipRuntime_Fatal("Draw3DSetMaterials - out of memory");
		SlipDraw3DMaterialTable *const temporary = calls->lock(calls->context, temporaryResource);
		temporary->count = state->materials->table->count;
		for (uint32_t i = 0; i < oldCount; ++i)
			temporary->records[i] = state->materials->table->records[i];
		const uint16_t oldResource = state->materials->resource;
		calls->unlock(calls->context, oldResource);
		calls->release(calls->context, oldResource);
		const uint32_t combinedCount = temporary->count + state->incomingMaterialCount;
		uint16_t resource = oldResource;
		(void)calls->allocate(calls->context,
		                      combinedCount * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE +
		                          SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES,
		                      0, &resource);
		state->materials->resource = resource;
		state->materials->table = calls->lock(calls->context, resource);
		state->materials->table->count = combinedCount;
		destination = state->materials->table->records;
		uint32_t remaining = temporary->count;
		const SlipDraw3DMaterialRecord *previous = temporary->records;
		do {
			*destination++ = *previous++;
		} while (--remaining != 0);
		calls->unlock(calls->context, temporaryResource);
		calls->release(calls->context, temporaryResource);
		count = state->incomingMaterialCount;
		source = state->incomingRecords;
	} else {
		state->incomingRecords = source;
		uint16_t resource;
		if (!calls->allocate(calls->context,
		                     count * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE +
		                         SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES,
		                     0, &resource))
			SlipRuntime_Fatal("Draw3DSetMaterials - out of memory");
		state->materials->resource = resource;
		state->materials->table = calls->lock(calls->context, resource);
		count = (uint16_t)count;
		state->materials->table->count = count;
		destination = state->materials->table->records;
		source = state->incomingRecords;
	}
	do {
		SlipMaterial_ExpandRecord(destination, source);
		++destination;
		source += SLIP_MAT_RECORD_BYTES;
		count = (count & SLIP_MATERIAL_COUNTER_UPPER_WORD_MASK) | (uint16_t)(count - 1u);
	} while ((uint16_t)count != 0);
	calls->notifyMaterialsChanged(calls->context);
	calls->loadTextureFrames(calls->context);
}
