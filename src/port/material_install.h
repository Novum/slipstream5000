#ifndef SLIPSTREAM5000_MATERIAL_INSTALL_H
#define SLIPSTREAM5000_MATERIAL_INSTALL_H
#include "material_residency.h"

typedef struct SlipMaterialInstallState {
	SlipMaterialResidency *materials;
	uint32_t rampOverrideEnabled;
	const uint8_t *incomingRecords;
	uint32_t incomingMaterialCount;
} SlipMaterialInstallState;

typedef struct SlipMaterialInstallCalls {
	void *context;
	bool (*allocate)(void *, uint32_t bytes, uint32_t flags, uint16_t *resource);
	SlipDraw3DMaterialTable *(*lock)(void *, uint16_t resource);
	void (*unlock)(void *, uint16_t resource);
	void (*release)(void *, uint16_t resource);
	void (*notifyMaterialsChanged)(void *);
	void (*loadTextureFrames)(void *);
} SlipMaterialInstallCalls;

void SlipMaterial_Install(SlipMaterialInstallState *, const uint8_t *asset, const SlipMaterialInstallCalls *);
#endif
