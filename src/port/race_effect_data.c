#include "race_effects.h"

enum {
	SLIP_DAMAGE_SMOKE_DESCRIPTOR_TOKEN = 0x4f15c,
	SLIP_BLACK_SMOKE_INITIAL_FRAMES_TOKEN = 0x4f1e0,
	SLIP_BLACK_SMOKE_FINAL_FRAMES_TOKEN = 0x4f1f0,
	SLIP_PROJECTILE_TRAIL_DESCRIPTOR_TOKEN = 0x4f208,
	SLIP_GRAY_SMOKE_INITIAL_FRAMES_TOKEN = 0x4f234,
	SLIP_GRAY_SMOKE_FINAL_FRAMES_TOKEN = 0x4f244,
	SLIP_WEAPON_SMOKE_DESCRIPTOR_TOKEN = 0x4f1b4,
	SLIP_BLACK_SMOKE_FRAME_DELAY_MS = 50,
	SLIP_GRAY_SMOKE_FRAME_DELAY_MS = 30,
	SLIP_FIRE_FRAME_DELAY_MS = 10
};

uint16_t SlipRaceEffects_blackSmokeHandles[SLIP_RACE_SMOKE_INITIAL_FRAME_COUNT];
uint16_t SlipRaceEffects_finalBlackSmokeHandles[SLIP_RACE_SMOKE_FINAL_FRAME_COUNT];
uint16_t SlipRaceEffects_graySmokeHandles[SLIP_RACE_SMOKE_INITIAL_FRAME_COUNT];
uint16_t SlipRaceEffects_finalGraySmokeHandles[SLIP_RACE_SMOKE_FINAL_FRAME_COUNT];

uint16_t SlipRaceEffects_fireHandles[SLIP_RACE_FIRE_FRAME_COUNT];
static const SlipTimedEffectFrames blackSmokeFrames = {
    SLIP_RACE_SMOKE_INITIAL_FRAME_COUNT, SLIP_BLACK_SMOKE_FRAME_DELAY_MS, SlipRaceEffects_blackSmokeHandles};
static const SlipTimedEffectFrames finalBlackSmokeFrames = {
    SLIP_RACE_SMOKE_FINAL_FRAME_COUNT, SLIP_BLACK_SMOKE_FRAME_DELAY_MS, SlipRaceEffects_finalBlackSmokeHandles};
const SlipTimedEffectDescriptor SlipRaceEffects_damageSmoke = {
    .initialExtent = 488,
    .holdExtent = 3904,
    .finalExtent = 4392,
    .growthDuration = 250,
    .holdDuration = 1500,
    .finalDuration = 300,
    .initialFrames = &blackSmokeFrames,
    .finalFrames = &finalBlackSmokeFrames,
    .attachedFrames = NULL,
    .emissionPeriod = 250,
    .displacementRange = 0,
    .dosAddress = SLIP_DAMAGE_SMOKE_DESCRIPTOR_TOKEN,
    .initialFramesDosAddress = SLIP_BLACK_SMOKE_INITIAL_FRAMES_TOKEN,
    .finalFramesDosAddress = SLIP_BLACK_SMOKE_FINAL_FRAMES_TOKEN};

static const SlipTimedEffectFrames graySmokeFrames = {SLIP_RACE_SMOKE_INITIAL_FRAME_COUNT,
                                                      SLIP_GRAY_SMOKE_FRAME_DELAY_MS, SlipRaceEffects_graySmokeHandles};
static const SlipTimedEffectFrames finalGraySmokeFrames = {
    SLIP_RACE_SMOKE_FINAL_FRAME_COUNT, SLIP_GRAY_SMOKE_FRAME_DELAY_MS, SlipRaceEffects_finalGraySmokeHandles};
static const SlipTimedEffectFrames fireFrames = {SLIP_RACE_FIRE_FRAME_COUNT, SLIP_FIRE_FRAME_DELAY_MS,
                                                 SlipRaceEffects_fireHandles};

const SlipTimedEffectDescriptor SlipRaceEffects_projectileTrail = {
    .initialExtent = 488,
    .holdExtent = 976,
    .finalExtent = 976,
    .growthDuration = 100,
    .holdDuration = 400,
    .finalDuration = 200,
    .initialFrames = &graySmokeFrames,
    .finalFrames = &finalGraySmokeFrames,
    .attachedFrames = &fireFrames,
    .emissionPeriod = 200,
    .displacementRange = 0,
    .dosAddress = SLIP_PROJECTILE_TRAIL_DESCRIPTOR_TOKEN,
    .initialFramesDosAddress = SLIP_GRAY_SMOKE_INITIAL_FRAMES_TOKEN,
    .finalFramesDosAddress = SLIP_GRAY_SMOKE_FINAL_FRAMES_TOKEN};
const SlipTimedEffectDescriptor SlipRaceEffects_weaponSmoke = {
    .initialExtent = 2440,
    .holdExtent = 7808,
    .finalExtent = 9760,
    .growthDuration = 500,
    .holdDuration = 2000,
    .finalDuration = 1000,
    .initialFrames = &graySmokeFrames,
    .finalFrames = &finalGraySmokeFrames,
    .attachedFrames = NULL,
    .emissionPeriod = 400,
    .displacementRange = 5856,
    .dosAddress = SLIP_WEAPON_SMOKE_DESCRIPTOR_TOKEN,
    .initialFramesDosAddress = SLIP_GRAY_SMOKE_INITIAL_FRAMES_TOKEN,
    .finalFramesDosAddress = SLIP_GRAY_SMOKE_FINAL_FRAMES_TOKEN};
