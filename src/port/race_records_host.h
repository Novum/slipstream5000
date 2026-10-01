#ifndef SLIPSTREAM5000_RACE_RECORDS_HOST_H
#define SLIPSTREAM5000_RACE_RECORDS_HOST_H
#include "race_records_draw.h"
#include "race_records_screen.h"
extern SlipLapRecordsScreen SlipLapRecordsHost_screen;
extern const SlipLapRecordsScreenCalls SlipLapRecordsHost_lifecycle;
extern const SlipLapRecordShapeCalls SlipLapRecordsHost_shapeCalls;
void SlipLapRecordsHost_DrawRow(void *, SlipLapRecordsScreen *, uint16_t resource, SlipLapRecord *);
void SlipLapRecordsHost_BindDrawing(SlipLapRecordsFrameCalls *);

void SlipLapRecordsHost_BindRuntime(SlipLapRecordsFrameCalls *, const SlipView3DMaths *maths);
void SlipLapRecordsHost_Update(uint32_t track, const SlipRaceRacerTable *, const SlipView3DMaths *maths);
#endif
