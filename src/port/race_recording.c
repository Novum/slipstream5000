#include "race_recording.h"
#include "byte_order.h"
#include "frame_timer.h"
#include <stdlib.h>

enum {
	SLIP_RECORDING_DURATION_BYTES = 2,
	SLIP_RECORDING_CONTROL_STEERING_OFFSET = 0,
	SLIP_RECORDING_CONTROL_PITCH_OFFSET = 2,
	SLIP_RECORDING_CONTROL_ACTIONS_OFFSET = 4,
	SLIP_RECORDING_MAX_CONTROL_BYTES = 32,
	SLIP_RECORDING_TAIL_RESERVE_BYTES = 32
};

SlipRaceRecording SlipRaceRecording_state;

void SlipRaceRecording_Install(uint16_t controlBytes, uint32_t capacityBytes, const SlipRaceRecordingHost *host) {
	SlipRaceRecording *const state = &SlipRaceRecording_state;
	if (state->installed)
		return;
	state->installed = true;
	state->host = *host;
	state->capacityBytes = capacityBytes;

	if (controlBytes > SLIP_RECORDING_MAX_CONTROL_BYTES)
		SlipRuntime_Fatal("KeyRecInstall - KeyStroke data specified too large");
	state->controlBytes = controlBytes;

	if (host->resources) {
		const SlipRaceRecordingResources *const resources = host->resources;
		if (!resources->allocate(resources->context, capacityBytes, 0, &state->resource))
			SlipRuntime_Fatal("KeyRecInstall - out of memory");
		state->data = resources->lock(resources->context, state->resource);
		state->frames = NULL;
	} else {
		/* Host-only typed storage for isolated callers without a resource manager. */
		const size_t frameCapacity = capacityBytes / (SLIP_RECORDING_DURATION_BYTES + controlBytes);
		state->frames = malloc(frameCapacity * sizeof(*state->frames));
		if (state->frames == NULL)
			SlipRuntime_Fatal("KeyRecInstall - out of memory");
		state->data = NULL;
	}
	state->writtenFrames = 0;
	SlipRuntime_RegisterExit(SlipRaceRecording_Release);
}

void SlipRaceRecording_Release(void) {
	SlipRaceRecording *const state = &SlipRaceRecording_state;
	if (!state->installed)
		return;
	state->installed = false;
	if (state->host.resources) {
		const SlipRaceRecordingResources *const resources = state->host.resources;
		resources->unlock(resources->context, state->resource);
		resources->release(resources->context, state->resource);
	} else
		free(state->frames);
}

SlipRaceRacerTable *SlipRaceRecording_PrepareRace(SlipRaceRecordingStart *start, bool replay,
                                                  SlipRaceRacerTable *racers, SlipRaceRecording *recording,
                                                  const SlipRaceRecordingHost *host) {
	if (!replay) {

		if (recording != NULL)
			SlipRaceRecording_Reset(recording);

		start->initialRacers = *racers;
		start->random = SlipRandom_GetState();
		return racers;
	}

	if (recording != NULL)
		SlipRaceRecording_ResetPlayback(recording, host);
	start->playbackRacers = start->initialRacers;
	SlipRandom_SetState(start->random.stateWords, (uint16_t)start->random.stateTail);
	return &start->playbackRacers;
}

void SlipRaceRecording_ResetPlayback(SlipRaceRecording *state, const SlipRaceRecordingHost *host) {
	state->playbackFrame = 0;
	state->lastPlaybackTick = 0;
	state->playbackTime = 0;
	state->recording = false;
	state->skipPlaybackWait = true;
	host->reset(host->context);
}

bool SlipRaceRecording_PlayFrame(SlipRaceRecording *state, const SlipRaceRecordingHost *host,
                                 SlipRacePlayerControl controls[2]) {
	if (state->playbackFrame == state->writtenFrames)
		return true;

	uint32_t tick;
	for (;;) {
		tick = host->readTick(host->context);
		if (state->skipPlaybackWait)
			break;
		const uint16_t duration =
		    state->data ? SlipBytes_ReadLE16(state->data + state->playbackFrame *
		                                                       (SLIP_RECORDING_DURATION_BYTES + state->controlBytes))
		                : state->frames[state->playbackFrame].milliseconds;
		const uint16_t elapsed = (uint16_t)(tick - state->lastPlaybackTick);
		if (elapsed >= duration) {
			state->lateness = (uint16_t)(elapsed - duration);
			break;
		}
	}
	state->lastPlaybackTick = tick;
	state->skipPlaybackWait = false;
	const uint32_t nextFrame = SlipRaceRecording_ReadFrame(state, controls);
	host->frame(host->context);
	state->playbackFrame = nextFrame;

	return state->playbackFrame == state->writtenFrames;
}

bool SlipRaceRecording_Read(SlipRaceRecording *state, const SlipRaceRecordingHost *host,
                            SlipRacePlayerControl controls[2]) {
	state->seeking = false;
	return SlipRaceRecording_PlayFrame(state, host, controls);
}

void SlipRaceRecording_Reset(SlipRaceRecording *state) {
	state->writtenFrames = 0;
	state->playbackFrame = 0;
	state->elapsed = 0;
	state->recording = true;
}

void SlipRaceRecording_Write(SlipRaceRecording *state, const SlipRacePlayerControl controls[2]) {
	if (!state->recording)
		return;
	const uint16_t delta = (uint16_t)SlipFrameTimer_Values().deltaMilliseconds;
	state->elapsed += delta;

	const uint32_t recordBytes = SLIP_RECORDING_DURATION_BYTES + state->controlBytes;
	uint32_t remaining = state->capacityBytes - state->writtenFrames * recordBytes;
	remaining -= recordBytes;
	if ((int32_t)remaining < SLIP_RECORDING_TAIL_RESERVE_BYTES)
		return;
	if (state->data) {
		uint8_t *const frame = state->data + state->writtenFrames * recordBytes;
		SlipBytes_WriteLE16(frame, delta);
		for (uint16_t i = 0; i < state->controlBytes / SLIP_RECORDING_CONTROL_BYTES; ++i) {
			uint8_t *const control = frame + SLIP_RECORDING_DURATION_BYTES + i * SLIP_RECORDING_CONTROL_BYTES;
			SlipBytes_WriteLE16(control + SLIP_RECORDING_CONTROL_STEERING_OFFSET, (uint16_t)controls[i].steering);
			SlipBytes_WriteLE16(control + SLIP_RECORDING_CONTROL_PITCH_OFFSET, (uint16_t)controls[i].pitch);
			SlipBytes_WriteLE16(control + SLIP_RECORDING_CONTROL_ACTIONS_OFFSET, controls[i].actions);
		}
	} else {
		SlipRaceRecordingFrame *const frame = &state->frames[state->writtenFrames];
		frame->milliseconds = delta;
		for (uint16_t i = 0; i < state->controlBytes / SLIP_RECORDING_CONTROL_BYTES; ++i)
			frame->controls[i] = controls[i];
	}
	++state->writtenFrames;
}

uint32_t SlipRaceRecording_ReadFrame(SlipRaceRecording *state, SlipRacePlayerControl controls[2]) {
	if (state->data) {
		const uint8_t *const frame =
		    state->data + state->playbackFrame * (SLIP_RECORDING_DURATION_BYTES + state->controlBytes);
		SlipFrameTimer_SetRecordedDelta(SlipBytes_ReadLE16(frame));
		for (uint16_t i = 0; i < state->controlBytes / SLIP_RECORDING_CONTROL_BYTES; ++i) {
			const uint8_t *const control = frame + SLIP_RECORDING_DURATION_BYTES + i * SLIP_RECORDING_CONTROL_BYTES;
			controls[i].steering = SlipBytes_ReadLEI16(control + SLIP_RECORDING_CONTROL_STEERING_OFFSET);
			controls[i].pitch = SlipBytes_ReadLEI16(control + SLIP_RECORDING_CONTROL_PITCH_OFFSET);
			controls[i].actions = SlipBytes_ReadLE16(control + SLIP_RECORDING_CONTROL_ACTIONS_OFFSET);
		}
	} else {
		const SlipRaceRecordingFrame *const frame = &state->frames[state->playbackFrame];
		SlipFrameTimer_SetRecordedDelta(frame->milliseconds);
		for (uint16_t i = 0; i < state->controlBytes / SLIP_RECORDING_CONTROL_BYTES; ++i)
			controls[i] = frame->controls[i];
	}
	return state->playbackFrame + 1u;
}
