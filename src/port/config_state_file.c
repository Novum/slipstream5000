#include "config_state_file.h"
#include "byte_order.h"
#include "checksum.h"
#include "config_controls.h"
#include "config_file.h"
#include "config_settings.h"
#include "game_errors.h"
#include "joystick_calibration.h"
#include "menu_music.h"
#include "race_player.h"
#include "resource_host.h"
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SlipLapRecordTable SlipConfig_lapRecords;

/* Serialized file image, not a raw view of native game-state memory. Fields
 * still owned by untranslated screens retain their on-disk representation. */
static uint8_t image[SLIP_CONFIG_FILE_BYTES] = {
#include "config_default_image.inc"
};

static uint16_t SlipConfigFile_DiskWord(unsigned address) {
	const unsigned offset = address - 0x4924d;
	return SlipBytes_ReadLE16(image + offset);
}

static uint32_t SlipConfigFile_DiskDword(unsigned address) {
	const unsigned offset = address - 0x4924d;
	return SlipBytes_ReadLE32(image + offset);
}

static void SlipConfigFile_StoreWord(unsigned address, uint16_t value) {
	const unsigned offset = address - 0x4924d;
	image[offset] = (uint8_t)value;
	image[offset + 1] = (uint8_t)(value >> 8);
}

static void SlipConfigFile_StoreDword(unsigned address, uint32_t value) {
	SlipConfigFile_StoreWord(address, (uint16_t)value);
	SlipConfigFile_StoreWord(address + 2, (uint16_t)(value >> 16));
}

/* Typed serialization bindings for the original configuration fields. */
typedef struct ConfigWordBinding {
	unsigned address;
	uint16_t *value;
} ConfigWordBinding;

static const ConfigWordBinding words[] = {
    {0x492da, &SlipConfig_rearMonitor},
    {0x492dc, &SlipConfig_weaponsMonitor},
    {0x492de, &SlipConfig_speedDisplay},
    {0x492e0, &SlipConfig_soundEffects},
    {0x492e2, &SlipConfig_engineSounds},
    {0x492e4, &SlipConfig_speech},
    {0x492e6, &SlipConfig_music},
    {0x492e8, &SlipConfig_language},
    {0x492f2, &SlipConfig_environmentDetail},
    {0x492f4, &SlipConfig_clouds},
    {0x492f6, &SlipConfig_shading},
    {0x492f8, &SlipConfig_textures},
    {0x492fa, &SlipConfig_shadows},
    {0x492fc, &SlipConfig_windowSize},
    {0x492fe, &SlipConfig_trackMap},
    {0x493e6, &SlipConfig_trackProgress},
    {0x493e8, &SlipRace_reverseAccelerator},
};

static void SlipConfigFile_ImportImage(void) {

	for (unsigned track = 0; track < 10; ++track) {
		for (unsigned position = 0; position < 3; ++position) {
			const unsigned address = 0x493ea + track * 0x78 + position * 0x28;
			SlipLapRecord *const record = &SlipConfig_lapRecords.tracks[track][position];
			record->driverIndex = SlipConfigFile_DiskWord(address);
			memcpy(record->name, image + address + 2 - 0x4924d, sizeof(record->name));
			record->lapTime = SlipConfigFile_DiskDword(address + 0x22);
			record->editing = SlipConfigFile_DiskWord(address + 0x26);
		}
	}
	for (unsigned index = 0; index < sizeof(words) / sizeof(words[0]); ++index)
		*words[index].value = SlipConfigFile_DiskWord(words[index].address);
	SlipConfig_fallbackMode = (int32_t)SlipConfigFile_DiskDword(0x492ea);
	SlipConfig_damageEnabled = SlipConfigFile_DiskDword(0x492ee);
	for (unsigned player = 0; player < 2; ++player) {
		SlipRaceControlBinding *const record = &SlipRace_controlBindings[player];
		unsigned address = 0x492ba + player * 16;
		record->movementControl = SlipConfigFile_DiskWord(address);
		record->left = (SlipInputCode)SlipConfigFile_DiskWord(address + 2);
		record->right = (SlipInputCode)SlipConfigFile_DiskWord(address + 4);
		record->up = (SlipInputCode)SlipConfigFile_DiskWord(address + 6);
		record->down = (SlipInputCode)SlipConfigFile_DiskWord(address + 8);
		record->accelerate = (SlipInputCode)SlipConfigFile_DiskWord(address + 10);
		record->fire = (SlipInputCode)SlipConfigFile_DiskWord(address + 12);
		record->select = (SlipInputCode)SlipConfigFile_DiskWord(address + 14);
		SlipConfigControls_calibrated[player] = image[0x493cc - 0x4924d + player];
		address = 0x493ce + player * 12;
		SlipConfigControls_calibration[player] =
		    (SlipJoystickCalibration){SlipConfigFile_DiskWord(address),
		                              SlipConfigFile_DiskWord(address + 2),
		                              (int16_t)SlipConfigFile_DiskWord(address + 4),
		                              (int16_t)SlipConfigFile_DiskWord(address + 6),
		                              (int16_t)SlipConfigFile_DiskWord(address + 8),
		                              (int16_t)SlipConfigFile_DiskWord(address + 10)};
	}
}

static void SlipConfigFile_ExportImage(void) {
	for (unsigned track = 0; track < 10; ++track) {
		for (unsigned position = 0; position < 3; ++position) {
			const unsigned address = 0x493ea + track * 0x78 + position * 0x28;
			const SlipLapRecord *const record = &SlipConfig_lapRecords.tracks[track][position];
			SlipConfigFile_StoreWord(address, record->driverIndex);
			memcpy(image + address + 2 - 0x4924d, record->name, sizeof(record->name));
			SlipConfigFile_StoreDword(address + 0x22, record->lapTime);
			SlipConfigFile_StoreWord(address + 0x26, record->editing);
		}
	}
	for (unsigned index = 0; index < sizeof(words) / sizeof(words[0]); ++index)
		SlipConfigFile_StoreWord(words[index].address, *words[index].value);
	SlipConfigFile_StoreDword(0x492ea, (uint32_t)SlipConfig_fallbackMode);
	SlipConfigFile_StoreDword(0x492ee, SlipConfig_damageEnabled);
	for (unsigned player = 0; player < 2; ++player) {
		const SlipRaceControlBinding *const record = &SlipRace_controlBindings[player];
		unsigned address = 0x492ba + player * 16;
		SlipConfigFile_StoreWord(address, record->movementControl);
		SlipConfigFile_StoreWord(address + 2, (uint16_t)record->left);
		SlipConfigFile_StoreWord(address + 4, (uint16_t)record->right);
		SlipConfigFile_StoreWord(address + 6, (uint16_t)record->up);
		SlipConfigFile_StoreWord(address + 8, (uint16_t)record->down);
		SlipConfigFile_StoreWord(address + 10, (uint16_t)record->accelerate);
		SlipConfigFile_StoreWord(address + 12, (uint16_t)record->fire);
		SlipConfigFile_StoreWord(address + 14, (uint16_t)record->select);
		image[0x493cc - 0x4924d + player] = SlipConfigControls_calibrated[player];
		address = 0x493ce + player * 12;
		const SlipJoystickCalibration *const calibration = &SlipConfigControls_calibration[player];
		SlipConfigFile_StoreWord(address, calibration->centerX);
		SlipConfigFile_StoreWord(address + 2, calibration->centerY);
		SlipConfigFile_StoreWord(address + 4, (uint16_t)calibration->minimumX);
		SlipConfigFile_StoreWord(address + 6, (uint16_t)calibration->maximumX);
		SlipConfigFile_StoreWord(address + 8, (uint16_t)calibration->minimumY);
		SlipConfigFile_StoreWord(address + 10, (uint16_t)calibration->maximumY);
	}
}

void SlipConfigFile_Load(const char *path, const SlipConfigDeviceCalls *devices) {
	const SlipFileReadCalls *const calls = &SlipResourceHost_fileCalls;
	const uint32_t signature = SlipConfigFile_DiskDword(0x4924d);
	int32_t file;
	if (calls->open(calls->context, path, &file)) {
		if (!calls->read(calls->context, file, 0, image, sizeof(image)) ||
		    SlipConfigFile_DiskDword(0x4924d) != signature)
			SlipGame_ConfigurationFailure();
		calls->close(calls->context, file);
		uint16_t checksum[256];
		SlipChecksum_Initialize(checksum);
		if (SlipChecksum_Calculate(checksum, image, SLIP_CONFIG_PAYLOAD_BYTES) != SlipConfigFile_DiskWord(0x4989a))
			SlipGame_ConfigurationFailure();
		SlipConfigFile_ImportImage();
		if (SlipConfigControls_calibrated[0])
			SlipJoystick_SetCalibration(0, &SlipConfigControls_calibration[0]);
		if (SlipConfigControls_calibrated[1])
			SlipJoystick_SetCalibration(1, &SlipConfigControls_calibration[1]);
	} else {
		SlipConfigFile_ImportImage(); /* Native typed view of unchanged original default data. */
	}
	SlipMenuMusic_SetSetting(SlipConfig_music);
	const uint16_t joystickPresence = devices->joystickPresence(devices->context);
	SlipRaceControlBinding *const first = &SlipRace_controlBindings[0];
	SlipRaceControlBinding *const second = &SlipRace_controlBindings[1];
	if (first->movementControl == 1 && (joystickPresence & 255u) == 0) {
		first->movementControl = 0;
		if (first->accelerate == 0x82) {
			first->accelerate = (SlipInputCode)0x39;
			first->fire = (SlipInputCode)0x38;
			first->select = (SlipInputCode)0x1d;
		}
	}
	if (first->movementControl == 2 && (joystickPresence >> 8) == 0)
		first->movementControl = 0;
	if (second->movementControl == 1 && (joystickPresence & 255u) == 0)
		second->movementControl = 0;
	if (second->movementControl == 2 && (joystickPresence >> 8) == 0)
		second->movementControl = 0;
	if (devices->mousePresence(devices->context) == 0) {
		if (first->movementControl == 3)
			first->movementControl = 0;
		if (second->movementControl == 3)
			second->movementControl = 0;
	}
}

void SlipConfigFile_Save(const char *path) {
	SlipConfigFile_ExportImage();
	SlipConfig_SaveImage(path, image);
}
