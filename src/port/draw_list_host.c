#include "draw_list_host.h"
#include "draw3d.h"
#include "resource_host.h"
#include "resource_storage.h"
#include "runtime.h"

static uint16_t listResource;

void SlipDrawListHost_Shutdown(void) {
	if (SlipDraw3D_listInitialized != 0) {
		SlipDraw3D_listInitialized = 0;
		SlipResourceHost_Unlock(NULL, listResource);
		SlipResourceHost_Release(NULL, listResource);
	}
}

void SlipDrawListHost_Initialize(uint16_t count) {
	SlipDrawListHost_Shutdown();
	++SlipDraw3D_listInitialized;
	SlipDraw3D_listState.capacity = count;
	SlipDraw3D_listState.remaining = count;
	if (!SlipResourceHost_Allocate(NULL, (uint32_t)count * SLIP_DRAW3D_LIST_NODE_SIZE, 0, &listResource))
		SlipRuntime_Fatal("Not enough memory for tree buffer.");
	(void)SlipResourceHost_Lock(NULL, listResource);
	SlipDraw3D_listPool = SlipResourceStorage_ListNodes(SlipResource_handles[listResource].block, count);
	SlipDraw3D_listState.baseOffset = 0;
	SlipDraw3D_listState.frameRootOffsets[0] = 0;
	SlipDraw3D_listState.currentOffset = 0;
	SlipRuntime_RegisterExit(SlipDrawListHost_Shutdown);
	SlipDraw3D_listState.frameDepth = UINT32_MAX;
}
