#ifndef SLIPSTREAM5000_RESOURCE_PLATFORM_H
#define SLIPSTREAM5000_RESOURCE_PLATFORM_H
#include "resource_setup.h"

enum { SLIP_RESOURCE_EXTENDED_REGION_CAPACITY = 16, SLIP_RESOURCE_CONVENTIONAL_REGION_CAPACITY = 16 };

/* DPMI allocation identifiers are opaque API tokens, not native addresses.
 * The platform binding supplies a separate typed view of each region header. */
typedef struct SlipResourceExtendedAllocation {
	SlipResourceBlock *block;
	uint32_t handle;
} SlipResourceExtendedAllocation;

typedef struct SlipResourceConventionalAllocation {
	bool failed;
	uint16_t errorCode;
	uint16_t availableParagraphs;
	uint16_t selector;
	SlipResourceBlock *block;
} SlipResourceConventionalAllocation;

typedef struct SlipResourceMemoryServices {
	void *context;
	bool (*queryLargestExtendedBlock)(void *, uint32_t *largestBlockBytes);
	bool (*allocateExtended)(void *, uint32_t bytes, SlipResourceExtendedAllocation *);
	bool (*freeExtended)(void *, uint32_t handle);
	SlipResourceConventionalAllocation (*allocateConventional)(void *, uint16_t paragraphs);
	uint16_t (*freeConventional)(void *, uint16_t selector);
} SlipResourceMemoryServices;

extern uint32_t SlipResource_extendedHandles[SLIP_RESOURCE_EXTENDED_REGION_CAPACITY];
extern uint16_t SlipResource_extendedCount;
extern uint16_t SlipResource_conventionalSelectors[SLIP_RESOURCE_CONVENTIONAL_REGION_CAPACITY];
extern uint16_t SlipResource_conventionalCount;
extern uint32_t SlipResource_allocationRetries;
extern uint32_t SlipResource_requestedAllocationBytes;

/* Context is a SlipResourceMemoryServices view. These signatures bind directly
 * to the typed installer/shutdown callbacks, without intermediate wrappers. */
void SlipResource_ResetPlatform(void *context);
uint32_t SlipResource_AvailableExtended(void *context);
bool SlipResource_AllocateExtended(void *context, uint32_t bytes, SlipResourceRegion *region);
uint16_t SlipResource_AvailableConventional(void *context);
bool SlipResource_AllocateConventional(void *context, uint16_t paragraphs, SlipResourceRegion *region);
void SlipResource_FreePlatform(void *context);
#endif
