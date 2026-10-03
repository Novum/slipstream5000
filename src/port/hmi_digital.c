#include "hmi_digital.h"

#include <string.h>

void HmiDigitalDriver_Reset(HmiDigitalDriver *driver, uint32_t version) {
	memset(driver, 0, sizeof(*driver));
	driver->version = version;
}

int32_t HmiDigital_StartSample(HmiDigitalDriver *driver, const HmiDigitalSampleDescriptor *descriptor) {
	uint32_t voiceIndex;
	HmiDigitalVoice *voice;

	if (driver->version >= HMI_DIGITAL_UNSUPPORTED_START_VERSION) {
		return 0;
	}

	for (voiceIndex = 0; voiceIndex < HMI_DIGITAL_VOICE_COUNT; ++voiceIndex) {
		voice = &driver->voices[voiceIndex];
		if ((voice->flags & HMI_DIGITAL_ACTIVE) == 0) {
			voice->sampleBase = descriptor->sampleData;
			voice->sampleCursor = descriptor->sampleData;
			voice->loopBase = descriptor->sampleData + descriptor->loopStart;
			if ((descriptor->flags & HMI_DIGITAL_LOOP_SEGMENT) == 0) {
				voice->initialSegmentBytes = descriptor->sampleLength;
				voice->remainingSegmentBytes = descriptor->sampleLength;
			} else {
				voice->initialSegmentBytes = descriptor->loopStart;
				voice->remainingSegmentBytes = descriptor->loopStart;
				voice->loopSegmentBytes = descriptor->loopLength;
				voice->trailingSegmentBytes =
				    descriptor->totalSampleBytes - (descriptor->loopStart + descriptor->loopLength);
			}
			voice->volume = descriptor->volume;
			voice->sampleID = descriptor->sampleID;
			voice->flags = descriptor->flags | HMI_DIGITAL_START_FLAGS;
			voice->channel = descriptor->channel;
			voice->callbackOffset = descriptor->callbackOffset;
			voice->callbackSelector = descriptor->callbackSelector;
			voice->remainingLoops = descriptor->loopCount;
			voice->samplePort = descriptor->samplePort;
			voice->outputCursor = 0;
			voice->playbackRateQ16 = descriptor->playbackRateQ16;
			voice->resampleFraction = 0;
			voice->processedSourceBytes = 0;
			voice->sampleLength = descriptor->sampleLength;
			voice->panLocation = descriptor->panLocation;
			voice->panSpeed = descriptor->panSpeed;
			voice->panDirection = descriptor->panDirection;
			voice->panStart = descriptor->panStart;
			voice->panEnd = descriptor->panEnd;
			voice->delayBytes = descriptor->delayBytes;
			voice->delayRepeat = descriptor->delayRepeat;
			voice->adpcmPredictedSample = 0;
			voice->adpcmStepIndex = 0;
			return (int32_t)voiceIndex;
		}
	}

	return -1;
}

uint32_t HmiDigital_StopSample(HmiDigitalDriver *driver, uint32_t voiceIndex) {
	HmiDigitalVoice *voice;

	if (voiceIndex >= HMI_DIGITAL_VOICE_COUNT) {
		return HMI_DIGITAL_ERROR_INVALID_HANDLE;
	}
	voice = &driver->voices[voiceIndex];
	if ((voice->flags & HMI_DIGITAL_ACTIVE) == 0 || (voice->flags & HMI_DIGITAL_DRAINING) != 0) {
		return 0;
	}
	voice->flags &= HMI_DIGITAL_ACTIVE_CLEAR_MASK;
	voice->sampleID = 0;
	return 0;
}

int32_t HmiDigital_SetVolume(HmiDigitalDriver *driver, uint32_t voiceIndex, int16_t value) {
	HmiDigitalVoice *const voice = &driver->voices[voiceIndex];
	int32_t previous;

	if ((voice->flags & HMI_DIGITAL_ACTIVE) == 0) {
		return 0;
	}
	previous = voice->volume;
	voice->volume = value;
	return previous;
}

uint32_t HmiDigital_SetPlaybackRate(HmiDigitalDriver *driver, uint32_t voiceIndex, uint32_t value) {
	HmiDigitalVoice *const voice = &driver->voices[voiceIndex];
	uint32_t previous;

	if ((voice->flags & HMI_DIGITAL_ACTIVE) == 0) {
		return 0;
	}
	previous = voice->playbackRateQ16;
	voice->playbackRateQ16 = value;
	return previous;
}

uint32_t HmiDigital_ActiveVoiceCount(const HmiDigitalDriver *driver) {
	uint32_t count = 0;
	uint32_t voiceIndex;

	for (voiceIndex = 0; voiceIndex < HMI_DIGITAL_VOICE_COUNT; ++voiceIndex) {
		if ((driver->voices[voiceIndex].flags & HMI_DIGITAL_ACTIVE) != 0) {
			++count;
		}
	}
	return count;
}

uint32_t HmiDigital_VoiceStatus(const HmiDigitalDriver *driver, uint32_t voiceIndex) {
	if (voiceIndex >= HMI_DIGITAL_VOICE_COUNT) {
		return HMI_DIGITAL_ERROR_INVALID_HANDLE;
	}
	if ((driver->voices[voiceIndex].flags & HMI_DIGITAL_ACTIVE) == 0) {
		return HMI_DIGITAL_STATUS_STOPPED;
	}
	return 0;
}
