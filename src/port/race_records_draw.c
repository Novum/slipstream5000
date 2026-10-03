#include "race_records_draw.h"
#include "race_hud.h"
#include "raster/raster.h"

void SlipLapRecords_DrawRow(SlipLapRecord *record, const SlipLapRecordNameInput *input, const SlipSprite *row,
                            uint8_t *rowPixels, const SlipLapRecordRowAssets *assets, const SlipView3DMatrix *matrix,
                            const SlipLapRecordShapeCalls *shapes) {
	RasterSurfaceBinding saved;
	Raster_BindSprite(rowPixels, row->width, row->height, &saved);
	SlipSprite_DrawClipped(&assets->inactive, rowPixels, row->width, (int16_t)(0u - (uint16_t)row->x),
	                       (int16_t)(0u - (uint16_t)row->y));
	const uint16_t shape = assets->shapes[record->driverIndex];
	SlipSprite_Draw(&assets->portraits[record->driverIndex], rowPixels, row->width, 1, 1);
	SlipSprite_Draw(&assets->shapeBackground, rowPixels, row->width, SLIP_LAP_RECORDS_ROW_SHAPE_LEFT,
	                SLIP_LAP_RECORDS_ROW_SHAPE_TOP);
	SlipText_SelectFont(&SlipText_state, &assets->font);
	SlipText_SetStyle(&SlipText_state, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_LAP_RECORDS_ROW_TEXT_LEFT,
	                  SLIP_LAP_RECORDS_ROW_TEXT_RIGHT);
	SlipText_SetColor(&SlipText_state, SLIP_LAP_RECORDS_ROW_TEXT_COLOUR);
	SlipTextPosition position = {0, SLIP_LAP_RECORDS_ROW_NAME_Y};
	SlipText_Draw(&SlipText_state, record->name, NULL, &position);
	if (input->cursorVisible != 0 && record->editing != 0) {

		const char character = record->name[input->cursor];
		record->name[input->cursor] = '\0';
		const uint16_t prefixWidth = (uint16_t)SlipFont_MeasureText(&assets->font, record->name);
		record->name[input->cursor] = character;
		const int16_t wholeWidth = (int16_t)(uint16_t)SlipFont_MeasureText(&assets->font, record->name);
		const int16_t cursorX = (int16_t)(uint16_t)(prefixWidth + SLIP_LAP_RECORDS_ROW_TEXT_CENTER - (wholeWidth >> 1));
		Raster_DrawLineClipped(SLIP_LAP_RECORDS_ROW_TEXT_COLOUR, cursorX, position.y, cursorX,
		                       (int16_t)(position.y + SLIP_LAP_RECORDS_ROW_CURSOR_HEIGHT));
	}
	char time[SLIP_RACE_TIME_TEXT_BYTES];
	SlipRaceHud_FormatTime(record->lapTime, time);
	time[SLIP_RACE_TIME_MINUTES_SEPARATOR] = '\'';
	time[SLIP_RACE_TIME_SECONDS_SEPARATOR] = '"';
	position = (SlipTextPosition){0, SLIP_LAP_RECORDS_ROW_TIME_Y};
	SlipText_Draw(&SlipText_state, time + SLIP_RACE_TIME_MINUTES_OFFSET, NULL, &position);
	shapes->viewport(shapes->context, SLIP_LAP_RECORDS_ROW_SHAPE_LEFT, SLIP_LAP_RECORDS_ROW_SHAPE_TOP,
	                 SLIP_LAP_RECORDS_ROW_SHAPE_RIGHT, SLIP_LAP_RECORDS_ROW_SHAPE_BOTTOM,
	                 SLIP_LAP_RECORDS_ROW_SHAPE_CENTER_X, SLIP_LAP_RECORDS_ROW_SHAPE_CENTER_Y);
	SlipView3DVec32 shapePosition = {0, 0, SLIP_LAP_RECORDS_ROW_SHAPE_DEPTH};
	shapes->position(shapes->context, shapePosition, shapePosition);
	shapes->drawShape(shapes->context, shape, matrix, matrix);
	Raster_RestoreScreen(&saved);
}
