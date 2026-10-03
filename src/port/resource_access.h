#ifndef SLIPSTREAM5000_RESOURCE_ACCESS_H
#define SLIPSTREAM5000_RESOURCE_ACCESS_H
#include "resource.h"
#include "resource_names.h"

typedef struct SlipResourceAccessCalls {
	void *context;
	bool (*load)(void *, SlipResourceHandle *);
	bool (*fileSize)(void *, uint32_t nameOffset, uint32_t *size);

	void (*loadError)(void *, SlipResourceHandle *);
} SlipResourceAccessCalls;

bool SlipResource_EnsureResident(uint16_t handle, const SlipResourceAccessCalls *calls);
bool SlipResource_IsResident(uint16_t handle);
void SlipResource_Protect(uint16_t handle);
bool SlipResource_LoadNamedHandle(const char name[SLIP_RESOURCE_NAME_BYTES], uint16_t *handle,
                                  const SlipResourceNameCalls *names, const SlipResourceAccessCalls *access);

SlipResourceBlock *SlipResource_Lock(uint16_t handle, const SlipResourceAccessCalls *calls);
bool SlipResource_Size(uint16_t handle, uint32_t *size, const SlipResourceAccessCalls *calls);

typedef struct SlipResourceCopyCalls {
	void *context;
	bool (*load)(void *, SlipResourceHandle *);
	bool (*allocate)(void *, uint32_t bytes, uint32_t flags, uint32_t *handle);
	uint8_t *(*lock)(void *, uint16_t handle);
	void (*unlock)(void *, uint16_t handle);
	void (*loadError)(void *, SlipResourceHandle *);

	const uint8_t *(*payload)(void *, SlipResourceBlock *);
} SlipResourceCopyCalls;

extern uint32_t SlipResource_copiedHandle;

bool SlipResource_Copy(uint16_t source, uint16_t *destination, const SlipResourceCopyCalls *);
#endif
