#ifndef SLIPSTREAM5000_RACE_RECORDS_DRAW_H
#define SLIPSTREAM5000_RACE_RECORDS_DRAW_H
#include "race_records.h"
#include "race_time_format.h"
#include "sprite.h"
#include "text_layout.h"
#include "view3d.h"

enum {
	SLIP_LAP_RECORDS_ROW_TEXT_LEFT = 49,
	SLIP_LAP_RECORDS_ROW_TEXT_RIGHT = 206,
	SLIP_LAP_RECORDS_ROW_TEXT_CENTER = (SLIP_LAP_RECORDS_ROW_TEXT_LEFT + SLIP_LAP_RECORDS_ROW_TEXT_RIGHT) / 2,
	SLIP_LAP_RECORDS_ROW_NAME_Y = 12,
	SLIP_LAP_RECORDS_ROW_TIME_Y = 26,
	SLIP_LAP_RECORDS_ROW_CURSOR_HEIGHT = 9,
	SLIP_LAP_RECORDS_ROW_TEXT_COLOUR = 255,
	SLIP_LAP_RECORDS_ROW_SHAPE_LEFT = 207,
	SLIP_LAP_RECORDS_ROW_SHAPE_TOP = 1,
	SLIP_LAP_RECORDS_ROW_SHAPE_RIGHT = 254,
	SLIP_LAP_RECORDS_ROW_SHAPE_BOTTOM = 44,
	SLIP_LAP_RECORDS_ROW_SHAPE_CENTER_X = (SLIP_LAP_RECORDS_ROW_SHAPE_LEFT + SLIP_LAP_RECORDS_ROW_SHAPE_RIGHT) / 2,
	SLIP_LAP_RECORDS_ROW_SHAPE_CENTER_Y = (SLIP_LAP_RECORDS_ROW_SHAPE_TOP + SLIP_LAP_RECORDS_ROW_SHAPE_BOTTOM) / 2,
	SLIP_LAP_RECORDS_ROW_SHAPE_DEPTH = 63440
};

typedef struct SlipLapRecordRowAssets {
	SlipSprite inactive, portraits[SLIP_RACE_RACER_COUNT], shapeBackground;
	SlipFont font;
	uint16_t shapes[SLIP_RACE_RACER_COUNT];
} SlipLapRecordRowAssets;

typedef struct SlipLapRecordShapeCalls {
	void *context;
	void (*viewport)(void *, int32_t left, int32_t top, int32_t right, int32_t bottom, int32_t centerX,
	                 int32_t centerY);
	void (*position)(void *, SlipView3DVec32 view, SlipView3DVec32 world);
	void (*drawShape)(void *, uint16_t shape, const SlipView3DMatrix *world, const SlipView3DMatrix *draw);
} SlipLapRecordShapeCalls;

void SlipLapRecords_DrawRow(SlipLapRecord *record, const SlipLapRecordNameInput *input, const SlipSprite *row,
                            uint8_t *rowPixels, const SlipLapRecordRowAssets *assets, const SlipView3DMatrix *matrix,
                            const SlipLapRecordShapeCalls *shapes);
#endif
