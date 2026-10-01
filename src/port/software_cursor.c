#include "software_cursor.h"

void SlipSoftwareCursor_Show(SlipSoftwareCursor *cursor, const SlipSoftwareCursorCalls *calls) {
	if (cursor->visible == 0) {
		uint8_t *const displayPage = calls->displayPage;
		calls->saveBackground(calls->context, displayPage);
		calls->draw(calls->context, displayPage);
		cursor->visible = 1;
	}
}

void SlipSoftwareCursor_Hide(SlipSoftwareCursor *cursor, const SlipSoftwareCursorCalls *calls) {
	if (cursor->visible != 0) {
		cursor->visible = 0;
		uint8_t *const displayPage = calls->displayPage;
		calls->restoreBackground(calls->context, displayPage);
	}
}

void SlipSoftwareCursor_Move(SlipSoftwareCursor *cursor, int16_t x, int16_t y, const SlipSoftwareCursorCalls *calls) {
	int16_t oldX = cursor->x, oldY = cursor->y;
	cursor->x = x;
	cursor->y = y;
	if (oldX != cursor->x || oldY != cursor->y) {
		if (cursor->visible != 0) {
			if (cursor->updatesSuspended == 0) {
				uint8_t *const displayPage = calls->displayPage;
				calls->restoreBackground(calls->context, displayPage);
				calls->saveBackground(calls->context, displayPage);
				calls->draw(calls->context, displayPage);
			}
		}
	}
}
