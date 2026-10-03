#include "resource_access.h"
#include "runtime.h"
#include <string.h>

bool SlipResource_IsResident(uint16_t handle) { return SlipResource_handles[handle].block != NULL; }

void SlipResource_Protect(uint16_t handle) {
	SlipResourceBlock *const block = SlipResource_handles[handle].block;
	if (block != NULL)
		block->flags &= (uint16_t)~SLIP_RESOURCE_BLOCK_RECLAIM_WHEN_UNLOCKED;
}

bool SlipResource_LoadNamedHandle(const char name[SLIP_RESOURCE_NAME_BYTES], uint16_t *handle,
                                  const SlipResourceNameCalls *names, const SlipResourceAccessCalls *access) {
	if (!SlipResource_FindOrCreateName(name, handle, names))
		return false;
	return SlipResource_EnsureResident(*handle, access);
}

bool SlipResource_EnsureResident(uint16_t handle, const SlipResourceAccessCalls *calls) {
	SlipResourceHandle *const record = &SlipResource_handles[handle];
	if (record->block == NULL) {
		bool loaded = calls->load(calls->context, record);
		if (!loaded)
			SlipRuntime_error = SLIP_RUNTIME_ERROR_MEMORY_EXHAUSTED;
		return loaded;
	}
	SlipResource_ActivateResident(record);
	return true;
}

SlipResourceBlock *SlipResource_Lock(uint16_t handle, const SlipResourceAccessCalls *calls) {
	SlipResourceHandle *const record = &SlipResource_handles[handle];
	SlipResourceBlock *block = record->block;
	if (block == NULL) {
		if (!calls->load(calls->context, record)) {
			if (calls->loadError != NULL)
				calls->loadError(calls->context, record);
			else
				SlipRuntime_Fatal("ResOpen - failed because LoadFile was unable to purge sufficient memory");
		}
		block = record->block;
	} else {
		block = SlipResource_ActivateResident(record);
	}
	block->lockCount = (uint16_t)(block->lockCount + 1);
	return block;
}

bool SlipResource_Size(uint16_t handle, uint32_t *size, const SlipResourceAccessCalls *calls) {
	SlipResourceHandle *const record = &SlipResource_handles[handle];
	if (record->block == NULL)
		return calls->fileSize(calls->context, record->nameOffset, size);
	*size = record->block->requestedBytes;
	return true;
}

uint32_t SlipResource_copiedHandle;

bool SlipResource_Copy(uint16_t source, uint16_t *destination, const SlipResourceCopyCalls *calls) {
	SlipResourceHandle *const record = &SlipResource_handles[source];
	SlipResourceBlock *block = record->block;
	if (block == NULL) {
		if (!calls->load(calls->context, record)) {
			if (calls->loadError != NULL)
				calls->loadError(calls->context, record);
			else
				SlipRuntime_Fatal("ResCopy - failed because LoadFile was unable to purge sufficient memory");
		}
		block = record->block;
	} else
		block = SlipResource_ActivateResident(record);
	const uint8_t *const sourceBytes = calls->payload(calls->context, block);
	const uint32_t bytes = block->requestedBytes;
	uint32_t handle;
	if (!calls->allocate(calls->context, bytes, 0, &handle))
		return false;
	SlipResource_copiedHandle = handle;
	uint8_t *const destinationBytes = calls->lock(calls->context, (uint16_t)handle);
	memcpy(destinationBytes, sourceBytes, bytes);
	calls->unlock(calls->context, (uint16_t)SlipResource_copiedHandle);
	*destination = (uint16_t)SlipResource_copiedHandle;
	return true;
}
