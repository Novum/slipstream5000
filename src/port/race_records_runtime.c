#include "config_menu_host.h"
#include "config_state_file.h"
#include "input_bios_host.h"
#include "port_app_bridge.h"
#include "race_records_host.h"

static uint32_t SlipLapRecordsRuntime_Ticks(void *context) {
	(void)context;
	return (uint32_t)SlipSdl_TicksMs();
}

void SlipLapRecordsHost_BindRuntime(SlipLapRecordsFrameCalls *calls, const SlipView3DMaths *maths) {
	*calls = (SlipLapRecordsFrameCalls){.context = SlipConfigHost_calls.context,
	                                    .maths = maths,
	                                    .bios = SlipInputBiosHost_calls,
	                                    .ticks = SlipLapRecordsRuntime_Ticks,
	                                    .present = SlipConfigHost_calls.present,
	                                    .poll = SlipConfigHost_calls.poll};
	SlipLapRecordsHost_BindDrawing(calls);
}

static void SlipLapRecordsRuntime_EnterName(void *context, uint32_t trackIndex, SlipLapRecord *record) {
	const SlipLapRecordsFrameCalls *const calls = context;
	SlipLapRecordsHost_screen.trackIndex = trackIndex;
	SlipLapRecords_EnterName(&SlipLapRecordsHost_screen, &SlipConfig_lapRecords, record, &SlipStringTable_state,
	                         &SlipLapRecordsHost_lifecycle, calls);
}

static void SlipLapRecordsRuntime_SaveConfiguration(void *context) {
	(void)context;
	SlipConfigHost_calls.save(SlipConfigHost_calls.context);
}

void SlipLapRecordsHost_Update(uint32_t track, const SlipRaceRacerTable *racers, const SlipView3DMaths *maths) {
	SlipLapRecordsFrameCalls frame;
	SlipLapRecordsHost_BindRuntime(&frame, maths);
	const SlipLapRecordsHost calls = {SlipLapRecordsRuntime_EnterName, SlipLapRecordsRuntime_SaveConfiguration, &frame};
	SlipLapRecords_Update(&SlipConfig_lapRecords, track, racers, &calls);
}
