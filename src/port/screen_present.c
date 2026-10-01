#include "screen_present.h"
#include <string.h>

void SlipScreen_PresentSingle(uint8_t *drawPage, uint8_t *display, SlipSoftwareCursor *cursor, SlipCursorPixels *pixels,
                              const SlipScreenPresentCalls *calls) {
	calls->waitRetrace(calls->context);
	calls->commitPalette(calls->context);
	cursor->updatesSuspended = 1;
	if (cursor->visible != 0) {
		SlipCursor_SaveBackground(pixels, cursor, drawPage);
		SlipCursor_Draw(pixels, cursor, drawPage);
	}
	memcpy(display, drawPage, 64000);
	if (cursor->visible != 0) {
		SlipCursor_RestoreBackground(pixels, drawPage);
		SlipCursor_RestoreWidth(pixels);
	}
	cursor->updatesSuspended = 0;
}

void SlipScreen_CopyChanged(const uint8_t *current, const uint8_t *previous, uint8_t *display) {
	uint32_t remaining = 16000;
	const uint8_t *source = current;
	const uint8_t *comparison = previous;
	for (;;) {
		int equal;
		do {
			equal = memcmp(source, comparison, 4) == 0;
			source += 4;
			comparison += 4;
			--remaining;
		} while (remaining != 0 && equal);
		if (equal)
			return;
		const uint8_t *const changedStart = source;
		const uint32_t before = remaining;
		while (remaining != 0) {
			equal = memcmp(source, comparison, 4) == 0;
			source += 4;
			comparison += 4;
			--remaining;
			if (equal)
				break;
		}
		const uint32_t groups = before - remaining + 1;
		source = changedStart - 4;
		uint8_t *const destination = display + (source - current);
		memcpy(destination, source, groups * 4);
		source += groups * 4;
		if (remaining == 0)
			return;
		comparison = previous + (source - current);
	}
}

void SlipScreen_Present(uint8_t **drawPage, uint8_t **previousPage, uint8_t *display, SlipSoftwareCursor *cursor,
                        SlipCursorPixels *pixels, const SlipScreenPresentCalls *calls) {
	calls->waitRetrace(calls->context);
	calls->commitPalette(calls->context);
	uint8_t *const previous = *previousPage;
	*previousPage = *drawPage;
	*drawPage = previous;
	cursor->updatesSuspended = 1;
	SlipCursor_RestoreBackground(pixels, display);
	if (cursor->visible != 0) {
		SlipCursor_SaveBackground(pixels, cursor, *previousPage);
		SlipCursor_Draw(pixels, cursor, *previousPage);
	}
	SlipScreen_CopyChanged(*previousPage, *drawPage, display);
	SlipCursor_RestoreBackground(pixels, *previousPage);
	if (cursor->visible != 0)
		SlipCursor_RestoreWidth(pixels);
	cursor->updatesSuspended = 0;
}
