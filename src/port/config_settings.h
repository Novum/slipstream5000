#ifndef SLIPSTREAM5000_CONFIG_SETTINGS_H
#define SLIPSTREAM5000_CONFIG_SETTINGS_H
#include <stdint.h>

enum {
	SLIP_CONFIG_TRACK_COUNT = 10,
	SLIP_CONFIG_DIFFICULTY_COUNT = 3,
	SLIP_CONFIG_DIFFICULTY_MAXIMUM = SLIP_CONFIG_DIFFICULTY_COUNT - 1
};

typedef enum SlipConfigLanguage {
	SLIP_CONFIG_LANGUAGE_ENGLISH = 0,
	SLIP_CONFIG_LANGUAGE_FRENCH = 1,
	SLIP_CONFIG_LANGUAGE_GERMAN = 2,
	SLIP_CONFIG_LANGUAGE_COUNT = 3
} SlipConfigLanguage;

typedef enum SlipConfigEngineSound {
	SLIP_CONFIG_ENGINE_SOUND_OFF = 0,
	SLIP_CONFIG_ENGINE_SOUND_QUIET = 1,
	SLIP_CONFIG_ENGINE_SOUND_LOUD = 2,
	SLIP_CONFIG_ENGINE_SOUND_COUNT = 3
} SlipConfigEngineSound;

enum { SLIP_CONFIG_MUSIC_SETTING_COUNT = 3, SLIP_CONFIG_SHADING_SETTING_COUNT = 3 };

typedef enum SlipConfigEnvironmentDetail {
	SLIP_CONFIG_ENVIRONMENT_VERY_LOW = 0,
	SLIP_CONFIG_ENVIRONMENT_LOW = 1,
	SLIP_CONFIG_ENVIRONMENT_MEDIUM = 2,
	SLIP_CONFIG_ENVIRONMENT_HIGH = 3,
	SLIP_CONFIG_ENVIRONMENT_COUNT = 4
} SlipConfigEnvironmentDetail;

typedef enum SlipConfigTextureMode {
	SLIP_CONFIG_TEXTURE_OFF = 0,
	SLIP_CONFIG_TEXTURE_COARSE = 1,
	SLIP_CONFIG_TEXTURE_FINE = 2,
	SLIP_CONFIG_TEXTURE_COUNT = 3
} SlipConfigTextureMode;

extern uint16_t SlipConfig_trackProgress;
extern uint32_t SlipConfig_trackProgressOverride;
uint32_t SlipConfig_TrackProgress(void);
void SlipConfig_SetTrackProgress(uint16_t progress);

extern uint16_t SlipConfig_rearMonitor;
int SlipConfig_RearMonitor(void);
void SlipConfig_ToggleRearMonitor(void);
extern uint16_t SlipConfig_weaponsMonitor;
int SlipConfig_WeaponsMonitor(void);
void SlipConfig_ToggleWeaponsMonitor(void);
extern uint16_t SlipConfig_language;
int SlipConfig_Language(void);
uint8_t SlipConfig_LanguageInitial(void);
void SlipConfig_CycleLanguage(void);
extern uint16_t SlipConfig_trackMap;
int SlipConfig_TrackMapEnabled(void);
int SlipConfig_ToggleTrackMap(void);
extern uint16_t SlipConfig_speedDisplay;
int SlipConfig_SpeedDisplay(void);
void SlipConfig_ToggleSpeedDisplay(void);
extern uint16_t SlipConfig_soundEffects;
int SlipConfig_SoundEffects(void);
void SlipConfig_ToggleSoundEffects(void);
extern uint16_t SlipConfig_engineSounds;
int SlipConfig_EngineSounds(void);
void SlipConfig_CycleEngineSounds(void);
extern uint16_t SlipConfig_speech;
int SlipConfig_Speech(void);
void SlipConfig_ToggleSpeech(void);
extern uint16_t SlipConfig_music;
int SlipConfig_Music(void);
void SlipConfig_CycleMusic(void);
extern uint16_t SlipConfig_environmentDetail;
int SlipConfig_EnvironmentDetail(void);
void SlipConfig_CycleEnvironmentDetail(void);
extern uint16_t SlipConfig_clouds;
int SlipConfig_CloudsEnabled(void);
void SlipConfig_ToggleClouds(void);
extern uint16_t SlipConfig_shading;
int SlipConfig_Shading(void);
void SlipConfig_CycleShading(void);
extern uint16_t SlipConfig_textures;
int SlipConfig_Textures(void);
void SlipConfig_CycleTextures(void);
extern uint16_t SlipConfig_windowSize;
int SlipConfig_WindowSize(void);
void SlipConfig_ToggleWindowSize(void);
extern uint16_t SlipConfig_shadows;
int SlipConfig_Shadows(void);
void SlipConfig_ToggleShadows(void);
const char *SlipConfig_LanguageName(void);
const char *SlipConfig_EnvironmentName(void);
#endif
