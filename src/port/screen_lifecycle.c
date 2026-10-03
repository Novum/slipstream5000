#include "screen_lifecycle.h"
#include <string.h>

enum {
	SLIP_SCREEN_PAGE_BYTES = SLIPSTREAM_SCREEN_WIDTH * SLIPSTREAM_SCREEN_HEIGHT,
	/* Preserve the original allocation, including unused trailing space. */
	SLIP_SCREEN_PERSPECTIVE_ALLOCATION_BYTES = 32768,
	SLIP_SCREEN_SIGNATURE = 0x5a4a
};

void SlipScreen_InstallPerspective(SlipScreenLifecycle *state, const SlipScreenLifecycleCalls *calls) {
	if (state->perspectiveHandle != 0)
		return;
	uint16_t handle;
	if (!calls->allocate(calls->context, SLIP_SCREEN_PERSPECTIVE_ALLOCATION_BYTES, 0, &handle)) {
		calls->fatal(calls->context, "TextMapTabInstall - out of memory");
		return;
	}
	state->perspectiveHandle = handle;
	state->perspectiveTable = calls->lockPerspective(calls->context, handle);
	Raster_BuildPerspectiveTable(state->perspectiveTable);
}

void SlipScreen_FreePerspective(SlipScreenLifecycle *state, const SlipScreenLifecycleCalls *calls) {
	if (state->perspectiveHandle == 0)
		return;
	calls->unlock(calls->context, state->perspectiveHandle);
	calls->release(calls->context, state->perspectiveHandle);
	state->perspectiveHandle = 0;
}

void SlipScreen_Cleanup(SlipScreenLifecycle *state, const SlipScreenLifecycleCalls *calls) {
	if (state->installed == 0)
		return;
	state->installed = 0;
	SlipScreen_FreePerspective(state, calls);
	calls->setBiosMode(calls->context, state->previousBiosMode);
	if (state->mode != SLIP_SCREEN_SINGLE_PAGE) {
		if (state->mode != SLIP_SCREEN_CHANGED_PAGES)
			return;
		calls->unlock(calls->context, state->previousHandle);
		calls->release(calls->context, state->previousHandle);
	}
	calls->unlock(calls->context, state->drawHandle);
	calls->release(calls->context, state->drawHandle);
}

static bool SlipScreen_BankedModeAvailable(void) { return false; }

void SlipScreen_Install(SlipScreenLifecycle *state, uint32_t mode, const SlipScreenLifecycleCalls *calls) {
	state->cursor.visible = 0;
	SlipScreen_Cleanup(state, calls);
	state->previousBiosMode = calls->getBiosMode(calls->context);
	if (mode == SLIP_SCREEN_AUTO) {
		(void)SlipScreen_BankedModeAvailable();
		mode = SLIP_SCREEN_CHANGED_PAGES;
	} else if (mode == SLIP_SCREEN_BANKED) {
		(void)SlipScreen_BankedModeAvailable();
		calls->fatal(calls->context, "VideoInstall: Invalid mode.");
		return;
	} else if (mode != SLIP_SCREEN_CHANGED_PAGES && mode != SLIP_SCREEN_SINGLE_PAGE) {
		calls->fatal(calls->context, "VideoInstall: Invalid mode.");
		return;
	}

	uint16_t handle;
	if (!calls->allocate(calls->context, SLIP_SCREEN_PAGE_BYTES, 0, &handle)) {
		calls->fatal(calls->context, "Video Error: Memory problem.");
		return;
	}
	state->drawHandle = handle;
	state->drawPage = calls->lockPixels(calls->context, handle);
	if (mode == SLIP_SCREEN_CHANGED_PAGES) {
		if (!calls->allocate(calls->context, SLIP_SCREEN_PAGE_BYTES, 0, &handle)) {
			calls->fatal(calls->context, "Video Error: Memory problem.");
			return;
		}
		state->previousHandle = handle;
		state->previousPage = calls->lockPixels(calls->context, handle);
		state->pitch = SLIPSTREAM_SCREEN_WIDTH;
		state->present = calls->presentChangedPages;
		calls->setBiosMode(calls->context, SLIP_SCREEN_BIOS_MODE_320X200_256_COLOURS);
		memset(state->drawPage, 0, SLIP_SCREEN_PAGE_BYTES);
		memset(state->previousPage, 0, SLIP_SCREEN_PAGE_BYTES);
		state->mode = SLIP_SCREEN_CHANGED_PAGES;
	} else {
		state->pitch = SLIPSTREAM_SCREEN_WIDTH;
		state->present = calls->presentSinglePage;
		calls->setBiosMode(calls->context, SLIP_SCREEN_BIOS_MODE_320X200_256_COLOURS);
		memset(state->drawPage, 0, SLIP_SCREEN_PAGE_BYTES);
		state->mode = SLIP_SCREEN_SINGLE_PAGE;
	}
	state->spriteTarget = false;
	state->transparentColor = UINT16_MAX;
	state->signature = SLIP_SCREEN_SIGNATURE;
	state->cursor.updatesSuspended = 0;
	calls->bindRows(calls->context, state->drawPage, 0, state->pitch);
	uint32_t offset = 0;
	for (unsigned row = 0; row < SLIPSTREAM_SCREEN_HEIGHT; ++row) {
		state->rowOffsets[row] = offset;
		offset += state->pitch;
	}
	calls->readDac(calls->context, state->palette);
	memset(state->dirtyColors, 0, sizeof(state->dirtyColors));

	state->clip = (SlipScreenClip){0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1};
	calls->setClip(calls->context, state->clip);
	calls->registerExit(calls->context, calls->cleanup);
	state->cursorClip = (SlipScreenClip){0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1};
	SlipScreen_InstallPerspective(state, calls);
	state->textureRowScroll = 0;
	state->installed = 1;
}
