#include "resource_load.h"
#include "runtime.h"

bool SlipResource_LoadRecord(SlipResourceHandle *record, const SlipResourceLoadCalls *calls) {
	const uint32_t nameOffset = record->nameOffset;
	if (nameOffset == UINT32_MAX)
		SlipRuntime_Fatal("LoadFile - this resource is NOT an external resource");
	uint32_t fileBytes;
	if (!calls->fileSize(calls->context, nameOffset, &fileBytes))
		SlipRuntime_Fatal("LoadFile - attempted to load a file which is missing");
	SlipResourceBlock *block;
	if (!calls->allocate(calls->context, fileBytes, &block))
		return false;
	block->flags |= SLIP_RESOURCE_BLOCK_MOVABLE;
	record->block = block;
	block->handleByteOffset = (uint32_t)(record - SlipResource_handles) * SLIP_RESOURCE_DOS_HANDLE_BYTES;
	block->flags |= SLIP_RESOURCE_BLOCK_RECLAIM_WHEN_UNLOCKED;
	SlipResource_MoveBlockToTail(block);
	if (!calls->readFile(calls->context, nameOffset, block))
		SlipRuntime_Fatal("LoadFile - attempted to load a file which is missing");
	calls->loaded(calls->context, record);
	return true;
}
