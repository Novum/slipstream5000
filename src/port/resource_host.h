#ifndef SLIPSTREAM5000_RESOURCE_HOST_H
#define SLIPSTREAM5000_RESOURCE_HOST_H
#include "actor_pool.h"
#include "file_read.h"
#include "resource_access.h"
#include "resource_modify.h"

/* Native DPMI/file service bindings; regionBytes is the host memory budget. */
void SlipResourceHost_Initialize(uint32_t regionBytes);
struct SlipResourceMemoryServices;
/* Host binding selected before initialization; NULL restores native services. */
void SlipResourceHost_SetMemoryServices(const struct SlipResourceMemoryServices *services);
void SlipResourceHost_OpenArchives(const char *primary, const char *secondary);
bool SlipResourceHost_Load(void *, const char *, uint16_t *);
bool SlipResourceHost_LoadSequence(void *, const char *pattern, uint32_t first, uint16_t count, uint16_t *resources);
void SlipResourceHost_ReleaseSequence(void *, const uint16_t *resources, uint16_t count);
bool SlipResourceHost_Find(void *, const char *, uint16_t *);
bool SlipResourceHost_Resize(void *, uint16_t resource, uint32_t bytes);
bool SlipResourceHost_IsResident(void *, uint16_t resource);
bool SlipResourceHost_EnsureResident(void *, uint16_t resource);
void SlipResourceHost_VisitResident(uint32_t extension, SlipResourceResidentCallback);
const uint8_t *SlipResourceHost_Lock(void *, uint16_t);
uint8_t *SlipResourceHost_LockWritable(void *, uint16_t);
struct SlipSprite;
struct SlipSprite *SlipResourceHost_LockGeneratedSprite(void *, uint16_t);
SlipActorPoolStorage SlipResourceHost_LockActorPool(uint16_t resource, uint16_t actors, uint16_t parts);
union SlipDraw3DVertexRecord;
union SlipDraw3DVertexRecord *SlipResourceHost_LockVertices(void *, uint16_t resource);
uint16_t *SlipResourceHost_LockSpecular(void *, uint16_t resource);
struct SlipRendererPolygon;
struct SlipRendererDrawState;
struct RasterTexturedPoint;
struct SlipRendererPolygon *SlipResourceHost_LockPolygons(void *, uint16_t resource);
struct SlipRendererDrawState *SlipResourceHost_LockDrawStates(void *, uint16_t resource);
struct SlipDraw3DMaterialTable;
struct SlipDraw3DMaterialTable *SlipResourceHost_LockMaterials(void *, uint16_t resource);
struct RasterTexturedPoint *SlipResourceHost_LockPoints(void *, uint16_t resource);
SlipResourceBlock *SlipResourceHost_LockReserved(void *, uint16_t resource);
struct RasterPerspectiveEntry;
struct RasterPerspectiveEntry *SlipResourceHost_LockPerspectiveTable(void *, uint16_t resource);
bool SlipResourceHost_Allocate(void *, uint32_t bytes, uint32_t flags, uint16_t *handle);
bool SlipResourceHost_Size(void *, uint16_t handle, uint32_t *bytes);
void SlipResourceHost_Unlock(void *, uint16_t);
void SlipResourceHost_Release(void *, uint16_t);
bool SlipResourceHost_Copy(void *, uint16_t *);
SlipResourceModifyResult SlipResourceHost_Modify(void *, uint16_t);
uint16_t SlipResourceHost_ModifyReturnedSI(SlipResourceModifyResult);

void SlipResourceHost_ReportModifyContinuation(uint32_t callerReturn, uint32_t continuationAddress,
                                               const void *continuationBytes, SlipResourceModifyResult);
SlipResourcePayload SlipResourceHost_Payload(uint16_t);
extern const SlipFileReadCalls SlipResourceHost_fileCalls;
#endif
