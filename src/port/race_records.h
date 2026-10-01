#ifndef SLIPSTREAM5000_RACE_RECORDS_H
#define SLIPSTREAM5000_RACE_RECORDS_H

#include "race.h"

typedef struct SlipLapRecord {
	uint16_t driverIndex;
	char name[32];
	uint32_t lapTime;
	uint16_t editing;
} SlipLapRecord;

typedef struct SlipLapRecordTable {
	SlipLapRecord tracks[10][3];
} SlipLapRecordTable;

typedef struct SlipLapRecordNameInput {
	uint16_t cursorVisible;
	uint16_t blinkRemaining;
	uint16_t cursor;
} SlipLapRecordNameInput;

void SlipLapRecords_BeginNameInput(SlipLapRecordNameInput *input);
void SlipLapRecords_UpdateNameBlink(SlipLapRecordNameInput *input, uint16_t elapsed);

bool SlipLapRecords_EditName(SlipLapRecordNameInput *input, SlipLapRecord *record, uint8_t character);

/* Bindings for the original blocking callees, not optional notifications. */
typedef struct SlipLapRecordsHost {
	void (*enterName)(void *context, uint32_t trackIndex, SlipLapRecord *record);
	void (*saveConfiguration)(void *context);
	void *context;
} SlipLapRecordsHost;

void SlipLapRecords_Update(SlipLapRecordTable *records, uint32_t track, const SlipRaceRacerTable *racers,
                           const SlipLapRecordsHost *host);

#endif
