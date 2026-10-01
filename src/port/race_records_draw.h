#ifndef SLIPSTREAM5000_RACE_RECORDS_DRAW_H
#define SLIPSTREAM5000_RACE_RECORDS_DRAW_H
#include "race_records.h"
#include "sprite.h"
#include "text_layout.h"
#include "view3d.h"

typedef struct SlipLapRecordRowAssets {
	SlipSprite inactive, portraits[10], shapeBackground;
	SlipFont font;
	uint16_t shapes[10];
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
