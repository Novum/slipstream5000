#ifndef SLIPSTREAM5000_RACE_INTRO_H
#define SLIPSTREAM5000_RACE_INTRO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
	SLIP_RACE_INTRO_SCRIPT_HEADER_BYTES = 16,
	SLIP_RACE_INTRO_TRACK_NAME_BYTES = 8,
	SLIP_RACE_INTRO_STRING_NAME_OFFSET = 0,
	SLIP_RACE_INTRO_STRING_NAME_BYTES = 8,
	SLIP_RACE_INTRO_PRESENTER_VARIANT_OFFSET = 8,
	SLIP_RACE_INTRO_COUNTDOWN_OFFSET = 12
};

typedef enum SlipRaceIntroOpcode {
	SLIP_INTRO_WAIT_TRACK = 0,
	SLIP_INTRO_DELAY = 1,
	SLIP_INTRO_SPEECH = 2,
	SLIP_INTRO_END = 3,
	SLIP_INTRO_INLINE = 4,
	SLIP_INTRO_CAPTION = 5,
	SLIP_INTRO_WAIT_PROGRESS = 6
} SlipRaceIntroOpcode;

typedef struct SlipRaceIntroScript {
	size_t cursor;
	uint16_t delay;
	uint32_t caption;
	uint32_t voice;
} SlipRaceIntroScript;

typedef struct SlipRaceIntroScriptHost {
	void *context;
	uint32_t (*isStopped)(void *context, uint32_t voice);
	uint32_t (*play)(void *context, uint16_t resource, uint32_t *sampleBytes);
	void (*resetConsole)(void *context);
	bool (*raceProgress)(void *context, int32_t *progress);
	bool (*trackName)(void *context, uint8_t name[SLIP_RACE_INTRO_TRACK_NAME_BYTES]);
} SlipRaceIntroScriptHost;

typedef enum SlipRaceIntroScriptResult {
	SLIP_RACE_INTRO_SCRIPT_YIELD,
	SLIP_RACE_INTRO_SCRIPT_FINISHED,
	SLIP_RACE_INTRO_SCRIPT_INVALID
} SlipRaceIntroScriptResult;

typedef struct SlipRaceIntroResources {
	void *context;
	bool (*sampleSize)(void *context, const char *name, uint32_t *size);
	bool (*load)(void *context, const char *name, uint16_t *handle);
	void (*release)(void *context, uint16_t handle);
} SlipRaceIntroResources;

bool SlipRaceIntro_PreloadResource(uint16_t resource, uint16_t language, const SlipRaceIntroResources *resources);
bool SlipRaceIntro_ReleaseResource(uint16_t resource, const SlipRaceIntroResources *resources);
bool SlipRaceIntro_Preload(uint8_t *script, size_t scriptBytes, uint16_t languageIndex, uint32_t availableMemory,
                           const SlipRaceIntroResources *resources);
bool SlipRaceIntro_Release(const uint8_t *script, size_t scriptBytes, const SlipRaceIntroResources *resources);

SlipRaceIntroScriptResult SlipRaceIntro_Step(SlipRaceIntroScript *state, const uint8_t *script, size_t scriptBytes,
                                             uint16_t delta, const SlipRaceIntroScriptHost *host);

SlipRaceIntroScriptResult SlipRaceIntro_StepPresenter(SlipRaceIntroScript *state, const uint8_t *script, size_t bytes,
                                                      uint16_t delta, const SlipRaceIntroScriptHost *host,
                                                      uint16_t language, void (*queue)(void *, const uint8_t *, size_t),
                                                      uint32_t *speechBytes);

#endif
