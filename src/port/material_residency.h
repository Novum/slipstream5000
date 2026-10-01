#ifndef SLIPSTREAM5000_MATERIAL_RESIDENCY_H
#define SLIPSTREAM5000_MATERIAL_RESIDENCY_H
#include "draw3d.h"

typedef struct SlipMaterialResidency {
	SlipDraw3DMaterialTable *table;
	uint32_t frame;
	uint32_t maximumTextureFrame;
	uint32_t memoryBudget;
	uint32_t maximumTextureSize;
	uint32_t scale;
	uint16_t selectedIndex;
	uint16_t resource;
} SlipMaterialResidency;

typedef struct SlipMaterialResidencyCalls {
	void *context;
	uint32_t (*getReclaim)(void *);
	void (*setReclaim)(void *, uint32_t enabled);
	void (*loadFrames)(void *);
	uint16_t (*frame)(void *, uint32_t *index);
	void (*setFrame)(void *, uint32_t index, uint16_t handle);
	bool (*resident)(void *, uint16_t handle);
	uint32_t (*resourceSize)(void *, uint16_t handle);
	uint32_t (*allocationSize)(void *, uint16_t handle);
	void (*release)(void *, uint16_t handle);
	uint32_t (*squareRoot)(void *, uint64_t value);
	void (*resetFrames)(void *);
	bool (*resize)(void *, uint16_t handle, uint32_t scale);
	void (*protect)(void *, uint16_t handle);
	void (*unlock)(void *, uint16_t handle);
} SlipMaterialResidencyCalls;

void SlipMaterial_ReleaseTextures(SlipMaterialResidency *, const SlipMaterialResidencyCalls *);
void SlipMaterial_Shutdown(SlipMaterialResidency *, const SlipMaterialResidencyCalls *);
void SlipMaterial_LargestResident(SlipMaterialResidency *, const SlipMaterialResidencyCalls *, uint16_t *handle,
                                  uint32_t *size);
uint16_t SlipMaterial_LargestUnloaded(SlipMaterialResidency *, const SlipMaterialResidencyCalls *);
void SlipMaterial_MakeResident(SlipMaterialResidency *, const SlipMaterialResidencyCalls *);
void SlipMaterial_SetLimits(SlipMaterialResidency *, uint32_t memoryBudget, uint32_t maximumTextureSize,
                            uint32_t maximumTextureFrame);
#endif
