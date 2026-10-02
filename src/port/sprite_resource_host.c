#include "sprite_resource_host.h"
#include "game_errors.h"
#include "raster/raster.h"
#include "resource_host.h"

static void SlipSpriteResourceHost_AllocationError(void *context) {
	(void)context;
	SlipGame_MemoryFailure();
}

static void SlipSpriteResourceHost_DrawScaled(void *context, uint16_t resource, int16_t left, int16_t top,
                                              int16_t right, int16_t bottom) {

	if (right < g_clipMinX || left > g_clipMaxX || top > g_clipMaxY || bottom < g_clipMinY)
		return;
	const uint8_t *const bytes = SlipResourceHost_Lock(context, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	Raster_DrawSpriteScaled(bytes, payload.size, bytes + 16, payload.size - 16, left, top, right, bottom);
	SlipResourceHost_Unlock(context, resource);
}

const SlipSpriteEffectResources SlipSpriteHost_effectResources = {
    .lock = SlipResourceHost_LockWritable,
    .unlock = SlipResourceHost_Unlock,
    .allocate = SlipResourceHost_Allocate,
    .allocationError = SlipSpriteResourceHost_AllocationError,
    .drawScaled = SlipSpriteResourceHost_DrawScaled,
};
