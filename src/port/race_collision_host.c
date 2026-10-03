#include "race_collision_host.h"
#include "race_collision.h"
#include "resource_host.h"
#include "resource_storage.h"
#include "runtime.h"

static uint16_t bodyResource;
static uint16_t collisionVertexResource;

enum { COLLISION_BODY_DOS_BYTES = 0x58, COLLISION_OBJECT_RELEASE_SERVER = SLIP_OBJECT_RELEASE_SERVER_ID };

void SlipRaceCollisionHost_Shutdown(void) {
	if (SlipRaceCollision_enabled != 0) {
		SlipRaceCollision_enabled = 0;
		SlipResourceHost_Unlock(NULL, bodyResource);
		SlipResourceHost_Release(NULL, bodyResource);
		SlipResourceHost_Unlock(NULL, collisionVertexResource);
		SlipResourceHost_Release(NULL, collisionVertexResource);
	}
}

static void SlipRaceCollisionHost_InitializeVertices(void) {
	if (!SlipResourceHost_Allocate(NULL, SLIP_COLLISION_VERTEX_DOS_BYTES * SLIP_COLLISION_VERTEX_COUNT, 0,
	                               &collisionVertexResource))
		return;
	(void)SlipResourceHost_Lock(NULL, collisionVertexResource);
	SlipRaceCollision_InitializeResourceVertices(
	    SlipResourceStorage_CollisionVertices(SlipResource_handles[collisionVertexResource].block));
}

bool SlipRaceCollisionHost_Initialize(uint16_t count) {
	if (SlipRaceCollision_enabled == UINT16_MAX)
		return true;
	SlipRaceCollision_enabled = UINT16_MAX;
	SlipRaceCollision_preStep = NULL;
	SlipRaceCollision_postStep = NULL;
	SlipRaceCollision_trackQuery = NULL;
	SlipRaceCollision_bodyCount = count;
	if (!SlipResourceHost_Allocate(NULL, (uint32_t)(uint16_t)(count + 2u) * COLLISION_BODY_DOS_BYTES, 0, &bodyResource))
		return false;
	SlipRaceCollision_physicsTable = (uint8_t *)SlipResourceHost_Lock(NULL, bodyResource);
	SlipRaceCollision_activeBodyOffset = 0;
	SlipRaceCollision_freeBodyOffset = COLLISION_BODY_DOS_BYTES;
	SlipRaceCollision_InitializeBodyLists();
	SlipRaceCollisionHost_InitializeVertices();
	SlipObject_SetServer(COLLISION_OBJECT_RELEASE_SERVER, SlipRaceCollision_RemoveBodyIfFlagged);
	SlipRaceCollision_preStep = NULL;
	SlipRaceCollision_postStep = NULL;
	SlipRaceCollision_trackQuery = NULL;
	SlipRaceCollision_segmentQuery = NULL;
	SlipRaceCollision_lineOfSight = NULL;
	SlipRuntime_RegisterExit(SlipRaceCollisionHost_Shutdown);
	return true;
}
