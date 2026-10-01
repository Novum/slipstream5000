#include "resource_host.h"
#include "texture_resize.h"

SlipTextureResizeState SlipTextureHost_resize;
const SlipTextureResizeCalls SlipTextureHost_resizeCalls = {.resident = SlipResourceHost_IsResident,
                                                            .load = SlipResourceHost_EnsureResident,
                                                            .lock = SlipResourceHost_LockWritable,
                                                            .unlock = SlipResourceHost_Unlock,
                                                            .resize = SlipResourceHost_Resize};
