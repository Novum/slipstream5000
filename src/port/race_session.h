#ifndef SLIPSTREAM5000_RACE_SESSION_H
#define SLIPSTREAM5000_RACE_SESSION_H

#include "race.h"
#include "race_intro.h"
#include "race_recording.h"
#include "sound_effects.h"

#include <stdint.h>

struct SlipRaceHudState;
extern struct SlipRaceHudState SlipRaceSession_hudState;
extern uint16_t SlipRaceSession_track;
extern uint16_t SlipRaceSession_exitRequested;
extern const SlipRaceRecordingHost SlipRaceSession_recordingHost;
extern bool SlipRaceSession_lastRenderSucceeded;

extern SlipView3DMaths SlipRaceSession_maths;
void SlipRaceSession_FireSuperSeeker(uint16_t shooter, uint16_t target);
void SlipRaceSession_FireSuperFrag(uint16_t shooter, uint16_t target);
void SlipRaceSession_FireFrag(uint16_t shooter, uint16_t target);
void SlipRaceSession_FireScrambler(uint16_t shooter, uint16_t target);
extern uint32_t SlipRaceSession_lastRawBspCallbacks;
extern uint32_t SlipRaceSession_lastRasterizedPrimitives;

typedef enum SlipRaceFrameResult {
	SLIP_RACE_FRAME_CONTINUE,
	SLIP_RACE_FRAME_OPEN_CONFIGURATION,
	SLIP_RACE_FRAME_ENDED,
	SLIP_RACE_FRAME_EXIT_PROGRAM
} SlipRaceFrameResult;

bool SlipRaceSession_DebugRenderCapturedState(const uint32_t cameraPosition[3], const uint16_t cameraMatrix[9],
                                              const uint32_t playerPosition[3], const uint16_t playerMatrix[9]);

void SlipRaceSession_Begin(const char *resPath, uint16_t axTrack, SlipRaceRacerTable *racerTable,
                           uint32_t environmentDetail, uint32_t shading, uint32_t textures, uint32_t shadows);

void SlipRaceSession_StartNew(const char *resPath, uint16_t track, SlipRaceRacerTable *racers,
                              uint32_t environmentDetail, uint32_t shading, uint32_t textures, uint32_t shadows);
void SlipRaceSession_Replay(const char *resPath, uint16_t track, SlipRaceRacerTable *racers, uint32_t environmentDetail,
                            uint32_t shading, uint32_t textures, uint32_t shadows);

SlipRaceFrameResult SlipRaceSession_RunFrame(uint32_t tick, const bool inputHeld[256], bool inputPressed[256],
                                             uint32_t windowSize, int mouseX, int mouseY);

void SlipRaceSession_ConfigurationReturn(void);
void SlipRaceSession_ApplyConfigurationValues(void);

void SlipRaceSession_BindSoundHost(SlipGameSoundState *gameSound, uint16_t soundSet, SlipSoundEffectLock lockSound,
                                   SlipSoundEffectUnlock unlockSound, void *context);

void SlipRaceSession_PlayIntro(const char *resPath, uint16_t axTrack, uint16_t selectedDriver, uint16_t language,
                               uint32_t environmentDetail, uint32_t shading, uint32_t textures, uint32_t shadows,
                               uint32_t windowSize, const bool inputHeld[256], bool inputPressed[256],
                               const SlipRaceIntroScriptHost *scriptHost, const SlipRaceIntroResources *resources);

struct SlipTimedEffect;
uint32_t SlipRaceSession_AttachTimedEffect(uint16_t object, int32_t radius, const struct SlipTimedEffect *emitter,
                                           uint32_t callerValue);
void SlipRaceSession_UpdateTimedEffect(uint16_t object, uint32_t speedResult);
uint32_t SlipRaceSession_UpdateCrossEffect(uint16_t object);
void SlipRaceSession_CreateDebris(uint32_t count, uint32_t destruction, uint16_t source,
                                  uint32_t drawPrefixOrImpulseScale);
uint32_t SlipRaceSession_DebrisEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                     uint32_t eventFlags, uint16_t object, uintptr_t dispatchData,
                                     uint32_t dispatchFrame);
void SlipRaceSession_FireSeeker(uint16_t shooter, uint16_t target);
void SlipRaceSession_FireBomber(uint16_t shooter, uint16_t target);
void SlipRaceSession_FireDisrupter(uint16_t shooter, uint16_t target);
void SlipRaceSession_FireAmbler(uint16_t shooter, uint16_t target);
void SlipRaceSession_FireHyperNeuro(uint16_t shooter, uint16_t target);
void SlipRaceSession_FireMiniMines(uint16_t shooter, uint16_t target);
#endif
