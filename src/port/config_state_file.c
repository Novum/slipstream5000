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

/* Byte offsets in the saved configuration image. Keep these independent of
 * native structure sizes so existing save files retain their layout. */
enum {
	SLIP_CONFIG_OFFSET_SIGNATURE = 0,
	SLIP_CONFIG_OFFSET_CONTROLS = 109,
	SLIP_CONFIG_OFFSET_FALLBACK_MODE = 157,
	SLIP_CONFIG_OFFSET_DAMAGE_ENABLED = 161,
	SLIP_CONFIG_OFFSET_CALIBRATED = 383,
	SLIP_CONFIG_OFFSET_CALIBRATION = 385,
	SLIP_CONFIG_OFFSET_LAP_RECORDS = 413,
	SLIP_CONFIG_OFFSET_CHECKSUM = 1613,
	SLIP_CONFIG_OFFSET_REAR_MONITOR = 141,
	SLIP_CONFIG_OFFSET_WEAPONS_MONITOR = 143,
	SLIP_CONFIG_OFFSET_SPEED_DISPLAY = 145,
	SLIP_CONFIG_OFFSET_SOUND_EFFECTS = 147,
	SLIP_CONFIG_OFFSET_ENGINE_SOUNDS = 149,
	SLIP_CONFIG_OFFSET_SPEECH = 151,
	SLIP_CONFIG_OFFSET_MUSIC = 153,
	SLIP_CONFIG_OFFSET_LANGUAGE = 155,
	SLIP_CONFIG_OFFSET_ENVIRONMENT_DETAIL = 165,
	SLIP_CONFIG_OFFSET_CLOUDS = 167,
	SLIP_CONFIG_OFFSET_SHADING = 169,
	SLIP_CONFIG_OFFSET_TEXTURES = 171,
	SLIP_CONFIG_OFFSET_SHADOWS = 173,
	SLIP_CONFIG_OFFSET_WINDOW_SIZE = 175,
	SLIP_CONFIG_OFFSET_TRACK_MAP = 177,
	SLIP_CONFIG_OFFSET_TRACK_PROGRESS = 409,
	SLIP_CONFIG_OFFSET_REVERSE_ACCELERATOR = 411,
	SLIP_CONFIG_CONTROL_MOVEMENT_OFFSET = 0,
	SLIP_CONFIG_CONTROL_LEFT_OFFSET = 2,
	SLIP_CONFIG_CONTROL_RIGHT_OFFSET = 4,
	SLIP_CONFIG_CONTROL_UP_OFFSET = 6,
	SLIP_CONFIG_CONTROL_DOWN_OFFSET = 8,
	SLIP_CONFIG_CONTROL_ACCELERATE_OFFSET = 10,
	SLIP_CONFIG_CONTROL_FIRE_OFFSET = 12,
	SLIP_CONFIG_CONTROL_SELECT_OFFSET = 14,
	SLIP_CONFIG_CALIBRATION_CENTER_X_OFFSET = 0,
	SLIP_CONFIG_CALIBRATION_CENTER_Y_OFFSET = 2,
	SLIP_CONFIG_CALIBRATION_MINIMUM_X_OFFSET = 4,
	SLIP_CONFIG_CALIBRATION_MAXIMUM_X_OFFSET = 6,
	SLIP_CONFIG_CALIBRATION_MINIMUM_Y_OFFSET = 8,
	SLIP_CONFIG_CALIBRATION_MAXIMUM_Y_OFFSET = 10,
	SLIP_CONFIG_CONTROL_RECORD_BYTES = 16,
	SLIP_CONFIG_CALIBRATION_RECORD_BYTES = 12,
	SLIP_CONFIG_LAP_RECORD_BYTES = 40,
	SLIP_CONFIG_LAP_TRACK_BYTES = SLIP_LAP_RECORDS_ROW_COUNT * SLIP_CONFIG_LAP_RECORD_BYTES,
	SLIP_CONFIG_LAP_NAME_OFFSET = 2,
	SLIP_CONFIG_LAP_TIME_OFFSET = 34,
	SLIP_CONFIG_LAP_EDITING_OFFSET = 38
};

SlipLapRecordTable SlipConfig_lapRecords;

/* Serialized file image, not a raw view of native game-state memory. Fields
 * still owned by untranslated screens retain their on-disk representation. */
static uint8_t image[SLIP_CONFIG_FILE_BYTES] = {
#include "config_default_image.inc"
};

static uint16_t SlipConfigFile_DiskWord(unsigned offset) { return SlipBytes_ReadLE16(image + offset); }

static uint32_t SlipConfigFile_DiskDword(unsigned offset) { return SlipBytes_ReadLE32(image + offset); }

static void SlipConfigFile_StoreWord(unsigned offset, uint16_t value) {
	image[offset] = (uint8_t)value;
	image[offset + 1] = (uint8_t)(value >> 8);
}

static void SlipConfigFile_StoreDword(unsigned offset, uint32_t value) {
	SlipConfigFile_StoreWord(offset, (uint16_t)value);
	SlipConfigFile_StoreWord(offset + 2, (uint16_t)(value >> 16));
}

/* Typed serialization bindings for the original configuration fields. */
typedef struct ConfigWordBinding {
	unsigned offset;
	uint16_t *value;
} ConfigWordBinding;

static const ConfigWordBinding words[] = {
    {SLIP_CONFIG_OFFSET_REAR_MONITOR, &SlipConfig_rearMonitor},
    {SLIP_CONFIG_OFFSET_WEAPONS_MONITOR, &SlipConfig_weaponsMonitor},
    {SLIP_CONFIG_OFFSET_SPEED_DISPLAY, &SlipConfig_speedDisplay},
    {SLIP_CONFIG_OFFSET_SOUND_EFFECTS, &SlipConfig_soundEffects},
    {SLIP_CONFIG_OFFSET_ENGINE_SOUNDS, &SlipConfig_engineSounds},
    {SLIP_CONFIG_OFFSET_SPEECH, &SlipConfig_speech},
    {SLIP_CONFIG_OFFSET_MUSIC, &SlipConfig_music},
    {SLIP_CONFIG_OFFSET_LANGUAGE, &SlipConfig_language},
    {SLIP_CONFIG_OFFSET_ENVIRONMENT_DETAIL, &SlipConfig_environmentDetail},
    {SLIP_CONFIG_OFFSET_CLOUDS, &SlipConfig_clouds},
    {SLIP_CONFIG_OFFSET_SHADING, &SlipConfig_shading},
    {SLIP_CONFIG_OFFSET_TEXTURES, &SlipConfig_textures},
    {SLIP_CONFIG_OFFSET_SHADOWS, &SlipConfig_shadows},
    {SLIP_CONFIG_OFFSET_WINDOW_SIZE, &SlipConfig_windowSize},
    {SLIP_CONFIG_OFFSET_TRACK_MAP, &SlipConfig_trackMap},
    {SLIP_CONFIG_OFFSET_TRACK_PROGRESS, &SlipConfig_trackProgress},
    {SLIP_CONFIG_OFFSET_REVERSE_ACCELERATOR, &SlipRace_reverseAccelerator},
};

static void SlipConfigFile_ImportImage(void) {
	for (unsigned track = 0; track < SLIP_RACE_TRACK_COUNT; ++track) {
		for (unsigned position = 0; position < SLIP_LAP_RECORDS_ROW_COUNT; ++position) {
			const unsigned offset = SLIP_CONFIG_OFFSET_LAP_RECORDS + track * SLIP_CONFIG_LAP_TRACK_BYTES +
			                        position * SLIP_CONFIG_LAP_RECORD_BYTES;
			SlipLapRecord *const record = &SlipConfig_lapRecords.tracks[track][position];
			record->driverIndex = SlipConfigFile_DiskWord(offset);
			memcpy(record->name, image + offset + SLIP_CONFIG_LAP_NAME_OFFSET, sizeof(record->name));
			record->lapTime = SlipConfigFile_DiskDword(offset + SLIP_CONFIG_LAP_TIME_OFFSET);
			record->editing = SlipConfigFile_DiskWord(offset + SLIP_CONFIG_LAP_EDITING_OFFSET);
		}
	}
	for (unsigned index = 0; index < sizeof(words) / sizeof(words[0]); ++index)
		*words[index].value = SlipConfigFile_DiskWord(words[index].offset);
	SlipConfig_fallbackMode = (int32_t)SlipConfigFile_DiskDword(SLIP_CONFIG_OFFSET_FALLBACK_MODE);
	SlipConfig_damageEnabled = SlipConfigFile_DiskDword(SLIP_CONFIG_OFFSET_DAMAGE_ENABLED);
	for (unsigned player = 0; player < 2; ++player) {
		SlipRaceControlBinding *const record = &SlipRace_controlBindings[player];
		unsigned offset = SLIP_CONFIG_OFFSET_CONTROLS + player * SLIP_CONFIG_CONTROL_RECORD_BYTES;
		record->movementControl = SlipConfigFile_DiskWord(offset);
		record->left = (SlipInputCode)SlipConfigFile_DiskWord(offset + SLIP_CONFIG_CONTROL_LEFT_OFFSET);
		record->right = (SlipInputCode)SlipConfigFile_DiskWord(offset + SLIP_CONFIG_CONTROL_RIGHT_OFFSET);
		record->up = (SlipInputCode)SlipConfigFile_DiskWord(offset + SLIP_CONFIG_CONTROL_UP_OFFSET);
		record->down = (SlipInputCode)SlipConfigFile_DiskWord(offset + SLIP_CONFIG_CONTROL_DOWN_OFFSET);
		record->accelerate = (SlipInputCode)SlipConfigFile_DiskWord(offset + SLIP_CONFIG_CONTROL_ACCELERATE_OFFSET);
		record->fire = (SlipInputCode)SlipConfigFile_DiskWord(offset + SLIP_CONFIG_CONTROL_FIRE_OFFSET);
		record->select = (SlipInputCode)SlipConfigFile_DiskWord(offset + SLIP_CONFIG_CONTROL_SELECT_OFFSET);
		SlipConfigControls_calibrated[player] = image[SLIP_CONFIG_OFFSET_CALIBRATED + player];
		offset = SLIP_CONFIG_OFFSET_CALIBRATION + player * SLIP_CONFIG_CALIBRATION_RECORD_BYTES;
		SlipConfigControls_calibration[player] = (SlipJoystickCalibration){
		    SlipConfigFile_DiskWord(offset),
		    SlipConfigFile_DiskWord(offset + SLIP_CONFIG_CALIBRATION_CENTER_Y_OFFSET),
		    (int16_t)SlipConfigFile_DiskWord(offset + SLIP_CONFIG_CALIBRATION_MINIMUM_X_OFFSET),
		    (int16_t)SlipConfigFile_DiskWord(offset + SLIP_CONFIG_CALIBRATION_MAXIMUM_X_OFFSET),
		    (int16_t)SlipConfigFile_DiskWord(offset + SLIP_CONFIG_CALIBRATION_MINIMUM_Y_OFFSET),
		    (int16_t)SlipConfigFile_DiskWord(offset + SLIP_CONFIG_CALIBRATION_MAXIMUM_Y_OFFSET)};
	}
}

static void SlipConfigFile_ExportImage(void) {
	for (unsigned track = 0; track < SLIP_RACE_TRACK_COUNT; ++track) {
		for (unsigned position = 0; position < SLIP_LAP_RECORDS_ROW_COUNT; ++position) {
			const unsigned offset = SLIP_CONFIG_OFFSET_LAP_RECORDS + track * SLIP_CONFIG_LAP_TRACK_BYTES +
			                        position * SLIP_CONFIG_LAP_RECORD_BYTES;
			const SlipLapRecord *const record = &SlipConfig_lapRecords.tracks[track][position];
			SlipConfigFile_StoreWord(offset, record->driverIndex);
			memcpy(image + offset + SLIP_CONFIG_LAP_NAME_OFFSET, record->name, sizeof(record->name));
			SlipConfigFile_StoreDword(offset + SLIP_CONFIG_LAP_TIME_OFFSET, record->lapTime);
			SlipConfigFile_StoreWord(offset + SLIP_CONFIG_LAP_EDITING_OFFSET, record->editing);
		}
	}
	for (unsigned index = 0; index < sizeof(words) / sizeof(words[0]); ++index)
		SlipConfigFile_StoreWord(words[index].offset, *words[index].value);
	SlipConfigFile_StoreDword(SLIP_CONFIG_OFFSET_FALLBACK_MODE, (uint32_t)SlipConfig_fallbackMode);
	SlipConfigFile_StoreDword(SLIP_CONFIG_OFFSET_DAMAGE_ENABLED, SlipConfig_damageEnabled);
	for (unsigned player = 0; player < 2; ++player) {
		const SlipRaceControlBinding *const record = &SlipRace_controlBindings[player];
		unsigned offset = SLIP_CONFIG_OFFSET_CONTROLS + player * SLIP_CONFIG_CONTROL_RECORD_BYTES;
		SlipConfigFile_StoreWord(offset, record->movementControl);
		SlipConfigFile_StoreWord(offset + SLIP_CONFIG_CONTROL_LEFT_OFFSET, (uint16_t)record->left);
		SlipConfigFile_StoreWord(offset + SLIP_CONFIG_CONTROL_RIGHT_OFFSET, (uint16_t)record->right);
		SlipConfigFile_StoreWord(offset + SLIP_CONFIG_CONTROL_UP_OFFSET, (uint16_t)record->up);
		SlipConfigFile_StoreWord(offset + SLIP_CONFIG_CONTROL_DOWN_OFFSET, (uint16_t)record->down);
		SlipConfigFile_StoreWord(offset + SLIP_CONFIG_CONTROL_ACCELERATE_OFFSET, (uint16_t)record->accelerate);
		SlipConfigFile_StoreWord(offset + SLIP_CONFIG_CONTROL_FIRE_OFFSET, (uint16_t)record->fire);
		SlipConfigFile_StoreWord(offset + SLIP_CONFIG_CONTROL_SELECT_OFFSET, (uint16_t)record->select);
		image[SLIP_CONFIG_OFFSET_CALIBRATED + player] = SlipConfigControls_calibrated[player];
		offset = SLIP_CONFIG_OFFSET_CALIBRATION + player * SLIP_CONFIG_CALIBRATION_RECORD_BYTES;
		const SlipJoystickCalibration *const calibration = &SlipConfigControls_calibration[player];
		SlipConfigFile_StoreWord(offset, calibration->centerX);
		SlipConfigFile_StoreWord(offset + SLIP_CONFIG_CALIBRATION_CENTER_Y_OFFSET, calibration->centerY);
		SlipConfigFile_StoreWord(offset + SLIP_CONFIG_CALIBRATION_MINIMUM_X_OFFSET, (uint16_t)calibration->minimumX);
		SlipConfigFile_StoreWord(offset + SLIP_CONFIG_CALIBRATION_MAXIMUM_X_OFFSET, (uint16_t)calibration->maximumX);
		SlipConfigFile_StoreWord(offset + SLIP_CONFIG_CALIBRATION_MINIMUM_Y_OFFSET, (uint16_t)calibration->minimumY);
		SlipConfigFile_StoreWord(offset + SLIP_CONFIG_CALIBRATION_MAXIMUM_Y_OFFSET, (uint16_t)calibration->maximumY);
	}
}

void SlipConfigFile_Load(const char *path, const SlipConfigDeviceCalls *devices) {
	const SlipFileReadCalls *const calls = &SlipResourceHost_fileCalls;
	const uint32_t signature = SlipConfigFile_DiskDword(SLIP_CONFIG_OFFSET_SIGNATURE);
	int32_t file;
	if (calls->open(calls->context, path, &file)) {
		if (!calls->read(calls->context, file, 0, image, sizeof(image)) ||
		    SlipConfigFile_DiskDword(SLIP_CONFIG_OFFSET_SIGNATURE) != signature)
			SlipGame_ConfigurationFailure();
		calls->close(calls->context, file);
		uint16_t checksum[SLIP_CHECKSUM_TABLE_COUNT];
		SlipChecksum_Initialize(checksum);
		if (SlipChecksum_Calculate(checksum, image, SLIP_CONFIG_PAYLOAD_BYTES) !=
		    SlipConfigFile_DiskWord(SLIP_CONFIG_OFFSET_CHECKSUM))
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
	if (first->movementControl == SLIP_MOVEMENT_JOYSTICK_ONE && (joystickPresence & UINT8_MAX) == 0) {
		first->movementControl = SLIP_MOVEMENT_KEYBOARD;
		if (first->accelerate == SLIP_INPUT_JOYSTICK_1_BUTTON_1) {
			first->accelerate = SLIP_INPUT_SCAN_SPACE;
			first->fire = SLIP_INPUT_SCAN_ALT;
			first->select = SLIP_INPUT_SCAN_CONTROL;
		}
	}
	if (first->movementControl == SLIP_MOVEMENT_JOYSTICK_TWO && (joystickPresence >> 8) == 0)
		first->movementControl = SLIP_MOVEMENT_KEYBOARD;
	if (second->movementControl == SLIP_MOVEMENT_JOYSTICK_ONE && (joystickPresence & UINT8_MAX) == 0)
		second->movementControl = SLIP_MOVEMENT_KEYBOARD;
	if (second->movementControl == SLIP_MOVEMENT_JOYSTICK_TWO && (joystickPresence >> 8) == 0)
		second->movementControl = SLIP_MOVEMENT_KEYBOARD;
	if (devices->mousePresence(devices->context) == 0) {
		if (first->movementControl == SLIP_MOVEMENT_MOUSE)
			first->movementControl = SLIP_MOVEMENT_KEYBOARD;
		if (second->movementControl == SLIP_MOVEMENT_MOUSE)
			second->movementControl = SLIP_MOVEMENT_KEYBOARD;
	}
}

void SlipConfigFile_Save(const char *path) {
	SlipConfigFile_ExportImage();
	SlipConfig_SaveImage(path, image);
}
