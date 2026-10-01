#include "hmi_mixer_1000.h"

#include <string.h>

static uint32_t HmiMixer1000_CircularDistance(uint32_t from, uint32_t to, uint32_t size) {
	if (to >= from) {
		return to - from;
	}
	return size - from + to;
}

static uint8_t HmiMixer1000_SaturatingAdd(uint8_t output, int8_t sample) {
	int32_t sum = (int8_t)(output ^ 0x80u) + sample;
	if (sum > 127) {
		sum = 127;
	} else if (sum < -128) {
		sum = -128;
	}
	return (uint8_t)((uint8_t)(int8_t)sum ^ 0x80u);
}

static int8_t HmiMixer1000_ScaleSample(int8_t sample, int8_t coefficient) {
	const int16_t product = (int16_t)sample * (int16_t)coefficient;
	return (int8_t)((uint16_t)product >> 7);
}

static int8_t HmiMixer1000_VolumeCoefficient(int16_t volume, int16_t masterVolume) {
	const int32_t product = (int32_t)volume * (int32_t)masterVolume;
	const uint16_t highWord = (uint16_t)((uint32_t)product >> 16);
	return (int8_t)((uint16_t)(highWord << 1) >> 8);
}

static uint32_t HmiMixer1000_LimitOutputCount(const HmiDigitalVoice *voice, uint32_t requested) {
	uint32_t available;

	if ((voice->remainingSegmentBytes >> 16) != 0) {
		return requested;
	}
	available = voice->remainingSegmentBytes & 0xffffu;
	if ((voice->flags & 0x0010u) != 0) {
		available >>= 1;
	}
	if ((voice->flags & 0x0080u) != 0) {
		available >>= 1;
	}
	if ((voice->flags & 0x0400u) != 0) {
		available = (uint32_t)(((uint64_t)available << 16) / voice->playbackRateQ16);
	}
	return available < requested ? available : requested;
}

static bool HmiMixer1000_MixVoice(HmiMixer1000State *state, HmiDigitalVoice *voice, uint32_t outputCount) {
	uint32_t outputIndex = voice->outputCursor;
	uint32_t sourceAdvance = 0;
	uint16_t phase = voice->resampleFraction;
	int8_t coefficient = 0;
	uint32_t i;

	/* Mixer 1000 supports these paths too, but Slipstream's SMP descriptors do not use them yet. */
	if ((voice->flags & (0x0010u | 0x0080u)) != 0 || ((voice->flags & 0x0400u) != 0 && voice->playbackRateQ16 == 0)) {
		return false;
	}
	if ((voice->flags & 0x0100u) != 0) {
		coefficient = HmiMixer1000_VolumeCoefficient(voice->volume, state->masterVolume);
	}

	for (i = 0; i < outputCount; ++i) {
		if (outputIndex == state->dmaBufferSize) {
			outputIndex = 0;
		}
		const uint8_t sourceByte = voice->sampleCursor[sourceAdvance];
		int8_t sample = (int8_t)(sourceByte ^ 0x80u);
		if ((voice->flags & 0x0100u) != 0) {
			sample = HmiMixer1000_ScaleSample(sample, coefficient);
		}
		state->dmaBuffer[outputIndex] = HmiMixer1000_SaturatingAdd(state->dmaBuffer[outputIndex], sample);
		/* 17e7..1802 splits only when the end exceeds the DMA size;
		 * 18a1 retains an exact-end cursor until the next mix. */
		outputIndex += 1;
		if ((voice->flags & 0x0400u) != 0) {
			const uint32_t nextPhase = (uint32_t)phase + (voice->playbackRateQ16 & 0xffffu);
			sourceAdvance += (voice->playbackRateQ16 >> 16) + (nextPhase >> 16);
			phase = (uint16_t)nextPhase;
		} else {
			sourceAdvance += 1;
		}
	}

	voice->sampleCursor += sourceAdvance;
	voice->resampleFraction = phase;
	voice->outputCursor = (uint16_t)outputIndex;
	return true;
}

static uint32_t HmiMixer1000_ConsumedSourceBytes(HmiDigitalVoice *voice, uint32_t outputCount) {
	uint32_t count = outputCount;
	if ((voice->flags & 0x0010u) != 0) {
		count += count;
	}
	if ((voice->flags & 0x0080u) != 0) {
		count += count;
	}
	if ((voice->flags & 0x0400u) != 0) {
		const uint64_t product = (uint64_t)count * voice->playbackRateQ16;
		const uint32_t fractional = (uint32_t)voice->consumedSourceFraction + ((uint32_t)product & 0xffffu);
		voice->consumedSourceFraction = (uint16_t)fractional;
		count = (uint32_t)(product >> 16) + (fractional >> 16);
	}
	return count;
}

static void HmiMixer1000_DeferVoice(HmiMixer1000State *state, uint32_t voiceIndex, uint32_t dmaPosition) {
	HmiDigitalVoice *const voice = &state->driver->voices[voiceIndex];
	voice->flags |= 0x1000u;
	state->pendingVoiceIndices[state->pendingVoiceCount++] = (uint8_t)voiceIndex;
	voice->pendingOutputBytes =
	    (uint16_t)HmiMixer1000_CircularDistance(dmaPosition, voice->outputCursor, state->dmaBufferSize);
}

static void HmiMixer1000_FinishOrLoop(HmiMixer1000State *state, uint32_t voiceIndex, uint32_t dmaPosition) {
	HmiDigitalVoice *const voice = &state->driver->voices[voiceIndex];

	if ((voice->flags & 0x4000u) != 0 && voice->remainingLoops != 0) {
		if ((uint16_t)voice->remainingLoops != 0xffffu) {
			voice->remainingLoops -= 1;
		}
		if ((voice->flags & 0x0040u) != 0) {
			if (voice->remainingLoops != 0) {
				voice->sampleCursor = voice->loopBase;
				voice->remainingSegmentBytes = voice->loopSegmentBytes;
			} else {
				voice->remainingSegmentBytes = voice->trailingSegmentBytes;
			}
		} else {
			voice->sampleCursor = voice->sampleBase;
			voice->remainingSegmentBytes = voice->initialSegmentBytes;
		}
		return;
	}
	HmiMixer1000_DeferVoice(state, voiceIndex, dmaPosition);
}

void HmiMixer1000_Initialize(HmiMixer1000State *state, HmiDigitalDriver *driver, uint8_t *dmaBuffer,
                             uint32_t dmaBufferSize, uint16_t dmaChannel) {
	memset(state, 0, sizeof(*state));
	state->driver = driver;
	state->dmaBuffer = dmaBuffer;
	state->dmaBufferSize = dmaBufferSize;
	state->dmaChannel = dmaChannel;
	state->masterVolume = 0x7fff;
	memset(dmaBuffer, 0x80, dmaBufferSize);
}

bool HmiMixer1000_Update(HmiMixer1000State *state, uint32_t dmaRemainingCount) {
	uint32_t dmaPosition;
	uint32_t clearDistance;
	uint32_t mixDistance;
	uint32_t i;

	for (i = 0; i < HMI_DIGITAL_VOICE_COUNT; ++i) {
		const HmiDigitalVoice *const voice = &state->driver->voices[i];
		if ((voice->flags & 0x8000u) != 0) {
			if (voice->callbackOffset != 0) {
				return false;
			}
			if ((voice->flags & 0x1000u) == 0 && ((voice->flags & (0x0010u | 0x0080u | 0x0800u)) != 0 ||
			                                      ((voice->flags & 0x0400u) != 0 && voice->playbackRateQ16 == 0))) {
				return false;
			}
		}
	}

	dmaRemainingCount += 1;
	if (state->dmaChannel > 7) {
		dmaRemainingCount += dmaRemainingCount;
	}
	if (dmaRemainingCount > state->dmaBufferSize) {
		dmaRemainingCount = state->dmaBufferSize;
	}
	dmaPosition = state->dmaBufferSize - dmaRemainingCount;
	dmaPosition &= 0xfffffffcu;
	/* 159e..15ab adjusts the position before 15b1 subtracts the previous
	 * position. It does not adjust a zero distance. */
	if (dmaPosition == 0) {
		dmaPosition += 0x20u;
	}
	clearDistance = HmiMixer1000_CircularDistance(state->previousDmaPosition, dmaPosition, state->dmaBufferSize);
	mixDistance = clearDistance;
	mixDistance += (mixDistance >> 3) & 0xfffffffcu;

	for (i = 0; i < clearDistance; ++i) {
		state->dmaBuffer[(state->previousDmaPosition + i) % state->dmaBufferSize] = 0x80;
	}

	for (i = 0; i < state->pendingVoiceCount;) {
		HmiDigitalVoice *const voice = &state->driver->voices[state->pendingVoiceIndices[i]];
		const uint16_t previous = voice->pendingOutputBytes;
		voice->pendingOutputBytes = (uint16_t)(previous - clearDistance);
		if (previous >= clearDistance) {
			++i;
		} else {
			voice->pendingOutputBytes = 0;
			voice->flags &= 0x6fffu;
			memmove(&state->pendingVoiceIndices[i], &state->pendingVoiceIndices[i + 1],
			        state->pendingVoiceCount - i - 1);
			state->pendingVoiceCount -= 1;
		}
	}

	for (i = 0; i < HMI_DIGITAL_VOICE_COUNT; ++i) {
		HmiDigitalVoice *const voice = &state->driver->voices[i];
		uint32_t outputCount;
		uint32_t distanceToPreviousDmaPosition;
		uint32_t consumed;
		if ((voice->flags & 0x8000u) == 0 || (voice->flags & 0x1000u) != 0) {
			continue;
		}
		if ((voice->flags & 0x2000u) != 0) {
			uint32_t initialPosition = dmaPosition + 0x100u;
			voice->flags &= 0xdfffu;
			if (initialPosition > state->dmaBufferSize) {
				initialPosition -= state->dmaBufferSize;
			}
			voice->outputCursor = (uint16_t)initialPosition;
		}
		outputCount = HmiMixer1000_LimitOutputCount(voice, mixDistance);
		distanceToPreviousDmaPosition =
		    HmiMixer1000_CircularDistance(voice->outputCursor, state->previousDmaPosition, state->dmaBufferSize);
		if (outputCount > distanceToPreviousDmaPosition) {
			outputCount = distanceToPreviousDmaPosition;
		}
		if (!HmiMixer1000_MixVoice(state, voice, outputCount)) {
			return false;
		}
		consumed = HmiMixer1000_ConsumedSourceBytes(voice, outputCount);
		voice->processedSourceBytes += consumed;
		voice->remainingSegmentBytes -= consumed;
		if ((int32_t)(voice->remainingSegmentBytes - 4u) < 0) {
			HmiMixer1000_FinishOrLoop(state, i, dmaPosition);
		}
	}
	state->previousDmaPosition = dmaPosition;
	return true;
}
