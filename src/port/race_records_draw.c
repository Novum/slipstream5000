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
	SlipSprite_Draw(&assets->shapeBackground, rowPixels, row->width, 207, 1);
	SlipText_SelectFont(&SlipText_state, &assets->font);
	SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, 49, 206);
	SlipText_SetColor(&SlipText_state, 255);
	SlipTextPosition position = {0, 12};
	SlipText_Draw(&SlipText_state, record->name, NULL, &position);
	if (input->cursorVisible != 0 && record->editing != 0) {

		const char character = record->name[input->cursor];
		record->name[input->cursor] = '\0';
		const uint16_t prefixWidth = (uint16_t)SlipFont_MeasureText(&assets->font, record->name);
		record->name[input->cursor] = character;
		const int16_t wholeWidth = (int16_t)(uint16_t)SlipFont_MeasureText(&assets->font, record->name);
		const int16_t cursorX = (int16_t)(uint16_t)(prefixWidth + 127 - (wholeWidth >> 1));
		Raster_DrawLineClipped(255, cursorX, position.y, cursorX, (int16_t)(position.y + 9));
	}
	char time[12];
	SlipRaceHud_FormatTime(record->lapTime, time);
	time[5] = '\'';
	time[8] = '"';
	position = (SlipTextPosition){0, 26};
	SlipText_Draw(&SlipText_state, time + 3, NULL, &position);
	shapes->viewport(shapes->context, 207, 1, 254, 44, 230, 22);
	SlipView3DVec32 shapePosition = {0, 0, 63440};
	shapes->position(shapes->context, shapePosition, shapePosition);
	shapes->drawShape(shapes->context, shape, matrix, matrix);
	Raster_RestoreScreen(&saved);
}
