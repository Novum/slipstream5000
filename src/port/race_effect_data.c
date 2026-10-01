#include "race_effects.h"

uint16_t SlipRaceEffects_blackSmokeHandles[6];
uint16_t SlipRaceEffects_finalBlackSmokeHandles[9];
uint16_t SlipRaceEffects_graySmokeHandles[6];
uint16_t SlipRaceEffects_finalGraySmokeHandles[9];

uint16_t SlipRaceEffects_fireHandles[4];
static const SlipTimedEffectFrames blackSmokeFrames = {6, 50, SlipRaceEffects_blackSmokeHandles};
static const SlipTimedEffectFrames finalBlackSmokeFrames = {9, 50, SlipRaceEffects_finalBlackSmokeHandles};
const SlipTimedEffectDescriptor SlipRaceEffects_damageSmoke = {.initialExtent = 488,
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
                                                               .dosAddress = 0x4f15c,
                                                               .initialFramesDosAddress = 0x4f1e0,
                                                               .finalFramesDosAddress = 0x4f1f0};

static const SlipTimedEffectFrames graySmokeFrames = {6, 30, SlipRaceEffects_graySmokeHandles};
static const SlipTimedEffectFrames finalGraySmokeFrames = {9, 30, SlipRaceEffects_finalGraySmokeHandles};
static const SlipTimedEffectFrames fireFrames = {4, 10, SlipRaceEffects_fireHandles};

const SlipTimedEffectDescriptor SlipRaceEffects_projectileTrail = {.initialExtent = 488,
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
                                                                   .dosAddress = 0x4f208,
                                                                   .initialFramesDosAddress = 0x4f234,
                                                                   .finalFramesDosAddress = 0x4f244};
const SlipTimedEffectDescriptor SlipRaceEffects_weaponSmoke = {.initialExtent = 2440,
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
                                                               .dosAddress = 0x4f1b4,
                                                               .initialFramesDosAddress = 0x4f234,
                                                               .finalFramesDosAddress = 0x4f244};
