#include "material_frames.h"
#include <string.h>

void SlipMaterial_LoadFrames(SlipDraw3DMaterialTable *table, char name[SLIP_MATERIAL_FRAME_NAME_BYTES],
                             const SlipMaterialFrameCalls *calls) {
	if (table != NULL) {
		uint32_t remaining = table->count;
		SlipDraw3DMaterialRecord *record = table->records;
		do {
			for (unsigned i = 0; i < SLIP_DRAW3D_MATERIAL_FRAME_COUNT; ++i)
				record->textureHandles[i] = 0;
			if (record->textureName[0] != 0) {
				memcpy(name, record->textureName, sizeof(record->textureName));
				char *marker = name;
				while (*marker != 0 && *marker != '*')
					++marker;
				if (*marker == 0) {
					uint16_t resource;
					if (calls->find(calls->context, name, &resource))
						for (unsigned i = 0; i < SLIP_DRAW3D_MATERIAL_FRAME_COUNT; ++i)
							record->textureHandles[i] = resource;
				} else if (marker[1] == 0 || marker[1] == '.') {
					for (unsigned frame = 0; frame < SLIP_DRAW3D_MATERIAL_FRAME_COUNT; ++frame) {
						*marker = (char)('0' + frame);
						uint16_t resource;
						if (calls->find(calls->context, name, &resource))
							for (unsigned i = frame; i < SLIP_DRAW3D_MATERIAL_FRAME_COUNT; ++i)
								record->textureHandles[i] = resource;
					}

					char suffix[SLIP_MAT_TEXTURE_SUFFIX_BYTES] = {marker[1], marker[2], marker[3], marker[4]};
					for (unsigned i = 0; i < sizeof(suffix); ++i)
						marker[i] = suffix[i];
					marker[SLIP_MAT_TEXTURE_SUFFIX_BYTES] = 0;
					uint16_t resource;
					if (calls->find(calls->context, name, &resource))
						for (unsigned i = 0; i < SLIP_DRAW3D_MATERIAL_FRAME_COUNT; ++i)
							if (record->textureHandles[i] == 0)
								record->textureHandles[i] = resource;
				}
			}
			++record;
		} while (--remaining != 0);
	}
}

void SlipMaterial_ResetFrames(SlipDraw3DMaterialTable *table, char name[SLIP_MATERIAL_FRAME_NAME_BYTES],
                              const uint32_t *maximumFrame, const SlipMaterialFrameCalls *calls) {
	if (table != NULL) {
		uint32_t remaining = table->count;
		SlipDraw3DMaterialRecord *record = table->records;
		do {
			for (unsigned i = 0; i < SLIP_DRAW3D_MATERIAL_FRAME_COUNT; ++i)
				record->textureHandles[i] = 0;
			if (record->textureName[0] != 0) {
				memcpy(name, record->textureName, sizeof(record->textureName));
				char *marker = name;
				while (*marker != 0 && *marker != '*')
					++marker;
				if (*marker == 0) {
					uint16_t resource;
					if (calls->find(calls->context, name, &resource))
						for (unsigned i = 0; i < SLIP_DRAW3D_MATERIAL_FRAME_COUNT; ++i)
							record->textureHandles[i] = resource;
				} else if (marker[1] == 0 || marker[1] == '.') {
					for (unsigned frame = 0; frame < SLIP_DRAW3D_MATERIAL_FRAME_COUNT; ++frame) {
						if (frame > *maximumFrame)
							continue;
						*marker = (char)('0' + frame);
						uint16_t resource;
						if (calls->find(calls->context, name, &resource))
							for (unsigned i = frame; i < SLIP_DRAW3D_MATERIAL_FRAME_COUNT; ++i)
								record->textureHandles[i] = resource;
					}

					char suffix[SLIP_MAT_TEXTURE_SUFFIX_BYTES] = {marker[1], marker[2], marker[3], marker[4]};
					for (unsigned i = 0; i < sizeof(suffix); ++i)
						marker[i] = suffix[i];
					marker[SLIP_MAT_TEXTURE_SUFFIX_BYTES] = 0;
					uint16_t resource;
					if (calls->find(calls->context, name, &resource))
						for (unsigned i = 0; i < SLIP_DRAW3D_MATERIAL_FRAME_COUNT; ++i)
							if (record->textureHandles[i] == 0)
								record->textureHandles[i] = resource;
				}
			}
			++record;
		} while (--remaining != 0);
	}
}

uint32_t SlipMaterial_GetFrame(const SlipDraw3DMaterialTable *table, uint16_t loaded, uint32_t *materialIndex,
                               uint32_t frame) {
	if (loaded == 0)
		return 0;
	*materialIndex &= SLIP_DRAW3D_MATERIAL_INDEX_MASK;
	uint32_t recordIndex = 0;
	if (*materialIndex < (uint16_t)table->count)
		recordIndex = *materialIndex;
	return table->records[recordIndex].textureHandles[frame];
}

void SlipMaterial_SetFrame(SlipDraw3DMaterialTable *table, uint16_t loaded, uint32_t *materialIndex, uint32_t frame,
                           uint16_t handle) {
	if (loaded != 0) {
		*materialIndex &= SLIP_DRAW3D_MATERIAL_INDEX_MASK;
		uint32_t recordIndex = 0;
		if (*materialIndex < (uint16_t)table->count)
			recordIndex = *materialIndex;
		table->records[recordIndex].textureHandles[frame] = handle;
	}
}
