#include "renderer_allocation.h"
#include "resource_host.h"

const SlipRendererAllocationCalls SlipRendererHost_allocationCalls = {.allocate = SlipResourceHost_Allocate,
                                                                      .lockVertices = SlipResourceHost_LockVertices,
                                                                      .lockSpecular = SlipResourceHost_LockSpecular,
                                                                      .unlock = SlipResourceHost_Unlock,
                                                                      .release = SlipResourceHost_Release,
                                                                      .lockPolygons = SlipResourceHost_LockPolygons};
