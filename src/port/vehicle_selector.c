#include "vehicle_selector.h"
#include "frame_timer.h"
#include "raster.h"
#include "text_layout.h"
#include <string.h>

SlipInputNavigationTable SlipVehicleSelector_vehicleNavigation = {
    10,
    0,
    {5, 6, 3, 4, -1, 8, 9, 4, -1, -1},
    {-1, -1, -1, 2, 7, 0, 1, 2, 5, 6},
    {1, -1, 0, 7, 8, 6, -1, 5, 9, 6},
    {2, 0, -1, -1, -1, 7, 5, 3, 4, 8},
    {{152, 155}, {58, 143}, {263, 146}, {275, 98}, {230, 63}, {124, 98}, {36, 94}, {206, 96}, {152, 59}, {91, 67}}};
SlipInputNavigationTable SlipVehicleSelector_driverNavigation = {
    3, 0, {-1, 0, 1}, {1, 2, -1}, {-1, -1, -1}, {-1, -1, -1}, {{259, 152}, {259, 164}, {259, 176}}};
SlipVehicleSelector SlipVehicleSelector_state = {
    .actionRects = {{208, 138, 257, 147}, {208, 150, 257, 159}, {208, 162, 257, 171}, {69, 24, 113, 74}}};

SlipSelectorCardSetupResult SlipVehicleSelector_CardSetup(SlipVehicleSelector *selector,
                                                          const SlipVehicleSelectorCalls *calls,
                                                          SlipResourceModifyResult (*modify)(void *, uint16_t),
                                                          uint16_t (*modifiedHandle)(SlipResourceModifyResult)) {
	static const char *const speechNames[11] = {NULL,       "EM47.SMP", "EM50.SMP", "EM49.SMP", "EF45.SMP", "EM51.SMP",
	                                            "EM52.SMP", "EM48.SMP", "EF43.SMP", "EF44.SMP", "EM53.SMP"};
	static const SlipSelectorRectangle faceRects[10] = {
	    {96, 34, 140, 84},  {173, 41, 218, 84}, {89, 25, 131, 72},  {85, 45, 132, 85},  {112, 49, 146, 85},
	    {106, 28, 152, 81}, {98, 29, 147, 80},  {109, 29, 143, 61}, {136, 42, 175, 76}, {141, 34, 188, 76}};
	void *const context = calls->context;
	calls->clearNavigation(context);
	calls->musicBranch(context, selector->animation.selectedVehicle);
	char cardName[] = "DRIVER0.SPR", hoverName[] = "DRIVER0A.SPR";
	cardName[6] = hoverName[6] = (char)(uint8_t)(selector->animation.selectedVehicle + 0x2f);
	uint16_t card;
	if (!calls->resources.load(calls->resources.context, cardName, &card))
		calls->errors.resourceError(calls->errors.context);
	SlipResourceModifyResult modified = modify(context, card);
	if (modified.exit == SLIP_RESOURCE_MODIFY_DISPLACED_RETURN)
		return (SlipSelectorCardSetupResult){modified, 0x45d71, 0x465ea};
	card = modifiedHandle(modified);
	selector->card = card;
	selector->speech = 0;
	uint16_t speech;
	if (calls->resources.load(calls->resources.context, speechNames[selector->animation.selectedVehicle], &speech)) {
		selector->speechData = calls->lock(context, speech);
		selector->speech = speech;
	}
	for (unsigned i = 0; i < 3; ++i) {
		uint16_t hover;
		if (!calls->resources.load(calls->resources.context, hoverName, &hover))
			calls->errors.resourceError(calls->errors.context);
		modified = modify(context, hover);
		if (modified.exit == SLIP_RESOURCE_MODIFY_DISPLACED_RETURN)
			return (SlipSelectorCardSetupResult){modified, 0x45dd4, 0x4f505431 + i};
		hover = modifiedHandle(modified);
		selector->hover[i] = hover;
		const char *const label = SlipStringTable_Get(selector->assets.strings, 0x4f505431 + i, &calls->resources);
		calls->bakeText(context, selector->hover[i], label, 0xff, 1);
		SlipStringTable_Unlock(selector->assets.strings, &calls->resources);
		++hoverName[7];
	}
	SlipSelectorDimensions dimensions = calls->spriteDimensions(context, selector->card);
	calls->bindSprite(context, selector->card, dimensions);
	SlipText_SetColor(&SlipText_state, 0xff);
	for (unsigned i = 0; i < 3; ++i) {
		SlipSelectorRectangle *const rectangle = &selector->actionRects[i];
		SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, rectangle->left, rectangle->right);
		SlipTextPosition position = {rectangle->left, (int16_t)(rectangle->top + 1)};
		const char *const label = SlipStringTable_Get(selector->assets.strings, 0x4f505431 + i, &calls->resources);
		SlipText_Draw(&SlipText_state, label, NULL, &position);
		SlipStringTable_Unlock(selector->assets.strings, &calls->resources);
	}
	calls->restoreScreen(context);
	calls->spritePalette(context, selector->card);
	calls->spritePalette(context, selector->assets.driverBackground);
	SlipSelectorRectangle rectangle = selector->frameRects[selector->animation.selectedVehicle - 1];
	selector->zoomCenter.x = (int16_t)((int16_t)(rectangle.left + rectangle.right) >> 1);
	selector->zoomCenter.y = (int16_t)((int16_t)(rectangle.top + rectangle.bottom) >> 1);
	rectangle = faceRects[selector->animation.selectedVehicle - 1];
	selector->actionRects[3] =
	    (SlipSelectorRectangle){(int16_t)(rectangle.left - 27), (int16_t)(rectangle.top - 10),
	                            (int16_t)(rectangle.right - 27), (int16_t)(rectangle.bottom - 10)};
	return (SlipSelectorCardSetupResult){.modify = {.exit = SLIP_RESOURCE_MODIFY_HANDLE}};
}

void SlipVehicleSelector_CardRelease(SlipVehicleSelector *selector, const SlipVehicleSelectorCalls *calls) {
	calls->resources.release(calls->resources.context, selector->card);
	for (unsigned i = 0; i < 3; ++i)
		calls->resources.release(calls->resources.context, selector->hover[i]);
	if (selector->speech != 0) {
		calls->resources.unlock(calls->resources.context, selector->speech);
		calls->resources.release(calls->resources.context, selector->speech);
	}
}

void SlipVehicleSelector_Setup(SlipVehicleSelector *selector, uint16_t excludedVehicle, const uint16_t *demo,
                               uint16_t smallFont, uint16_t language, SlipStringTableState *strings,
                               const SlipVehicleSelectorCalls *calls) {
	void *const context = calls->context;
	selector->excludedVehicle = excludedVehicle;
	selector->demo = demo;
	selector->demoDelay = 1000;
	selector->demoPhase = 0;
	calls->selectFont(context, smallFont);
	SlipVehicleSelection_Setup(&selector->animation, &selector->assets, strings, language, &calls->resources,
	                           &calls->errors);
	char frameName[] = "CAR0_FR1.SPR";
	for (unsigned vehicle = 0; vehicle < 10; ++vehicle) {
		frameName[7] = '1';
		for (unsigned frame = 0; frame < 4; ++frame) {
			if (!calls->resources.load(calls->resources.context, frameName, &selector->frames[vehicle][frame]))
				calls->errors.resourceError(calls->errors.context);
			++frameName[7];
		}
		selector->frameRects[vehicle] = calls->spriteBounds(context, selector->frames[vehicle][3]);
		++frameName[3];
	}
	if (!calls->resources.load(calls->resources.context, "CH_TEAMZ.ZON", &selector->zones))
		calls->errors.resourceError(calls->errors.context);
	selector->playerMarker = 0;
	if (selector->excludedVehicle != 0) {
		char markerName[] = "CAR0P1.SPR";
		markerName[3] = (char)(uint8_t)(selector->excludedVehicle + 0x2f);
		if (!calls->resources.load(calls->resources.context, markerName, &selector->playerMarker))
			calls->errors.resourceError(calls->errors.context);
	}
	if (!calls->resources.load(calls->resources.context, "CH_TEAM.SPR", &selector->assets.background))
		calls->errors.resourceError(calls->errors.context);
	const uint8_t *const background = calls->lock(context, selector->assets.background);
	const uint32_t size = calls->resourceSize(context, selector->assets.background);
	if (!calls->allocate(context, size, 0, &selector->assets.driverBackground))
		calls->errors.resourceError(calls->errors.context);
	uint8_t *const copy = calls->lock(context, selector->assets.driverBackground);
	memcpy(copy, background, size);
	calls->resources.unlock(calls->resources.context, selector->assets.driverBackground);
	calls->resources.unlock(calls->resources.context, selector->assets.background);
	calls->grayscale(context, selector->assets.driverBackground);
	calls->spritePalette(context, selector->assets.background);
	selector->fade = 0;
	selector->confirmed = 0;
}

void SlipVehicleSelector_Release(SlipVehicleSelector *selector, const SlipVehicleSelectorCalls *calls) {
	void *const context = calls->context;
	if (selector->playerMarker != 0)
		calls->resources.release(calls->resources.context, selector->playerMarker);
	for (unsigned vehicle = 0; vehicle < 10; ++vehicle)
		for (unsigned frame = 0; frame < 4; ++frame)
			calls->resources.release(calls->resources.context, selector->frames[vehicle][frame]);
	calls->clearNavigation(context);
	calls->resources.release(calls->resources.context, selector->zones);
	SlipVehicleSelection_Release(&selector->assets, &calls->resources);
}

void SlipVehicleSelector_Draw(SlipVehicleSelector *selector, const SlipVehicleSelectorCalls *calls) {
	void *const context = calls->context;
	if (selector->animation.backgroundRedraws != 0) {
		--selector->animation.backgroundRedraws;
		calls->background(context, selector);
	}
	for (unsigned vehicle = 0; vehicle < 10; ++vehicle) {
		SlipVehicleDoor *const door = &selector->animation.doors[vehicle];
		if (door->redrawPasses != 0) {
			--door->redrawPasses;
			if (door->frame != 0)
				calls->drawSprite(context, selector->frames[vehicle][door->frame - 1], 0x7fff, 0);
			else {
				SlipSelectorRectangle saved;
				Raster_GetClipRect(&saved.left, &saved.top, &saved.right, &saved.bottom);
				SlipSelectorRectangle bounds = calls->spriteBounds(context, selector->frames[vehicle][0]);
				Raster_SetClipRect(bounds.left, bounds.top, bounds.right, bounds.bottom);
				calls->drawSprite(context, selector->assets.background, 0, 0);
				Raster_SetClipRect(saved.left, saved.top, saved.right, saved.bottom);
			}
		}
	}
}

uint16_t SlipVehicleSelector_Run(SlipVehicleSelector *selector, uint16_t excludedVehicle, const uint16_t *demo,
                                 uint16_t smallFont, uint16_t language, uint32_t gameMode,
                                 SlipStringTableState *strings, const SlipVehicleSelectorCalls *calls) {
	static const char *const speechNames[11] = {NULL,       "EM47.SMP", "EM50.SMP", "EM49.SMP", "EF45.SMP", "EM51.SMP",
	                                            "EM52.SMP", "EM48.SMP", "EF43.SMP", "EF44.SMP", "EM53.SMP"};
	static const SlipSelectorRectangle faceRects[10] = {
	    {96, 34, 140, 84},  {173, 41, 218, 84}, {89, 25, 131, 72},  {85, 45, 132, 85},  {112, 49, 146, 85},
	    {106, 28, 152, 81}, {98, 29, 147, 80},  {109, 29, 143, 61}, {136, 42, 175, 76}, {141, 34, 188, 76}};
	void *const context = calls->context;
	SlipVehicleSelector_Setup(selector, excludedVehicle, demo, smallFont, language, strings, calls);
	bool finished = false;
	do {
		selector->fadeTarget = 0;
		selector->animation.backgroundRedraws = 2;
		selector->animation.doorElapsed = 0;
		SlipFrameTimer_Reset();
		calls->setNavigation(context, &SlipVehicleSelector_vehicleNavigation);
		bool cancelled = false;
		do {
			SlipFrameTimer_Update(calls->frameClockMilliseconds(context));
			calls->poll(context);
			if (calls->pressed(context, SLIP_INPUT_SCAN_ESCAPE)) {
				selector->animation.selectedVehicle = 0;
				cancelled = true;
				break;
			}
			SlipVehicleSelection_Update(&selector->animation, gameMode, selector->excludedVehicle);
			if (!selector->confirmed) {
				bool readSelection = false;
				uint16_t selection = 0;
				if (selector->demo != NULL) {
					if (selector->demoPhase == 0) {
						selector->animation.selectedVehicle = 0;
						selector->demoDelay =
						    (uint16_t)(selector->demoDelay - (uint16_t)SlipFrameTimer_Values().deltaMilliseconds);
						if ((int16_t)selector->demoDelay < 0) {
							selector->demoDelay = 1000;
							selector->demoPhase = 1;
							selection = *selector->demo;
							readSelection = true;
						}
					}
				} else {
					selection = calls->zone(context, selector->zones);
					readSelection = true;
				}
				if (readSelection)
					selector->animation.selectedVehicle = selection == selector->excludedVehicle ? 0 : selection;
			}
			SlipVehicleSelector_Draw(selector, calls);
			if (selector->fadeTarget != selector->fade) {
				/* SPR and palette packet fields below are serialized resource data. */
				const uint8_t *const sprite = calls->lock(context, selector->assets.background);
				const uint8_t *const palette = sprite + (uint16_t)(sprite[14] | (uint16_t)sprite[15] << 8);
				const uint16_t count = (uint16_t)(palette[2] | (uint16_t)palette[3] << 8);
				const uint16_t bytes = (uint16_t)(count * 3 + 4);
				uint32_t remaining = bytes;
				uint32_t position = 0;
				do {
					selector->sourcePalette[position] = palette[position];
					selector->grayPalette[position] = palette[position];
					++position;
				} while (--remaining != 0);
				calls->resources.unlock(calls->resources.context, selector->assets.background);
				remaining = (uint16_t)(selector->grayPalette[2] | (uint16_t)selector->grayPalette[3] << 8);
				uint8_t *rgb = selector->grayPalette + 4;
				do {
					uint8_t gray = (uint8_t)(((uint16_t)rgb[0] * 0x55) >> 8);
					gray = (uint8_t)(gray + (((uint16_t)rgb[1] * 0x55) >> 8));
					gray = (uint8_t)(gray + (((uint16_t)rgb[2] * 0x55) >> 8));
					rgb[0] = rgb[1] = rgb[2] = gray;
					rgb += 3;
				} while (--remaining != 0);
				const uint16_t blended =
				    calls->blendPalette(context, selector->sourcePalette, selector->grayPalette, selector->fade);
				const uint8_t *const blendedData = calls->lock(context, blended);
				calls->palette(context, blendedData);
				calls->resources.unlock(calls->resources.context, blended);
				calls->resources.release(calls->resources.context, blended);
				const uint16_t step = (uint16_t)(((uint32_t)(uint16_t)SlipFrameTimer_Values().stepQ14 * 0x8000) >> 14);
				if (selector->fadeTarget < selector->fade) {
					selector->fade = (int16_t)((uint16_t)selector->fade - step);
					if (selector->fade < selector->fadeTarget)
						selector->fade = selector->fadeTarget;
				} else if (selector->fadeTarget > selector->fade) {
					selector->fade = (int16_t)((uint16_t)selector->fade + step);
					if (selector->fade > selector->fadeTarget)
						selector->fade = selector->fadeTarget;
				}
			}
			calls->present(context);
			bool accept = false;
			if (selector->demo != NULL) {
				if (selector->demoPhase == 1) {
					selector->demoDelay =
					    (uint16_t)(selector->demoDelay - (uint16_t)SlipFrameTimer_Values().deltaMilliseconds);
					if ((int16_t)selector->demoDelay < 0) {
						selector->demoDelay = 1000;
						selector->demoPhase = 0;
						accept = true;
					}
				}
			} else if (calls->pressed(context, SLIP_INPUT_SCAN_ENTER))
				accept = true;
			else
				accept = calls->pressed(context, SLIP_INPUT_MOUSE_LEFT);
			if (accept && !selector->confirmed && selector->animation.selectedVehicle != 0) {
				selector->confirmed = 1;
				selector->fadeTarget = 0x4000;
			}
		} while (!selector->confirmed || selector->fade != selector->fadeTarget);
		if (cancelled)
			break;
		calls->clearNavigation(context);
		calls->musicBranch(context, selector->animation.selectedVehicle);
		char cardName[] = "DRIVER0.SPR", hoverName[] = "DRIVER0A.SPR";
		cardName[6] = hoverName[6] = (char)(uint8_t)(selector->animation.selectedVehicle + 0x2f);
		uint16_t card;
		if (!calls->resources.load(calls->resources.context, cardName, &card))
			calls->errors.resourceError(calls->errors.context);
		if (!calls->modify(context, &card))
			calls->errors.resourceError(calls->errors.context);
		selector->card = card;
		selector->speech = 0;
		uint16_t speech;
		if (calls->resources.load(calls->resources.context, speechNames[selector->animation.selectedVehicle],
		                          &speech)) {
			selector->speechData = calls->lock(context, speech);
			selector->speech = speech;
		}
		for (unsigned i = 0; i < 3; ++i) {
			uint16_t hover;
			if (!calls->resources.load(calls->resources.context, hoverName, &hover))
				calls->errors.resourceError(calls->errors.context);
			if (!calls->modify(context, &hover))
				calls->errors.resourceError(calls->errors.context);
			selector->hover[i] = hover;
			const char *const label = SlipStringTable_Get(selector->assets.strings, 0x4f505431 + i, &calls->resources);
			calls->bakeText(context, selector->hover[i], label, 0xff, 1);
			SlipStringTable_Unlock(selector->assets.strings, &calls->resources);
			++hoverName[7];
		}
		SlipSelectorDimensions dimensions = calls->spriteDimensions(context, selector->card);
		calls->bindSprite(context, selector->card, dimensions);
		SlipText_SetColor(&SlipText_state, 0xff);
		for (unsigned i = 0; i < 3; ++i) {
			SlipSelectorRectangle *const rectangle = &selector->actionRects[i];
			SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, rectangle->left, rectangle->right);
			SlipTextPosition position = {rectangle->left, (int16_t)(rectangle->top + 1)};
			const char *const label = SlipStringTable_Get(selector->assets.strings, 0x4f505431 + i, &calls->resources);
			SlipText_Draw(&SlipText_state, label, NULL, &position);
			SlipStringTable_Unlock(selector->assets.strings, &calls->resources);
		}
		calls->restoreScreen(context);
		calls->spritePalette(context, selector->card);
		calls->spritePalette(context, selector->assets.driverBackground);
		SlipSelectorRectangle rectangle = selector->frameRects[selector->animation.selectedVehicle - 1];
		selector->zoomCenter.x = (int16_t)((int16_t)(rectangle.left + rectangle.right) >> 1);
		selector->zoomCenter.y = (int16_t)((int16_t)(rectangle.top + rectangle.bottom) >> 1);
		rectangle = faceRects[selector->animation.selectedVehicle - 1];
		selector->actionRects[3] =
		    (SlipSelectorRectangle){(int16_t)(rectangle.left - 27), (int16_t)(rectangle.top - 10),
		                            (int16_t)(rectangle.right - 27), (int16_t)(rectangle.bottom - 10)};
		calls->drawSprite(context, selector->assets.driverBackground, 0, 0);
		calls->present(context);
		calls->drawSprite(context, selector->assets.driverBackground, 0, 0);
		calls->setNavigation(context, &SlipVehicleSelector_driverNavigation);
		RasterSurfaceBounds bounds = Raster_GetSurfaceBounds();
		Raster_SetClipRect((int16_t)bounds.left, (int16_t)bounds.top, (int16_t)bounds.right, (int16_t)bounds.bottom);
		for (int16_t scale = 0; scale <= 0x4000; scale += 0x400) {
			calls->poll(context);
			calls->zoom(context, selector->card, scale, selector->zoomCenter, (SlipSelectorPoint){27, 10});
			calls->present(context);
		}
		selector->voice = 0;
		for (;;) {
			calls->poll(context);
			calls->drawSprite(context, selector->assets.driverBackground, 0, 0);
			calls->drawSprite(context, selector->card, 27, 10);
			uint32_t action;
			if (selector->demo != NULL) {
				selector->demoDelay =
				    (uint16_t)(selector->demoDelay - (uint16_t)SlipFrameTimer_Values().deltaMilliseconds);
				if ((int16_t)selector->demoDelay < 0) {
					selector->demoDelay = 1000;
					++selector->demo;
					action = *selector->demo != 0 ? 2 : 1;
				} else
					action = 0;
			} else {
				SlipSelectorPoint pointer = calls->pointer(context);
				pointer.x = (int16_t)(pointer.x - 27);
				pointer.y = (int16_t)(pointer.y - 10);
				action = calls->hitTest(context, selector->actionRects, 4, pointer);
				if (action != 0 && action != 4)
					calls->drawSprite(context, selector->hover[action - 1], 0x7fff, 0);
			}
			calls->present(context);
			if (calls->pressed(context, SLIP_INPUT_SCAN_ESCAPE))
				action = 2;
			else {
				if (action == 0)
					continue;
				if (selector->demo == NULL && !calls->pressed(context, SLIP_INPUT_SCAN_ENTER)) {
					if (!calls->pressed(context, SLIP_INPUT_MOUSE_LEFT))
						continue;
					if (action == 4) {
						if (selector->speech != 0) {
							calls->stopVoice(context, selector->voice);
							const uint32_t speechSize = calls->resourceSize(context, selector->speech);
							selector->voice = calls->playVoice(context, selector->speechData, speechSize);
						}
						continue;
					}
				}
			}
			calls->stopVoice(context, selector->voice);
			if ((uint16_t)action == 2) {
				calls->musicBranch(context, 0);
				bounds = Raster_GetSurfaceBounds();
				Raster_SetClipRect((int16_t)bounds.left, (int16_t)bounds.top, (int16_t)bounds.right,
				                   (int16_t)bounds.bottom);
				for (int16_t scale = 0x4000; scale >= 0; scale -= 0x400) {
					calls->poll(context);
					calls->drawSprite(context, selector->assets.driverBackground, 0, 0);
					calls->zoom(context, selector->card, scale, selector->zoomCenter, (SlipSelectorPoint){27, 10});
					calls->present(context);
				}
				if (selector->speech != 0) {
					calls->resources.unlock(calls->resources.context, selector->speech);
					calls->resources.release(calls->resources.context, selector->speech);
				}
				calls->resources.release(calls->resources.context, selector->card);
				for (unsigned i = 0; i < 3; ++i)
					calls->resources.release(calls->resources.context, selector->hover[i]);
				selector->confirmed = 0;
				break;
			}
			if ((uint16_t)action == 3) {
				calls->viewVehicle(context, selector->animation.selectedVehicle);
				bounds = Raster_GetSurfaceBounds();
				Raster_SetClipRect((int16_t)bounds.left, (int16_t)bounds.top, (int16_t)bounds.right,
				                   (int16_t)bounds.bottom);
				calls->spritePalette(context, selector->card);
				calls->spritePalette(context, selector->assets.driverBackground);
				continue;
			}
			calls->resources.release(calls->resources.context, selector->card);
			for (unsigned i = 0; i < 3; ++i)
				calls->resources.release(calls->resources.context, selector->hover[i]);
			if (selector->speech != 0) {
				calls->resources.unlock(calls->resources.context, selector->speech);
				calls->resources.release(calls->resources.context, selector->speech);
			}
			finished = true;
			break;
		}
	} while (!finished);
	SlipVehicleSelector_Release(selector, calls);
	return selector->animation.selectedVehicle;
}
