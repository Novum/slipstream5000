#include "config_settings.h"

uint16_t SlipConfig_trackProgress = 1;
uint32_t SlipConfig_trackProgressOverride;

uint32_t SlipConfig_TrackProgress(void) {
	uint32_t progress = SlipConfig_trackProgress;
	if (SlipConfig_trackProgressOverride != 0)
		progress = SlipConfig_trackProgressOverride;
	return progress;
}

void SlipConfig_SetTrackProgress(uint16_t progress) { SlipConfig_trackProgress = progress; }

uint16_t SlipConfig_rearMonitor = 0;

int SlipConfig_RearMonitor(void) { return SlipConfig_rearMonitor; }

void SlipConfig_ToggleRearMonitor(void) { SlipConfig_rearMonitor ^= 1u; }

uint16_t SlipConfig_weaponsMonitor = 1;

int SlipConfig_WeaponsMonitor(void) { return SlipConfig_weaponsMonitor; }

void SlipConfig_ToggleWeaponsMonitor(void) { SlipConfig_weaponsMonitor ^= 1u; }

uint16_t SlipConfig_language = SLIP_CONFIG_LANGUAGE_ENGLISH;

int SlipConfig_Language(void) { return SlipConfig_language; }

uint8_t SlipConfig_LanguageInitial(void) {
	static const uint8_t languageInitials[] = {'E', 'F', 'G'};
	return languageInitials[SlipConfig_language];
}

void SlipConfig_CycleLanguage(void) {
	SlipConfig_language = (uint16_t)(SlipConfig_language + 1u);
	if ((int16_t)SlipConfig_language >= SLIP_CONFIG_LANGUAGE_COUNT)
		SlipConfig_language = SLIP_CONFIG_LANGUAGE_ENGLISH;
}

uint16_t SlipConfig_trackMap = 1;

int SlipConfig_TrackMapEnabled(void) { return SlipConfig_trackMap; }

int SlipConfig_ToggleTrackMap(void) {
	SlipConfig_trackMap ^= 1u;

	return SlipConfig_environmentDetail;
}

uint16_t SlipConfig_speedDisplay = 0;

int SlipConfig_SpeedDisplay(void) { return SlipConfig_speedDisplay; }

void SlipConfig_ToggleSpeedDisplay(void) { SlipConfig_speedDisplay ^= 1u; }

uint16_t SlipConfig_soundEffects = 1;

int SlipConfig_SoundEffects(void) { return SlipConfig_soundEffects; }

void SlipConfig_ToggleSoundEffects(void) { SlipConfig_soundEffects ^= 1u; }

uint16_t SlipConfig_engineSounds = SLIP_CONFIG_ENGINE_SOUND_QUIET;

int SlipConfig_EngineSounds(void) { return SlipConfig_engineSounds; }

void SlipConfig_CycleEngineSounds(void) {
	SlipConfig_engineSounds = (uint16_t)(SlipConfig_engineSounds + 1u);
	if ((int16_t)SlipConfig_engineSounds >= SLIP_CONFIG_ENGINE_SOUND_COUNT)
		SlipConfig_engineSounds = SLIP_CONFIG_ENGINE_SOUND_OFF;
}

uint16_t SlipConfig_speech = 1;

int SlipConfig_Speech(void) { return SlipConfig_speech; }

void SlipConfig_ToggleSpeech(void) { SlipConfig_speech ^= 1u; }

uint16_t SlipConfig_music = 1;

int SlipConfig_Music(void) { return SlipConfig_music; }

void SlipConfig_CycleMusic(void) {
	SlipConfig_music = (uint16_t)(SlipConfig_music + 1u);
	if ((int16_t)SlipConfig_music >= SLIP_CONFIG_MUSIC_SETTING_COUNT)
		SlipConfig_music = 0;
}

uint16_t SlipConfig_environmentDetail = SLIP_CONFIG_ENVIRONMENT_HIGH;

int SlipConfig_EnvironmentDetail(void) { return SlipConfig_environmentDetail; }

void SlipConfig_CycleEnvironmentDetail(void) {
	SlipConfig_environmentDetail = (uint16_t)(SlipConfig_environmentDetail + 1u);
	if ((int16_t)SlipConfig_environmentDetail >= SLIP_CONFIG_ENVIRONMENT_COUNT)
		SlipConfig_environmentDetail = SLIP_CONFIG_ENVIRONMENT_VERY_LOW;
}

uint16_t SlipConfig_clouds = 1;

int SlipConfig_CloudsEnabled(void) { return SlipConfig_clouds; }

void SlipConfig_ToggleClouds(void) { SlipConfig_clouds ^= 1u; }

uint16_t SlipConfig_shading = 2;

int SlipConfig_Shading(void) { return SlipConfig_shading; }

void SlipConfig_CycleShading(void) {
	SlipConfig_shading = (uint16_t)(SlipConfig_shading + 1u);
	if (SlipConfig_shading == SLIP_CONFIG_SHADING_SETTING_COUNT)
		SlipConfig_shading = 0;
}

uint16_t SlipConfig_textures = SLIP_CONFIG_TEXTURE_FINE;

int SlipConfig_Textures(void) { return SlipConfig_textures; }

void SlipConfig_CycleTextures(void) {
	SlipConfig_textures = (uint16_t)(SlipConfig_textures + 1u);
	if ((int16_t)SlipConfig_textures >= SLIP_CONFIG_TEXTURE_COUNT)
		SlipConfig_textures = SLIP_CONFIG_TEXTURE_COARSE;
}

uint16_t SlipConfig_windowSize = 0;

int SlipConfig_WindowSize(void) { return SlipConfig_windowSize; }

void SlipConfig_ToggleWindowSize(void) { SlipConfig_windowSize ^= 1u; }

uint16_t SlipConfig_shadows = 1;

int SlipConfig_Shadows(void) { return SlipConfig_shadows; }

void SlipConfig_ToggleShadows(void) { SlipConfig_shadows ^= 1u; }

const char *SlipConfig_LanguageName(void) {
	static const char *const names[] = {"English", "Francais", "Deutsch"};
	return names[SlipConfig_language];
}

const char *SlipConfig_EnvironmentName(void) {
	static const char *const names[] = {"Very Low", "Low", "Medium", "High"};
	return names[SlipConfig_environmentDetail];
}
