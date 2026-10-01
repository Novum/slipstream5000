#ifndef SLIPSTREAM5000_RESOURCE_ANONYMOUS_H
#define SLIPSTREAM5000_RESOURCE_ANONYMOUS_H
#include "resource_allocation.h"
#include "resource_handles.h"

bool SlipResource_AllocateAnonymous(uint32_t bytes, uint32_t flags, uint32_t *handle,
                                    const SlipResourceAllocationCalls *blocks, const SlipResourceHandleCalls *handles);
#endif
