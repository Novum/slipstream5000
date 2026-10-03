#include "race_voice.h"
#include "race.h"
#include "race_player.h"

#include <stddef.h>

SlipRaceVoiceRecord SlipRaceVoice_resultsRecords[SLIP_RACE_VOICE_RESULTS_COUNT] = {
    {"EM28a.SMP", 0, NULL, 0, 0}, {"EM28c.SMP", 0, NULL, 0, 0}, {"EM28e.SMP", 0, NULL, 0, 0},
    {"EM29.SMP", 0, NULL, 0, 0},  {"EM30.SMP", 0, NULL, 0, 0},  {"EM31.SMP", 0, NULL, 0, 0},
    {"EM32.SMP", 0, NULL, 0, 0},  {"EM33.SMP", 0, NULL, 0, 0},  {"EM34.SMP", 0, NULL, 0, 0},
    {"EM35.SMP", 0, NULL, 0, 0},  {"EM36.SMP", 0, NULL, 0, 0},  {"EM37.SMP", 0, NULL, 0, 0},
};

SlipRaceVoiceRecord SlipRaceVoice_alternateRecords[SLIP_RACE_VOICE_ALTERNATE_COUNT] = {
    {"EF01.SMP", 0, NULL, 0, 0}, {"EM23.SMP", 0, NULL, 0, 0}, {"EF05.SMP", 0, NULL, 0, 0}, {"EM27.SMP", 0, NULL, 0, 0},
    {"EF04.SMP", 0, NULL, 0, 0}, {"EM24.SMP", 0, NULL, 0, 0}, {"EF02.SMP", 0, NULL, 0, 0}, {"EM26.SMP", 0, NULL, 0, 0},
    {"EF03.SMP", 0, NULL, 0, 0}, {"EM25.SMP", 0, NULL, 0, 0},
};

SlipRaceVoiceRecord SlipRaceVoice_raceRecords[SLIP_RACE_VOICE_RACE_COUNT] = {
    {"EF93.SMP", 0, NULL, 0, 0},   {"EF104.SMP", 0, NULL, 0, 0}, {"EM38.SMP", 0, NULL, 0, 0},
    {"EM41.SMP", 0, NULL, 0, 0},   {"EM56.SMP", 0, NULL, 0, 1},  {"EM67.SMP", 0, NULL, 0, 2},
    {"EM64.SMP", 0, NULL, 0, 3},   {"EF56.SMP", 0, NULL, 0, 4},  {"EM70B.SMP", 0, NULL, 0, 5},
    {"EM76.SMP", 0, NULL, 0, 6},   {"EM60.SMP", 0, NULL, 0, 7},  {"EF48.SMP", 0, NULL, 0, 8},
    {"EF53.SMP", 0, NULL, 0, 9},   {"EM78.SMP", 0, NULL, 0, 10}, {"EM102.SMP", 0, NULL, 0, 1},
    {"EM105.SMP", 0, NULL, 0, 2},  {"EM104.SMP", 0, NULL, 0, 3}, {"EF69.SMP", 0, NULL, 0, 4},
    {"EM106.SMP", 0, NULL, 0, 5},  {"EM107.SMP", 0, NULL, 0, 6}, {"EM103.SMP", 0, NULL, 0, 7},
    {"EF67.SMP", 0, NULL, 0, 8},   {"EF68.SMP", 0, NULL, 0, 9},  {"EM108.SMP", 0, NULL, 0, 10},
    {"EM54.SMP", 0, NULL, 0, 1},   {"EM69.SMP", 0, NULL, 0, 2},  {"EM65.SMP", 0, NULL, 0, 3},
    {"EF57.SMP", 0, NULL, 0, 4},   {"EM72.SMP", 0, NULL, 0, 5},  {"EM75.SMP", 0, NULL, 0, 6},
    {"EM59.SMP", 0, NULL, 0, 7},   {"EF47.SMP", 0, NULL, 0, 8},  {"EF50.SMP", 0, NULL, 0, 9},
    {"EM80.SMP", 0, NULL, 0, 10},  {"EM55.SMP", 0, NULL, 0, 1},  {"EM66.SMP", 0, NULL, 0, 2},
    {"EM63.SMP", 0, NULL, 0, 3},   {"EF55.SMP", 0, NULL, 0, 4},  {"EM70.SMP", 0, NULL, 0, 5},
    {"EM74.SMP", 0, NULL, 0, 6},   {"EM58.SMP", 0, NULL, 0, 7},  {"EF49.SMP", 0, NULL, 0, 8},
    {"EF51.SMP", 0, NULL, 0, 9},   {"EM79.SMP", 0, NULL, 0, 10}, {"EM81.SMP", 0, NULL, 0, 1},
    {"EM84.SMP", 0, NULL, 0, 2},   {"EM83.SMP", 0, NULL, 0, 3},  {"EF60.SMP", 0, NULL, 0, 4},
    {"EM85.SMP", 0, NULL, 0, 5},   {"EM86.SMP", 0, NULL, 0, 6},  {"EM82.SMP", 0, NULL, 0, 7},
    {"EF58.SMP", 0, NULL, 0, 8},   {"EF59.SMP", 0, NULL, 0, 9},  {"EM87.SMP", 0, NULL, 0, 10},
    {"EF73.SMP", 0, NULL, 0, 0},   {"EF74.SMP", 0, NULL, 0, 0},  {"EF75.SMP", 0, NULL, 0, 0},
    {"EF76.SMP", 0, NULL, 0, 0},   {"EF77.SMP", 0, NULL, 0, 0},  {"EF78.SMP", 0, NULL, 0, 0},
    {"EF79.SMP", 0, NULL, 0, 0},   {"EF80.SMP", 0, NULL, 0, 0},  {"EF82.SMP", 0, NULL, 0, 0},
    {"EF88.SMP", 0, NULL, 0, 0},   {"EF86.SMP", 0, NULL, 0, 0},  {"EPS0.SMP", 0, NULL, 0, 0},
    {"EPS1.SMP", 0, NULL, 0, 0},   {"EPS2.SMP", 0, NULL, 0, 0},  {"EPS3.SMP", 0, NULL, 0, 0},
    {"EPS4.SMP", 0, NULL, 0, 0},   {"EPS5.SMP", 0, NULL, 0, 0},  {"EPS6.SMP", 0, NULL, 0, 0},
    {"EPS7.SMP", 0, NULL, 0, 0},   {"EPS8.SMP", 0, NULL, 0, 0},  {"EPS9.SMP", 0, NULL, 0, 0},
    {"EM95.SMP", 0, NULL, 0, 1},   {"EM98.SMP", 0, NULL, 0, 2},  {"EM97.SMP", 0, NULL, 0, 3},
    {"EF66.SMP", 0, NULL, 0, 4},   {"EM99.SMP", 0, NULL, 0, 5},  {"EM100.SMP", 0, NULL, 0, 6},
    {"EM96.SMP", 0, NULL, 0, 7},   {"EF64.SMP", 0, NULL, 0, 8},  {"EF65.SMP", 0, NULL, 0, 9},
    {"EM101.SMP", 0, NULL, 0, 10},
};

uint32_t SlipRaceVoice_bank;
uint32_t SlipRaceVoice_speakingDriver;
SlipRaceVoiceRecord *SlipRaceVoice_currentRecord;

static SlipRaceVoiceRecord *const SlipRaceVoice_banks[SLIP_RACE_VOICE_BANK_COUNT] = {
    NULL, SlipRaceVoice_resultsRecords, SlipRaceVoice_alternateRecords, SlipRaceVoice_raceRecords};
static const uint32_t SlipRaceVoice_bankCounts[SLIP_RACE_VOICE_BANK_COUNT] = {
    0, SLIP_RACE_VOICE_RESULTS_COUNT, SLIP_RACE_VOICE_ALTERNATE_COUNT, SLIP_RACE_VOICE_RACE_COUNT};

uint32_t SlipRaceVoice_loadOnDemand;
uint32_t SlipRaceVoice_suppressRecent;
uint32_t SlipRaceVoice_recentSelections[SLIP_RACE_VOICE_RECENT_COUNT];
uint32_t SlipRaceVoice_pendingSelection;

bool SlipRaceVoice_CanPreload(uint32_t digitalCard, uint32_t bank, uint32_t reserve, const SlipRaceVoiceCalls *calls) {
	if (digitalCard == 0)
		return true;
	SlipRaceVoiceRecord *const records = SlipRaceVoice_banks[bank];
	const uint32_t count = SlipRaceVoice_bankCounts[bank];
	for (uint32_t i = 0; i < count; ++i) {
		uint16_t handle;
		if (!calls->find(calls->context, records[i].sampleName, &handle))
			calls->error(calls->context);
		reserve += calls->resourceSize(calls->context, handle);
	}
	return calls->available(calls->context) >= reserve;
}

bool SlipRaceVoice_Setup(uint32_t digitalCard, uint32_t bank, uint32_t loadOnDemand, uint32_t suppressRecent,
                         uint32_t reserve, const SlipRaceVoiceCalls *calls) {
	if (digitalCard == 0)
		return false;
	SlipRaceVoice_ShutdownWithCalls(digitalCard, calls);
	if (calls->speechEnabled(calls->context) == 0)
		return false;
	if (loadOnDemand == 0 && !SlipRaceVoice_CanPreload(digitalCard, bank, reserve, calls))
		return false;
	SlipRaceVoice_bank = bank;
	SlipRaceVoice_loadOnDemand = loadOnDemand;
	SlipRaceVoice_suppressRecent = suppressRecent;
	SlipRaceVoice_speakingDriver = 0;
	SlipRaceVoiceRecord *const records = SlipRaceVoice_banks[bank];
	const uint32_t count = SlipRaceVoice_bankCounts[bank];
	for (uint32_t i = 0; i < count; ++i) {
		uint16_t handle;
		if (SlipRaceVoice_loadOnDemand == 0) {
			records[i].sampleName[0] = (char)calls->languageInitial(calls->context);
			if (!calls->load(calls->context, records[i].sampleName, &handle))
				calls->error(calls->context);
		} else
			handle = 0;
		records[i].resourceHandle = handle;
		records[i].sampleData = NULL;
		records[i].playbackHandle = 0;
	}
	SlipRaceVoice_recentSelections[0] = UINT32_MAX;
	SlipRaceVoice_recentSelections[1] = UINT32_MAX;
	SlipRaceVoice_recentSelections[2] = UINT32_MAX;
	SlipRaceVoice_recentSelections[3] = UINT32_MAX;
	return true;
}

void SlipRaceVoice_ShutdownWithCalls(uint32_t digitalCard, const SlipRaceVoiceCalls *calls) {
	if (digitalCard == 0 || SlipRaceVoice_bank == SLIP_RACE_VOICE_BANK_NONE)
		return;
	SlipRaceVoiceRecord *const records = SlipRaceVoice_banks[SlipRaceVoice_bank];
	const uint32_t count = SlipRaceVoice_bankCounts[SlipRaceVoice_bank];
	for (uint32_t i = 0; i < count; ++i) {
		SlipRaceVoiceRecord *const record = &records[i];
		if (record->resourceHandle != 0) {
			if (record->playbackHandle != 0)
				calls->stop(calls->context, record->playbackHandle);
			if (record->sampleData != NULL)
				calls->unlock(calls->context, record->resourceHandle);
			calls->release(calls->context, record->resourceHandle);
		}
	}
	SlipRaceVoice_bank = SLIP_RACE_VOICE_BANK_NONE;
}

uint32_t SlipRaceVoice_SpeakingDriverWithCalls(uint32_t digitalCard, const SlipRaceVoiceCalls *calls) {
	if (SlipRaceVoice_bank == SLIP_RACE_VOICE_BANK_NONE || digitalCard == 0)
		return 0;
	if (SlipRaceVoice_speakingDriver == 0)
		return 0;
	SlipRaceVoiceRecord *const record = SlipRaceVoice_currentRecord;
	if (record->playbackHandle != 0 && calls->stopped(calls->context, record->playbackHandle) != 0) {
		record->playbackHandle = 0;
		calls->unlock(calls->context, record->resourceHandle);
		record->sampleData = NULL;
		SlipRaceVoice_speakingDriver = 0;
		return 0;
	}
	return SlipRaceVoice_speakingDriver;
}

void SlipRaceVoice_Play(uint32_t digitalCard, uint32_t selection, const SlipRaceVoiceCalls *calls) {
	if (digitalCard == 0 || SlipRaceVoice_bank == SLIP_RACE_VOICE_BANK_NONE)
		return;
	if (SlipRaceVoice_suppressRecent != 0) {
		if (selection == SlipRaceVoice_recentSelections[0])
			return;
		if (selection == SlipRaceVoice_recentSelections[1])
			return;
		if (selection == SlipRaceVoice_recentSelections[2])
			return;
		if (selection == SlipRaceVoice_recentSelections[3])
			return;
	}
	SlipRaceVoice_pendingSelection = selection;
	if (SlipRaceVoice_bank == SLIP_RACE_VOICE_BANK_NONE)
		return;
	SlipRaceVoiceRecord *records = SlipRaceVoice_banks[SlipRaceVoice_bank];
	const uint32_t count = SlipRaceVoice_bankCounts[SlipRaceVoice_bank];
	for (uint32_t i = 0; i < count; ++i) {
		SlipRaceVoiceRecord *const record = &records[i];
		if (record->resourceHandle != 0 && record->playbackHandle != 0) {
			if (calls->stopped(calls->context, record->playbackHandle) == 0)
				return;
			record->playbackHandle = 0;
			calls->unlock(calls->context, record->resourceHandle);
			record->sampleData = NULL;
		}
	}
	records = SlipRaceVoice_banks[SlipRaceVoice_bank];
	SlipRaceVoiceRecord *const record = &records[SlipRaceVoice_pendingSelection];
	uint16_t handle = record->resourceHandle;
	if (handle == 0) {
		record->sampleName[0] = (char)calls->languageInitial(calls->context);
		if (!calls->load(calls->context, record->sampleName, &handle))
			calls->error(calls->context);
		record->resourceHandle = handle;
	}
	const uint32_t size = calls->resourceSize(calls->context, handle);
	record->sampleData = calls->lock(calls->context, handle);
	record->playbackHandle = calls->play(calls->context, record->sampleData, size);
	SlipRaceVoice_recentSelections[3] = SlipRaceVoice_recentSelections[2];
	SlipRaceVoice_recentSelections[2] = SlipRaceVoice_recentSelections[1];
	SlipRaceVoice_recentSelections[1] = SlipRaceVoice_recentSelections[0];
	SlipRaceVoice_recentSelections[0] = SlipRaceVoice_pendingSelection;
	SlipRaceVoice_speakingDriver = record->speakingDriver;
	SlipRaceVoice_currentRecord = record;
}

/* Native signature bindings for the existing sound/resource services. */
static void SlipRaceVoice_Unlock(void *context, uint16_t handle) {
	(void)context;
	SlipResource_Unlock(handle);
}

static void SlipRaceVoice_Release(void *context, uint16_t handle) {
	(void)context;
	SlipResource_ReleaseRecord(handle);
}

static void SlipRaceVoice_Stop(void *context, uint32_t handle) { SlipGameSound_Stop(context, handle); }

static uint32_t SlipRaceVoice_Stopped(void *context, uint32_t handle) {
	return SlipGameSound_IsStopped(context, handle);
}

void SlipRaceVoice_Shutdown(SlipGameSoundState *sound) {
	const SlipRaceVoiceCalls calls = {
	    .context = sound, .stop = SlipRaceVoice_Stop, .unlock = SlipRaceVoice_Unlock, .release = SlipRaceVoice_Release};
	SlipRaceVoice_ShutdownWithCalls(sound->digitalCard, &calls);
}

uint32_t SlipRaceVoice_SpeakingDriver(const SlipGameSoundState *sound) {
	const SlipRaceVoiceCalls calls = {
	    .context = (void *)sound, .stopped = SlipRaceVoice_Stopped, .unlock = SlipRaceVoice_Unlock};
	return SlipRaceVoice_SpeakingDriverWithCalls(sound->digitalCard, &calls);
}

static const uint32_t positionVoiceFirst[SLIP_RACE_RACER_COUNT] = {24, 25, 26, 27, 28, 29, 30, 31, 32, 33};
static const uint32_t positionVoiceSecond[SLIP_RACE_RACER_COUNT] = {34, 35, 36, 37, 38, 39, 40, 41, 42, 43};

void SlipRaceVoice_PositionChange(uint16_t minimumPosition, uint16_t previousMinimumPosition,
                                  const SlipRacePositionVoiceCalls *calls) {
	SlipRandomState savedRandom = calls->randomState(calls->context);
	if (minimumPosition != previousMinimumPosition) {
		const SlipRaceRacerState *const racer = calls->racer(calls->context);
		const uint16_t position = racer->racePosition;
		if (position != racer->previousRacePosition && position == previousMinimumPosition &&
		    (int16_t)position < (int16_t)minimumPosition) {
			const uint32_t *voices = positionVoiceFirst;
			if ((int8_t)calls->random(calls->context) < 0)
				voices = positionVoiceSecond;
			calls->play(calls->context, voices[racer->tuningIndex - 1]);
		}
	}
	calls->restoreRandom(calls->context, savedRandom);
}
