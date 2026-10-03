#ifndef SLIPSTREAM5000_HMI_DIGITAL_H
#define SLIPSTREAM5000_HMI_DIGITAL_H

#include <stddef.h>
#include <stdint.h>

#define HMI_DIGITAL_VOICE_COUNT 32u

/* Unsupported format bits 0010/0080 each double source-byte consumption;
 * 0800 selects another unimplemented mixer path. Their format identities
 * are not established by the current implementation. */
enum {
	HMI_DIGITAL_ACTIVE = 0x8000,
	HMI_DIGITAL_DRAINING = 0x1000,
	HMI_DIGITAL_INITIALIZE_OUTPUT_CURSOR = 0x2000,
	HMI_DIGITAL_LOOP = 0x4000,
	HMI_DIGITAL_LOOP_SEGMENT = 0x40,
	HMI_DIGITAL_APPLY_VOLUME = 0x100,
	HMI_DIGITAL_RESAMPLE = 0x400,
	HMI_DIGITAL_UNSUPPORTED_FORMAT_0010 = 0x10,
	HMI_DIGITAL_UNSUPPORTED_FORMAT_0080 = 0x80,
	HMI_DIGITAL_UNSUPPORTED_FORMAT_0800 = 0x800,
	HMI_DIGITAL_ACTIVE_CLEAR_MASK = UINT16_MAX ^ HMI_DIGITAL_ACTIVE,
	HMI_DIGITAL_CURSOR_INITIALIZED_MASK = UINT16_MAX ^ HMI_DIGITAL_INITIALIZE_OUTPUT_CURSOR,
	HMI_DIGITAL_DRAINED_CLEAR_MASK = UINT16_MAX ^ (HMI_DIGITAL_ACTIVE | HMI_DIGITAL_DRAINING),
	HMI_DIGITAL_START_FLAGS = HMI_DIGITAL_ACTIVE | HMI_DIGITAL_INITIALIZE_OUTPUT_CURSOR,
	HMI_DIGITAL_UNSUPPORTED_START_VERSION = 0xe106,
	HMI_DIGITAL_ERROR_INVALID_HANDLE = 10,
	HMI_DIGITAL_STATUS_STOPPED = 1,
	HMI_DIGITAL_FRACTION_BITS = 16,
	HMI_DIGITAL_RATE_ONE_Q16 = 1 << HMI_DIGITAL_FRACTION_BITS,
	HMI_DIGITAL_LOOP_FOREVER = -1,
	HMI_DIGITAL_PCM_SILENCE = 0x80
};

typedef struct HmiDigitalSampleDescriptor {
	const uint8_t *sampleData;
	uint32_t sampleLength;
	int16_t loopCount;
	uint16_t channel;
	int16_t volume;
	int16_t sampleID;
	uint32_t callbackOffset;
	uint16_t callbackSelector;
	uint16_t samplePort;
	uint16_t flags;
	uint32_t totalSampleBytes;
	uint32_t loopStart;
	uint32_t loopLength;
	uint32_t playbackRateQ16;
	uint16_t panLocation;
	uint16_t panSpeed;
	uint16_t panDirection;
	uint16_t panStart;
	uint16_t panEnd;
	uint16_t delayBytes;
	uint16_t delayRepeat;
} HmiDigitalSampleDescriptor;

typedef struct HmiDigitalVoice {
	const uint8_t *sampleBase;
	const uint8_t *sampleCursor;
	const uint8_t *loopBase;
	uint32_t initialSegmentBytes;
	uint32_t remainingSegmentBytes;
	uint32_t loopSegmentBytes;
	uint16_t pendingOutputBytes;
	uint32_t trailingSegmentBytes;
	uint16_t flags;
	int16_t volume;
	int16_t sampleID;
	uint16_t channel;
	int16_t remainingLoops;
	uint16_t outputCursor;
	uint32_t callbackOffset;
	uint16_t callbackSelector;
	uint32_t playbackRateQ16;
	uint16_t resampleFraction;
	uint16_t samplePort;
	uint32_t processedSourceBytes;
	uint32_t sampleLength;
	uint16_t panLocation;
	uint16_t panSpeed;
	uint16_t panDirection;
	uint16_t panStart;
	uint16_t panEnd;
	uint16_t delayBytes;
	uint16_t delayRepeat;
	uint32_t adpcmPredictedSample;
	uint16_t adpcmStepIndex;
	uint16_t consumedSourceFraction;
} HmiDigitalVoice;

typedef struct HmiDigitalDriver {
	uint32_t version;
	HmiDigitalVoice voices[HMI_DIGITAL_VOICE_COUNT];
} HmiDigitalDriver;

void HmiDigitalDriver_Reset(HmiDigitalDriver *driver, uint32_t version);
int32_t HmiDigital_StartSample(HmiDigitalDriver *driver, const HmiDigitalSampleDescriptor *descriptor);
uint32_t HmiDigital_StopSample(HmiDigitalDriver *driver, uint32_t voiceIndex);
int32_t HmiDigital_SetVolume(HmiDigitalDriver *driver, uint32_t voiceIndex, int16_t value);
uint32_t HmiDigital_SetPlaybackRate(HmiDigitalDriver *driver, uint32_t voiceIndex, uint32_t value);
uint32_t HmiDigital_ActiveVoiceCount(const HmiDigitalDriver *driver);
uint32_t HmiDigital_VoiceStatus(const HmiDigitalDriver *driver, uint32_t voiceIndex);

#endif
