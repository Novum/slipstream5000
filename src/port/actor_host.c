#include "actor_host.h"
#include "actor_object_host.h"
#include "actor_resources.h"
#include "resource_host.h"
#include "runtime.h"
#include "track_world.h"

SlipActorPool SlipActorHost_pool;
SlipActorConstruction SlipActorHost_construction;

static SlipActorPoolCleanup exitCallback;
static SlipActorPoolEvent eventCallback;

static bool SlipActorHost_AllocatePool(void *context, uint32_t bytes, uint16_t flags, uint16_t *resource) {
	return SlipResourceHost_Allocate(context, bytes, flags, resource);
}

static SlipActorPoolStorage SlipActorHost_LockPool(void *context, uint16_t resource) {
	SlipActorPool *const pool = context;
	return SlipResourceHost_LockActorPool(resource, (uint16_t)pool->actorCount, pool->partCount);
}

static void SlipActorHost_ShutdownPool(void) { exitCallback(&SlipActorHost_pool, &SlipActorHost_poolCalls); }

static void SlipActorHost_RegisterExit(void *context, SlipActorPoolCleanup callback) {
	(void)context;
	exitCallback = callback;
	SlipRuntime_RegisterExit(SlipActorHost_ShutdownPool);
}

static uint32_t SlipActorHost_ObjectEvent(uint32_t events, uint32_t payload, uint32_t value, uint32_t flags,
                                          uint16_t object, uintptr_t data, uint32_t frame) {
	(void)payload;
	(void)value;
	(void)flags;
	(void)data;
	(void)frame;
	eventCallback(&SlipActorHost_pool, object, (uint16_t)events, &SlipActorHost_poolCalls);

	return events;
}

static void SlipActorHost_RegisterEvent(void *context, uint16_t event, SlipActorPoolEvent callback) {
	(void)context;
	eventCallback = callback;
	SlipObject_SetServer(event, SlipActorHost_ObjectEvent);
}

static void SlipActorHost_DestroyActor(void *context, uint16_t object) {
	SlipActor_Destroy(context, object, &SlipActorObject_access);
}

static uint32_t SlipActorHost_VectorLength(void *context, SlipView3DVec32 position) {
	(void)context;
	return SlipView3D_VectorLength(position.x, position.y, position.z);
}

const SlipActorPoolCalls SlipActorHost_poolCalls = {.context = &SlipActorHost_pool,
                                                    .allocate = SlipActorHost_AllocatePool,
                                                    .lock = SlipActorHost_LockPool,
                                                    .registerExit = SlipActorHost_RegisterExit,
                                                    .registerEvent = SlipActorHost_RegisterEvent,
                                                    .destroyObjectActor = SlipActorHost_DestroyActor,
                                                    .unlock = SlipResourceHost_Unlock,
                                                    .release = SlipResourceHost_Release};
const SlipActorConstructionCalls SlipActorHost_constructionCalls = {.lockResource = SlipResourceHost_Lock,
                                                                    .attachActor = SlipActorObject_Attach,
                                                                    .objectPosition = SlipActorObject_Position,
                                                                    .vectorLength = SlipActorHost_VectorLength,
                                                                    .findResourceByName = SlipResourceHost_Find,
                                                                    .unlockResource = SlipResourceHost_Unlock};

static int SlipActorHost_LoadResource(void *context, const char *name, uint32_t *handle) {
	uint16_t resource;
	bool loaded = SlipResourceHost_Load(context, name, &resource);
	if (loaded)
		*handle = resource;
	return loaded;
}

static int SlipActorHost_FindResource(void *context, const char *name, uint32_t *handle) {
	uint16_t resource;
	bool found = SlipResourceHost_Find(context, name, &resource);
	if (found)
		*handle = resource;
	return found;
}

static void SlipActorHost_ReleaseResource(void *context, uint32_t handle) {
	SlipResourceHost_Release(context, (uint16_t)handle);
}

const SlipActorResourceCalls SlipActorHost_resourceCalls = {.lock = SlipResourceHost_Lock,
                                                            .unlock = SlipResourceHost_Unlock,
                                                            .load = SlipActorHost_LoadResource,
                                                            .find = SlipActorHost_FindResource,
                                                            .release = SlipActorHost_ReleaseResource};
