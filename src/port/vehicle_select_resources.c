#include "string_tags.h"
#include "vehicle_select.h"

void SlipVehicleSelection_Setup(SlipVehicleSelection *state, SlipVehicleSelectionAssets *assets,
                                SlipStringTableState *strings, uint16_t language,
                                const SlipStringTableResources *resources, const SlipVehicleSelectionErrors *errors) {
	state->titleTag = SLIP_STRING_TITLE;
	state->titleDelay = 0;
	SlipStringTable_SetLanguage(strings, (uint8_t)language);
	SlipStringTableSlot *slot;
	if (!SlipStringTable_Load(strings, "CH_TEAM ", resources, &slot))
		errors->resourceError(errors->context);
	assets->strings = slot;
	for (unsigned i = 0; i < SLIP_RACE_RACER_COUNT; ++i) {
		state->doors[i].frame = 0;
		state->doors[i].redrawPasses = 0;
	}
	uint16_t font;
	if (!resources->load(resources->context, "TEAMFONT.FNT", &font))
		errors->fatalError(errors->context);
	assets->titleFont = font;
}

void SlipVehicleSelection_Release(SlipVehicleSelectionAssets *assets, const SlipStringTableResources *resources) {
	resources->release(resources->context, assets->titleFont);
	resources->release(resources->context, assets->background);
	resources->release(resources->context, assets->driverBackground);
	SlipStringTable_Release(assets->strings, resources);
}
