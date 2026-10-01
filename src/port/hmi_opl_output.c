#include "hmi_opl_output.h"
#include <assert.h>
#include <string.h>

static void HmiOplOutput_OplWrite(void *context, uint16_t port, uint8_t value) {
	HmiOplOutput *const output = context;
	assert(port == 0x388 || port == 0x389);
	if (port == 0x388)
		output->registerAddress = value;
	else {

		if ((output->chip.writeBufLast + 1) % OPAL_WRITEBUF_SIZE == output->chip.writeBufCur) {
			SlipAssertFail("OPL timed write queue exhausted; refusing zero-time flush", __FILE__, __LINE__);
			return;
		}
		opalWriteRegBuffered(&output->chip, output->registerAddress, value);
	}
}

static uint8_t HmiOplOutput_OplRead(void *context, uint16_t port) {
	HmiOplOutput *const output = context;
	assert(port == 0x388 || port == 0x389);
	return port == 0x388 ? opalReadStatus(&output->chip) : 0xff;
}

void HmiOplOutput_Construct(HmiOplOutput *output, HmiA002State *driver, SlipGameTimerState *timer) {
	memset(output, 0, sizeof(*output));
	opalInit(&output->chip, OPAL_OPL3_SAMPLE_RATE);
	output->timer = timer;
	driver->portContext = output;
	driver->portRead = HmiOplOutput_OplRead;
	driver->portWrite = HmiOplOutput_OplWrite;
}

void HmiOplOutput_Render(HmiOplOutput *output, int16_t *stereo, uint32_t frames) {
	uint32_t i;
	for (i = 0; i < frames; ++i) {
		uint64_t period;
		opalSample(&output->chip, stereo + i * 2, stereo + i * 2 + 1);
		if (stereo[i * 2] != 0 || stereo[i * 2 + 1] != 0)
			++output->nonzeroFrames;

		output->pitPhase += 0x1234dc;
		period = (uint64_t)output->timer->divisor * OPAL_OPL3_SAMPLE_RATE;
		assert(period != 0);
		while (output->pitPhase >= period) {
			output->pitPhase -= period;
			SlipGameTimer_Interrupt(output->timer);
			period = (uint64_t)output->timer->divisor * OPAL_OPL3_SAMPLE_RATE;
			assert(period != 0);
		}
	}
}

static void SDLCALL HmiOplOutput_OplCallback(void *context, SDL_AudioStream *stream, int additional, int total) {
	HmiOplOutput *const output = context;
	int16_t pcm[512 * 2];
	(void)total;
	while (additional > 0 && !output->streamWriteFailed) {
		uint32_t frames = ((uint32_t)additional + 3) / 4;
		if (frames > 512)
			frames = 512;
		HmiOplOutput_Render(output, pcm, frames);
		if (!SDL_PutAudioStreamData(stream, pcm, (int)frames * 4))
			output->streamWriteFailed = true;
		additional -= (int)frames * 4;
	}
}

bool HmiOplOutput_Open(HmiOplOutput *output) {
	SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, OPAL_OPL3_SAMPLE_RATE};
	output->stream =
	    SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, HmiOplOutput_OplCallback, output);
	return output->stream != NULL;
}

bool HmiOplOutput_Resume(HmiOplOutput *output) { return SDL_ResumeAudioStreamDevice(output->stream); }

void HmiOplOutput_Close(HmiOplOutput *output) {
	if (output->stream != NULL)
		SDL_DestroyAudioStream(output->stream);
	output->stream = NULL;
}
