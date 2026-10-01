#ifndef SLIPSTREAM5000_RESOURCE_SETUP_H
#define SLIPSTREAM5000_RESOURCE_SETUP_H
#include "resource_names.h"
#include "runtime.h"

void SlipResource_InitializeHandles(const SlipResourceHandleCalls *calls);
void SlipResource_InitializeNames(const SlipResourceNameAllocationCalls *calls);

typedef struct SlipResourceRegion {
	SlipResourceBlock *block;
	uint32_t bytes;
} SlipResourceRegion;

typedef struct SlipResourceHeapCalls {
	void *context;
	void (*resetPlatform)(void *);
	uint32_t (*availableExtended)(void *);
	bool (*allocateExtended)(void *, uint32_t requested, SlipResourceRegion *);
	uint16_t (*availableConventional)(void *);
	bool (*allocateConventional)(void *, uint16_t paragraphs, SlipResourceRegion *);
} SlipResourceHeapCalls;

extern uint32_t SlipResource_allocationReserveBytes;
void SlipResource_InitializeHeap(const SlipResourceHeapCalls *calls);

typedef struct SlipResourceSetupServices {
	SlipResourceHeapCalls heap;
	SlipResourceHandleCalls handles;
	SlipResourceNameAllocationCalls names;
	void (*registerExit)(SlipRuntimeCleanup);
	void (*releasePlatform)(void *);
	void *releaseContext;
} SlipResourceSetupServices;

extern SlipResourceSetupServices SlipResource_setupServices;
extern uint32_t SlipResource_installed;
extern uint32_t SlipResource_reservedExtendedBytes;
extern uint16_t SlipResource_reservedConventionalParagraphs;

extern uint32_t SlipResource_unusedInstallationValue;
typedef void (*SlipResourceLoadErrorHandler)(SlipResourceHandle *);
extern SlipResourceLoadErrorHandler SlipResource_loadErrorHandler;
void SlipResource_SetLoadErrorHandler(SlipResourceLoadErrorHandler);
void SlipResource_Initialize(uint16_t handleCount, uint32_t reservedConventionalBytes, uint32_t reservedExtendedBytes,
                             uint32_t unusedValue);
void SlipResource_Shutdown(void);
void SlipResource_ClearHandleIndex(void);
void SlipResource_ReleaseHeap(void);
void SlipResource_SetReclaimEnabled(uint32_t enabled);
uint32_t SlipResource_GetReclaimEnabled(void);
#endif
