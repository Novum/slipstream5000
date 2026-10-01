#include "race_recording_host.h"
#include "resource_host.h"
const SlipRaceRecordingResources SlipRaceRecordingHost_resources = {
    .allocate = SlipResourceHost_Allocate,
    .lock = SlipResourceHost_LockWritable,
    .unlock = SlipResourceHost_Unlock,
    .release = SlipResourceHost_Release,
};
