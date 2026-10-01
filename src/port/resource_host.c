#include "resource_host.h"
#include "resource_anonymous.h"
#include "resource_load.h"
#include "resource_platform.h"
#include "resource_resize.h"
#include "resource_storage.h"
#include "runtime.h"
#include "shape3d.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <io.h>
#define host_open _open
#define host_close _close
#define host_seek _lseek
#define host_read _read
#define HOST_READ_FLAGS (_O_RDONLY | _O_BINARY)
#else
#include <unistd.h>
#define host_open open
#define host_close close
#define host_seek lseek
#define host_read read
#define HOST_READ_FLAGS O_RDONLY
#endif

static SlipResourceStorage regions[16];
static uint32_t availableBytes;
static const SlipResourceMemoryServices *memoryServicesOverride;

void SlipResourceHost_SetMemoryServices(const SlipResourceMemoryServices *services) {
	memoryServicesOverride = services;
}

static const SlipResourceHandleCalls handleCalls = {.allocate = SlipResourceStorage_HandleTable};
static const SlipResourceNameAllocationCalls nameStorageCalls = {.allocate = SlipResourceStorage_NameTable};

static bool SlipResourceHost_FileOpen(void *context, const char *path, int32_t *file) {
	(void)context;
	*file = host_open(path, HOST_READ_FLAGS);
	return *file >= 0;
}

static bool SlipResourceHost_FileClose(void *context, int32_t file) {
	(void)context;
	return host_close(file) == 0;
}

static bool SlipResourceHost_FileSize(void *context, int32_t file, uint32_t *size) {
	(void)context;
	const long result = host_seek(file, 0, SEEK_END);
	if (result < 0)
		return false;
	*size = (uint32_t)result;
	return true;
}

static bool SlipResourceHost_FileSeekEnd(void *context, int32_t file, int32_t offset) {
	(void)context;
	return host_seek(file, offset, SEEK_END) >= 0;
}

static bool SlipResourceHost_FileRead(void *context, int32_t file, int32_t offset, uint8_t *destination,
                                      uint32_t length) {
	(void)context;
	if (offset >= 0 && host_seek(file, offset, SEEK_SET) < 0)
		return false;
	const int result = host_read(file, destination, length);
	return result >= 0 && (uint32_t)result == length;
}

static const char *SlipResourceHost_NameAt(uint32_t offset) {
	const SlipResourceNameEntry *entry = SlipResource_nameTable;
	uint32_t position = 0;
	while (position != offset) {
		position += entry->byteSize;
		++entry;
	}
	return entry->name;
}

static const char *SlipResourceHost_ResidentName(void *context, uint32_t offset) {
	(void)context;
	return SlipResourceHost_NameAt(offset);
}

static uint8_t *SlipResourceHost_ResidentPayload(void *context, SlipResourceBlock *block) {
	(void)context;
	return SlipResourceStorage_Payload(block);
}

void SlipResourceHost_VisitResident(uint32_t extension, SlipResourceResidentCallback callback) {
	const SlipResourceResidentCalls calls = {.activeHandles = &SlipResource_activeHandles,
	                                         .name = SlipResourceHost_ResidentName,
	                                         .payload = SlipResourceHost_ResidentPayload};
	SlipResource_VisitResident(extension, callback, &calls);
	SlipResource_VisitResident(extension, callback, &SlipResource_cachedResidentCalls);
}

static bool SlipResourceHost_NamedSize(void *context, const char *name, uint32_t *size) {
	(void)context;
	return SlipFile_Size(name, size, &SlipResourceHost_fileCalls);
}

static bool SlipResourceHost_SizeAt(void *context, uint32_t offset, uint32_t *size) {
	return SlipResourceHost_NamedSize(context, SlipResourceHost_NameAt(offset), size);
}

static void SlipResourceHost_AddName(void *context, const char *name, uint16_t *handle) {
	(void)context;
	SlipResource_AddName(name, handle, &nameStorageCalls, &handleCalls);
}

static const SlipResourceNameCalls nameCalls = {.fileSize = SlipResourceHost_NamedSize,
                                                .add = SlipResourceHost_AddName};

static bool SlipResourceHost_AllocateBlock(void *context, uint32_t bytes, SlipResourceBlock **block) {
	(void)context;
	return SlipResource_Allocate(bytes, block, &SlipResourceStorage_allocationCalls);
}

static bool SlipResourceHost_ReadAt(void *context, uint32_t offset, SlipResourceBlock *block) {
	(void)context;
	return SlipFile_Read(SlipResourceHost_NameAt(offset), SlipResourceStorage_Payload(block),
	                     &SlipResourceHost_fileCalls);
}

static void SlipResourceHost_Loaded(void *context, SlipResourceHandle *record) {
	(void)context;
	SlipResource_DispatchLoaded(record, SlipResourceHost_NameAt(record->nameOffset),
	                            (SlipShape3DHeader *)SlipResourceStorage_Payload(record->block));
}

static const SlipResourceLoadCalls loadCalls = {.fileSize = SlipResourceHost_SizeAt,
                                                .allocate = SlipResourceHost_AllocateBlock,
                                                .readFile = SlipResourceHost_ReadAt,
                                                .loaded = SlipResourceHost_Loaded};

static bool SlipResourceHost_LoadRecord(void *context, SlipResourceHandle *record) {
	(void)context;
	return SlipResource_LoadRecord(record, &loadCalls);
}

static void SlipResourceHost_LoadError(void *context, SlipResourceHandle *record) {
	(void)context;
	SlipResource_loadErrorHandler(record);
}

static SlipResourceAccessCalls SlipResourceHost_AccessCalls(void) {
	return (SlipResourceAccessCalls){.load = SlipResourceHost_LoadRecord,
	                                 .fileSize = SlipResourceHost_SizeAt,
	                                 .loadError =
	                                     SlipResource_loadErrorHandler != NULL ? SlipResourceHost_LoadError : NULL};
}

bool SlipResourceHost_Load(void *context, const char *name, uint16_t *handle) {
	(void)context;
	char source[13] = {0};

	for (unsigned index = 0; index < 12 && name[index] != 0; ++index)
		source[index] = name[index];
	SlipResourceAccessCalls access = SlipResourceHost_AccessCalls();
	return SlipResource_LoadNamedHandle(source, handle, &nameCalls, &access);
}

bool SlipResourceHost_LoadSequence(void *context, const char *pattern, uint32_t first, uint16_t count,
                                   uint16_t *resources) {
	char filename[14];
	unsigned length = 0;
	while (*pattern != '*') {
		filename[length++] = *pattern++;
		if (length == 13)
			SlipRuntime_Fatal("RsrcFindIDs - could not find * character in search string");
	}
	const unsigned wildcard = length;
	uint16_t digits = 1;
	filename[length++] = *pattern++;
	while (*pattern == '*') {
		filename[length++] = *pattern++;
		++digits;
	}
	do {
		filename[length++] = *pattern;
	} while (*pattern++ != '\0');
	do {

		static const uint16_t divisors[] = {1, 10, 100, 1000};
		uint16_t value = (uint16_t)first;
		for (uint16_t digit = 0; digit < digits; ++digit) {
			const uint16_t divisor = divisors[digits - digit - 1];
			filename[wildcard + digit] = (char)(uint8_t)((value / divisor) + '0');
			value = (uint16_t)(value % divisor);
		}
		if (!SlipResourceHost_Load(context, filename, resources))
			return false;
		++resources;
		++first;
		count = (uint16_t)(count - 1u);
	} while (count != 0);
	return true;
}

void SlipResourceHost_ReleaseSequence(void *context, const uint16_t *resources, uint16_t count) {
	uint32_t remaining = count;
	do {
		SlipResourceHost_Release(context, *resources++);
		--remaining;
	} while (remaining != 0);
}

bool SlipResourceHost_Find(void *context, const char *name, uint16_t *handle) {
	(void)context;
	char source[13] = {0};

	for (unsigned index = 0; index < 12 && name[index] != 0; ++index)
		source[index] = name[index];
	return SlipResource_FindOrCreateName(source, handle, &nameCalls);
}

const uint8_t *SlipResourceHost_Lock(void *context, uint16_t handle) {
	(void)context;
	SlipResourceAccessCalls access = SlipResourceHost_AccessCalls();
	return SlipResourceStorage_Payload(SlipResource_Lock(handle, &access));
}

uint8_t *SlipResourceHost_LockWritable(void *context, uint16_t handle) {
	return (uint8_t *)SlipResourceHost_Lock(context, handle);
}

struct SlipSprite *SlipResourceHost_LockGeneratedSprite(void *context, uint16_t resource) {
	(void)context;
	SlipResourceAccessCalls access = SlipResourceHost_AccessCalls();
	SlipResourceBlock *const block = SlipResource_Lock(resource, &access);
	return SlipResourceStorage_GeneratedSprite(block);
}

SlipActorPoolStorage SlipResourceHost_LockActorPool(uint16_t resource, uint16_t actors, uint16_t parts) {
	SlipResourceAccessCalls access = SlipResourceHost_AccessCalls();
	SlipResourceBlock *const block = SlipResource_Lock(resource, &access);
	return SlipResourceStorage_ActorPool(block, actors, parts);
}

union SlipDraw3DVertexRecord *SlipResourceHost_LockVertices(void *context, uint16_t resource) {
	(void)context;
	SlipResourceAccessCalls access = SlipResourceHost_AccessCalls();
	SlipResourceBlock *const block = SlipResource_Lock(resource, &access);
	return SlipResourceStorage_Vertices(block, block->requestedBytes / 64u);
}

uint16_t *SlipResourceHost_LockSpecular(void *context, uint16_t resource) {
	(void)context;
	SlipResourceAccessCalls access = SlipResourceHost_AccessCalls();
	SlipResourceBlock *const block = SlipResource_Lock(resource, &access);
	return SlipResourceStorage_Specular(block, block->requestedBytes / 2u);
}

struct SlipRendererPolygon *SlipResourceHost_LockPolygons(void *context, uint16_t resource) {
	(void)context;
	SlipResourceAccessCalls access = SlipResourceHost_AccessCalls();
	SlipResourceBlock *const block = SlipResource_Lock(resource, &access);
	return SlipResourceStorage_Polygons(block, block->requestedBytes / 60u);
}

struct SlipRendererDrawState *SlipResourceHost_LockDrawStates(void *context, uint16_t resource) {
	(void)context;
	SlipResourceAccessCalls access = SlipResourceHost_AccessCalls();
	SlipResourceBlock *const block = SlipResource_Lock(resource, &access);
	return SlipResourceStorage_DrawStates(block, block->requestedBytes / 56u);
}

struct SlipDraw3DMaterialTable *SlipResourceHost_LockMaterials(void *context, uint16_t resource) {
	(void)context;
	SlipResourceAccessCalls access = SlipResourceHost_AccessCalls();
	SlipResourceBlock *const block = SlipResource_Lock(resource, &access);
	return SlipResourceStorage_Materials(block, (block->requestedBytes - 4u) / 84u);
}

struct RasterTexturedPoint *SlipResourceHost_LockPoints(void *context, uint16_t resource) {
	(void)context;
	SlipResourceAccessCalls access = SlipResourceHost_AccessCalls();
	SlipResourceBlock *const block = SlipResource_Lock(resource, &access);
	return SlipResourceStorage_Points(block, block->requestedBytes / 32u);
}

SlipResourceBlock *SlipResourceHost_LockReserved(void *context, uint16_t resource) {
	(void)context;
	SlipResourceAccessCalls access = SlipResourceHost_AccessCalls();
	return SlipResource_Lock(resource, &access);
}

bool SlipResourceHost_Size(void *context, uint16_t handle, uint32_t *bytes) {
	(void)context;
	SlipResourceAccessCalls access = SlipResourceHost_AccessCalls();
	return SlipResource_Size(handle, bytes, &access);
}

bool SlipResourceHost_IsResident(void *context, uint16_t resource) {
	(void)context;
	return SlipResource_IsResident(resource);
}

SlipResourceModifyResult SlipResourceHost_Modify(void *context, uint16_t source) {
	(void)context;
	SlipResourceAccessCalls access = SlipResourceHost_AccessCalls();
	return SlipResource_Modify(source, &access, &handleCalls);
}

uint16_t SlipResourceHost_ModifyReturnedSI(SlipResourceModifyResult result) {
	if (result.exit == SLIP_RESOURCE_MODIFY_HANDLE)
		return (uint16_t)result.handle;
	uint32_t tableAddress;
	if (!SlipResourceStorage_BlockLinearAddress(SlipResource_handleTableBlock, &tableAddress))
		tableAddress = (uint32_t)(uintptr_t)SlipResourceStorage_Payload(SlipResource_handleTableBlock) -
		               SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES;
	return (uint16_t)(tableAddress + SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES +
	                  (uint32_t)(result.record - SlipResource_handles) * SLIP_RESOURCE_DOS_HANDLE_BYTES);
}

void SlipResourceHost_ReportModifyContinuation(uint32_t callerReturn, uint32_t continuationAddress,
                                               const void *continuationBytes, SlipResourceModifyResult result) {

	fprintf(stderr,
	        "Unimplemented DOS execution: 249a0 displaced RET, saved EDI DOS offset/tag=0x%08x "
	        "(host bytes %p), normal return 0x%08x remains on stack, ESP delta -4, CF=1; "
	        "EAX<-saved EBX, EBX<-saved ECX, ECX<-saved EDX, EDX<-saved EBP, "
	        "EBP/ESI<-source record %p, EDI<-saved EAX.\n",
	        continuationAddress, continuationBytes, callerReturn, (void *)result.record);
}

bool SlipResourceHost_EnsureResident(void *context, uint16_t resource) {
	(void)context;
	SlipResourceAccessCalls access = SlipResourceHost_AccessCalls();
	return SlipResource_EnsureResident(resource, &access);
}

bool SlipResourceHost_Resize(void *context, uint16_t handle, uint32_t bytes) {
	(void)context;
	SlipResourceAccessCalls access = SlipResourceHost_AccessCalls();
	const SlipResourceResizeCalls calls = {.remainder = SlipResourceStorage_ResizeRemainder,
	                                       .activate = SlipResource_ActivateResident,
	                                       .coalesce = SlipResource_Coalesce};
	return SlipResource_Resize(handle, bytes, &access, &calls);
}

void SlipResourceHost_Unlock(void *context, uint16_t handle) {
	(void)context;
	SlipResource_Unlock(handle);
}

void SlipResourceHost_Release(void *context, uint16_t handle) {
	(void)context;
	SlipResource_ReleaseRecord(handle);
}

static bool SlipResourceHost_AllocateAnonymous(void *context, uint32_t bytes, uint32_t flags, uint32_t *handle) {
	(void)context;
	return SlipResource_AllocateAnonymous(bytes, flags, handle, &SlipResourceStorage_allocationCalls, &handleCalls);
}

bool SlipResourceHost_Allocate(void *context, uint32_t bytes, uint32_t flags, uint16_t *handle) {
	uint32_t value;
	bool success = SlipResourceHost_AllocateAnonymous(context, bytes, flags, &value);
	if (success)
		*handle = (uint16_t)value;
	return success;
}

static const uint8_t *SlipResourceHost_BlockPayload(void *context, SlipResourceBlock *block) {
	(void)context;
	return SlipResourceStorage_Payload(block);
}

bool SlipResourceHost_Copy(void *context, uint16_t *handle) {
	(void)context;
	SlipResourceCopyCalls calls = {.load = SlipResourceHost_LoadRecord,
	                               .allocate = SlipResourceHost_AllocateAnonymous,
	                               .lock = SlipResourceHost_LockWritable,
	                               .unlock = SlipResourceHost_Unlock,
	                               .payload = SlipResourceHost_BlockPayload,
	                               .loadError =
	                                   SlipResource_loadErrorHandler != NULL ? SlipResourceHost_LoadError : NULL};
	return SlipResource_Copy(*handle, handle, &calls);
}

SlipResourcePayload SlipResourceHost_Payload(uint16_t handle) {
	SlipResourceBlock *const block = SlipResource_handles[handle].block;
	uint8_t *const data = SlipResourceStorage_Payload(block);
	uint32_t address;
	if (SlipResourceStorage_BlockLinearAddress(block, &address))
		address += SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES;
	else
		address = (uint32_t)(uintptr_t)data;
	return (SlipResourcePayload){.data = data, .size = block->requestedBytes, .address = address};
}

struct RasterPerspectiveEntry *SlipResourceHost_LockPerspectiveTable(void *context, uint16_t handle) {
	return SlipResourceStorage_PerspectiveTable(SlipResourceHost_LockReserved(context, handle));
}

const SlipFileReadCalls SlipResourceHost_fileCalls = {.open = SlipResourceHost_FileOpen,
                                                      .close = SlipResourceHost_FileClose,
                                                      .size = SlipResourceHost_FileSize,
                                                      .read = SlipResourceHost_FileRead,
                                                      .seekEnd = SlipResourceHost_FileSeekEnd,
                                                      .allocate = SlipResourceHost_Allocate,
                                                      .lock = SlipResourceHost_LockWritable,
                                                      .unlock = SlipResourceHost_Unlock,
                                                      .release = SlipResourceHost_Release};

static bool SlipResourceHost_MemoryAvailable(void *context, uint32_t *bytes) {
	(void)context;
	*bytes = availableBytes;
	return true;
}

static bool SlipResourceHost_MemoryAllocate(void *context, uint32_t bytes, SlipResourceExtendedAllocation *allocation) {
	(void)context;
	if (bytes > availableBytes)
		return false;
	for (unsigned index = 0; index < 16; ++index) {
		if (regions[index].bytes == NULL) {
			uint8_t *const data = malloc(bytes);
			if (data == NULL)
				return false;
			SlipResourceStorage_Bind(&regions[index], data, bytes);
			allocation->block = SlipResourceStorage_BlockAt(&regions[index], 0);
			allocation->handle = index + 1;
			availableBytes -= bytes;
			return true;
		}
	}
	return false;
}

static bool SlipResourceHost_MemoryRelease(void *context, uint32_t handle) {
	(void)context;
	SlipResourceStorage *const region = &regions[handle - 1];
	SlipResourceStorage_ReleaseViews(region);
	free(region->bytes);
	availableBytes += region->size;
	*region = (SlipResourceStorage){0};
	return true;
}

static SlipResourceConventionalAllocation SlipResourceHost_ConventionalAllocate(void *context, uint16_t paragraphs) {
	(void)context;
	(void)paragraphs;
	/* The native process has no conventional-memory arena. */
	return (SlipResourceConventionalAllocation){.failed = true, .errorCode = 8, .availableParagraphs = 0};
}

static uint16_t SlipResourceHost_ConventionalRelease(void *context, uint16_t selector) {
	(void)context;
	(void)selector;
	return 9; /* No conventional allocation selectors exist in this host. */
}

static const SlipResourceMemoryServices memoryServices = {.queryLargestExtendedBlock = SlipResourceHost_MemoryAvailable,
                                                          .allocateExtended = SlipResourceHost_MemoryAllocate,
                                                          .freeExtended = SlipResourceHost_MemoryRelease,
                                                          .allocateConventional = SlipResourceHost_ConventionalAllocate,
                                                          .freeConventional = SlipResourceHost_ConventionalRelease};

void SlipResourceHost_Initialize(uint32_t regionBytes) {
	availableBytes = regionBytes;
	const SlipResourceMemoryServices *const services =
	    memoryServicesOverride ? memoryServicesOverride : &memoryServices;
	SlipResource_setupServices =
	    (SlipResourceSetupServices){.heap = {.context = (void *)services,
	                                         .resetPlatform = SlipResource_ResetPlatform,
	                                         .availableExtended = SlipResource_AvailableExtended,
	                                         .allocateExtended = SlipResource_AllocateExtended,
	                                         .availableConventional = SlipResource_AvailableConventional,
	                                         .allocateConventional = SlipResource_AllocateConventional},
	                                .handles = handleCalls,
	                                .names = nameStorageCalls,
	                                .registerExit = SlipRuntime_RegisterExit,
	                                .releasePlatform = SlipResource_FreePlatform,
	                                .releaseContext = (void *)services};

	SlipResource_Initialize(4000, 15000, 0, 0);
}

void SlipResourceHost_OpenArchives(const char *primary, const char *secondary) {
	if (!SlipArchive_Open(primary, &SlipFile_primaryArchive, &SlipResourceHost_fileCalls))
		SlipRuntime_Fatal("Could not open primary resource archive");
	if (secondary != NULL && !SlipArchive_Open(secondary, &SlipFile_secondaryArchive, &SlipResourceHost_fileCalls))
		SlipRuntime_Fatal("Could not open secondary resource archive");
}
