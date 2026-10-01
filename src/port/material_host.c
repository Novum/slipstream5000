#include "material_host.h"
#include "material_lookup.h"
#include "resource_host.h"
#include "resource_setup.h"
#include "shape_prepare.h"
#include "texture_resize.h"

/* Native identities for the original singleton material globals. */
SlipMaterialResidency SlipMaterialHost_residency;
SlipMaterialInstallState SlipMaterialHost_install = {.materials = &SlipMaterialHost_residency};
static char frameName[13];
static char resetFrameName[13];
static const SlipMaterialFrameCalls frameCalls = {.find = SlipResourceHost_Find};

/* Host ABI adapters only; the callers and resource policy remain translated. */
static void SlipMaterialHost_Notify(void *context) {
	(void)context;
	SlipDraw3D_NotifyMaterials();
}

static void SlipMaterialHost_Frames(void *context) {
	SlipMaterialInstallState *const state = context;
	SlipMaterial_LoadFrames(state->materials->table, frameName, &frameCalls);
}

const SlipMaterialInstallCalls SlipMaterialHost_installCalls = {.context = &SlipMaterialHost_install,
                                                                .allocate = SlipResourceHost_Allocate,
                                                                .lock = SlipResourceHost_LockMaterials,
                                                                .unlock = SlipResourceHost_Unlock,
                                                                .release = SlipResourceHost_Release,
                                                                .notifyMaterialsChanged = SlipMaterialHost_Notify,
                                                                .loadTextureFrames = SlipMaterialHost_Frames};

static uint32_t SlipMaterialHost_GetReclaim(void *context) {
	(void)context;
	return SlipResource_GetReclaimEnabled();
}

static void SlipMaterialHost_SetReclaim(void *context, uint32_t enabled) {
	(void)context;
	SlipResource_SetReclaimEnabled(enabled);
}

static void SlipMaterialHost_LoadFrames(void *context) {
	SlipMaterialResidency *const state = context;
	SlipMaterial_LoadFrames(state->table, frameName, &frameCalls);
}

static uint16_t SlipMaterialHost_Frame(void *context, uint32_t *index) {
	SlipMaterialResidency *const state = context;
	return (uint16_t)SlipMaterial_GetFrame(state->table, state->resource, index, state->frame);
}

static void SlipMaterialHost_SetFrame(void *context, uint32_t index, uint16_t resource) {
	SlipMaterialResidency *const state = context;
	SlipMaterial_SetFrame(state->table, state->resource, &index, state->frame, resource);
}

static uint32_t SlipMaterialHost_Size(void *context, uint16_t resource) {
	uint32_t bytes;
	(void)SlipResourceHost_Size(context, resource, &bytes);
	return bytes;
}

static uint32_t SlipMaterialHost_AllocationSize(void *context, uint16_t resource) {
	const uint32_t bytes = SlipMaterialHost_Size(context, resource);
	return ((bytes + 3u) & ~3u) + 0x20u;
}

static uint32_t SlipMaterialHost_Root(void *context, uint64_t value) {
	(void)context;
	return SlipDraw3D_Root64((uint32_t)value, (uint32_t)(value >> 32));
}

static void SlipMaterialHost_ResetFrames(void *context) {
	SlipMaterialResidency *const state = context;
	SlipMaterial_ResetFrames(state->table, resetFrameName, &state->maximumTextureFrame, &frameCalls);
}

static bool SlipMaterialHost_Resize(void *context, uint16_t resource, uint32_t scale) {
	(void)context;
	return SlipTexture_Resize(&SlipTextureHost_resize, resource, scale, &SlipTextureHost_resizeCalls);
}

static void SlipMaterialHost_Protect(void *context, uint16_t resource) {
	(void)context;
	SlipResource_Protect(resource);
}

const SlipMaterialResidencyCalls SlipMaterialHost_residencyCalls = {.context = &SlipMaterialHost_residency,
                                                                    .getReclaim = SlipMaterialHost_GetReclaim,
                                                                    .setReclaim = SlipMaterialHost_SetReclaim,
                                                                    .loadFrames = SlipMaterialHost_LoadFrames,
                                                                    .frame = SlipMaterialHost_Frame,
                                                                    .setFrame = SlipMaterialHost_SetFrame,
                                                                    .resident = SlipResourceHost_IsResident,
                                                                    .resourceSize = SlipMaterialHost_Size,
                                                                    .allocationSize = SlipMaterialHost_AllocationSize,
                                                                    .release = SlipResourceHost_Release,
                                                                    .squareRoot = SlipMaterialHost_Root,
                                                                    .resetFrames = SlipMaterialHost_ResetFrames,
                                                                    .resize = SlipMaterialHost_Resize,
                                                                    .protect = SlipMaterialHost_Protect,
                                                                    .unlock = SlipResourceHost_Unlock};

static char materialKey[16];

static bool SlipMaterialHost_FindMaterial(void *context, const char *name, uint16_t *index) {
	return SlipMaterial_Find(context, name, materialKey, index);
}

const SlipShapePrepareCalls SlipShapeHost_prepareCalls = {.context = &SlipMaterialHost_residency,
                                                          .findMaterialByName = SlipMaterialHost_FindMaterial};
