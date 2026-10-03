#include "hmi_music.h"
#include "byte_order.h"

#include "hmi_a002_data.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>

void HmiMusic_GetDriverFunctions(HmiMusicDriverTableQuery entry, void *context, uint16_t codeSelector,
                                 uint16_t dataSelector, uint8_t destination[HMI_DRIVER_FUNCTION_TABLE_BYTES]) {
	const uint8_t *source = entry(context, HMI_DRIVER_QUERY_FUNCTION_TABLE, dataSelector);
	for (uint32_t i = 0; i < HMI_DRIVER_FUNCTION_COUNT; ++i) {
		const uint32_t offset = (uint32_t)source[0] | ((uint32_t)source[1] << 8) | ((uint32_t)source[2] << 16) |
		                        ((uint32_t)source[3] << 24);
		destination[0] = (uint8_t)offset;
		destination[1] = (uint8_t)(offset >> 8);
		destination[2] = (uint8_t)(offset >> 16);
		destination[3] = (uint8_t)(offset >> 24);
		SlipBytes_WriteLE16(destination + HMI_DRIVER_FAR_POINTER_SELECTOR_OFFSET, codeSelector);
		destination += HMI_DRIVER_FAR_POINTER_BYTES;
		source += HMI_DRIVER_FUNCTION_SOURCE_BYTES;
	}
}

const char *HmiMusic_ErrorString(uint32_t code) {

	static const char *const errors[HMI_MUSIC_ERROR_COUNT] = {"Error Code Does Not Indicate An Error",
	                                                          "Specified Driver Is Not Loaded",
	                                                          "Specified Pointer Is NULL",
	                                                          "Detection System Is Already Initialized",
	                                                          "File Open Failure",
	                                                          "Memory Allocation Failure",
	                                                          "Invalid Driver ID",
	                                                          "Driver Not Found",
	                                                          "Detection System Failed To Find Hardware",
	                                                          "Driver Already Loaded Using Specified Handle",
	                                                          "Invalid Handle",
	                                                          "No Handles Available",
	                                                          "Hardware Already Paused",
	                                                          "Hardware Not Paused",
	                                                          "Data Is Not Valid",
	                                                          "HMI*.386 File Open Failure",
	                                                          "Incorrect Port",
	                                                          "Incorrect IRQ",
	                                                          "Incorrect DMA",
	                                                          "Incorrect DMA/IRQ"};
	return errors[code];
}

void HmiMusic_Construct(HmiMusicState *state) {
	uint32_t i;
	uint8_t *bytes;

	*state = (HmiMusicState){0};
	bytes = state->dispatchGlobals;
	memset(bytes, UINT8_MAX, HMI_DISPATCH_MESSAGE_OFFSET);
	memset(bytes + HMI_DISPATCH_INITIAL_FLAGS_OFFSET, 1, HMI_DISPATCH_INITIAL_FLAGS_BYTES);
	for (i = HMI_DISPATCH_INITIAL_FLAGS_OFFSET + HMI_MIDI_PERCUSSION_CHANNEL; i < HMI_DISPATCH_MASTER_VOLUME_OFFSET;
	     i += HMI_MIDI_CHANNEL_COUNT)
		bytes[i] = 0;
	bytes[HMI_DISPATCH_INITIAL_ZERO_FLAG_OFFSET] = 0;
	memset(bytes + HMI_DISPATCH_INITIAL_ZERO_FLAGS_OFFSET, 0, HMI_MUSIC_SONG_COUNT);
	bytes[HMI_DISPATCH_MASTER_VOLUME_OFFSET] = HMI_MIDI_DATA_MAXIMUM;
	for (i = 0; i < HMI_MUSIC_SONG_COUNT; ++i) {
		state->timerHandles[i] = UINT32_MAX;
		bytes[HMI_DISPATCH_SONG_VOLUMES_OFFSET + i * sizeof(uint32_t)] = HMI_MIDI_DATA_MAXIMUM;
	}
	memset(bytes + HMI_DISPATCH_CHANNEL_VOLUMES_OFFSET, HMI_MIDI_DATA_MAXIMUM,
	       HMI_MUSIC_DRIVER_COUNT * HMI_MIDI_CHANNEL_COUNT);
	memcpy(bytes + HMI_DISPATCH_SIGNATURE_OFFSET, "HMIMIDIP013195", sizeof("HMIMIDIP013195"));
}

uint32_t HmiMusic_SetLoopCallback(HmiMusicState *state, uint32_t song, HmiMusicLoopCallback callback) {
	state->loopCallbacks[song] = callback;
	return 0;
}

uint32_t HmiMusic_SetBranchCallback(HmiMusicState *state, uint32_t song, HmiMusicBranchCallback callback) {
	state->branchCallbacks[song] = callback;
	return 0;
}

uint32_t HmiMusic_SetTriggerCallback(HmiMusicState *state, uint32_t song, uint8_t trigger,
                                     HmiMusicTriggerCallback callback) {

	const uint32_t index = song * HMI_MUSIC_TRIGGER_COUNT + trigger;
	if (index >= HMI_MUSIC_SONG_COUNT * HMI_MUSIC_TRIGGER_COUNT)
		SlipAssertFail("Trigger write outside native callback table requires original pointer bytes", __FILE__,
		               __LINE__);
	state->triggerCallbacks[index / HMI_MUSIC_TRIGGER_COUNT][index % HMI_MUSIC_TRIGGER_COUNT] = callback;
	return 0;
}

void HmiMusic_WriteTriggerAlias(HmiMusicState *state, uint32_t offset, uint16_t selector) {

	state->fadeMode[0] = (state->fadeMode[0] & HMI_TRIGGER_ALIAS_OFFSET_HIGH_MASK) |
	                     ((uint32_t)(selector & UINT8_MAX) << HMI_TRIGGER_ALIAS_SELECTOR_LOW_SHIFT);
	state->fadeMode[1] = (state->fadeMode[1] & HMI_TRIGGER_ALIAS_SELECTOR_HIGH_PRESERVE_MASK) | (selector >> 8);
	state->callbackPending = (uint8_t)offset;
	state->fadeMode[0] = (state->fadeMode[0] & HMI_TRIGGER_ALIAS_SELECTOR_LOW_MASK) | (offset >> 8);
}

uint32_t HmiMusic_SetTriggerPointer(HmiMusicState *state, uint32_t song, uint8_t trigger,
                                    HmiMusicTriggerCallback callback, HmiMusicFarPointer pointer) {
	const uint32_t index = song * HMI_MUSIC_TRIGGER_COUNT + trigger;
	if (index == HMI_MUSIC_SONG_COUNT * HMI_MUSIC_TRIGGER_COUNT) {
		HmiMusic_WriteTriggerAlias(state, pointer.offset, pointer.selector);
		return 0;
	}
	return HmiMusic_SetTriggerCallback(state, song, trigger, callback);
}

HmiMusicFarPointer HmiMusic_ReadTriggerAlias(const HmiMusicState *state) {
	HmiMusicFarPointer result;
	/* Read live overlapped bytes, not the identity originally registered.
	 * In particular the scheduler's pending=1 changes the target low byte. */
	result.offset = state->callbackPending | (state->fadeMode[0] << 8);
	result.selector = (uint16_t)((state->fadeMode[0] >> HMI_TRIGGER_ALIAS_SELECTOR_LOW_SHIFT) |
	                             ((state->fadeMode[1] & UINT8_MAX) << 8));
	return result;
}

/* Offsets into the checked-in A002 driver data image and its controller array. */
enum {
	HMI_A002_IMAGE_FREQUENCY_LOW_OFFSET = 0x900,
	HMI_A002_IMAGE_FREQUENCY_HIGH_OFFSET = 0x909,
	HMI_A002_IMAGE_ACTIVE_NOTES_OFFSET = 0x930,
	HMI_A002_IMAGE_OPERATOR_LEVELS_OFFSET = 0x8d0,
	HMI_A002_IMAGE_OPERATOR_RELEASE_OFFSET = 0x8f0,
	HMI_A002_IMAGE_INSTRUMENT_SET_OFFSET = 0x925,
	HMI_A002_IMAGE_RHYTHM_OFFSET = 0x924,
	HMI_A002_IMAGE_CHIP_ENABLED_OFFSET = 0x93f,
	HMI_A002_IMAGE_MODE_OFFSET = 0x940,
	HMI_A002_IMAGE_OPERATOR_OFFSETS_OFFSET = 0x941,
	HMI_A002_IMAGE_VOLUME_CURVE_OFFSET = 0x95c,
	HMI_A002_IMAGE_DEFERRED_NOTES_OFFSET = 0x2a8,
	HMI_A002_IMAGE_INITIAL_PROGRAM_MESSAGE_OFFSET = 0x266,
	HMI_A002_IMAGE_OFF_MESSAGE_OFFSET = 0x256,
	HMI_A002_IMAGE_PROGRAM_MESSAGE_OFFSET = 0x259,
	HMI_A002_IMAGE_CONTROL_MESSAGE_OFFSET = 0x25b,
	HMI_A002_IMAGE_PITCH_MESSAGE_OFFSET = 0x25e,
	HMI_A002_IMAGE_PITCH_TABLES_OFFSET = 0x9a8,
	HMI_A002_IMAGE_VOICE_CHANNELS_OFFSET = 0x80,
	HMI_A002_IMAGE_SUSTAIN_OFFSET = 0x20c,
	HMI_A002_IMAGE_DEFERRED_COUNTS_OFFSET = 0x268,
	HMI_A002_IMAGE_CONTROLLERS_OFFSET = 0xa4,
	HMI_A002_IMAGE_PROGRAMS_OFFSET = 0x40,
	HMI_A002_IMAGE_PORT_BASE_OFFSET = 0x8b8,
	HMI_A002_IMAGE_PORT_COPY_OFFSET = 0x93b,
	HMI_A002_IMAGE_PORT_ARGUMENT_OFFSET = 0x8a8,
	HMI_A002_IMAGE_INITIALIZED_OFFSET = 0x0,
	HMI_A002_IMAGE_PORT_INPUT_OFFSET = 0x8ac,
	HMI_A002_IMAGE_BANK_TOGGLE_OFFSET = 0x8b4,
	HMI_A002_IMAGE_PITCH_ENABLED_OFFSET = 0x3c,
	HMI_A002_IMAGE_NOTE_FREQUENCIES_OFFSET = 0x978,
	HMI_A002_IMAGE_MELODIC_COUNT_OFFSET = 0x4,
	HMI_A002_IMAGE_MELODIC_LOADED_OFFSET = 0x1e,
	HMI_A002_IMAGE_PERCUSSION_LOADED_OFFSET = 0x34,
	HMI_A002_CONTROLLER_PITCH_CHANGED_INDEX = 16,
	HMI_A002_CONTROLLER_PITCH_RANGE_INDEX = 32,
	HMI_A002_CONTROLLER_CHANNEL_VOLUME_INDEX = 49,
	HMI_A002_CONTROLLER_VOICE_VELOCITY_INDEX = 65,
	HMI_A002_CONTROLLER_VOLUME_CHANGED_INDEX = 74
};

static void HmiA002_InitialData(HmiA002State *state, const uint8_t *data) {
	uint32_t i;
	memcpy(state->cache.frequency.frequencyLow, data + HMI_A002_IMAGE_FREQUENCY_LOW_OFFSET,
	       sizeof(state->cache.frequency.frequencyLow));
	memcpy(state->cache.frequency.frequencyHigh, data + HMI_A002_IMAGE_FREQUENCY_HIGH_OFFSET,
	       sizeof(state->cache.frequency.frequencyHigh));
	memcpy(state->activeNotes, data + HMI_A002_IMAGE_ACTIVE_NOTES_OFFSET, sizeof(state->activeNotes));
	memcpy(state->operatorLevels, data + HMI_A002_IMAGE_OPERATOR_LEVELS_OFFSET, sizeof(state->operatorLevels));
	memcpy(state->cache.operatorRelease, data + HMI_A002_IMAGE_OPERATOR_RELEASE_OFFSET,
	       sizeof(state->cache.operatorRelease));
	memcpy(state->instrumentSet, data + HMI_A002_IMAGE_INSTRUMENT_SET_OFFSET, sizeof(state->instrumentSet));
	state->rhythm = data[HMI_A002_IMAGE_RHYTHM_OFFSET];
	state->chipEnabled = data[HMI_A002_IMAGE_CHIP_ENABLED_OFFSET];
	state->mode = data[HMI_A002_IMAGE_MODE_OFFSET];
	memcpy(state->operatorOffsets, data + HMI_A002_IMAGE_OPERATOR_OFFSETS_OFFSET, sizeof(state->operatorOffsets));
	memcpy(state->volumeCurve, data + HMI_A002_IMAGE_VOLUME_CURVE_OFFSET, sizeof(state->volumeCurve));
	memcpy(state->deferred, data + HMI_A002_IMAGE_DEFERRED_NOTES_OFFSET, sizeof(state->deferred));
	memcpy(state->initialProgramMessage, data + HMI_A002_IMAGE_INITIAL_PROGRAM_MESSAGE_OFFSET,
	       sizeof(state->initialProgramMessage));
	memcpy(state->offMessage, data + HMI_A002_IMAGE_OFF_MESSAGE_OFFSET, sizeof(state->offMessage));
	memcpy(state->programMessage, data + HMI_A002_IMAGE_PROGRAM_MESSAGE_OFFSET, sizeof(state->programMessage));
	memcpy(state->controlMessage, data + HMI_A002_IMAGE_CONTROL_MESSAGE_OFFSET, sizeof(state->controlMessage));
	memcpy(state->pitchMessage, data + HMI_A002_IMAGE_PITCH_MESSAGE_OFFSET, sizeof(state->pitchMessage));
	for (i = 0; i < sizeof(state->pitchTables) / sizeof(state->pitchTables[0]); ++i)
		state->pitchTables[i] =
		    SlipBytes_ReadLE32(data + HMI_A002_IMAGE_PITCH_TABLES_OFFSET + i * HMI_SERIALIZED_DWORD_BYTES);
	for (i = 0; i < HMI_A002_VOICE_COUNT; ++i)
		state->voiceChannels[i] =
		    SlipBytes_ReadLE32(data + HMI_A002_IMAGE_VOICE_CHANNELS_OFFSET + i * HMI_SERIALIZED_DWORD_BYTES);
	for (i = 0; i < HMI_MIDI_CHANNEL_COUNT; ++i)
		state->sustain[i] = SlipBytes_ReadLE32(data + HMI_A002_IMAGE_SUSTAIN_OFFSET + i * HMI_SERIALIZED_DWORD_BYTES);
	for (i = 0; i < HMI_MIDI_CHANNEL_COUNT; ++i)
		state->deferredCount[i] =
		    SlipBytes_ReadLE32(data + HMI_A002_IMAGE_DEFERRED_COUNTS_OFFSET + i * HMI_SERIALIZED_DWORD_BYTES);
	for (i = 0; i < sizeof(state->controllerGlobals) / sizeof(state->controllerGlobals[0]); ++i)
		state->controllerGlobals[i] =
		    SlipBytes_ReadLE32(data + HMI_A002_IMAGE_CONTROLLERS_OFFSET + i * HMI_SERIALIZED_DWORD_BYTES);
	for (i = 0; i < HMI_MIDI_CHANNEL_COUNT; ++i)
		state->programs[i] = SlipBytes_ReadLE32(data + HMI_A002_IMAGE_PROGRAMS_OFFSET + i * HMI_SERIALIZED_DWORD_BYTES);
	state->portBase = SlipBytes_ReadLE32(data + HMI_A002_IMAGE_PORT_BASE_OFFSET);
	state->portCopy = SlipBytes_ReadLE32(data + HMI_A002_IMAGE_PORT_COPY_OFFSET);
	state->portArgument = SlipBytes_ReadLE32(data + HMI_A002_IMAGE_PORT_ARGUMENT_OFFSET);
	state->initialized = SlipBytes_ReadLE32(data + HMI_A002_IMAGE_INITIALIZED_OFFSET);
	memcpy(state->portInput, data + HMI_A002_IMAGE_PORT_INPUT_OFFSET, sizeof(state->portInput));
	state->bankToggle = SlipBytes_ReadLE32(data + HMI_A002_IMAGE_BANK_TOGGLE_OFFSET);
	state->pitchEnabled = SlipBytes_ReadLE32(data + HMI_A002_IMAGE_PITCH_ENABLED_OFFSET);
	for (i = 0; i < HMI_MIDI_NOTE_COUNT; ++i)
		state->noteFrequencies[i] =
		    SlipBytes_ReadLE32(data + HMI_A002_IMAGE_NOTE_FREQUENCIES_OFFSET + i * HMI_SERIALIZED_DWORD_BYTES);
	state->melodicCount = (int32_t)SlipBytes_ReadLE32(data + HMI_A002_IMAGE_MELODIC_COUNT_OFFSET);
	state->melodicLoaded = SlipBytes_ReadLE32(data + HMI_A002_IMAGE_MELODIC_LOADED_OFFSET);
	state->percussionLoaded = SlipBytes_ReadLE32(data + HMI_A002_IMAGE_PERCUSSION_LOADED_OFFSET);
}

void HmiA002_ConstructStatic(HmiA002State *state) {
	*state = (HmiA002State){0};
	HmiA002_InitialData(state, a002InitialData);
}

void HmiA002_NoteOn(HmiA002State *state, uint8_t note, uint8_t velocity, uint8_t channel) {
	uint8_t voice, op, level;
	uint32_t i, scaled;
	if (channel < HMI_MIDI_CHANNEL_COUNT) {
		if (channel == HMI_MIDI_PERCUSSION_CHANNEL) {
			voice = HmiA002_Select(state, HMI_MIDI_PERCUSSION_CHANNEL);
			HmiA002_KeyOff(state, voice);
			state->voiceChannels[voice] = HMI_MIDI_PERCUSSION_CHANNEL;
			for (i = 0; i < HMI_A002_RELEASE_FLUSH_WRITES; ++i) {
				op = state->operatorOffsets[voice * HMI_A002_OPERATOR_COUNT + 1];
				HmiA002_Write(state, (uint8_t)(op + HMI_A002_OPERATOR_RELEASE_BASE),
				              (uint8_t)(state->cache.operatorRelease[op] | HMI_A002_RELEASE_MAXIMUM));
				op = state->operatorOffsets[voice * HMI_A002_OPERATOR_COUNT + 0];
				HmiA002_Write(state, (uint8_t)(op + HMI_A002_OPERATOR_RELEASE_BASE),
				              (uint8_t)(state->cache.operatorRelease[op] | HMI_A002_RELEASE_MAXIMUM));
			}
			HmiA002_Instrument(state, state->percussionData + (uint32_t)note * HMI_A002_INSTRUMENT_BYTES, voice);
			state->controllerGlobals[HMI_A002_CONTROLLER_VOICE_VELOCITY_INDEX + voice] = velocity;
			scaled =
			    ((state->controllerGlobals[HMI_A002_CONTROLLER_CHANNEL_VOLUME_INDEX + HMI_MIDI_PERCUSSION_CHANNEL]
			      << HMI_A002_VOLUME_FRACTION_BITS) /
			     HMI_MIDI_DATA_MAXIMUM * state->controllerGlobals[HMI_A002_CONTROLLER_VOICE_VELOCITY_INDEX + voice]) >>
			    HMI_A002_VOLUME_FRACTION_BITS;
			if ((state->melodicData[state->programs[HMI_MIDI_PERCUSSION_CHANNEL] * HMI_A002_INSTRUMENT_BYTES +
			                        HMI_A002_INSTRUMENT_FEEDBACK_OFFSET] &
			     HMI_A002_ADDITIVE_CONNECTION) != 0) {
				op = state->operatorOffsets[voice * HMI_A002_OPERATOR_COUNT + 0];
				level =
				    (uint8_t)((HMI_A002_LEVEL_PRODUCT_ONE -
				               (HMI_A002_LEVEL_STEPS -
				                state->volumeCurve[(scaled & UINT8_MAX) / HMI_A002_VOLUME_CURVE_STEP]) *
				                   HMI_A002_VOLUME_CURVE_STEP *
				                   (HMI_A002_LEVEL_STEPS - (state->operatorLevels[op] & HMI_A002_TOTAL_LEVEL_MASK))) >>
				              HMI_A002_VOLUME_FRACTION_BITS);
				HmiA002_Write(state, (uint8_t)(op + HMI_A002_OPERATOR_LEVEL_BASE),
				              (uint8_t)(level | (state->operatorLevels[op] & HMI_A002_KEY_SCALE_LEVEL_MASK)));
			}
			op = state->operatorOffsets[voice * HMI_A002_OPERATOR_COUNT + 1];
			level = (uint8_t)((HMI_A002_LEVEL_PRODUCT_ONE -
			                   (HMI_A002_LEVEL_STEPS -
			                    state->volumeCurve[(scaled & UINT8_MAX) / HMI_A002_VOLUME_CURVE_STEP]) *
			                       HMI_A002_VOLUME_CURVE_STEP *
			                       (HMI_A002_LEVEL_STEPS - (state->operatorLevels[op] & HMI_A002_TOTAL_LEVEL_MASK))) >>
			                  HMI_A002_VOLUME_FRACTION_BITS);
			HmiA002_Write(state, (uint8_t)(op + HMI_A002_OPERATOR_LEVEL_BASE),
			              (uint8_t)(level | (state->operatorLevels[op] & HMI_A002_KEY_SCALE_LEVEL_MASK)));
			HmiA002_KeyOn(state, voice,
			              state->noteFrequencies[state->percussionIndex[(uint32_t)note * HMI_A002_BANK_INDEX_BYTES +
			                                                            HMI_A002_BANK_INDEX_NOTE_OFFSET]]);
			state->activeNotes[voice] = note;
		} else {
			voice = HmiA002_Select(state, channel);
			HmiA002_KeyOff(state, voice);
			state->voiceChannels[voice] = channel;
			for (i = 0; i < HMI_A002_RELEASE_FLUSH_WRITES; ++i) {
				op = state->operatorOffsets[voice * HMI_A002_OPERATOR_COUNT + 1];
				HmiA002_Write(state, (uint8_t)(op + HMI_A002_OPERATOR_RELEASE_BASE),
				              (uint8_t)(state->cache.operatorRelease[op] | HMI_A002_RELEASE_MAXIMUM));
				op = state->operatorOffsets[voice * HMI_A002_OPERATOR_COUNT + 0];
				HmiA002_Write(state, (uint8_t)(op + HMI_A002_OPERATOR_RELEASE_BASE),
				              (uint8_t)(state->cache.operatorRelease[op] | HMI_A002_RELEASE_MAXIMUM));
			}
			HmiA002_Instrument(state, state->melodicData + state->programs[channel] * HMI_A002_INSTRUMENT_BYTES, voice);
			state->controllerGlobals[HMI_A002_CONTROLLER_VOICE_VELOCITY_INDEX + voice] = velocity;
			scaled =
			    ((state->controllerGlobals[HMI_A002_CONTROLLER_CHANNEL_VOLUME_INDEX + channel]
			      << HMI_A002_VOLUME_FRACTION_BITS) /
			     HMI_MIDI_DATA_MAXIMUM * state->controllerGlobals[HMI_A002_CONTROLLER_VOICE_VELOCITY_INDEX + voice]) >>
			    HMI_A002_VOLUME_FRACTION_BITS;
			if ((state->melodicData[state->programs[channel] * HMI_A002_INSTRUMENT_BYTES +
			                        HMI_A002_INSTRUMENT_FEEDBACK_OFFSET] &
			     HMI_A002_ADDITIVE_CONNECTION) != 0) {
				op = state->operatorOffsets[voice * HMI_A002_OPERATOR_COUNT + 0];
				level =
				    (uint8_t)((HMI_A002_LEVEL_PRODUCT_ONE -
				               (HMI_A002_LEVEL_STEPS -
				                state->volumeCurve[(scaled & UINT8_MAX) / HMI_A002_VOLUME_CURVE_STEP]) *
				                   HMI_A002_VOLUME_CURVE_STEP *
				                   (HMI_A002_LEVEL_STEPS - (state->operatorLevels[op] & HMI_A002_TOTAL_LEVEL_MASK))) >>
				              HMI_A002_VOLUME_FRACTION_BITS);
				HmiA002_Write(state, (uint8_t)(op + HMI_A002_OPERATOR_LEVEL_BASE),
				              (uint8_t)(level | (state->operatorLevels[op] & HMI_A002_KEY_SCALE_LEVEL_MASK)));
			}
			op = state->operatorOffsets[voice * HMI_A002_OPERATOR_COUNT + 1];
			level = (uint8_t)((HMI_A002_LEVEL_PRODUCT_ONE -
			                   (HMI_A002_LEVEL_STEPS -
			                    state->volumeCurve[(scaled & UINT8_MAX) / HMI_A002_VOLUME_CURVE_STEP]) *
			                       HMI_A002_VOLUME_CURVE_STEP *
			                       (HMI_A002_LEVEL_STEPS - (state->operatorLevels[op] & HMI_A002_TOTAL_LEVEL_MASK))) >>
			                  HMI_A002_VOLUME_FRACTION_BITS);
			HmiA002_Write(state, (uint8_t)(op + HMI_A002_OPERATOR_LEVEL_BASE),
			              (uint8_t)(level | (state->operatorLevels[op] & HMI_A002_KEY_SCALE_LEVEL_MASK)));
			HmiA002_KeyOn(state, voice, state->noteFrequencies[note]);
			state->activeNotes[voice] = note;
			if (state->pitchEnabled != 0 &&
			    state->controllerGlobals[HMI_A002_CONTROLLER_PITCH_CHANGED_INDEX + channel] != 0) {
				const uint32_t frequency = HmiA002_PitchCalc(state, state->controllerGlobals[channel], note, voice);
				HmiA002_Frequency(state, voice, frequency);
			}
		}
	}
}

void HmiA002_Send(HmiA002State *state, uint32_t ignoredLeadingValue, uint32_t ignoredFollowingValue,
                  const uint8_t *message) {
	const uint8_t status = message[0] & HMI_MIDI_STATUS_MASK;
	(void)ignoredLeadingValue;
	(void)ignoredFollowingValue;
	if (status < HMI_MIDI_CONTROL_CHANGE) {
		if (status > HMI_MIDI_DATA_MAXIMUM) {
			if (status < HMI_MIDI_NOTE_OFF + 1) {
				state->offMessage[HMI_A002_OFF_CHANNEL_OFFSET] = message[0] & (HMI_MIDI_CHANNEL_COUNT - 1);
				state->offMessage[HMI_A002_OFF_NOTE_OFFSET] = message[1];
				state->offMessage[HMI_A002_OFF_VELOCITY_OFFSET] = message[2];
				HmiA002_Off(state, 0, 0, state->offMessage);
			} else if (status == HMI_MIDI_NOTE_ON) {
				if (message[2] == 0) {
					state->offMessage[HMI_A002_OFF_CHANNEL_OFFSET] = message[0] & (HMI_MIDI_CHANNEL_COUNT - 1);
					state->offMessage[HMI_A002_OFF_NOTE_OFFSET] = message[1];
					state->offMessage[HMI_A002_OFF_VELOCITY_OFFSET] = message[2];
					HmiA002_Off(state, 0, 0, state->offMessage);
				} else
					HmiA002_NoteOn(state, message[1], message[2], message[0] & (HMI_MIDI_CHANNEL_COUNT - 1));
			}
		}
	} else if (status < HMI_MIDI_CONTROL_CHANGE + 1) {
		state->controlMessage[HMI_A002_CONTROL_CHANNEL_OFFSET] = message[0] & (HMI_MIDI_CHANNEL_COUNT - 1);
		state->controlMessage[HMI_A002_CONTROL_NUMBER_OFFSET] = message[1];
		state->controlMessage[HMI_A002_CONTROL_VALUE_OFFSET] = message[2];
		HmiA002_Control(state, 0, 0, state->controlMessage);
	} else if (status > HMI_MIDI_PROGRAM_CHANGE - 1) {
		if (status < HMI_MIDI_PROGRAM_CHANGE + 1) {
			state->programMessage[HMI_A002_PROGRAM_NUMBER_OFFSET] = message[1];
			state->programMessage[HMI_A002_PROGRAM_CHANNEL_OFFSET] = message[0] & (HMI_MIDI_CHANNEL_COUNT - 1);
			HmiA002_Channel(state, 0, 0, state->programMessage);
		} else if (status == HMI_MIDI_PITCH_BEND) {
			state->pitchMessage[HMI_A002_PITCH_CHANNEL_OFFSET] = message[0] & (HMI_MIDI_CHANNEL_COUNT - 1);
			state->pitchMessage[HMI_A002_PITCH_VALUE_OFFSET] = message[2];
			HmiA002_Pitch(state, 0, 0, state->pitchMessage);
		}
	}
}

void HmiA002_Convert(uint8_t *bank) {
	uint16_t i;
	uint8_t *record;
	bank[HMI_A002_BANK_FORMAT_OFFSET] = 'H';
	record = bank + SlipBytes_ReadLE32(bank + HMI_A002_BANK_INSTRUMENTS_OFFSET);
	for (i = 0; (int32_t)i < (int16_t)((uint16_t)bank[HMI_A002_BANK_COUNT_OFFSET] |
	                                   ((uint16_t)bank[HMI_A002_BANK_COUNT_OFFSET + 1] << 8)) -
	                             HMI_A002_BANK_UNCONVERTED_TAIL_COUNT;
	     ++i) {
		record[HMI_A002_INSTRUMENT_OP1_CHARACTERISTIC_OFFSET] =
		    (uint8_t)((record[HMI_A002_INSTRUMENT_OP1_CHARACTERISTIC_OFFSET] << HMI_A002_AMPLITUDE_MODULATION_SHIFT) |
		              (record[HMI_A002_INSTRUMENT_OP1_VIBRATO_OFFSET] << HMI_A002_VIBRATO_SHIFT) |
		              (record[HMI_A002_INSTRUMENT_OP1_SUSTAIN_ENABLE_OFFSET] << HMI_A002_SUSTAIN_ENABLE_SHIFT) |
		              (record[HMI_A002_INSTRUMENT_OP1_KEY_SCALE_RATE_OFFSET] << HMI_A002_KEY_SCALE_RATE_SHIFT) |
		              record[HMI_A002_INSTRUMENT_OP1_MULTIPLIER_OFFSET]);
		record[HMI_A002_INSTRUMENT_OP1_LEVEL_OFFSET] =
		    (uint8_t)((record[HMI_A002_INSTRUMENT_OP1_LEVEL_OFFSET] << HMI_A002_KEY_SCALE_LEVEL_SHIFT) |
		              record[HMI_A002_INSTRUMENT_OP1_TOTAL_LEVEL_OFFSET]);
		record[HMI_A002_INSTRUMENT_OP1_ATTACK_OFFSET] =
		    (uint8_t)((record[HMI_A002_INSTRUMENT_OP1_ATTACK_OFFSET] << HMI_A002_ENVELOPE_HIGH_SHIFT) |
		              record[HMI_A002_INSTRUMENT_OP1_DECAY_OFFSET]);
		record[HMI_A002_INSTRUMENT_OP1_RELEASE_OFFSET] =
		    (uint8_t)((record[HMI_A002_INSTRUMENT_OP1_RELEASE_OFFSET] << HMI_A002_ENVELOPE_HIGH_SHIFT) |
		              record[HMI_A002_INSTRUMENT_OP1_RELEASE_RATE_OFFSET]);
		record[HMI_A002_INSTRUMENT_FEEDBACK_OFFSET] =
		    (uint8_t)((record[HMI_A002_INSTRUMENT_FEEDBACK_SOURCE_OFFSET] << HMI_A002_FEEDBACK_SHIFT) |
		              record[HMI_A002_INSTRUMENT_FEEDBACK_OFFSET]);
		record[HMI_A002_INSTRUMENT_OP2_CHARACTERISTIC_OFFSET] =
		    (uint8_t)((record[HMI_A002_INSTRUMENT_OP2_CHARACTERISTIC_OFFSET] << HMI_A002_AMPLITUDE_MODULATION_SHIFT) |
		              (record[HMI_A002_INSTRUMENT_OP2_VIBRATO_OFFSET] << HMI_A002_VIBRATO_SHIFT) |
		              (record[HMI_A002_INSTRUMENT_OP2_SUSTAIN_ENABLE_OFFSET] << HMI_A002_SUSTAIN_ENABLE_SHIFT) |
		              (record[HMI_A002_INSTRUMENT_OP2_KEY_SCALE_RATE_OFFSET] << HMI_A002_KEY_SCALE_RATE_SHIFT) |
		              record[HMI_A002_INSTRUMENT_OP2_MULTIPLIER_OFFSET]);
		record[HMI_A002_INSTRUMENT_OP2_LEVEL_OFFSET] =
		    (uint8_t)((record[HMI_A002_INSTRUMENT_OP2_LEVEL_OFFSET] << HMI_A002_KEY_SCALE_LEVEL_SHIFT) |
		              record[HMI_A002_INSTRUMENT_OP2_TOTAL_LEVEL_OFFSET]);
		record[HMI_A002_INSTRUMENT_OP2_ATTACK_OFFSET] =
		    (uint8_t)((record[HMI_A002_INSTRUMENT_OP2_ATTACK_OFFSET] << HMI_A002_ENVELOPE_HIGH_SHIFT) |
		              record[HMI_A002_INSTRUMENT_OP2_DECAY_OFFSET]);
		record[HMI_A002_INSTRUMENT_OP2_RELEASE_OFFSET] =
		    (uint8_t)((record[HMI_A002_INSTRUMENT_OP2_RELEASE_OFFSET] << HMI_A002_ENVELOPE_HIGH_SHIFT) |
		              record[HMI_A002_INSTRUMENT_OP2_RELEASE_RATE_OFFSET]);
		record += HMI_A002_INSTRUMENT_BYTES;
	}
}

void HmiA002_Channel(HmiA002State *state, uint32_t ignoredLeadingValue, uint32_t ignoredFollowingValue,
                     const uint8_t *message) {
	(void)ignoredLeadingValue;
	(void)ignoredFollowingValue;
	state->programs[message[HMI_A002_PROGRAM_CHANNEL_OFFSET]] = message[HMI_A002_PROGRAM_NUMBER_OFFSET];
}

uint32_t HmiA002_InitEntry(HmiA002State *state, uint16_t port) {
	uint32_t result;
	/* The original entry reads a word and zero-extends it for the port argument.
	 * Unlike send, init/shutdown do not test the previous busy value. */
	state->busy = 1;
	result = HmiA002_InitWrapper(state, port);
	state->busy = 0;
	return result;
}

uint32_t HmiA002_ShutdownEntry(HmiA002State *state) {
	uint32_t result;
	state->busy = 1;
	result = HmiA002_ShutdownWrapper(state);
	state->busy = 0;
	return result;
}

uint32_t HmiA002_BankEntry(HmiA002State *state, uint8_t *bank, uint32_t driverIndex, uint32_t bankByteLength) {
	uint32_t result;
	state->bankByteLength = bankByteLength;
	state->bankDriverIndex = driverIndex;
	state->bankInput = bank;
	state->busy = 1;
	result = HmiA002_BankWrapper(state, state->bankInput, state->bankDriverIndex, state->bankByteLength);
	state->busy = 0;
	return result;
}

uint32_t HmiA002_BankWrapper(HmiA002State *state, uint8_t *bank, uint32_t ignoredDriverIndex, uint32_t bankByteLength) {
	(void)ignoredDriverIndex;

	return HmiA002_Bank(state, bankByteLength, bankByteLength, bank);
}

uint32_t HmiMusic_SetBank(HmiMusicState *state, uint32_t driver, uint32_t length, uint8_t *bank) {
	return state->driverBank[driver](state, bank, driver, length);
}

uint32_t HmiA002_MiddlewareBank(HmiMusicState *music, uint8_t *bank, uint32_t driver, uint32_t length) {
	return HmiA002_BankEntry(music->driverContexts[driver], bank, driver, length);
}

static uint32_t HmiA002_MiddlewareInit(HmiMusicState *music, uint32_t driver, uint16_t port) {
	return HmiA002_InitEntry(music->driverContexts[driver], port);
}

static uint32_t HmiA002_MiddlewareShutdown(HmiMusicState *music, uint32_t driver) {
	return HmiA002_ShutdownEntry(music->driverContexts[driver]);
}

void HmiA002_BindNativeFunctions(HmiMusicState *music, uint32_t driver, HmiA002State *module) {

	assert(driver < HMI_MUSIC_DRIVER_COUNT);
	music->driverContexts[driver] = module;
	music->driverSend[driver] = HmiA002_MiddlewareSend;
	music->driverInit[driver] = HmiA002_MiddlewareInit;
	music->driverShutdown[driver] = HmiA002_MiddlewareShutdown;
	music->driverReset[driver] = HmiA002_MiddlewareShutdown;
	music->driverBank[driver] = HmiA002_MiddlewareBank;
}

void HmiA002_MiddlewareSend(HmiMusicState *music, uint8_t *message, uint32_t length, uint32_t driver) {
	(void)length; /* A002 far entry consumes only the message far pointer. */

	(void)HmiA002_SendEntry(music->driverContexts[driver], message, 0);
}

uint32_t HmiA002_SendEntry(HmiA002State *state, const uint8_t *message, uint32_t preservedReturnValue) {
	if (state->busy == 1)
		return HMI_A002_BUSY_RESULT;

	state->busy = 1;
	HmiA002_SendWrapper(state, message, 0, 0);
	state->busy = 0;
	return preservedReturnValue;
}

uint32_t HmiA002_SendWrapper(HmiA002State *state, const uint8_t *message, uint32_t ignoredFollowingValue,
                             uint32_t ignoredLeadingValue) {
	/* Native message pointer replaces the original offset/selector pair. */
	HmiA002_Send(state, ignoredLeadingValue, ignoredFollowingValue, message);
	return 0;
}

uint32_t HmiA002_InitWrapper(HmiA002State *state, uint32_t port) {
	state->portInput[0] = (uint8_t)port;
	state->portInput[1] = (uint8_t)(port >> 8);
	state->portInput[2] = (uint8_t)(port >> 16);
	state->portInput[3] = (uint8_t)(port >> 24);
	HmiA002_Initialize(state, port, port, state->portInput);
	return 0;
}

uint32_t HmiA002_ShutdownWrapper(HmiA002State *state) {
	HmiA002_Shutdown(state);
	return 0;
}

uint32_t HmiA002_Initialize(HmiA002State *state, uint32_t ignoredLeadingValue, uint32_t ignoredFollowingValue,
                            const uint8_t *config) {
	(void)ignoredLeadingValue;
	(void)ignoredFollowingValue;
	state->portArgument = SlipBytes_ReadLE32(config);
	HmiA002_SetPort(state, &state->portArgument);
	HmiA002_ResetChip(state);
	state->initialized = 1;
	return 0;
}

void HmiA002_Shutdown(HmiA002State *state) {
	state->portArgument = state->portBase;
	HmiA002_SetPort(state, &state->portArgument);
	HmiA002_ClearChip(state);
	HmiA002_DisableChip(state);
	state->initialized = 0;
}

uint8_t HmiA002_SetPort(HmiA002State *state, const uint32_t *port) {
	state->portCopy = *port;
	state->portBase = *port;
	HmiA002_Write(state, HMI_A002_TEST_REGISTER, HMI_A002_WAVEFORM_ENABLE);
	state->rhythm = 0;
	HmiA002_ResetChip(state);
	state->chipEnabled = 1;
	return 0;
}

uint8_t HmiA002_ResetChip(HmiA002State *state) {
	uint32_t i;
	for (i = 0; i < HMI_A002_VOICE_COUNT; ++i) {
		state->cache.frequency.frequencyHigh[i] = 0;
		HmiA002_Write(state, (uint8_t)(i + HMI_A002_FREQUENCY_HIGH_BASE), state->cache.frequency.frequencyHigh[i]);
	}
	for (i = 0; i < sizeof(state->instrumentSet); ++i)
		state->instrumentSet[i] = 0;
	state->mode = 0;
	state->rhythm &= HMI_A002_RHYTHM_DEPTH_MASK;
	state->rhythm |= HMI_A002_RHYTHM_DEPTH_MASK;
	HmiA002_Write(state, HMI_A002_RHYTHM_REGISTER, state->rhythm);
	return 0;
}

uint8_t HmiA002_ClearChip(HmiA002State *state) {
	uint8_t i;
	if (state->chipEnabled == 0)
		return HMI_A002_CHIP_DISABLED_RESULT;
	HmiA002_Write(state, HMI_A002_RHYTHM_REGISTER, state->rhythm);
	for (i = 0; i < HMI_A002_VOICE_COUNT; ++i)
		HmiA002_Write(state, (uint8_t)(i + HMI_A002_FREQUENCY_HIGH_BASE),
		              (uint8_t)(state->cache.frequency.frequencyHigh[i] & HMI_A002_KEY_OFF_MASK));
	for (i = 0; i < HMI_A002_VOICE_COUNT; ++i)
		HmiA002_Write(state,
		              (uint8_t)(state->operatorOffsets[i * HMI_A002_OPERATOR_COUNT + 1] + HMI_A002_OPERATOR_LEVEL_BASE),
		              HMI_A002_MUTED_LEVEL);
	for (i = 0; i < sizeof(state->instrumentSet); ++i)
		state->instrumentSet[i] = 0;
	return 0;
}

uint8_t HmiA002_DisableChip(HmiA002State *state) {
	state->chipEnabled = 0;
	return 0;
}

uint8_t HmiA002_Write(HmiA002State *state, uint8_t reg, uint8_t value) {
	uint16_t dataPort;
	state->portWrite(state->portContext, (uint16_t)state->portBase, reg);
	dataPort = (uint16_t)(state->portBase + 1u);
	state->portRead(state->portContext, dataPort);
	state->portRead(state->portContext, dataPort);
	state->portRead(state->portContext, dataPort);
	state->portRead(state->portContext, dataPort);
	state->portRead(state->portContext, dataPort);
	state->portRead(state->portContext, dataPort);
	state->portWrite(state->portContext, dataPort, value);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	state->portRead(state->portContext, (uint16_t)state->portBase);
	return state->portRead(state->portContext, (uint16_t)state->portBase);
}

uint8_t HmiA002_KeyOff(HmiA002State *state, uint8_t voice) {
	if (state->activeNotes[voice] == 0)
		return HMI_A002_NOTE_INACTIVE_RESULT;
	state->cache.frequency.frequencyHigh[voice] &= HMI_A002_KEY_OFF_MASK;
	HmiA002_Write(state, (uint8_t)(voice + HMI_A002_FREQUENCY_HIGH_BASE), state->cache.frequency.frequencyHigh[voice]);
	state->activeNotes[voice] = 0;
	return 0;
}

uint8_t HmiA002_Select(HmiA002State *state, uint8_t channel) {
	uint32_t i, voice;
	for (i = 0; i < HMI_A002_VOICE_COUNT; ++i)
		if (state->activeNotes[i] == 0)
			return (uint8_t)i;
	for (i = 0; i < HMI_MIDI_CHANNEL_COUNT; ++i) {
		if (state->controllerGlobals[HMI_A002_CONTROLLER_PITCH_CHANGED_INDEX + i] == 0) {
			for (voice = 0; voice < HMI_A002_VOICE_COUNT; ++voice)
				if (state->voiceChannels[voice] == i)
					return (uint8_t)voice;
		}
	}
	if (channel >= HMI_A002_VOICE_COUNT)
		channel = (uint8_t)(channel - HMI_A002_VOICE_COUNT);
	return channel;
}

uint8_t HmiA002_KeyOn(HmiA002State *state, uint8_t voice, uint32_t frequency) {
	state->cache.frequency.frequencyLow[voice] = (uint8_t)frequency;
	state->cache.frequency.frequencyHigh[voice] = (uint8_t)((frequency >> 8) | HMI_A002_KEY_ON);
	HmiA002_Write(state, (uint8_t)(voice + HMI_A002_FREQUENCY_LOW_BASE), state->cache.frequency.frequencyLow[voice]);
	HmiA002_Write(state, (uint8_t)(voice + HMI_A002_FREQUENCY_HIGH_BASE),
	              (uint8_t)(state->cache.frequency.frequencyHigh[voice] & HMI_A002_KEY_OFF_MASK));
	HmiA002_Write(state, (uint8_t)(voice + HMI_A002_FREQUENCY_HIGH_BASE), state->cache.frequency.frequencyHigh[voice]);
	state->activeNotes[voice] = 1;
	return 0;
}

uint8_t HmiA002_Instrument(HmiA002State *state, const uint8_t *record, uint8_t voice) {
	uint8_t op = state->operatorOffsets[voice * HMI_A002_OPERATOR_COUNT];
	uint8_t characteristic1 = record[HMI_A002_INSTRUMENT_OP1_CHARACTERISTIC_OFFSET],
	        level1 = record[HMI_A002_INSTRUMENT_OP1_LEVEL_OFFSET],
	        attack1 = record[HMI_A002_INSTRUMENT_OP1_ATTACK_OFFSET];
	uint8_t release1 = record[HMI_A002_INSTRUMENT_OP1_RELEASE_OFFSET],
	        feedback = record[HMI_A002_INSTRUMENT_FEEDBACK_OFFSET],
	        waveform1 = record[HMI_A002_INSTRUMENT_OP1_WAVEFORM_OFFSET];
	uint8_t characteristic2 = record[HMI_A002_INSTRUMENT_OP2_CHARACTERISTIC_OFFSET],
	        level2 = record[HMI_A002_INSTRUMENT_OP2_LEVEL_OFFSET],
	        attack2 = record[HMI_A002_INSTRUMENT_OP2_ATTACK_OFFSET];
	uint8_t release2 = record[HMI_A002_INSTRUMENT_OP2_RELEASE_OFFSET],
	        waveform2 = record[HMI_A002_INSTRUMENT_OP2_WAVEFORM_OFFSET];
	state->cache.operatorRelease[state->operatorOffsets[voice * HMI_A002_OPERATOR_COUNT + 1]] = release2;
	state->cache.operatorRelease[state->operatorOffsets[voice * HMI_A002_OPERATOR_COUNT]] = release1;
	HmiA002_Write(state, (uint8_t)(op + HMI_A002_OPERATOR_CHARACTERISTIC_BASE), characteristic1);
	HmiA002_Write(state, (uint8_t)(op + HMI_A002_OPERATOR_LEVEL_BASE), level1);
	state->operatorLevels[op] = level1;
	HmiA002_Write(state, (uint8_t)(op + HMI_A002_OPERATOR_ATTACK_BASE), attack1);
	HmiA002_Write(state, (uint8_t)(op + HMI_A002_OPERATOR_RELEASE_BASE), release1);
	HmiA002_Write(state, (uint8_t)(voice + HMI_A002_FEEDBACK_BASE), feedback);
	HmiA002_Write(state, (uint8_t)(op + HMI_A002_WAVEFORM_BASE), waveform1);
	op = state->operatorOffsets[voice * HMI_A002_OPERATOR_COUNT + 1];
	HmiA002_Write(state, (uint8_t)(op + HMI_A002_OPERATOR_CHARACTERISTIC_BASE), characteristic2);
	state->operatorLevels[op] = level2;
	HmiA002_Write(state, (uint8_t)(op + HMI_A002_OPERATOR_ATTACK_BASE), attack2);
	HmiA002_Write(state, (uint8_t)(op + HMI_A002_OPERATOR_RELEASE_BASE), release2);
	HmiA002_Write(state, (uint8_t)(op + HMI_A002_WAVEFORM_BASE), waveform2);
	state->instrumentSet[voice] = 1;
	return 0;
}

void HmiA002_Off(HmiA002State *state, uint32_t ignoredLeadingValue, uint32_t ignoredFollowingValue,
                 const uint8_t *message) {
	uint32_t voice;
	(void)ignoredLeadingValue;
	(void)ignoredFollowingValue;
	/* Preserve the redundant original channel predicate. */
	if (message[HMI_A002_OFF_CHANNEL_OFFSET] < HMI_MIDI_CHANNEL_COUNT &&
	    (message[HMI_A002_OFF_CHANNEL_OFFSET] < HMI_MIDI_CHANNEL_COUNT ||
	     message[HMI_A002_OFF_CHANNEL_OFFSET] == HMI_MIDI_PERCUSSION_CHANNEL)) {
		if (state->sustain[message[HMI_A002_OFF_CHANNEL_OFFSET]] == 0 ||
		    state->deferredCount[message[HMI_A002_OFF_CHANNEL_OFFSET]] >= HMI_A002_DEFERRED_CAPACITY) {
			for (voice = 0; voice < HMI_A002_VOICE_COUNT; ++voice) {
				if (state->activeNotes[voice] == message[HMI_A002_OFF_NOTE_OFFSET] &&
				    state->voiceChannels[voice] == message[HMI_A002_OFF_CHANNEL_OFFSET]) {
					HmiA002_KeyOff(state, (uint8_t)voice);
					state->activeNotes[voice] = 0;
				}
			}
		} else {
			state->deferred[message[HMI_A002_OFF_CHANNEL_OFFSET]]
			               [state->deferredCount[message[HMI_A002_OFF_CHANNEL_OFFSET]]][HMI_A002_OFF_NOTE_OFFSET] =
			    message[HMI_A002_OFF_NOTE_OFFSET];
			state->deferred[message[HMI_A002_OFF_CHANNEL_OFFSET]]
			               [state->deferredCount[message[HMI_A002_OFF_CHANNEL_OFFSET]]][HMI_A002_OFF_VELOCITY_OFFSET] =
			    message[HMI_A002_OFF_VELOCITY_OFFSET];
			state->deferred[message[HMI_A002_OFF_CHANNEL_OFFSET]]
			               [state->deferredCount[message[HMI_A002_OFF_CHANNEL_OFFSET]]][HMI_A002_OFF_CHANNEL_OFFSET] =
			    message[HMI_A002_OFF_CHANNEL_OFFSET];
			++state->deferredCount[message[HMI_A002_OFF_CHANNEL_OFFSET]];
		}
	}
}

void HmiA002_AllOff(HmiA002State *state, uint32_t channel) {
	uint32_t voice;
	if (channel < HMI_MIDI_CHANNEL_COUNT || (channel == HMI_MIDI_PERCUSSION_CHANNEL && state->percussionLoaded != 0)) {
		for (voice = 0; voice < HMI_A002_VOICE_COUNT; ++voice) {
			if (state->voiceChannels[voice] == channel) {
				HmiA002_KeyOff(state, (uint8_t)voice);
				state->activeNotes[voice] = 0;
			}
		}
	}
}

void HmiA002_Reset(HmiA002State *state, uint32_t channel) {
	if (channel < HMI_MIDI_CHANNEL_COUNT || (channel == HMI_MIDI_PERCUSSION_CHANNEL && state->percussionLoaded != 0)) {
		state->controllerGlobals[HMI_A002_CONTROLLER_CHANNEL_VOLUME_INDEX + channel] = HMI_MIDI_DATA_MAXIMUM;
		state->controllerGlobals[HMI_A002_CONTROLLER_VOICE_VELOCITY_INDEX + channel] = HMI_MIDI_DATA_MAXIMUM;
		state->controllerGlobals[HMI_A002_CONTROLLER_VOLUME_CHANGED_INDEX + channel] = 0;
		state->sustain[channel] = 0;
		state->controllerGlobals[channel] = HMI_A002_PITCH_CENTER;
		state->controllerGlobals[HMI_A002_CONTROLLER_PITCH_RANGE_INDEX + channel] = HMI_A002_DEFAULT_PITCH_RANGE;
	}
}

void HmiA002_Volume(HmiA002State *state, uint32_t channel, uint8_t volume) {
	uint32_t voice;
	if (channel < HMI_MIDI_CHANNEL_COUNT) {
		state->controllerGlobals[HMI_A002_CONTROLLER_CHANNEL_VOLUME_INDEX + channel] = volume;
		state->controllerGlobals[HMI_A002_CONTROLLER_VOLUME_CHANGED_INDEX + channel] = 1;
		for (voice = 0; voice < HMI_A002_VOICE_COUNT; ++voice) {
			if (state->activeNotes[voice] != 0 && state->voiceChannels[voice] == channel) {
				const uint32_t scaled = (((uint32_t)volume << HMI_A002_VOLUME_FRACTION_BITS) / HMI_MIDI_DATA_MAXIMUM *
				                         state->controllerGlobals[HMI_A002_CONTROLLER_VOICE_VELOCITY_INDEX + voice]) >>
				                        HMI_A002_VOLUME_FRACTION_BITS;
				uint32_t attenuation =
				    HMI_A002_LEVEL_STEPS - state->volumeCurve[(scaled & UINT8_MAX) / HMI_A002_VOLUME_CURVE_STEP];
				uint8_t op;
				if ((state->melodicData[state->programs[state->voiceChannels[voice]] * HMI_A002_INSTRUMENT_BYTES +
				                        HMI_A002_INSTRUMENT_FEEDBACK_OFFSET] &
				     HMI_A002_ADDITIVE_CONNECTION) != 0) {
					op = state->operatorOffsets[voice * HMI_A002_OPERATOR_COUNT];
					HmiA002_Write(state, (uint8_t)(op + HMI_A002_OPERATOR_LEVEL_BASE),
					              (uint8_t)(((HMI_A002_LEVEL_PRODUCT_ONE -
					                          attenuation * HMI_A002_VOLUME_CURVE_STEP *
					                              (HMI_A002_LEVEL_STEPS -
					                               (state->operatorLevels[op] & HMI_A002_TOTAL_LEVEL_MASK))) >>
					                         HMI_A002_VOLUME_FRACTION_BITS) |
					                        (state->operatorLevels[op] & HMI_A002_KEY_SCALE_LEVEL_MASK)));
				}
				op = state->operatorOffsets[voice * HMI_A002_OPERATOR_COUNT + 1];
				attenuation =
				    HMI_A002_LEVEL_STEPS - state->volumeCurve[(scaled & UINT8_MAX) / HMI_A002_VOLUME_CURVE_STEP];
				HmiA002_Write(
				    state, (uint8_t)(op + HMI_A002_OPERATOR_LEVEL_BASE),
				    (uint8_t)(((HMI_A002_LEVEL_PRODUCT_ONE -
				                attenuation * HMI_A002_VOLUME_CURVE_STEP *
				                    (HMI_A002_LEVEL_STEPS - (state->operatorLevels[op] & HMI_A002_TOTAL_LEVEL_MASK))) >>
				               HMI_A002_VOLUME_FRACTION_BITS) |
				              (state->operatorLevels[op] & HMI_A002_KEY_SCALE_LEVEL_MASK)));
			}
		}
	}
}

void HmiA002_Control(HmiA002State *state, uint32_t ignoredLeadingValue, uint32_t ignoredFollowingValue,
                     const uint8_t *message) {
	const uint8_t controller = message[HMI_A002_CONTROL_NUMBER_OFFSET];
	(void)ignoredLeadingValue;
	(void)ignoredFollowingValue;
	if (controller < HMI_A002_CONTROL_PITCH_RANGE) {
		if (controller > HMI_MIDI_CONTROL_VOLUME - 1) {
			if (controller < HMI_MIDI_CONTROL_VOLUME + 1) {
				HmiA002_Volume(state, message[HMI_A002_CONTROL_CHANNEL_OFFSET], message[HMI_A002_CONTROL_VALUE_OFFSET]);
			} else if (controller == HMI_MIDI_CONTROL_SUSTAIN) {
				state->sustain[message[HMI_A002_CONTROL_CHANNEL_OFFSET]] = message[HMI_A002_CONTROL_VALUE_OFFSET];
				if (message[HMI_A002_CONTROL_VALUE_OFFSET] == 0) {
					while (state->deferredCount[message[HMI_A002_CONTROL_CHANNEL_OFFSET]] != 0) {
						--state->deferredCount[message[HMI_A002_CONTROL_CHANNEL_OFFSET]];
						HmiA002_Off(state, 0, 0,
						            state->deferred[message[HMI_A002_CONTROL_CHANNEL_OFFSET]]
						                           [state->deferredCount[message[HMI_A002_CONTROL_CHANNEL_OFFSET]]]);
					}
				}
			}
		}
	} else if (controller < HMI_A002_CONTROL_PITCH_RANGE + 1) {
		state->controllerGlobals[HMI_A002_CONTROLLER_PITCH_RANGE_INDEX + message[HMI_A002_CONTROL_CHANNEL_OFFSET]] =
		    message[HMI_A002_CONTROL_VALUE_OFFSET];
	} else if (controller > HMI_MIDI_CONTROL_RESET - 1) {
		if (controller < HMI_MIDI_CONTROL_RESET + 1)
			HmiA002_Reset(state, message[HMI_A002_CONTROL_CHANNEL_OFFSET]);
		else if (controller == HMI_MIDI_CONTROL_ALL_NOTES_OFF)
			HmiA002_AllOff(state, message[HMI_A002_CONTROL_CHANNEL_OFFSET]);
	}
}

uint32_t HmiA002_PitchCalc(HmiA002State *state, uint32_t bend, uint32_t note, uint32_t voice) {
	uint32_t index = note - HMI_A002_NOTES_PER_OCTAVE, remainder, frequency, difference;
	const uint32_t range =
	    state->controllerGlobals[HMI_A002_CONTROLLER_PITCH_RANGE_INDEX + state->voiceChannels[voice]];
	for (remainder = index; remainder > HMI_A002_NOTES_PER_OCTAVE - 1; remainder -= HMI_A002_NOTES_PER_OCTAVE) {
	}
	frequency = state->pitchTables[index];
	if (bend < HMI_A002_PITCH_CENTER) {
		difference = frequency - state->pitchTables[index - range];
		if (difference > HMI_A002_PITCH_BLOCK_THRESHOLD)
			difference = ((frequency & HMI_A002_FREQUENCY_NUMBER_MASK) -
			              state->pitchTables[HMI_A002_PITCH_LOWER_VIEW_INDEX + range]) &
			             HMI_A002_FREQUENCY_NUMBER_MASK;
		frequency -= (difference * (((HMI_A002_PITCH_CENTER - 1u - bend) * HMI_A002_PITCH_INTERPOLATION_SCALE) >>
		                            HMI_A002_PITCH_FRACTION_BITS)) /
		             HMI_A002_PITCH_INTERPOLATION_SCALE;
	} else {
		difference = state->pitchTables[index + range] - frequency;
		if (difference > HMI_A002_PITCH_BLOCK_THRESHOLD) {
			frequency = ((frequency & HMI_A002_FREQUENCY_BLOCK_MASK) + HMI_A002_FREQUENCY_BLOCK_STEP) |
			            state->pitchTables[HMI_A002_PITCH_UPPER_VIEW_INDEX + HMI_A002_NOTES_PER_OCTAVE - 1 - remainder];
			difference = state->pitchTables[index + range] - frequency;
		}
		frequency += (difference * (((bend - HMI_A002_PITCH_CENTER) * HMI_A002_PITCH_INTERPOLATION_SCALE) >>
		                            HMI_A002_PITCH_FRACTION_BITS)) /
		             HMI_A002_PITCH_INTERPOLATION_SCALE;
	}
	return frequency;
}

void HmiA002_Pitch(HmiA002State *state, uint32_t ignoredLeadingValue, uint32_t ignoredFollowingValue,
                   const uint8_t *message) {
	uint32_t voice;
	(void)ignoredLeadingValue;
	(void)ignoredFollowingValue;
	if (message[HMI_A002_PITCH_CHANNEL_OFFSET] < HMI_MIDI_CHANNEL_COUNT &&
	    message[HMI_A002_PITCH_CHANNEL_OFFSET] != HMI_MIDI_PERCUSSION_CHANNEL) {
		state->controllerGlobals[message[HMI_A002_PITCH_CHANNEL_OFFSET]] = message[HMI_A002_PITCH_VALUE_OFFSET];
		state->controllerGlobals[HMI_A002_CONTROLLER_PITCH_CHANGED_INDEX + message[HMI_A002_PITCH_CHANNEL_OFFSET]] = 1;
		for (voice = 0; voice < HMI_A002_VOICE_COUNT; ++voice) {
			if (state->activeNotes[voice] != 0 &&
			    state->voiceChannels[voice] == message[HMI_A002_PITCH_CHANNEL_OFFSET]) {
				const uint32_t frequency =
				    HmiA002_PitchCalc(state, message[HMI_A002_PITCH_VALUE_OFFSET], state->activeNotes[voice], voice);
				HmiA002_Frequency(state, (uint8_t)voice, frequency);
			}
		}
	}
}

uint8_t HmiA002_Frequency(HmiA002State *state, uint8_t voice, uint32_t frequency) {
	state->cache.frequency.frequencyLow[voice] = (uint8_t)frequency;
	state->cache.frequency.frequencyHigh[voice] = (uint8_t)((frequency >> 8) | HMI_A002_KEY_ON);
	HmiA002_Write(state, (uint8_t)(voice + HMI_A002_FREQUENCY_LOW_BASE), state->cache.frequency.frequencyLow[voice]);
	HmiA002_Write(state, (uint8_t)(voice + HMI_A002_FREQUENCY_HIGH_BASE), state->cache.frequency.frequencyHigh[voice]);
	return 0;
}

uint32_t HmiA002_Bank(HmiA002State *state, uint32_t ignoredLeadingValue, uint32_t ignoredFollowingValue,
                      uint8_t *bank) {
	uint8_t channel;
	(void)ignoredLeadingValue;
	(void)ignoredFollowingValue;
	if (bank[HMI_A002_BANK_FORMAT_OFFSET] != 'H')
		HmiA002_Convert(bank);
	if (state->bankToggle == 0) {
		state->bankToggle = 1;
		state->melodicBank = bank;
		state->melodicCount = (int16_t)((uint16_t)bank[HMI_A002_BANK_COUNT_OFFSET] |
		                                ((uint16_t)bank[HMI_A002_BANK_COUNT_OFFSET + 1] << 8));
		state->melodicIndex = bank + SlipBytes_ReadLE32(bank + HMI_A002_BANK_INDEX_OFFSET);
		state->melodicData = bank + SlipBytes_ReadLE32(bank + HMI_A002_BANK_INSTRUMENTS_OFFSET);
		state->melodicLoaded = 1;
		for (channel = 0; channel < HMI_A002_VOICE_COUNT; ++channel) {
			state->initialProgramMessage[HMI_A002_PROGRAM_NUMBER_OFFSET] = 0;
			state->initialProgramMessage[HMI_A002_PROGRAM_CHANNEL_OFFSET] = channel;
			state->message = state->initialProgramMessage;
			HmiA002_Channel(state, channel, 0, state->initialProgramMessage);
		}
	} else {
		state->bankToggle = 0;
		state->percussionBank = bank;
		state->percussionIndex = bank + SlipBytes_ReadLE32(bank + HMI_A002_BANK_INDEX_OFFSET);
		state->percussionData = bank + SlipBytes_ReadLE32(bank + HMI_A002_BANK_INSTRUMENTS_OFFSET);
		state->percussionLoaded = 1;
	}
	return 0;
}

uint32_t HmiMusic_IsStopped(const HmiMusicState *state, uint32_t song) { return state->playing[song] == 0 ? 1u : 0u; }

static uint32_t HmiMusic_Abs(uint32_t value) { return (value & (UINT32_MAX ^ INT32_MAX)) != 0 ? 0u - value : value; }

uint8_t *HmiMusic_Instrument(HmiMusicState *state, uint32_t instrument, uint8_t note) {
	uint8_t *best, *candidate;
	uint32_t distance;
	if (state->instrumentBankEnabled == 0 || state->instrumentPointers[instrument] == NULL)
		return NULL;
	best = state->instrumentPointers[instrument];
	distance = HmiMusic_Abs(SlipBytes_ReadLE32(best + HMI_INSTRUMENT_NOTE_OFFSET) - note);
	/* Each instrument header is followed by its declared data length.
	 * The zero-length sentinel is tested before comparing its base note. */
	for (candidate = best + HMI_INSTRUMENT_HEADER_BYTES + SlipBytes_ReadLE32(best + HMI_INSTRUMENT_DATA_LENGTH_OFFSET);
	     SlipBytes_ReadLE32(candidate + HMI_INSTRUMENT_DATA_LENGTH_OFFSET) != 0;
	     candidate += HMI_INSTRUMENT_HEADER_BYTES + SlipBytes_ReadLE32(candidate + HMI_INSTRUMENT_DATA_LENGTH_OFFSET)) {
		if (HmiMusic_Abs(SlipBytes_ReadLE32(candidate + HMI_INSTRUMENT_NOTE_OFFSET) - note) < distance) {
			distance = HmiMusic_Abs(SlipBytes_ReadLE32(candidate + HMI_INSTRUMENT_NOTE_OFFSET) - note);
			best = candidate;
		}
	}
	return best;
}

uint32_t HmiMusic_Unregister(HmiMusicState *state, uint32_t song) {
	if (song < HMI_MUSIC_SONG_COUNT) {
		state->songs[song] = NULL;
	} else {
		return HMI_MUSIC_ERROR_INVALID_HANDLE;
	}
	return 0;
}

uint32_t HmiMusic_Register(HmiMusicState *state, const HmiMusicSongDescriptor *descriptor, uint32_t *routing,
                           uint32_t *songIndex, HmiMusicBranchRecord *branchStorage,
                           void (*completion)(HmiMusicState *)) {
	static const uint8_t signature[] = "HMIMIDIP013195";
	uint32_t slot, i, track, offset = 0;
	uint8_t *const song = descriptor->songData;
	uint8_t *counts, *records;
	for (i = 0; signature[i] != 0; ++i)
		if (signature[i] != song[i])
			return HMI_MUSIC_ERROR_INVALID_DATA;
	for (slot = 0; slot < HMI_MUSIC_SONG_COUNT; ++slot)
		if (state->songs[slot] == NULL)
			break;
	if (slot == HMI_MUSIC_SONG_COUNT)
		return HMI_MUSIC_ERROR_NO_HANDLES;
	for (i = 0; i < HMI_MUSIC_TRACK_COUNT; ++i)
		state->cursors[slot][i] = NULL;
	state->songs[slot] = song;
	for (i = 0; i < HMI_MUSIC_TRIGGER_COUNT; ++i)
		state->triggerCallbacks[slot][i] = NULL;
	state->branchCallbacks[slot] = NULL;
	state->loopCallbacks[slot] = NULL;
	counts = song + SlipBytes_ReadLE32(song + HMI_SONG_BRANCH_COUNTS_OFFSET);
	state->routing[slot] = routing;
	state->activeTracks[slot] = SlipBytes_ReadLE32(song + HMI_SONG_TRACK_COUNT_OFFSET);
	state->totalTracks[slot] = state->activeTracks[slot];
	for (i = 0; i < sizeof(uint32_t); ++i)
		song[HMI_SONG_COMPLETION_CALLBACK_OFFSET + i] = (uint8_t)(descriptor->completionCallbackOffset >> (i * 8));
	song[HMI_SONG_COMPLETION_SELECTOR_OFFSET] = (uint8_t)descriptor->completionCallbackSelector;
	song[HMI_SONG_COMPLETION_SELECTOR_OFFSET + 1] = (uint8_t)(descriptor->completionCallbackSelector >> 8);
	state->completionCallbacks[slot] = completion;
	for (track = 0; track < state->activeTracks[slot]; ++track) {
		const uint8_t *const header = song + HMI_SONG_TRACKS_OFFSET + offset;
		state->elapsed[slot][track] = 0;
		state->trackHeaders[slot][track] = header;
		state->cursors[slot][track] = header + HMI_SONG_TRACK_HEADER_BYTES;
		state->cursors[slot][track] +=
		    HmiMusic_ReadDelta(header + HMI_SONG_TRACK_HEADER_BYTES, &state->delta[slot][track]);
		offset += SlipBytes_ReadLE32(header + HMI_SONG_TRACK_SIZE_OFFSET);
	}
	records = counts + state->activeTracks[slot];
	for (track = 0; track < state->activeTracks[slot]; ++track) {
		if (*counts != 0)
			state->branches[slot][track] = branchStorage;
		for (i = 0; i < *counts; ++i) {
			branchStorage[i].dosBytes = records + i * HMI_SONG_BRANCH_BYTES;
			branchStorage[i].controllerPairs =
			    song + SlipBytes_ReadLE32(records + i * HMI_SONG_BRANCH_BYTES + HMI_SONG_BRANCH_CONTROLLERS_OFFSET);
		}
		records += *counts * HMI_SONG_BRANCH_BYTES;

		if (*counts != 0)
			branchStorage += *counts;
		++counts;
	}
	for (track = 0; track < state->totalTracks[slot]; ++track) {
		if (routing[track] == HMI_MUSIC_UNROUTED_DRIVER) {
			uint32_t candidate, found = 0;
			const uint8_t *const desired =
			    song + HMI_SONG_DESIRED_DRIVERS_OFFSET + track * HMI_SONG_DESIRED_DRIVER_RECORD_BYTES;
			for (candidate = 0; SlipBytes_ReadLE32(desired + candidate * HMI_SERIALIZED_DWORD_BYTES) != 0 &&
			                    found == 0 && candidate < HMI_MUSIC_DRIVER_COUNT;
			     ++candidate) {
				for (i = 0; i < HMI_MUSIC_DRIVER_COUNT; ++i) {
					const uint32_t want = SlipBytes_ReadLE32(desired + candidate * HMI_SERIALIZED_DWORD_BYTES);
					const uint32_t have = state->driverIds[i];
					if ((want == HMI_MUSIC_DRIVER_A000 &&
					     (have == HMI_MUSIC_DRIVER_A000 || have == HMI_MUSIC_DRIVER_A001 ||
					      have == HMI_MUSIC_DRIVER_A008)) ||
					    (want == HMI_MUSIC_DRIVER_A002 &&
					     (have == HMI_MUSIC_DRIVER_A002 || have == HMI_MUSIC_DRIVER_A009)) ||
					    (want != HMI_MUSIC_DRIVER_A000 && want != HMI_MUSIC_DRIVER_A002 && have == want)) {
						routing[track] = i;
						found = 1;
						break;
					}
				}
			}
			if (SlipBytes_ReadLE32(desired) == 0)
				routing[track] = 0;
			else if (found == 0) {
				state->cursors[slot][track] = NULL;
				routing[track] = HMI_MUSIC_UNROUTED_DRIVER;
				--state->activeTracks[slot];
			}
		}
	}
	for (i = 0; i < HMI_SONG_CONTROLLER_RESTORE_FLAGS_BYTES; ++i)
		song[HMI_SONG_CONTROLLER_RESTORE_FLAGS_OFFSET + i] = 1;
	*songIndex = slot;
	return 0;
}

uint32_t HmiMusic_Fade(HmiMusicState *state, HmiTimerState *timer, uint32_t song, uint32_t mode, uint32_t duration,
                       uint8_t from, uint8_t to, uint32_t divisor) {
	const uint32_t difference = (mode & HMI_MUSIC_FADE_INCREASE) != 0 ? (uint32_t)to - from : (uint32_t)from - to;
	const uint32_t rate = HmiTimer_GetRate(timer, state->timerHandles[song]);
	const uint32_t interval = HMI_MUSIC_FADE_TIME_ONE_Q16 / rate;
	const uint32_t steps = ((duration << HMI_MUSIC_FRACTION_BITS) / interval) / divisor;
	if (steps == 0) {
		if ((mode & HMI_MUSIC_FADE_STOP) != 0)
			HmiMusic_StopSong(state, timer, song);
		else
			HmiMusic_SetSongVolume(state, song, to);
		return 0;
	}
	HmiMusic_SetSongVolume(state, song, from);
	state->fadeMode[song] = mode;
	state->fadeStep[song] = (difference << HMI_MUSIC_FRACTION_BITS) / steps;
	state->fadeVolume[song] = (uint32_t)from << HMI_MUSIC_FRACTION_BITS;
	state->fadeRemaining[song] = steps;
	return 0;
}

uint32_t HmiMusic_StopSong(HmiMusicState *state, HmiTimerState *timer, uint32_t song) {
	uint32_t handle;
	if (song >= HMI_MUSIC_SONG_COUNT)
		return HMI_MUSIC_ERROR_INVALID_HANDLE;
	handle = state->timerHandles[song];
	if (handle != UINT32_MAX)
		HmiTimer_Remove(timer, handle);

	if (handle == UINT32_MAX)
		timer->accumulators[HMI_MUSIC_TIMER_LAST_SLOT] |= HMI_MUSIC_TIMER_ALIAS_MASK;
	else
		timer->songForSlot[handle] = HMI_MUSIC_TIMER_NO_SONG;
	state->timerHandles[song] = UINT32_MAX;
	if (state->playing[song] != 0) {
		HmiMusicSongDescriptor descriptor;
		descriptor.songData = state->songs[song];
		descriptor.completionCallbackOffset =
		    SlipBytes_ReadLE32(descriptor.songData + HMI_SONG_COMPLETION_CALLBACK_OFFSET);
		descriptor.completionCallbackSelector =
		    SlipBytes_ReadLE16(descriptor.songData + HMI_SONG_COMPLETION_SELECTOR_OFFSET);
		HmiMusic_Cleanup(state, song);
		state->playing[song] = 0;
		state->songs[song] = NULL;
		HmiMusic_ResetSong(state, song, &descriptor);
	}
	return 0;
}

uint32_t HmiMusic_SetTempo(HmiMusicState *state, HmiTimerState *timer, uint32_t song, uint32_t percent) {
	uint32_t scale;
	uint32_t rate;
	if (HmiMusic_IsStopped(state, song) != 0)
		return 0;
	scale = (percent << HMI_MUSIC_FRACTION_BITS) / HMI_MUSIC_PERCENT_ONE;
	rate = (scale * SlipBytes_ReadLE32(state->songs[song] + HMI_SONG_TEMPO_OFFSET)) >> HMI_MUSIC_FRACTION_BITS;
	if (rate == 0)
		rate = 1;
	HmiTimer_SetRate(timer, state->timerHandles[song], rate);
	return HmiTimer_GetRate(timer, state->timerHandles[song]);
}

uint32_t HmiMusic_ResetSong(HmiMusicState *state, uint32_t songIndex, const HmiMusicSongDescriptor *descriptor) {
	uint32_t offset = 0;
	uint32_t track;
	uint8_t *const song = descriptor->songData;
	const uint8_t *const tracks = song + HMI_SONG_TRACKS_OFFSET;
	const uint32_t callback = descriptor->completionCallbackOffset;
	state->songs[songIndex] = song;
	state->activeTracks[songIndex] = SlipBytes_ReadLE32(song + HMI_SONG_TRACK_COUNT_OFFSET);
	state->totalTracks[songIndex] = state->activeTracks[songIndex];
	song[HMI_SONG_COMPLETION_CALLBACK_OFFSET] = (uint8_t)callback;
	song[HMI_SONG_COMPLETION_CALLBACK_OFFSET + 1] = (uint8_t)(callback >> 8);
	song[HMI_SONG_COMPLETION_CALLBACK_OFFSET + 2] = (uint8_t)(callback >> 16);
	song[HMI_SONG_COMPLETION_CALLBACK_OFFSET + 3] = (uint8_t)(callback >> 24);
	song[HMI_SONG_COMPLETION_SELECTOR_OFFSET] = (uint8_t)descriptor->completionCallbackSelector;
	song[HMI_SONG_COMPLETION_SELECTOR_OFFSET + 1] = (uint8_t)(descriptor->completionCallbackSelector >> 8);
	for (track = 0; track < state->activeTracks[songIndex]; ++track) {
		uint32_t consumed;
		state->elapsed[songIndex][track] = 0;
		state->trackHeaders[songIndex][track] = tracks + offset;
		state->cursors[songIndex][track] = tracks + offset + HMI_SONG_TRACK_HEADER_BYTES;
		consumed = HmiMusic_ReadDelta(state->cursors[songIndex][track], &state->delta[songIndex][track]);
		state->cursors[songIndex][track] += consumed;
		offset += SlipBytes_ReadLE32(tracks + offset + HMI_SONG_TRACK_SIZE_OFFSET);
	}
	for (track = 0; track < state->totalTracks[songIndex]; ++track) {
		if (state->routing[songIndex][track] == HMI_MUSIC_UNROUTED_DRIVER) {
			state->cursors[songIndex][track] = NULL;
			--state->activeTracks[songIndex];
		}
	}
	return 0;
}

uint32_t HmiMusic_ReadDelta(const uint8_t *stream, uint32_t *value) {
	uint32_t finished = 0;
	uint32_t shift = 0;
	uint32_t accumulated = 0;
	uint32_t count = 0;
	uint8_t byte;

	do {
		++count;
		byte = *stream++;
		if ((byte & HMI_MUSIC_DELTA_TERMINATOR) != 0)
			finished = 1;
		byte &= HMI_MUSIC_DELTA_DATA_MASK;
		accumulated |= (uint32_t)byte << (shift & HMI_MUSIC_X86_SHIFT_MASK);
		shift += HMI_MUSIC_DELTA_BITS;
	} while (finished == 0);
	*value = accumulated;
	return count;
}
