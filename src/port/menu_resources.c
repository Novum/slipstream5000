#include "menu_resources.h"

SlipMenuResources SlipMenu_resources;

// clang-format off
static const uint8_t menuCursorPixels[] = {
    0,   0,   1, 1,   1,   0, 0,
    0, 255,   0, 1,   0, 255, 0,
    1,   0, 255, 0, 255,   0, 1,
    1,   1,   0, 1,   0,   1, 1,
    1,   0, 255, 0, 255,   0, 1,
    0, 255,   0, 1,   0, 255, 0,
    0,   0,   1, 1,   1,   0, 0};
// clang-format on
const SlipCursorSprite SlipMenu_cursor = {7, 7, 1, 3, 3, menuCursorPixels};

void SlipMenuResources_Initialize(SlipMenuResources *state, const SlipMenuResourceCalls *calls) {
	uint16_t handle;
	if (!calls->load(calls->context, "SMALL.FNT", &handle)) {
		calls->resourceFailure(calls->context);
		return;
	}
	state->smallFont = handle;
	if (!calls->load(calls->context, "SHADE.FNT", &handle)) {
		calls->resourceFailure(calls->context);
		return;
	}
	state->shadedFont = handle;
	if (!calls->load(calls->context, "SMALLEST.FNT", &handle)) {
		calls->resourceFailure(calls->context);
		return;
	}
	state->smallestFont = handle;
	calls->selectCursor(calls->context, false, &SlipMenu_cursor);
	calls->setLoadError(calls->context, calls->memoryFailure);
	calls->setReclaim(calls->context, 1);
}
