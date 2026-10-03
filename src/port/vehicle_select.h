#ifndef SLIPSTREAM5000_VEHICLE_SELECT_H
#define SLIPSTREAM5000_VEHICLE_SELECT_H

#include "font.h"
#include "race.h"
#include "sprite.h"
#include "string_table.h"
#include <stdint.h>

enum {
	SLIP_VEHICLE_DOOR_FRAME_COUNT = 4,
	SLIP_VEHICLE_SELECTION_REDRAW_PASSES = 2,
	SLIP_VEHICLE_SELECTION_TITLE_COLOUR = 0x90,
	SLIP_VEHICLE_SELECTION_TITLE_TOP = 10
};

typedef struct SlipVehicleDoor {
	uint8_t frame, redrawPasses;
} SlipVehicleDoor;

typedef struct SlipVehicleSelection {
	uint32_t titleTag;
	uint16_t titleDelay;
	SlipVehicleDoor doors[SLIP_RACE_RACER_COUNT];
	uint16_t doorElapsed;
	uint16_t selectedVehicle;
	uint16_t backgroundRedraws;
} SlipVehicleSelection;

void SlipVehicleSelection_Update(SlipVehicleSelection *state, uint32_t gameMode, uint16_t excludedVehicle);

typedef struct SlipVehicleSelectionScreen {
	SlipSprite background, playerMarker;
	SlipFont titleFont, smallFont;
	SlipStringTableSlot *strings;
	const SlipStringTableResources *stringResources;
} SlipVehicleSelectionScreen;

void SlipVehicleSelection_Draw(SlipVehicleSelection *state, const SlipVehicleSelectionScreen *screen,
                               uint16_t excludedVehicle);

typedef struct SlipVehicleSelectionAssets {
	SlipStringTableSlot *strings;
	uint16_t background;
	uint16_t driverBackground;
	uint16_t titleFont;
} SlipVehicleSelectionAssets;

/* These are the two original non-returning error targets. */
typedef struct SlipVehicleSelectionErrors {
	void *context;
	void (*resourceError)(void *context);
	void (*fatalError)(void *context);
} SlipVehicleSelectionErrors;

void SlipVehicleSelection_Setup(SlipVehicleSelection *state, SlipVehicleSelectionAssets *assets,
                                SlipStringTableState *strings, uint16_t language,
                                const SlipStringTableResources *resources, const SlipVehicleSelectionErrors *errors);
void SlipVehicleSelection_Release(SlipVehicleSelectionAssets *assets, const SlipStringTableResources *resources);

#endif
