#ifndef SLIPSTREAM5000_VEHICLE_SELECTOR_H
#define SLIPSTREAM5000_VEHICLE_SELECTOR_H

#include "input_navigation.h"
#include "resource_modify.h"
#include "sprite_format.h"
#include "vehicle_select.h"
#include "vga_dac.h"

enum {
	SLIP_SELECTOR_CARD_MODIFY_CONTINUATION = 0x45d71,
	SLIP_SELECTOR_CARD_MODIFY_OPERAND = 0x465ea,
	SLIP_SELECTOR_HOVER_MODIFY_CONTINUATION = 0x45dd4,
	SLIP_SELECTOR_OPTION_COUNT = 3,
	SLIP_SELECTOR_ACTION_COUNT = SLIP_SELECTOR_OPTION_COUNT + 1,
	SLIP_SELECTOR_PALETTE_BUFFER_BYTES = SLIP_PALETTE_HEADER_BYTES + SLIP_VGA_DAC_PALETTE_COUNT * SLIP_PALETTE_RGB_BYTES
};

typedef SlipInputRectangle SlipSelectorRectangle;

typedef struct SlipSelectorPoint {
	int16_t x, y;
} SlipSelectorPoint;

typedef struct SlipSelectorDimensions {
	uint16_t width, height;
} SlipSelectorDimensions;

extern SlipInputNavigationTable SlipVehicleSelector_vehicleNavigation;
extern SlipInputNavigationTable SlipVehicleSelector_driverNavigation;

typedef struct SlipVehicleSelector {
	SlipVehicleSelection animation;
	SlipVehicleSelectionAssets assets;
	uint16_t excludedVehicle;
	uint16_t playerMarker;
	const uint16_t *demo;
	uint16_t demoDelay, demoPhase;
	uint16_t frames[SLIP_RACE_RACER_COUNT][SLIP_VEHICLE_DOOR_FRAME_COUNT];
	SlipSelectorRectangle frameRects[SLIP_RACE_RACER_COUNT];
	uint16_t zones;
	int16_t fade, fadeTarget;
	uint8_t confirmed;
	uint8_t sourcePalette[SLIP_SELECTOR_PALETTE_BUFFER_BYTES], grayPalette[SLIP_SELECTOR_PALETTE_BUFFER_BYTES];
	uint16_t card, hover[SLIP_SELECTOR_OPTION_COUNT];
	uint16_t speech;
	const uint8_t *speechData;
	uint32_t voice;
	SlipSelectorPoint zoomCenter;
	SlipSelectorRectangle actionRects[SLIP_SELECTOR_ACTION_COUNT];
} SlipVehicleSelector;

extern SlipVehicleSelector SlipVehicleSelector_state;

/* Typed bindings to the original callees. Resource bytes here are serialized
 * SPR/palette/speech payloads; all selector state above is ordinary C fields. */
typedef struct SlipVehicleSelectorCalls {
	void *context;
	SlipStringTableResources resources;
	SlipVehicleSelectionErrors errors;
	uint8_t *(*lock)(void *, uint16_t);
	uint32_t (*resourceSize)(void *, uint16_t);
	bool (*allocate)(void *, uint32_t size, uint32_t flags, uint16_t *);
	bool (*modify)(void *, uint16_t *);
	SlipSelectorRectangle (*spriteBounds)(void *, uint16_t);
	void (*grayscale)(void *, uint16_t);
	void (*spritePalette)(void *, uint16_t);
	void (*selectFont)(void *, uint16_t);
	void (*setNavigation)(void *, SlipInputNavigationTable *);
	void (*clearNavigation)(void *);
	void (*poll)(void *);
	bool (*pressed)(void *, SlipInputCode);
	uint32_t (*frameClockMilliseconds)(void *);
	uint16_t (*zone)(void *, uint16_t);
	void (*background)(void *, SlipVehicleSelector *);
	void (*drawSprite)(void *, uint16_t, int16_t x, int16_t y);
	uint16_t (*blendPalette)(void *, const uint8_t *, const uint8_t *, int16_t);
	void (*palette)(void *, const uint8_t *);
	void (*present)(void *);
	void (*musicBranch)(void *, uint32_t);
	void (*bakeText)(void *, uint16_t, const char *, uint16_t color, int16_t y);
	SlipSelectorDimensions (*spriteDimensions)(void *, uint16_t);
	void (*bindSprite)(void *, uint16_t, SlipSelectorDimensions);
	void (*restoreScreen)(void *);
	void (*zoom)(void *, uint16_t, int16_t scale, SlipSelectorPoint center, SlipSelectorPoint target);
	SlipSelectorPoint (*pointer)(void *);
	uint32_t (*hitTest)(void *, const SlipSelectorRectangle *, uint16_t count, SlipSelectorPoint);
	void (*stopVoice)(void *, uint32_t);
	uint32_t (*playVoice)(void *, const uint8_t *, uint32_t size);
	void (*viewVehicle)(void *, uint16_t);
} SlipVehicleSelectorCalls;

void SlipVehicleSelector_Setup(SlipVehicleSelector *selector, uint16_t excludedVehicle, const uint16_t *demo,
                               uint16_t smallFont, uint16_t language, SlipStringTableState *strings,
                               const SlipVehicleSelectorCalls *calls);
void SlipVehicleSelector_Release(SlipVehicleSelector *selector, const SlipVehicleSelectorCalls *calls);
void SlipVehicleSelector_Draw(SlipVehicleSelector *selector, const SlipVehicleSelectorCalls *calls);

typedef struct SlipSelectorCardSetupResult {
	SlipResourceModifyResult modify;
	uint32_t continuationReturnAddress, continuationOperand;
} SlipSelectorCardSetupResult;

SlipSelectorCardSetupResult SlipVehicleSelector_CardSetup(SlipVehicleSelector *, const SlipVehicleSelectorCalls *,
                                                          SlipResourceModifyResult (*modify)(void *, uint16_t),
                                                          uint16_t (*modifiedHandle)(SlipResourceModifyResult));
void SlipVehicleSelector_CardRelease(SlipVehicleSelector *, const SlipVehicleSelectorCalls *);

uint16_t SlipVehicleSelector_Run(SlipVehicleSelector *state, uint16_t excludedVehicle, const uint16_t *demo,
                                 uint16_t smallFont, uint16_t language, uint32_t gameMode,
                                 SlipStringTableState *strings, const SlipVehicleSelectorCalls *calls);
#endif
