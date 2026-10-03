#include "vehicle_selector.h"
#include "fixed_point.h"
#include "frame_timer.h"
#include "raster/raster.h"
#include "sprite_format.h"
#include "string_tags.h"
#include "text_layout.h"
#include <string.h>

/* Continuation identities retained for resource-modification diagnostics. */
enum {
	SLIP_SELECTOR_CARD_LEFT = 27,
	SLIP_SELECTOR_CARD_TOP = 10,
	SLIP_SELECTOR_DEMO_DELAY_MS = 1000,
	SLIP_SELECTOR_DRIVER_NAME_INDEX = 6,
	SLIP_SELECTOR_HOVER_NAME_INDEX = 7,
	SLIP_SELECTOR_CAR_NAME_INDEX = 3,
	SLIP_SELECTOR_FRAME_NAME_INDEX = 7,
	SLIP_SELECTOR_ACTION_ACCEPT = 1,
	SLIP_SELECTOR_ACTION_BACK = 2,
	SLIP_SELECTOR_ACTION_VIEW_VEHICLE = 3,
	SLIP_SELECTOR_ACTION_SPEECH = 4,
	SLIP_SELECTOR_FACE_RECT_INDEX = SLIP_SELECTOR_OPTION_COUNT,
	SLIP_SELECTOR_TEXT_COLOUR = 0xff,
	SLIP_SELECTOR_GREY_WEIGHT_Q8 = 0x55,
	SLIP_SELECTOR_GREY_FRACTION_BITS = 8,
	SLIP_SELECTOR_FADE_RATE_Q14 = 0x8000,
	SLIP_SELECTOR_ZOOM_STEP_Q14 = 0x400
};

SlipInputNavigationTable SlipVehicleSelector_vehicleNavigation = {
    SLIP_RACE_RACER_COUNT,
    0,
    {5, 6, 3, 4, -1, 8, 9, 4, -1, -1},
    {-1, -1, -1, 2, 7, 0, 1, 2, 5, 6},
    {1, -1, 0, 7, 8, 6, -1, 5, 9, 6},
    {2, 0, -1, -1, -1, 7, 5, 3, 4, 8},
    {{152, 155}, {58, 143}, {263, 146}, {275, 98}, {230, 63}, {124, 98}, {36, 94}, {206, 96}, {152, 59}, {91, 67}}};
SlipInputNavigationTable SlipVehicleSelector_driverNavigation = {
    SLIP_SELECTOR_OPTION_COUNT,          0, {-1, 0, 1}, {1, 2, -1}, {-1, -1, -1}, {-1, -1, -1},
    {{259, 152}, {259, 164}, {259, 176}}};
SlipVehicleSelector SlipVehicleSelector_state = {
    .actionRects = {{208, 138, 257, 147}, {208, 150, 257, 159}, {208, 162, 257, 171}, {69, 24, 113, 74}}};

SlipSelectorCardSetupResult SlipVehicleSelector_CardSetup(SlipVehicleSelector *selector,
                                                          const SlipVehicleSelectorCalls *calls,
                                                          SlipResourceModifyResult (*modify)(void *, uint16_t),
                                                          uint16_t (*modifiedHandle)(SlipResourceModifyResult)) {
	static const char *const speechNames[SLIP_RACE_RACER_COUNT + 1] = {NULL,       "EM47.SMP", "EM50.SMP", "EM49.SMP",
	                                                                   "EF45.SMP", "EM51.SMP", "EM52.SMP", "EM48.SMP",
	                                                                   "EF43.SMP", "EF44.SMP", "EM53.SMP"};
	static const SlipSelectorRectangle faceRects[SLIP_RACE_RACER_COUNT] = {
	    {96, 34, 140, 84},  {173, 41, 218, 84}, {89, 25, 131, 72},  {85, 45, 132, 85},  {112, 49, 146, 85},
	    {106, 28, 152, 81}, {98, 29, 147, 80},  {109, 29, 143, 61}, {136, 42, 175, 76}, {141, 34, 188, 76}};
	void *const context = calls->context;
	calls->clearNavigation(context);
	calls->musicBranch(context, selector->animation.selectedVehicle);
	char cardName[] = "DRIVER0.SPR", hoverName[] = "DRIVER0A.SPR";
	cardName[SLIP_SELECTOR_DRIVER_NAME_INDEX] = hoverName[SLIP_SELECTOR_DRIVER_NAME_INDEX] =
	    (char)(uint8_t)(selector->animation.selectedVehicle + ('0' - 1));
	uint16_t card;
	if (!calls->resources.load(calls->resources.context, cardName, &card))
		calls->errors.resourceError(calls->errors.context);
	SlipResourceModifyResult modified = modify(context, card);
	if (modified.exit == SLIP_RESOURCE_MODIFY_DISPLACED_RETURN)
		return (SlipSelectorCardSetupResult){modified, SLIP_SELECTOR_CARD_MODIFY_CONTINUATION,
		                                     SLIP_SELECTOR_CARD_MODIFY_OPERAND};
	card = modifiedHandle(modified);
	selector->card = card;
	selector->speech = 0;
	uint16_t speech;
	if (calls->resources.load(calls->resources.context, speechNames[selector->animation.selectedVehicle], &speech)) {
		selector->speechData = calls->lock(context, speech);
		selector->speech = speech;
	}
	for (unsigned i = 0; i < SLIP_SELECTOR_OPTION_COUNT; ++i) {
		uint16_t hover;
		if (!calls->resources.load(calls->resources.context, hoverName, &hover))
			calls->errors.resourceError(calls->errors.context);
		modified = modify(context, hover);
		if (modified.exit == SLIP_RESOURCE_MODIFY_DISPLACED_RETURN)
			return (SlipSelectorCardSetupResult){modified, SLIP_SELECTOR_HOVER_MODIFY_CONTINUATION,
			                                     SLIP_STRING_FIRST_OPTION + i};
		hover = modifiedHandle(modified);
		selector->hover[i] = hover;
		const char *const label =
		    SlipStringTable_Get(selector->assets.strings, SLIP_STRING_FIRST_OPTION + i, &calls->resources);
		calls->bakeText(context, selector->hover[i], label, SLIP_SELECTOR_TEXT_COLOUR, 1);
		SlipStringTable_Unlock(selector->assets.strings, &calls->resources);
		++hoverName[SLIP_SELECTOR_HOVER_NAME_INDEX];
	}
	SlipSelectorDimensions dimensions = calls->spriteDimensions(context, selector->card);
	calls->bindSprite(context, selector->card, dimensions);
	SlipText_SetColor(&SlipText_state, SLIP_SELECTOR_TEXT_COLOUR);
	for (unsigned i = 0; i < SLIP_SELECTOR_OPTION_COUNT; ++i) {
		SlipSelectorRectangle *const rectangle = &selector->actionRects[i];
		SlipText_SetStyle(&SlipText_state, SLIP_TEXT_CENTERED, UINT16_MAX, rectangle->left, rectangle->right);
		SlipTextPosition position = {rectangle->left, (int16_t)(rectangle->top + 1)};
		const char *const label =
		    SlipStringTable_Get(selector->assets.strings, SLIP_STRING_FIRST_OPTION + i, &calls->resources);
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
	selector->actionRects[SLIP_SELECTOR_FACE_RECT_INDEX] = (SlipSelectorRectangle){
	    (int16_t)(rectangle.left - SLIP_SELECTOR_CARD_LEFT), (int16_t)(rectangle.top - SLIP_SELECTOR_CARD_TOP),
	    (int16_t)(rectangle.right - SLIP_SELECTOR_CARD_LEFT), (int16_t)(rectangle.bottom - SLIP_SELECTOR_CARD_TOP)};
	return (SlipSelectorCardSetupResult){.modify = {.exit = SLIP_RESOURCE_MODIFY_HANDLE}};
}

void SlipVehicleSelector_CardRelease(SlipVehicleSelector *selector, const SlipVehicleSelectorCalls *calls) {
	calls->resources.release(calls->resources.context, selector->card);
	for (unsigned i = 0; i < SLIP_SELECTOR_OPTION_COUNT; ++i)
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
	selector->demoDelay = SLIP_SELECTOR_DEMO_DELAY_MS;
	selector->demoPhase = 0;
	calls->selectFont(context, smallFont);
	SlipVehicleSelection_Setup(&selector->animation, &selector->assets, strings, language, &calls->resources,
	                           &calls->errors);
	char frameName[] = "CAR0_FR1.SPR";
	for (unsigned vehicle = 0; vehicle < SLIP_RACE_RACER_COUNT; ++vehicle) {
		frameName[SLIP_SELECTOR_FRAME_NAME_INDEX] = '1';
		for (unsigned frame = 0; frame < SLIP_VEHICLE_DOOR_FRAME_COUNT; ++frame) {
			if (!calls->resources.load(calls->resources.context, frameName, &selector->frames[vehicle][frame]))
				calls->errors.resourceError(calls->errors.context);
			++frameName[SLIP_SELECTOR_FRAME_NAME_INDEX];
		}
		selector->frameRects[vehicle] =
		    calls->spriteBounds(context, selector->frames[vehicle][SLIP_VEHICLE_DOOR_FRAME_COUNT - 1]);
		++frameName[SLIP_SELECTOR_CAR_NAME_INDEX];
	}
	if (!calls->resources.load(calls->resources.context, "CH_TEAMZ.ZON", &selector->zones))
		calls->errors.resourceError(calls->errors.context);
	selector->playerMarker = 0;
	if (selector->excludedVehicle != 0) {
		char markerName[] = "CAR0P1.SPR";
		markerName[SLIP_SELECTOR_CAR_NAME_INDEX] = (char)(uint8_t)(selector->excludedVehicle + ('0' - 1));
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
	for (unsigned vehicle = 0; vehicle < SLIP_RACE_RACER_COUNT; ++vehicle)
		for (unsigned frame = 0; frame < SLIP_VEHICLE_DOOR_FRAME_COUNT; ++frame)
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
	for (unsigned vehicle = 0; vehicle < SLIP_RACE_RACER_COUNT; ++vehicle) {
		SlipVehicleDoor *const door = &selector->animation.doors[vehicle];
		if (door->redrawPasses != 0) {
			--door->redrawPasses;
			if (door->frame != 0)
				calls->drawSprite(context, selector->frames[vehicle][door->frame - 1], SLIP_SPRITE_USE_STORED_POSITION,
				                  0);
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
	static const char *const speechNames[SLIP_RACE_RACER_COUNT + 1] = {NULL,       "EM47.SMP", "EM50.SMP", "EM49.SMP",
	                                                                   "EF45.SMP", "EM51.SMP", "EM52.SMP", "EM48.SMP",
	                                                                   "EF43.SMP", "EF44.SMP", "EM53.SMP"};
	static const SlipSelectorRectangle faceRects[SLIP_RACE_RACER_COUNT] = {
	    {96, 34, 140, 84},  {173, 41, 218, 84}, {89, 25, 131, 72},  {85, 45, 132, 85},  {112, 49, 146, 85},
	    {106, 28, 152, 81}, {98, 29, 147, 80},  {109, 29, 143, 61}, {136, 42, 175, 76}, {141, 34, 188, 76}};
	void *const context = calls->context;
	SlipVehicleSelector_Setup(selector, excludedVehicle, demo, smallFont, language, strings, calls);
	bool finished = false;
	do {
		selector->fadeTarget = 0;
		selector->animation.backgroundRedraws = SLIP_VEHICLE_SELECTION_REDRAW_PASSES;
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
							selector->demoDelay = SLIP_SELECTOR_DEMO_DELAY_MS;
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
				const uint8_t *const palette =
				    sprite + (uint16_t)(sprite[SLIP_SPRITE_PALETTE_OFFSET] |
				                        (uint16_t)sprite[SLIP_SPRITE_PALETTE_OFFSET + 1] << 8);
				const uint16_t count = (uint16_t)(palette[SLIP_PALETTE_COUNT_OFFSET] |
				                                  (uint16_t)palette[SLIP_PALETTE_COUNT_OFFSET + 1] << 8);
				const uint16_t bytes = (uint16_t)(count * SLIP_PALETTE_RGB_BYTES + SLIP_PALETTE_HEADER_BYTES);
				uint32_t remaining = bytes;
				uint32_t position = 0;
				do {
					selector->sourcePalette[position] = palette[position];
					selector->grayPalette[position] = palette[position];
					++position;
				} while (--remaining != 0);
				calls->resources.unlock(calls->resources.context, selector->assets.background);
				remaining = (uint16_t)(selector->grayPalette[SLIP_PALETTE_COUNT_OFFSET] |
				                       (uint16_t)selector->grayPalette[SLIP_PALETTE_COUNT_OFFSET + 1] << 8);
				uint8_t *rgb = selector->grayPalette + SLIP_PALETTE_HEADER_BYTES;
				do {
					uint8_t gray = (uint8_t)(((uint16_t)rgb[0] * SLIP_SELECTOR_GREY_WEIGHT_Q8) >>
					                         SLIP_SELECTOR_GREY_FRACTION_BITS);
					gray = (uint8_t)(gray + (((uint16_t)rgb[1] * SLIP_SELECTOR_GREY_WEIGHT_Q8) >>
					                         SLIP_SELECTOR_GREY_FRACTION_BITS));
					gray = (uint8_t)(gray + (((uint16_t)rgb[2] * SLIP_SELECTOR_GREY_WEIGHT_Q8) >>
					                         SLIP_SELECTOR_GREY_FRACTION_BITS));
					rgb[0] = rgb[1] = rgb[2] = gray;
					rgb += SLIP_PALETTE_RGB_BYTES;
				} while (--remaining != 0);
				const uint16_t blended =
				    calls->blendPalette(context, selector->sourcePalette, selector->grayPalette, selector->fade);
				const uint8_t *const blendedData = calls->lock(context, blended);
				calls->palette(context, blendedData);
				calls->resources.unlock(calls->resources.context, blended);
				calls->resources.release(calls->resources.context, blended);
				const uint16_t step =
				    (uint16_t)(((uint32_t)(uint16_t)SlipFrameTimer_Values().stepQ14 * SLIP_SELECTOR_FADE_RATE_Q14) >>
				               SLIP_Q14_FRACTION_BITS);
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
						selector->demoDelay = SLIP_SELECTOR_DEMO_DELAY_MS;
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
				selector->fadeTarget = SLIP_Q14_ONE;
			}
		} while (!selector->confirmed || selector->fade != selector->fadeTarget);
		if (cancelled)
			break;
		calls->clearNavigation(context);
		calls->musicBranch(context, selector->animation.selectedVehicle);
		char cardName[] = "DRIVER0.SPR", hoverName[] = "DRIVER0A.SPR";
		cardName[SLIP_SELECTOR_DRIVER_NAME_INDEX] = hoverName[SLIP_SELECTOR_DRIVER_NAME_INDEX] =
		    (char)(uint8_t)(selector->animation.selectedVehicle + ('0' - 1));
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
		for (unsigned i = 0; i < SLIP_SELECTOR_OPTION_COUNT; ++i) {
			uint16_t hover;
			if (!calls->resources.load(calls->resources.context, hoverName, &hover))
				calls->errors.resourceError(calls->errors.context);
			if (!calls->modify(context, &hover))
				calls->errors.resourceError(calls->errors.context);
			selector->hover[i] = hover;
			const char *const label =
			    SlipStringTable_Get(selector->assets.strings, SLIP_STRING_FIRST_OPTION + i, &calls->resources);
			calls->bakeText(context, selector->hover[i], label, SLIP_SELECTOR_TEXT_COLOUR, 1);
			SlipStringTable_Unlock(selector->assets.strings, &calls->resources);
			++hoverName[SLIP_SELECTOR_HOVER_NAME_INDEX];
		}
		SlipSelectorDimensions dimensions = calls->spriteDimensions(context, selector->card);
		calls->bindSprite(context, selector->card, dimensions);
		SlipText_SetColor(&SlipText_state, SLIP_SELECTOR_TEXT_COLOUR);
		for (unsigned i = 0; i < SLIP_SELECTOR_OPTION_COUNT; ++i) {
			SlipSelectorRectangle *const rectangle = &selector->actionRects[i];
			SlipText_SetStyle(&SlipText_state, SLIP_TEXT_CENTERED, UINT16_MAX, rectangle->left, rectangle->right);
			SlipTextPosition position = {rectangle->left, (int16_t)(rectangle->top + 1)};
			const char *const label =
			    SlipStringTable_Get(selector->assets.strings, SLIP_STRING_FIRST_OPTION + i, &calls->resources);
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
		selector->actionRects[SLIP_SELECTOR_FACE_RECT_INDEX] = (SlipSelectorRectangle){
		    (int16_t)(rectangle.left - SLIP_SELECTOR_CARD_LEFT), (int16_t)(rectangle.top - SLIP_SELECTOR_CARD_TOP),
		    (int16_t)(rectangle.right - SLIP_SELECTOR_CARD_LEFT), (int16_t)(rectangle.bottom - SLIP_SELECTOR_CARD_TOP)};
		calls->drawSprite(context, selector->assets.driverBackground, 0, 0);
		calls->present(context);
		calls->drawSprite(context, selector->assets.driverBackground, 0, 0);
		calls->setNavigation(context, &SlipVehicleSelector_driverNavigation);
		RasterSurfaceBounds bounds = Raster_GetSurfaceBounds();
		Raster_SetClipRect((int16_t)bounds.left, (int16_t)bounds.top, (int16_t)bounds.right, (int16_t)bounds.bottom);
		for (int16_t scale = 0; scale <= SLIP_Q14_ONE; scale += SLIP_SELECTOR_ZOOM_STEP_Q14) {
			calls->poll(context);
			calls->zoom(context, selector->card, scale, selector->zoomCenter,
			            (SlipSelectorPoint){SLIP_SELECTOR_CARD_LEFT, SLIP_SELECTOR_CARD_TOP});
			calls->present(context);
		}
		selector->voice = 0;
		for (;;) {
			calls->poll(context);
			calls->drawSprite(context, selector->assets.driverBackground, 0, 0);
			calls->drawSprite(context, selector->card, SLIP_SELECTOR_CARD_LEFT, SLIP_SELECTOR_CARD_TOP);
			uint32_t action;
			if (selector->demo != NULL) {
				selector->demoDelay =
				    (uint16_t)(selector->demoDelay - (uint16_t)SlipFrameTimer_Values().deltaMilliseconds);
				if ((int16_t)selector->demoDelay < 0) {
					selector->demoDelay = SLIP_SELECTOR_DEMO_DELAY_MS;
					++selector->demo;
					action = *selector->demo != 0 ? SLIP_SELECTOR_ACTION_BACK : SLIP_SELECTOR_ACTION_ACCEPT;
				} else
					action = 0;
			} else {
				SlipSelectorPoint pointer = calls->pointer(context);
				pointer.x = (int16_t)(pointer.x - SLIP_SELECTOR_CARD_LEFT);
				pointer.y = (int16_t)(pointer.y - SLIP_SELECTOR_CARD_TOP);
				action = calls->hitTest(context, selector->actionRects, SLIP_SELECTOR_ACTION_COUNT, pointer);
				if (action != 0 && action != SLIP_SELECTOR_ACTION_SPEECH)
					calls->drawSprite(context, selector->hover[action - 1], SLIP_SPRITE_USE_STORED_POSITION, 0);
			}
			calls->present(context);
			if (calls->pressed(context, SLIP_INPUT_SCAN_ESCAPE))
				action = SLIP_SELECTOR_ACTION_BACK;
			else {
				if (action == 0)
					continue;
				if (selector->demo == NULL && !calls->pressed(context, SLIP_INPUT_SCAN_ENTER)) {
					if (!calls->pressed(context, SLIP_INPUT_MOUSE_LEFT))
						continue;
					if (action == SLIP_SELECTOR_ACTION_SPEECH) {
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
			if ((uint16_t)action == SLIP_SELECTOR_ACTION_BACK) {
				calls->musicBranch(context, 0);
				bounds = Raster_GetSurfaceBounds();
				Raster_SetClipRect((int16_t)bounds.left, (int16_t)bounds.top, (int16_t)bounds.right,
				                   (int16_t)bounds.bottom);
				for (int16_t scale = SLIP_Q14_ONE; scale >= 0; scale -= SLIP_SELECTOR_ZOOM_STEP_Q14) {
					calls->poll(context);
					calls->drawSprite(context, selector->assets.driverBackground, 0, 0);
					calls->zoom(context, selector->card, scale, selector->zoomCenter,
					            (SlipSelectorPoint){SLIP_SELECTOR_CARD_LEFT, SLIP_SELECTOR_CARD_TOP});
					calls->present(context);
				}
				if (selector->speech != 0) {
					calls->resources.unlock(calls->resources.context, selector->speech);
					calls->resources.release(calls->resources.context, selector->speech);
				}
				calls->resources.release(calls->resources.context, selector->card);
				for (unsigned i = 0; i < SLIP_SELECTOR_OPTION_COUNT; ++i)
					calls->resources.release(calls->resources.context, selector->hover[i]);
				selector->confirmed = 0;
				break;
			}
			if ((uint16_t)action == SLIP_SELECTOR_ACTION_VIEW_VEHICLE) {
				calls->viewVehicle(context, selector->animation.selectedVehicle);
				bounds = Raster_GetSurfaceBounds();
				Raster_SetClipRect((int16_t)bounds.left, (int16_t)bounds.top, (int16_t)bounds.right,
				                   (int16_t)bounds.bottom);
				calls->spritePalette(context, selector->card);
				calls->spritePalette(context, selector->assets.driverBackground);
				continue;
			}
			calls->resources.release(calls->resources.context, selector->card);
			for (unsigned i = 0; i < SLIP_SELECTOR_OPTION_COUNT; ++i)
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
