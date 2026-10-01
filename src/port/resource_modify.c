#include "resource_modify.h"
#include "runtime.h"

SlipResourceModifyResult SlipResource_Modify(uint16_t source, const SlipResourceAccessCalls *access,
                                             const SlipResourceHandleCalls *handles) {
	SlipResourceHandle *const sourceRecord = &SlipResource_handles[source];
	for (;;) {
		SlipResourceBlock *const block = sourceRecord->block;
		if (block != NULL) {
			if (sourceRecord->nameOffset == UINT32_MAX)
				return (SlipResourceModifyResult){.exit = SLIP_RESOURCE_MODIFY_RECORD_POINTER, .record = sourceRecord};
			uint32_t destination;
			SlipResourceHandle *destinationRecord;
			if (!SlipResource_NewHandle(&destination, &destinationRecord, handles)) {
				SlipRuntime_error = 8;
				return (SlipResourceModifyResult){.exit = SLIP_RESOURCE_MODIFY_DISPLACED_RETURN,
				                                  .record = sourceRecord};
			}
			block->handleByteOffset =
			    (uint32_t)(destinationRecord - SlipResource_handles) * SLIP_RESOURCE_DOS_HANDLE_BYTES;
			block->flags &= (uint16_t)~SLIP_RESOURCE_BLOCK_RECLAIM_WHEN_UNLOCKED;
			destinationRecord->block = block;
			destinationRecord->nameOffset = UINT32_MAX;

			sourceRecord->block = NULL;
			return (SlipResourceModifyResult){.exit = SLIP_RESOURCE_MODIFY_HANDLE, .handle = destination};
		}
		if (!access->load(access->context, sourceRecord)) {
			if (access->loadError != NULL)
				access->loadError(access->context, sourceRecord);
			else
				SlipRuntime_Fatal("ResModify - failed because LoadFile was unable to purge sufficient memory");
		}
	}
}
