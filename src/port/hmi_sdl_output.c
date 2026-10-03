#include "hmi_sdl_output.h"

#include <string.h>

static uint32_t HmiSdlOutput_DmaRemainingCount(const HmiSdlOutput *output) {
	return output->mixer->dmaBufferSize - output->dmaReadPosition - 1u;
}

bool HmiSdlOutput_Provide(HmiSdlOutput *output, SDL_AudioStream *stream, uint32_t amount) {
	while (amount != 0) {
		const uint32_t phaseRemaining = output->outputRate - output->timerPhase;
		const uint32_t untilTimer = (phaseRemaining + output->mixerTimerRate - 1u) / output->mixerTimerRate;
		const uint32_t untilWrap = output->mixer->dmaBufferSize - output->dmaReadPosition;
		uint32_t count = amount;

		if (count > untilTimer) {
			count = untilTimer;
		}
		if (count > untilWrap) {
			count = untilWrap;
		}
		if (!SDL_PutAudioStreamData(stream, output->mixer->dmaBuffer + output->dmaReadPosition, (int)count)) {
			return false;
		}
		output->dmaReadPosition += count;
		if (output->dmaReadPosition == output->mixer->dmaBufferSize) {
			output->dmaReadPosition = 0;
		}
		output->timerPhase += count * output->mixerTimerRate;
		amount -= count;
		if (output->timerPhase >= output->outputRate) {
			output->timerPhase -= output->outputRate;
			if (!HmiMixer1000_Update(output->mixer, HmiSdlOutput_DmaRemainingCount(output))) {
				output->mixerFailed = true;
				return false;
			}
		}
	}
	return true;
}

static void SDLCALL HmiSdlOutput_Callback(void *userdata, SDL_AudioStream *stream, int additionalAmount,
                                          int totalAmount) {
	HmiSdlOutput *const output = (HmiSdlOutput *)userdata;
	(void)totalAmount;

	if (additionalAmount > 0 && !output->mixerFailed) {
		HmiSdlOutput_Provide(output, stream, (uint32_t)additionalAmount);
	}
}

bool HmiSdlOutput_Open(HmiSdlOutput *output, HmiMixer1000State *mixer, uint32_t outputRate, uint32_t mixerTimerRate) {
	SDL_AudioSpec spec;

	memset(output, 0, sizeof(*output));
	if (mixer->dmaChannel > HMI_MIXER_BYTE_DMA_CHANNEL_MAXIMUM || mixer->dmaBufferSize == 0 || outputRate == 0 ||
	    mixerTimerRate == 0) {
		return false;
	}
	output->mixer = mixer;
	output->outputRate = outputRate;
	output->mixerTimerRate = mixerTimerRate;
	spec.format = SDL_AUDIO_U8;
	spec.channels = 1;
	spec.freq = (int)outputRate;
	output->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, HmiSdlOutput_Callback, output);
	if (output->stream == NULL) {
		return false;
	}
	if (!SDL_ResumeAudioStreamDevice(output->stream)) {
		SDL_DestroyAudioStream(output->stream);
		output->stream = NULL;
		return false;
	}
	return true;
}

void HmiSdlOutput_Close(HmiSdlOutput *output) {
	if (output->stream != NULL) {
		SDL_DestroyAudioStream(output->stream);
		output->stream = NULL;
	}
}

bool HmiSdlOutput_Lock(HmiSdlOutput *output) { return output->stream != NULL && SDL_LockAudioStream(output->stream); }

bool HmiSdlOutput_Unlock(HmiSdlOutput *output) {
	return output->stream != NULL && SDL_UnlockAudioStream(output->stream);
}
