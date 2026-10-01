#include "material_install.h"
#include "byte_order.h"
#include "runtime.h"
#include <string.h>

/* Serialized MAT words, not native record fields. */

static uint8_t SlipMaterial_Uppercase(uint8_t value) {
	if (value >= 'a' && value <= 'z')
		value -= 0x20;
	return value;
}

static void SlipMaterial_ExpandRecord(SlipDraw3DMaterialRecord *record, const uint8_t *source) {
	memcpy(record->name, source, 16);
	record->textureTransparency = (int8_t)source[0x14];
	record->skipFlatPolygon = (int8_t)source[0x15];
	record->fixedShade = SlipBytes_ReadLE16(source + 0x16);
	record->ambientCoefficient = SlipBytes_ReadLE16(source + 0x18);
	record->diffuseCoefficient = SlipBytes_ReadLE16(source + 0x1a);
	record->specularCoefficient = SlipBytes_ReadLE16(source + 0x1c);
	record->vertexShading = (uint32_t)(int32_t)(int8_t)source[0x13];
	const uint32_t ditherBits = SlipBytes_ReadLE16(source + 0x1e);
	record->ditherBits = ditherBits;
	const uint32_t ditherMask = (1u << (ditherBits & 31u)) - 1u;
	const uint32_t first = source[0x10];
	uint32_t range = (uint32_t)source[0x11] - first;
	if (range > 0x70)
		range = 0x70;
	record->rampStart = first;
	record->rampEnd = range + first - ditherMask;
	record->importedMaterialByte = source[0x12];
	for (unsigned i = 0; i < 16; ++i)
		record->name[i] = (char)SlipMaterial_Uppercase((uint8_t)record->name[i]);
	memcpy(record->textureName, source + 0x22, 12);
}

void SlipMaterial_Install(SlipMaterialInstallState *state, const uint8_t *asset,
                          const SlipMaterialInstallCalls *calls) {
	state->rampOverrideEnabled = 0;
	uint32_t count = SlipBytes_ReadLE16(asset);
	const uint16_t version = SlipBytes_ReadLE16(asset + 2);
	const uint8_t *source = asset + 4;
	if (version != 1)
		SlipRuntime_Fatal("Draw3DSetMaterials - material data wrong version");
	SlipDraw3DMaterialRecord *destination;
	if (state->materials->resource != 0) {
		state->incomingRecords = source;
		state->incomingMaterialCount = count;
		const uint32_t oldCount = state->materials->table->count;
		uint16_t temporaryResource;
		if (!calls->allocate(calls->context, oldCount * 84u + 4u, 0, &temporaryResource))
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
		(void)calls->allocate(calls->context, combinedCount * 84u + 4u, 0, &resource);
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
		if (!calls->allocate(calls->context, count * 84u + 4u, 0, &resource))
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
		source += 46;
		count = (count & 0xffff0000u) | (uint16_t)(count - 1u);
	} while ((uint16_t)count != 0);
	calls->notifyMaterialsChanged(calls->context);
	calls->loadTextureFrames(calls->context);
}
