#include "race_records_screen.h"
#include "frame_timer.h"
#include "raster/raster.h"

uint16_t SlipLapRecords_CreateRow(int16_t x, int16_t y, const SlipLapRecordsScreenCalls *calls) {
	uint16_t resource;
	if (!calls->allocate(calls->context, 0x2e10, 0, &resource))
		calls->resourceFailure(calls->context);
	SlipSprite *const sprite = calls->lockSprite(calls->context, resource);
	sprite->width = 256;
	sprite->height = 46;
	sprite->x = x;
	sprite->y = y;
	sprite->transparentColor = 0xffff;
	calls->resources.unlock(calls->resources.context, resource);
	return resource;
}

void SlipLapRecords_Initialize(SlipLapRecordsScreen *screen, SlipStringTableState *strings,
                               const SlipLapRecordsScreenCalls *calls) {
	void *const context = calls->context;
	const SlipStringTableResources *const resources = &calls->resources;
	calls->language(context);
	if (!SlipStringTable_Load(strings, "BESTDRV ", resources, &screen->strings))
		calls->resourceFailure(context);
	calls->renderer(context, 300, 0);
	calls->minimumDepth(context, 12);
	calls->maximumDepth(context, 0x7fffffff);
	calls->resetLighting(context);
	calls->ambient(context, 0x1800);
	calls->light(context, 9459, -9459, 9459, 0x2800); /* 24f3,db0d,24f3 */
	calls->depthFade(context, 0);
	calls->rendererFlags(context, 0);
	calls->camera(context, (SlipView3DVec32){0, 0, 0}, calls->cameraMatrix);
	calls->shapes(context);
	uint16_t materials;
	if (!resources->load(resources->context, "CARS.MAT", &materials))
		calls->resourceFailure(context);
	const uint8_t *const materialAsset = resources->lock(resources->context, materials);
	calls->materials(context, materialAsset);
	resources->unlock(resources->context, materials);
	resources->release(resources->context, materials);
	screen->trackIndex = 0;
	if (!resources->load(resources->context, "BESTBACK.SPR", &screen->background))
		calls->resourceFailure(context);
	calls->palette(context, screen->background);
	if (!resources->load(resources->context, "BESTDARK.SPR", &screen->inactive))
		calls->resourceFailure(context);
	if (!resources->load(resources->context, "BEST3DBK.SPR", &screen->shapeBackground))
		calls->resourceFailure(context);
	if (!calls->sequence(context, "BESTF*.SPR", 0, 10, screen->portraits))
		calls->resourceFailure(context);
	if (!resources->load(resources->context, "RESULTS.FNT", &screen->rowFont))
		calls->resourceFailure(context);
	if (!resources->load(resources->context, "BESTDRV.FNT", &screen->titleFont))
		calls->resourceFailure(context);
	screen->rows[0] = SlipLapRecords_CreateRow(32, 30, calls);
	screen->rows[1] = SlipLapRecords_CreateRow(32, 78, calls);
	screen->rows[2] = SlipLapRecords_CreateRow(32, 126, calls);
	if (!calls->sequence(context, "RACER*.SHP", 0, 10, screen->shapes))
		calls->resourceFailure(context);
	screen->animation.fade[0] = 0;
	screen->animation.fade[1] = -32768;
	screen->animation.fade[2] = -65536;
}

void SlipLapRecords_Close(SlipLapRecordsScreen *screen, const SlipLapRecordsScreenCalls *calls) {
	const SlipStringTableResources *const resources = &calls->resources;
	SlipStringTable_Release(screen->strings, resources);
	resources->release(resources->context, screen->background);
	resources->release(resources->context, screen->inactive);
	resources->release(resources->context, screen->shapeBackground);
	calls->releaseSequence(calls->context, screen->portraits, 10);
	resources->release(resources->context, screen->rowFont);
	resources->release(resources->context, screen->titleFont);
	resources->release(resources->context, screen->rows[0]);
	resources->release(resources->context, screen->rows[1]);
	resources->release(resources->context, screen->rows[2]);
	calls->releaseSequence(calls->context, screen->shapes, 10);
	calls->closeShapes(calls->context);
	calls->closeRenderer(calls->context);
}

void SlipLapRecords_EnterName(SlipLapRecordsScreen *screen, SlipLapRecordTable *records, SlipLapRecord *record,
                              SlipStringTableState *strings, const SlipLapRecordsScreenCalls *lifecycle,
                              const SlipLapRecordsFrameCalls *calls) {
	screen->editingRecord = record;
	const uint32_t trackIndex = screen->trackIndex;
	SlipLapRecords_Initialize(screen, strings, lifecycle);
	screen->trackIndex = trackIndex;
	RasterSurfaceBounds bounds = Raster_GetSurfaceBounds();
	Raster_SetClipRect((int16_t)bounds.left, (int16_t)bounds.top, (int16_t)bounds.right, (int16_t)bounds.bottom);
	SlipLapRecords_BeginNameInput(&screen->input);
	SlipInput_SetBiosMode(1);
	for (;;) {
		SlipFrameTimer_Update(calls->ticks(calls->context));
		SlipLapRecords_Animate(&screen->animation, calls->maths);
		SlipLapRecords_UpdateNameBlink(&screen->input, (uint16_t)SlipFrameTimer_Values().deltaMilliseconds);
		const uint8_t character = SlipInput_ReadCharacter(SlipInput_pressed, SlipInput_held, &calls->bios);
		if (SlipLapRecords_EditName(&screen->input, screen->editingRecord, character))
			break;
		calls->drawSprite(calls->context, screen->background, 0, 0);
		calls->font(calls->context, screen->titleFont);
		calls->textColorOrMode(calls->context, UINT16_MAX);

		const uint32_t titleTag = 0x42550031u | (uint32_t)(uint8_t)(screen->trackIndex + '0') << 8;
		calls->panel(calls->context, (SlipInputRectangle){40, 5, 278, 23}, screen->inactive, screen->strings, titleTag);
		for (unsigned row = 0; row < 3; ++row)
			calls->row(calls->context, screen, screen->rows[row], &records->tracks[screen->trackIndex][row]);
		bounds = Raster_GetSurfaceBounds();
		Raster_SetClipRect((int16_t)bounds.left, (int16_t)bounds.top, (int16_t)bounds.right, (int16_t)bounds.bottom);
		for (unsigned row = 0; row < 3; ++row) {
			int32_t fade = screen->animation.fade[row];
			if (fade < 0)
				fade = 0;
			calls->dissolve(calls->context, screen->rows[row], 0x7fff, (uint16_t)fade);
		}
		calls->present(calls->context);
		calls->poll(calls->context);
		if (SlipInput_TestAndClear(SlipInput_pressed, SLIP_INPUT_SCAN_ESCAPE))
			break;
	}
	SlipInput_ClearNavigation();
	SlipInput_SetBiosMode(0);
	SlipLapRecords_Close(screen, lifecycle);
}

static const SlipInputRectangle bestDriversButtons[] = {
    {40, 5, 278, 23}, {88, 177, 109, 195}, {206, 177, 227, 195}, {115, 177, 200, 195}};
static SlipInputNavigationTable bestDriversNavigation = {
    3, 1, {-1, -1, -1}, {-1, -1, -1}, {-1, 0, 1}, {1, 2, -1}, {{98, 186}, {157, 186}, {216, 186}}};

void SlipLapRecords_Show(SlipLapRecordsScreen *screen, SlipLapRecordTable *records, uint32_t flags,
                         SlipStringTableState *strings, const SlipLapRecordsScreenCalls *lifecycle,
                         const SlipLapRecordsFrameCalls *calls) {
	enum {
		DISPLAY_MILLISECONDS = 4000,
		TRACK_COUNT = 10,
		ROW_COUNT = 3,
		TITLE_BUTTON = 1,
		PREVIOUS_BUTTON = 2,
		NEXT_BUTTON = 3,
		EXIT_BUTTON = 4,
		ROW_FADE_DELAY = 32768,
		BUTTON_FIRST_TAG = 0x42555431,
		TRACK_TITLE_TAG_BASE = 0x42550031,
		TAG_CHARACTER_SHIFT = 8
	};

	SlipLapRecords_Initialize(screen, strings, lifecycle);
	RasterSurfaceBounds bounds = Raster_GetSurfaceBounds();
	Raster_SetClipRect((int16_t)bounds.left, (int16_t)bounds.top, (int16_t)bounds.right, (int16_t)bounds.bottom);
	SlipInput_SetNavigation(&bestDriversNavigation);
	uint32_t remaining = DISPLAY_MILLISECONDS;
	SlipFrameTimer_Reset();
	for (;;) {
		SlipFrameTimer_Update(calls->ticks(calls->context));
		if ((flags & SLIP_LAP_RECORDS_TIMED_DISPLAY) != 0) {
			remaining -= SlipFrameTimer_Values().deltaMilliseconds;
			if ((int32_t)remaining < 0)
				break;
		}
		SlipLapRecords_Animate(&screen->animation, calls->maths);
		SlipInputPointerPosition pointer = SlipInput_Pointer();
		uint32_t selected = SlipInput_HitTest(bestDriversButtons, EXIT_BUTTON, (int16_t)pointer.x, (int16_t)pointer.y);
		if (selected == TITLE_BUTTON)
			selected = 0;
		if (SlipInput_TestAndClear(SlipInput_pressed, SLIP_INPUT_SCAN_ENTER) ||
		    SlipInput_TestAndClear(SlipInput_pressed, SLIP_INPUT_MOUSE_LEFT)) {
			if (selected == EXIT_BUTTON)
				break;
			if (selected == NEXT_BUTTON) {
				screen->animation.fade[0] = 0;
				screen->animation.fade[1] = -ROW_FADE_DELAY;
				screen->animation.fade[2] = -2 * ROW_FADE_DELAY;
				++screen->trackIndex;
				if ((int32_t)screen->trackIndex >= TRACK_COUNT)
					screen->trackIndex = 0;
			} else if (selected == PREVIOUS_BUTTON) {
				screen->animation.fade[0] = 0;
				screen->animation.fade[1] = -ROW_FADE_DELAY;
				screen->animation.fade[2] = -2 * ROW_FADE_DELAY;
				--screen->trackIndex;
				if ((int32_t)screen->trackIndex < 0)
					screen->trackIndex = TRACK_COUNT - 1;
			}
		}
		calls->drawSprite(calls->context, screen->background, 0, 0);
		calls->font(calls->context, screen->titleFont);
		calls->textColorOrMode(calls->context, UINT16_MAX);
		for (unsigned button = 0; button < EXIT_BUTTON; ++button) {
			uint32_t tag = BUTTON_FIRST_TAG + button;
			if (button == 0)
				tag = TRACK_TITLE_TAG_BASE | (uint32_t)(uint8_t)(screen->trackIndex + '0') << TAG_CHARACTER_SHIFT;
			calls->panel(calls->context, bestDriversButtons[button],
			             selected == button + 1 ? screen->background : screen->inactive, screen->strings, tag);
		}
		for (unsigned row = 0; row < ROW_COUNT; ++row)
			calls->row(calls->context, screen, screen->rows[row], &records->tracks[screen->trackIndex][row]);
		bounds = Raster_GetSurfaceBounds();
		Raster_SetClipRect((int16_t)bounds.left, (int16_t)bounds.top, (int16_t)bounds.right, (int16_t)bounds.bottom);
		for (unsigned row = 0; row < ROW_COUNT; ++row) {
			int32_t fade = screen->animation.fade[row];
			if (fade < 0)
				fade = 0;
			calls->dissolve(calls->context, screen->rows[row], INT16_MAX, (uint16_t)fade);
		}
		calls->present(calls->context);
		calls->poll(calls->context);
		if (SlipInput_TestAndClear(SlipInput_pressed, SLIP_INPUT_SCAN_ESCAPE))
			break;
	}
	SlipInput_ClearNavigation();
	SlipLapRecords_Close(screen, lifecycle);
}
