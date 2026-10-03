#ifndef SLIPSTREAM5000_HMI_MIXER_1000_H
#define SLIPSTREAM5000_HMI_MIXER_1000_H

#include "hmi_digital.h"

#include <stdbool.h>
#include <stdint.h>

enum {
	HMI_MIXER_VOLUME_COEFFICIENT_BITS = 7,
	HMI_MIXER_BYTE_DMA_CHANNEL_MAXIMUM = 7,
	HMI_MIXER_OUTPUT_ALIGNMENT_BYTES = 4,
	HMI_MIXER_OUTPUT_ALIGNMENT_MASK = ~(HMI_MIXER_OUTPUT_ALIGNMENT_BYTES - 1u),
	HMI_MIXER_ZERO_DMA_POSITION_BYTES = 32,
	HMI_MIXER_INITIAL_OUTPUT_LEAD_BYTES = 256,
	HMI_MIXER_OUTPUT_LEAD_SHIFT = 3,
	HMI_MIXER_SEGMENT_END_THRESHOLD_BYTES = 4
};

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
