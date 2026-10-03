#include "raster/raster.h"
#include "text_layout.h"
#include "vehicle_select.h"

void SlipVehicleSelection_Draw(SlipVehicleSelection *state, const SlipVehicleSelectionScreen *screen,
                               uint16_t excludedVehicle) {
	RasterSurfaceBounds bounds = Raster_GetSurfaceBounds();
	Raster_SetClipRect((int16_t)bounds.left, (int16_t)bounds.top, (int16_t)bounds.right, (int16_t)bounds.bottom);
	SlipSprite_DrawClipped(&screen->background, g_screenBufferBase, g_screenPitch, 0, 0);
	SlipText_SelectFont(&SlipText_state, &screen->titleFont);
	SlipText_SetStyle(&SlipText_state, SLIP_TEXT_CENTERED, UINT16_MAX, 0, SLIPSTREAM_SCREEN_WIDTH - 1);
	SlipText_SetColor(&SlipText_state, SLIP_VEHICLE_SELECTION_TITLE_COLOUR);
	const char *const title = SlipStringTable_Get(screen->strings, state->titleTag, screen->stringResources);
	SlipTextPosition position = {0, SLIP_VEHICLE_SELECTION_TITLE_TOP};
	SlipText_Draw(&SlipText_state, title, NULL, &position);
	SlipStringTable_Unlock(screen->strings, screen->stringResources);
	SlipText_SelectFont(&SlipText_state, &screen->smallFont);
	const int32_t markerIndex = (int32_t)excludedVehicle - 1;
	if (markerIndex >= 0) {
		static const int16_t markerPositions[SLIP_RACE_RACER_COUNT][2] = {
		    {109, 116}, {13, 107}, {206, 115}, {250, 76}, {200, 45}, {93, 77}, {2, 66}, {167, 73}, {123, 43}, {74, 50}};
		SlipSprite_DrawClipped(&screen->playerMarker, g_screenBufferBase, g_screenPitch,
		                       markerPositions[markerIndex][0], markerPositions[markerIndex][1]);
	}
	for (unsigned i = 0; i < SLIP_RACE_RACER_COUNT; ++i) {
		if (state->doors[i].frame != 0)
			state->doors[i].redrawPasses = SLIP_VEHICLE_SELECTION_REDRAW_PASSES;
	}
}
