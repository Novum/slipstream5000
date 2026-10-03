#include "track_world.h"

void SlipObject_DrawVisible(struct TrackViewRawBspContext *context) {
	SlipObject *object = SlipObject_table + 1;
	uint32_t remaining = (uint32_t)SlipObject_count - 1u;
	do {
		if (object->allocated != 0 && object->drawData != 0 && object->slotDrawCallback != NULL &&
		    (object->flags & SLIP_OBJECT_RENDER_HIDDEN) == 0) {
			const uint32_t objectOffset = (uint32_t)(object - SlipObject_table) * SLIP_OBJECT_DOS_STRIDE;
			(void)object->slotDrawCallback(context, objectOffset);
		}
		++object;
	} while (--remaining != 0);
}
