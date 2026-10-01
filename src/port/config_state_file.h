#ifndef SLIPSTREAM5000_CONFIG_STATE_FILE_H
#define SLIPSTREAM5000_CONFIG_STATE_FILE_H
#include "race_records.h"
#include <stdint.h>

extern SlipLapRecordTable SlipConfig_lapRecords;

typedef struct SlipConfigDeviceCalls {
	void *context;
	uint16_t (*joystickPresence)(void *);
	uint32_t (*mousePresence)(void *);
} SlipConfigDeviceCalls;

void SlipConfigFile_Load(const char *path, const SlipConfigDeviceCalls *devices);
void SlipConfigFile_Save(const char *path);
#endif
