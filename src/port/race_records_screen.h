#ifndef SLIPSTREAM5000_RACE_RECORDS_SCREEN_H
#define SLIPSTREAM5000_RACE_RECORDS_SCREEN_H
#include "input_navigation.h"
#include "race_records.h"
#include "race_records_view.h"
#include "sprite.h"
#include "string_table.h"

typedef struct SlipLapRecordsScreen {
	uint32_t trackIndex;
	SlipStringTableSlot *strings;
	uint16_t background, inactive, shapeBackground;
	uint16_t portraits[SLIP_RACE_RACER_COUNT], shapes[SLIP_RACE_RACER_COUNT];
	uint16_t rowFont, titleFont, rows[SLIP_LAP_RECORDS_ROW_COUNT];
	SlipLapRecordsAnimation animation;
	SlipLapRecordNameInput input;
	SlipLapRecord *editingRecord;
} SlipLapRecordsScreen;

typedef struct SlipLapRecordsScreenCalls {
	void *context;
	SlipStringTableResources resources;
	void (*language)(void *);
	void (*renderer)(void *, uint32_t vertices, uint32_t flags);
	void (*minimumDepth)(void *, uint32_t);
	void (*maximumDepth)(void *, uint32_t);
	void (*resetLighting)(void *);
	void (*ambient)(void *, uint16_t);
	void (*light)(void *, int16_t x, int16_t y, int16_t z, uint16_t strength);
	void (*depthFade)(void *, uint32_t);
	void (*rendererFlags)(void *, uint32_t);
	void (*camera)(void *, SlipView3DVec32, const SlipView3DMatrix *);
	const SlipView3DMatrix *cameraMatrix;
	void (*shapes)(void *);
	void (*materials)(void *, const uint8_t *asset);
	void (*palette)(void *, uint16_t);
	bool (*sequence)(void *, const char *, uint32_t first, uint16_t count, uint16_t *resources);
	bool (*allocate)(void *, uint32_t bytes, uint32_t flags, uint16_t *resource);
	SlipSprite *(*lockSprite)(void *, uint16_t);
	void (*releaseSequence)(void *, const uint16_t *, uint16_t count);
	void (*closeShapes)(void *);
	void (*closeRenderer)(void *);
	void (*resourceFailure)(void *);
} SlipLapRecordsScreenCalls;

uint16_t SlipLapRecords_CreateRow(int16_t x, int16_t y, const SlipLapRecordsScreenCalls *calls);
void SlipLapRecords_Initialize(SlipLapRecordsScreen *, SlipStringTableState *, const SlipLapRecordsScreenCalls *);
void SlipLapRecords_Close(SlipLapRecordsScreen *, const SlipLapRecordsScreenCalls *);

typedef struct SlipLapRecordsFrameCalls {
	void *context;
	const SlipView3DMaths *maths;
	SlipInputBiosCalls bios;
	uint32_t (*ticks)(void *);
	void (*drawSprite)(void *, uint16_t, int16_t x, int16_t y);
	void (*font)(void *, uint16_t);
	void (*textColorOrMode)(void *, uint16_t);
	void (*panel)(void *, SlipInputRectangle, uint16_t sprite, SlipStringTableSlot *, uint32_t tag);
	void (*row)(void *, SlipLapRecordsScreen *, uint16_t sprite, SlipLapRecord *);
	void (*dissolve)(void *, uint16_t sprite, int16_t x, uint16_t level);
	void (*present)(void *);
	void (*poll)(void *);
} SlipLapRecordsFrameCalls;

enum { SLIP_LAP_RECORDS_TIMED_DISPLAY = 1u };

void SlipLapRecords_Show(SlipLapRecordsScreen *, SlipLapRecordTable *, uint32_t flags, SlipStringTableState *,
                         const SlipLapRecordsScreenCalls *, const SlipLapRecordsFrameCalls *);

void SlipLapRecords_EnterName(SlipLapRecordsScreen *, SlipLapRecordTable *, SlipLapRecord *, SlipStringTableState *,
                              const SlipLapRecordsScreenCalls *, const SlipLapRecordsFrameCalls *);
#endif
