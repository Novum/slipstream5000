#ifndef SLIPSTREAM5000_HMI_MIXER_1000_H
#define SLIPSTREAM5000_HMI_MIXER_1000_H

#include "hmi_digital.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct HmiMixer1000State {
	HmiDigitalDriver *driver;
	uint8_t *dmaBuffer;
	uint32_t dmaBufferSize;
	uint16_t dmaChannel;
	int16_t masterVolume;
	uint32_t previousDmaPosition;
	uint32_t pendingVoiceCount;
	uint8_t pendingVoiceIndices[HMI_DIGITAL_VOICE_COUNT];
} HmiMixer1000State;

void HmiMixer1000_Initialize(HmiMixer1000State *state, HmiDigitalDriver *driver, uint8_t *dmaBuffer,
                             uint32_t dmaBufferSize, uint16_t dmaChannel);
bool HmiMixer1000_Update(HmiMixer1000State *state, uint32_t dmaRemainingCount);

#endif
