#ifndef SLIPSTREAM5000_RACE_RECORDING_H
#define SLIPSTREAM5000_RACE_RECORDING_H

#include "race.h"
#include "race_player.h"
#include "runtime.h"

/* Each serialized player control contains steering, pitch, and action words. */
enum { SLIP_RECORDING_CONTROL_BYTES = 6, SLIP_RECORDING_DEFAULT_CAPACITY_BYTES = 100000 };

typedef struct SlipRaceRecordingStart {
	SlipRandomState random;
	SlipRaceRacerTable initialRacers;
	SlipRaceRacerTable playbackRacers;
} SlipRaceRecordingStart;

typedef struct SlipRaceRecordingFrame {
	uint16_t milliseconds;
	SlipRacePlayerControl controls[2];
} SlipRaceRecordingFrame;

typedef struct SlipRaceRecordingResources {
	void *context;
	bool (*allocate)(void *, uint32_t bytes, uint32_t flags, uint16_t *);
	uint8_t *(*lock)(void *, uint16_t);
	void (*unlock)(void *, uint16_t);
	void (*release)(void *, uint16_t);
} SlipRaceRecordingResources;

typedef struct SlipRaceRecordingHost {
	uint32_t (*readTick)(void *context);
	void (*reset)(void *context);
	void (*frame)(void *context);
	void *context;
	const SlipRaceRecordingResources *resources;
} SlipRaceRecordingHost;

typedef struct SlipRaceRecording {
	/* Typed frame backend is retained for isolated recorder callers/tests.
	 * Resource-backed race callers consume the serialized locked stream. */
	SlipRaceRecordingFrame *frames;
	uint8_t *data;
	uint16_t resource;
	bool installed;
	SlipRaceRecordingHost host;
	uint32_t capacityBytes;
	uint32_t writtenFrames;
	uint32_t playbackFrame;
	uint32_t elapsed;
	bool recording;
	uint16_t controlBytes;
	uint32_t lastPlaybackTick;
	uint32_t playbackTime;
	bool skipPlaybackWait;
	bool seeking;
	uint16_t lateness;
} SlipRaceRecording;

extern SlipRaceRecording SlipRaceRecording_state;

void SlipRaceRecording_Install(uint16_t controlBytes, uint32_t capacityBytes, const SlipRaceRecordingHost *host);
void SlipRaceRecording_Release(void);

SlipRaceRacerTable *SlipRaceRecording_PrepareRace(SlipRaceRecordingStart *start, bool replay,
                                                  SlipRaceRacerTable *racers, SlipRaceRecording *recording,
                                                  const SlipRaceRecordingHost *host);

void SlipRaceRecording_ResetPlayback(SlipRaceRecording *state, const SlipRaceRecordingHost *host);

bool SlipRaceRecording_PlayFrame(SlipRaceRecording *state, const SlipRaceRecordingHost *host,
                                 SlipRacePlayerControl controls[2]);
bool SlipRaceRecording_Read(SlipRaceRecording *state, const SlipRaceRecordingHost *host,
                            SlipRacePlayerControl controls[2]);

void SlipRaceRecording_Reset(SlipRaceRecording *state);
void SlipRaceRecording_Write(SlipRaceRecording *state, const SlipRacePlayerControl controls[2]);
uint32_t SlipRaceRecording_ReadFrame(SlipRaceRecording *state, SlipRacePlayerControl controls[2]);

#endif
