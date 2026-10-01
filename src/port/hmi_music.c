#include "hmi_music.h"
#include "byte_order.h"

#include "hmi_a002_data.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>

void HmiMusic_GetDriverFunctions(HmiMusicDriverTableQuery entry, void *context, uint16_t codeSelector,
                                 uint16_t dataSelector, uint8_t destination[72]) {
	const uint8_t *source = entry(context, 1, dataSelector);
	uint32_t i;
	for (i = 0; i < 12; ++i) {
		const uint32_t offset = (uint32_t)source[0] | ((uint32_t)source[1] << 8) | ((uint32_t)source[2] << 16) |
		                        ((uint32_t)source[3] << 24);
		destination[0] = (uint8_t)offset;
		destination[1] = (uint8_t)(offset >> 8);
		destination[2] = (uint8_t)(offset >> 16);
		destination[3] = (uint8_t)(offset >> 24);
		destination[4] = (uint8_t)codeSelector;
		destination[5] = (uint8_t)(codeSelector >> 8);
		destination += 6;
		source += 8;
	}
}

const char *HmiMusic_ErrorString(uint32_t code) {

	static const char *const errors[20] = {"Error Code Does Not Indicate An Error",
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
	memset(bytes, 0xff, 0x86a35 - 0x85e05);
	memset(bytes + 0x86a4d - 0x85e05, 1, 0x86b4d - 0x86a4d);
	for (i = 0x86a56; i <= 0x86b46; i += 16)
		bytes[i - 0x85e05] = 0;
	bytes[0x86a8d - 0x85e05] = 0;
	memset(bytes + 0x86a95 - 0x85e05, 0, 8);
	bytes[0x86b4d - 0x85e05] = 127;
	for (i = 0; i < 8; ++i) {
		state->timerHandles[i] = UINT32_MAX;
		bytes[0x86b4e + i * 4 - 0x85e05] = 127;
	}
	memset(bytes + 0x86b6e - 0x85e05, 127, 80);
	memcpy(bytes + 0x86bbf - 0x85e05, "HMIMIDIP013195", sizeof("HMIMIDIP013195"));
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

	const uint32_t index = song * 127u + trigger;
	if (index >= 8u * 127u)
		SlipAssertFail("Trigger write outside native callback table requires original pointer bytes", __FILE__,
		               __LINE__);
	state->triggerCallbacks[index / 127u][index % 127u] = callback;
	return 0;
}

void HmiMusic_WriteTriggerAlias(HmiMusicState *state, uint32_t offset, uint16_t selector) {

	state->fadeMode[0] = (state->fadeMode[0] & 0x00ffffffu) | ((uint32_t)(selector & 0xffu) << 24);
	state->fadeMode[1] = (state->fadeMode[1] & 0xffffff00u) | (selector >> 8);
	state->callbackPending = (uint8_t)offset;
	state->fadeMode[0] = (state->fadeMode[0] & 0xff000000u) | (offset >> 8);
}

uint32_t HmiMusic_SetTriggerPointer(HmiMusicState *state, uint32_t song, uint8_t trigger,
                                    HmiMusicTriggerCallback callback, HmiMusicFarPointer pointer) {
	const uint32_t index = song * 127u + trigger;
	if (index == 8u * 127u) {
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
	result.selector = (uint16_t)((state->fadeMode[0] >> 24) | ((state->fadeMode[1] & 0xffu) << 8));
	return result;
}

static void HmiA002_InitialData(HmiA002State *state, const uint8_t *data) {
	uint32_t i;
	memcpy(state->cache.frequency.frequencyLow, data + 0x900, 9);
	memcpy(state->cache.frequency.frequencyHigh, data + 0x909, 9);
	memcpy(state->activeNotes, data + 0x930, 9);
	memcpy(state->operatorLevels, data + 0x8d0, 32);
	memcpy(state->cache.operatorRelease, data + 0x8f0, 32);
	memcpy(state->instrumentSet, data + 0x925, 11);
	state->rhythm = data[0x924];
	state->chipEnabled = data[0x93f];
	state->mode = data[0x940];
	memcpy(state->operatorOffsets, data + 0x941, 18);
	memcpy(state->volumeCurve, data + 0x95c, 128);
	memcpy(state->deferred, data + 0x2a8, 1536);
	memcpy(state->initialProgramMessage, data + 0x266, 2);
	memcpy(state->offMessage, data + 0x256, 3);
	memcpy(state->programMessage, data + 0x259, 2);
	memcpy(state->controlMessage, data + 0x25b, 3);
	memcpy(state->pitchMessage, data + 0x25e, 2);
	for (i = 0; i < 144; ++i)
		state->pitchTables[i] = SlipBytes_ReadLE32(data + 0x9a8 + i * 4);
	for (i = 0; i < 9; ++i)
		state->voiceChannels[i] = SlipBytes_ReadLE32(data + 0x80 + i * 4);
	for (i = 0; i < 16; ++i)
		state->sustain[i] = SlipBytes_ReadLE32(data + 0x20c + i * 4);
	for (i = 0; i < 16; ++i)
		state->deferredCount[i] = SlipBytes_ReadLE32(data + 0x268 + i * 4);
	for (i = 0; i < 90; ++i)
		state->controllerGlobals[i] = SlipBytes_ReadLE32(data + 0xa4 + i * 4);
	for (i = 0; i < 16; ++i)
		state->programs[i] = SlipBytes_ReadLE32(data + 0x40 + i * 4);
	state->portBase = SlipBytes_ReadLE32(data + 0x8b8);
	state->portCopy = SlipBytes_ReadLE32(data + 0x93b);
	state->portArgument = SlipBytes_ReadLE32(data + 0x8a8);
	state->initialized = SlipBytes_ReadLE32(data + 0x0);
	memcpy(state->portInput, data + 0x8ac, 4);
	state->bankToggle = SlipBytes_ReadLE32(data + 0x8b4);
	state->pitchEnabled = SlipBytes_ReadLE32(data + 0x3c);
	for (i = 0; i < 128; ++i)
		state->noteFrequencies[i] = SlipBytes_ReadLE32(data + 0x978 + i * 4);
	state->melodicCount = (int32_t)SlipBytes_ReadLE32(data + 0x4);
	state->melodicLoaded = SlipBytes_ReadLE32(data + 0x1e);
	state->percussionLoaded = SlipBytes_ReadLE32(data + 0x34);
}

void HmiA002_ConstructStatic(HmiA002State *state) {
	*state = (HmiA002State){0};
	HmiA002_InitialData(state, a002InitialData);
}

void HmiA002_NoteOn(HmiA002State *state, uint8_t note, uint8_t velocity, uint8_t channel) {
	uint8_t voice, op, level;
	uint32_t i, scaled;
	if (channel < 16) {
		if (channel == 9) {
			voice = HmiA002_Select(state, 9);
			HmiA002_KeyOff(state, voice);
			state->voiceChannels[voice] = 9;
			for (i = 0; i < 5; ++i) {
				op = state->operatorOffsets[voice * 2 + 1];
				HmiA002_Write(state, (uint8_t)(op + 0x80), (uint8_t)(state->cache.operatorRelease[op] | 15));
				op = state->operatorOffsets[voice * 2 + 0];
				HmiA002_Write(state, (uint8_t)(op + 0x80), (uint8_t)(state->cache.operatorRelease[op] | 15));
			}
			HmiA002_Instrument(state, state->percussionData + (uint32_t)note * 30, voice);
			state->controllerGlobals[(0x274c - 0x2648) / 4 + voice] = velocity;
			scaled = ((state->controllerGlobals[(0x270c - 0x2648) / 4 + 9] << 7) / 127u *
			          state->controllerGlobals[(0x274c - 0x2648) / 4 + voice]) >>
			         7;
			if ((state->melodicData[state->programs[9] * 30 + 14] & 1) != 0) {
				op = state->operatorOffsets[voice * 2 + 0];
				level = (uint8_t)((0x2000u - (64u - state->volumeCurve[(scaled & 255) >> 1]) * 2u *
				                                 (64u - (state->operatorLevels[op] & 63))) >>
				                  7);
				HmiA002_Write(state, (uint8_t)(op + 0x40), (uint8_t)(level | (state->operatorLevels[op] & 192)));
			}
			op = state->operatorOffsets[voice * 2 + 1];
			level = (uint8_t)((0x2000u - (64u - state->volumeCurve[(scaled & 255) >> 1]) * 2u *
			                                 (64u - (state->operatorLevels[op] & 63))) >>
			                  7);
			HmiA002_Write(state, (uint8_t)(op + 0x40), (uint8_t)(level | (state->operatorLevels[op] & 192)));
			HmiA002_KeyOn(state, voice, state->noteFrequencies[state->percussionIndex[(uint32_t)note * 12 + 2]]);
			state->activeNotes[voice] = note;
		} else {
			voice = HmiA002_Select(state, channel);
			HmiA002_KeyOff(state, voice);
			state->voiceChannels[voice] = channel;
			for (i = 0; i < 5; ++i) {
				op = state->operatorOffsets[voice * 2 + 1];
				HmiA002_Write(state, (uint8_t)(op + 0x80), (uint8_t)(state->cache.operatorRelease[op] | 15));
				op = state->operatorOffsets[voice * 2 + 0];
				HmiA002_Write(state, (uint8_t)(op + 0x80), (uint8_t)(state->cache.operatorRelease[op] | 15));
			}
			HmiA002_Instrument(state, state->melodicData + state->programs[channel] * 30, voice);
			state->controllerGlobals[(0x274c - 0x2648) / 4 + voice] = velocity;
			scaled = ((state->controllerGlobals[(0x270c - 0x2648) / 4 + channel] << 7) / 127u *
			          state->controllerGlobals[(0x274c - 0x2648) / 4 + voice]) >>
			         7;
			if ((state->melodicData[state->programs[channel] * 30 + 14] & 1) != 0) {
				op = state->operatorOffsets[voice * 2 + 0];
				level = (uint8_t)((0x2000u - (64u - state->volumeCurve[(scaled & 255) >> 1]) * 2u *
				                                 (64u - (state->operatorLevels[op] & 63))) >>
				                  7);
				HmiA002_Write(state, (uint8_t)(op + 0x40), (uint8_t)(level | (state->operatorLevels[op] & 192)));
			}
			op = state->operatorOffsets[voice * 2 + 1];
			level = (uint8_t)((0x2000u - (64u - state->volumeCurve[(scaled & 255) >> 1]) * 2u *
			                                 (64u - (state->operatorLevels[op] & 63))) >>
			                  7);
			HmiA002_Write(state, (uint8_t)(op + 0x40), (uint8_t)(level | (state->operatorLevels[op] & 192)));
			HmiA002_KeyOn(state, voice, state->noteFrequencies[note]);
			state->activeNotes[voice] = note;
			if (state->pitchEnabled != 0 && state->controllerGlobals[(0x2688 - 0x2648) / 4 + channel] != 0) {
				const uint32_t frequency = HmiA002_PitchCalc(state, state->controllerGlobals[channel], note, voice);
				HmiA002_Frequency(state, voice, frequency);
			}
		}
	}
}

void HmiA002_Send(HmiA002State *state, uint32_t ignoredLeadingValue, uint32_t ignoredFollowingValue,
                  const uint8_t *message) {
	const uint8_t status = message[0] & 0xf0;
	(void)ignoredLeadingValue;
	(void)ignoredFollowingValue;
	if (status < 0xb0) {
		if (status > 0x7f) {
			if (status < 0x81) {
				state->offMessage[2] = message[0] & 15;
				state->offMessage[0] = message[1];
				state->offMessage[1] = message[2];
				HmiA002_Off(state, 0, 0, state->offMessage);
			} else if (status == 0x90) {
				if (message[2] == 0) {
					state->offMessage[2] = message[0] & 15;
					state->offMessage[0] = message[1];
					state->offMessage[1] = message[2];
					HmiA002_Off(state, 0, 0, state->offMessage);
				} else
					HmiA002_NoteOn(state, message[1], message[2], message[0] & 15);
			}
		}
	} else if (status < 0xb1) {
		state->controlMessage[0] = message[0] & 15;
		state->controlMessage[1] = message[1];
		state->controlMessage[2] = message[2];
		HmiA002_Control(state, 0, 0, state->controlMessage);
	} else if (status > 0xbf) {
		if (status < 0xc1) {
			state->programMessage[0] = message[1];
			state->programMessage[1] = message[0] & 15;
			HmiA002_Channel(state, 0, 0, state->programMessage);
		} else if (status == 0xe0) {
			state->pitchMessage[0] = message[0] & 15;
			state->pitchMessage[1] = message[2];
			HmiA002_Pitch(state, 0, 0, state->pitchMessage);
		}
	}
}

void HmiA002_Convert(uint8_t *bank) {
	uint16_t i;
	uint8_t *record;
	bank[2] = 0x48;
	record = bank + SlipBytes_ReadLE32(bank + 0x10);
	for (i = 0; (int32_t)i < (int16_t)((uint16_t)bank[8] | ((uint16_t)bank[9] << 8)) - 2; ++i) {
		record[11] =
		    (uint8_t)((record[11] << 7) | (record[12] << 6) | (record[7] << 5) | (record[13] << 4) | record[3]);
		record[2] = (uint8_t)((record[2] << 6) | record[10]);
		record[5] = (uint8_t)((record[5] << 4) | record[8]);
		record[6] = (uint8_t)((record[6] << 4) | record[9]);
		record[14] = (uint8_t)((record[4] << 1) | record[14]);
		record[24] =
		    (uint8_t)((record[24] << 7) | (record[25] << 6) | (record[20] << 5) | (record[26] << 4) | record[16]);
		record[15] = (uint8_t)((record[15] << 6) | record[23]);
		record[18] = (uint8_t)((record[18] << 4) | record[21]);
		record[19] = (uint8_t)((record[19] << 4) | record[22]);
		record += 0x1e;
	}
}

void HmiA002_Channel(HmiA002State *state, uint32_t ignoredLeadingValue, uint32_t ignoredFollowingValue,
                     const uint8_t *message) {
	(void)ignoredLeadingValue;
	(void)ignoredFollowingValue;
	state->programs[message[1]] = message[0];
}

uint32_t HmiA002_InitEntry(HmiA002State *state, uint16_t port) {
	uint32_t result;
	/* The original entry reads a word and zero-extends it for +0x749.
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

	assert(driver < 5);
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
		return 0xf000;

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
	HmiA002_Write(state, 1, 0x20);
	state->rhythm = 0;
	HmiA002_ResetChip(state);
	state->chipEnabled = 1;
	return 0;
}

uint8_t HmiA002_ResetChip(HmiA002State *state) {
	uint32_t i;
	for (i = 0; i < 9; ++i) {
		state->cache.frequency.frequencyHigh[i] = 0;
		HmiA002_Write(state, (uint8_t)(i + 0xb0), state->cache.frequency.frequencyHigh[i]);
	}
	for (i = 0; i < 11; ++i)
		state->instrumentSet[i] = 0;
	state->mode = 0;
	state->rhythm &= 0xc0;
	state->rhythm |= 0xc0;
	HmiA002_Write(state, 0xbd, state->rhythm);
	return 0;
}

uint8_t HmiA002_ClearChip(HmiA002State *state) {
	uint8_t i;
	if (state->chipEnabled == 0)
		return 2;
	HmiA002_Write(state, 0xbd, state->rhythm);
	for (i = 0; i < 9; ++i)
		HmiA002_Write(state, (uint8_t)(i + 0xb0), (uint8_t)(state->cache.frequency.frequencyHigh[i] & 0xdf));
	for (i = 0; i < 9; ++i)
		HmiA002_Write(state, (uint8_t)(state->operatorOffsets[i * 2 + 1] + 0x40), 0xff);
	for (i = 0; i < 11; ++i)
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
		return 6;
	state->cache.frequency.frequencyHigh[voice] &= 0xdf;
	HmiA002_Write(state, (uint8_t)(voice + 0xb0), state->cache.frequency.frequencyHigh[voice]);
	state->activeNotes[voice] = 0;
	return 0;
}

uint8_t HmiA002_Select(HmiA002State *state, uint8_t channel) {
	uint32_t i, voice;
	for (i = 0; i < 9; ++i)
		if (state->activeNotes[i] == 0)
			return (uint8_t)i;
	for (i = 0; i < 16; ++i) {
		if (state->controllerGlobals[(0x2688 - 0x2648) / 4 + i] == 0) {
			for (voice = 0; voice < 9; ++voice)
				if (state->voiceChannels[voice] == i)
					return (uint8_t)voice;
		}
	}
	if (channel > 8)
		channel = (uint8_t)(channel - 9);
	return channel;
}

uint8_t HmiA002_KeyOn(HmiA002State *state, uint8_t voice, uint32_t frequency) {
	state->cache.frequency.frequencyLow[voice] = (uint8_t)frequency;
	state->cache.frequency.frequencyHigh[voice] = (uint8_t)((frequency >> 8) | 0x20);
	HmiA002_Write(state, (uint8_t)(voice + 0xa0), state->cache.frequency.frequencyLow[voice]);
	HmiA002_Write(state, (uint8_t)(voice + 0xb0), (uint8_t)(state->cache.frequency.frequencyHigh[voice] & 0xdf));
	HmiA002_Write(state, (uint8_t)(voice + 0xb0), state->cache.frequency.frequencyHigh[voice]);
	state->activeNotes[voice] = 1;
	return 0;
}

uint8_t HmiA002_Instrument(HmiA002State *state, const uint8_t *record, uint8_t voice) {
	uint8_t op = state->operatorOffsets[voice * 2];
	uint8_t characteristic1 = record[11], level1 = record[2], attack1 = record[5];
	uint8_t release1 = record[6], feedback = record[14], waveform1 = record[28];
	uint8_t characteristic2 = record[24], level2 = record[15], attack2 = record[18];
	uint8_t release2 = record[19], waveform2 = record[29];
	state->cache.operatorRelease[state->operatorOffsets[voice * 2 + 1]] = release2;
	state->cache.operatorRelease[state->operatorOffsets[voice * 2]] = release1;
	HmiA002_Write(state, (uint8_t)(op + 0x20), characteristic1);
	HmiA002_Write(state, (uint8_t)(op + 0x40), level1);
	state->operatorLevels[op] = level1;
	HmiA002_Write(state, (uint8_t)(op + 0x60), attack1);
	HmiA002_Write(state, (uint8_t)(op + 0x80), release1);
	HmiA002_Write(state, (uint8_t)(voice - 0x40), feedback);
	HmiA002_Write(state, (uint8_t)(op - 0x20), waveform1);
	op = state->operatorOffsets[voice * 2 + 1];
	HmiA002_Write(state, (uint8_t)(op + 0x20), characteristic2);
	state->operatorLevels[op] = level2;
	HmiA002_Write(state, (uint8_t)(op + 0x60), attack2);
	HmiA002_Write(state, (uint8_t)(op + 0x80), release2);
	HmiA002_Write(state, (uint8_t)(op - 0x20), waveform2);
	state->instrumentSet[voice] = 1;
	return 0;
}

void HmiA002_Off(HmiA002State *state, uint32_t ignoredLeadingValue, uint32_t ignoredFollowingValue,
                 const uint8_t *message) {
	uint32_t voice;
	(void)ignoredLeadingValue;
	(void)ignoredFollowingValue;
	/* Preserve the redundant original channel predicate. */
	if (message[2] < 16 && (message[2] < 16 || message[2] == 9)) {
		if (state->sustain[message[2]] == 0 || state->deferredCount[message[2]] > 31) {
			for (voice = 0; voice < 9; ++voice) {
				if (state->activeNotes[voice] == message[0] && state->voiceChannels[voice] == message[2]) {
					HmiA002_KeyOff(state, (uint8_t)voice);
					state->activeNotes[voice] = 0;
				}
			}
		} else {
			state->deferred[message[2]][state->deferredCount[message[2]]][0] = message[0];
			state->deferred[message[2]][state->deferredCount[message[2]]][1] = message[1];
			state->deferred[message[2]][state->deferredCount[message[2]]][2] = message[2];
			++state->deferredCount[message[2]];
		}
	}
}

void HmiA002_AllOff(HmiA002State *state, uint32_t channel) {
	uint32_t voice;
	if (channel < 16 || (channel == 9 && state->percussionLoaded != 0)) {
		for (voice = 0; voice < 9; ++voice) {
			if (state->voiceChannels[voice] == channel) {
				HmiA002_KeyOff(state, (uint8_t)voice);
				state->activeNotes[voice] = 0;
			}
		}
	}
}

void HmiA002_Reset(HmiA002State *state, uint32_t channel) {
	if (channel < 16 || (channel == 9 && state->percussionLoaded != 0)) {
		state->controllerGlobals[(0x270c - 0x2648) / 4 + channel] = 127;
		state->controllerGlobals[(0x274c - 0x2648) / 4 + channel] = 127;
		state->controllerGlobals[(0x2770 - 0x2648) / 4 + channel] = 0;
		state->sustain[channel] = 0;
		state->controllerGlobals[channel] = 64;
		state->controllerGlobals[(0x26c8 - 0x2648) / 4 + channel] = 2;
	}
}

void HmiA002_Volume(HmiA002State *state, uint32_t channel, uint8_t volume) {
	uint32_t voice;
	if (channel < 16) {
		state->controllerGlobals[(0x270c - 0x2648) / 4 + channel] = volume;
		state->controllerGlobals[(0x2770 - 0x2648) / 4 + channel] = 1;
		for (voice = 0; voice < 9; ++voice) {
			if (state->activeNotes[voice] != 0 && state->voiceChannels[voice] == channel) {
				const uint32_t scaled =
				    (((uint32_t)volume << 7) / 127u * state->controllerGlobals[(0x274c - 0x2648) / 4 + voice]) >> 7;
				uint32_t attenuation = 64u - state->volumeCurve[(scaled & 255u) >> 1];
				uint8_t op;
				if ((state->melodicData[state->programs[state->voiceChannels[voice]] * 30u + 14] & 1) != 0) {
					op = state->operatorOffsets[voice * 2];
					HmiA002_Write(
					    state, (uint8_t)(op + 0x40),
					    (uint8_t)(((0x2000u - attenuation * 2u * (64u - (state->operatorLevels[op] & 63u))) >> 7) |
					              (state->operatorLevels[op] & 192u)));
				}
				op = state->operatorOffsets[voice * 2 + 1];
				attenuation = 64u - state->volumeCurve[(scaled & 255u) >> 1];
				HmiA002_Write(
				    state, (uint8_t)(op + 0x40),
				    (uint8_t)(((0x2000u - attenuation * 2u * (64u - (state->operatorLevels[op] & 63u))) >> 7) |
				              (state->operatorLevels[op] & 192u)));
			}
		}
	}
}

void HmiA002_Control(HmiA002State *state, uint32_t ignoredLeadingValue, uint32_t ignoredFollowingValue,
                     const uint8_t *message) {
	const uint8_t controller = message[1];
	(void)ignoredLeadingValue;
	(void)ignoredFollowingValue;
	if (controller < 0x66) {
		if (controller > 6) {
			if (controller < 8) {
				HmiA002_Volume(state, message[0], message[2]);
			} else if (controller == 0x40) {
				state->sustain[message[0]] = message[2];
				if (message[2] == 0) {
					while (state->deferredCount[message[0]] != 0) {
						--state->deferredCount[message[0]];
						HmiA002_Off(state, 0, 0, state->deferred[message[0]][state->deferredCount[message[0]]]);
					}
				}
			}
		}
	} else if (controller < 0x67) {
		state->controllerGlobals[(0x26c8 - 0x2648) / 4 + message[0]] = message[2];
	} else if (controller > 0x78) {
		if (controller < 0x7a)
			HmiA002_Reset(state, message[0]);
		else if (controller == 0x7b)
			HmiA002_AllOff(state, message[0]);
	}
}

uint32_t HmiA002_PitchCalc(HmiA002State *state, uint32_t bend, uint32_t note, uint32_t voice) {
	uint32_t index = note - 12u, remainder, frequency, difference;
	const uint32_t range = state->controllerGlobals[(0x26c8 - 0x2648) / 4 + state->voiceChannels[voice]];
	for (remainder = index; remainder > 11; remainder -= 12) {
	}
	frequency = state->pitchTables[index];
	if (bend < 64) {
		difference = frequency - state->pitchTables[index - range];
		if (difference > 0x2cf)
			difference = ((frequency & 0x3ff) - state->pitchTables[(0x30e4 - 0x2f4c) / 4 + range]) & 0x3ff;
		frequency -= (difference * (((63u - bend) * 1000u) >> 6)) / 1000u;
	} else {
		difference = state->pitchTables[index + range] - frequency;
		if (difference > 0x2cf) {
			frequency = ((frequency & 0x1c00) + 0x400) | state->pitchTables[(0x30e8 - 0x2f4c) / 4 + 11 - remainder];
			difference = state->pitchTables[index + range] - frequency;
		}
		frequency += (difference * (((bend - 64u) * 1000u) >> 6)) / 1000u;
	}
	return frequency;
}

void HmiA002_Pitch(HmiA002State *state, uint32_t ignoredLeadingValue, uint32_t ignoredFollowingValue,
                   const uint8_t *message) {
	uint32_t voice;
	(void)ignoredLeadingValue;
	(void)ignoredFollowingValue;
	if (message[0] < 16 && message[0] != 9) {
		state->controllerGlobals[message[0]] = message[1];
		state->controllerGlobals[(0x2688 - 0x2648) / 4 + message[0]] = 1;
		for (voice = 0; voice < 9; ++voice) {
			if (state->activeNotes[voice] != 0 && state->voiceChannels[voice] == message[0]) {
				const uint32_t frequency = HmiA002_PitchCalc(state, message[1], state->activeNotes[voice], voice);
				HmiA002_Frequency(state, (uint8_t)voice, frequency);
			}
		}
	}
}

uint8_t HmiA002_Frequency(HmiA002State *state, uint8_t voice, uint32_t frequency) {
	state->cache.frequency.frequencyLow[voice] = (uint8_t)frequency;
	state->cache.frequency.frequencyHigh[voice] = (uint8_t)((frequency >> 8) | 0x20);
	HmiA002_Write(state, (uint8_t)(voice + 0xa0), state->cache.frequency.frequencyLow[voice]);
	HmiA002_Write(state, (uint8_t)(voice + 0xb0), state->cache.frequency.frequencyHigh[voice]);
	return 0;
}

uint32_t HmiA002_Bank(HmiA002State *state, uint32_t ignoredLeadingValue, uint32_t ignoredFollowingValue,
                      uint8_t *bank) {
	uint8_t channel;
	(void)ignoredLeadingValue;
	(void)ignoredFollowingValue;
	if (bank[2] != 'H')
		HmiA002_Convert(bank);
	if (state->bankToggle == 0) {
		state->bankToggle = 1;
		state->melodicBank = bank;
		state->melodicCount = (int16_t)((uint16_t)bank[8] | ((uint16_t)bank[9] << 8));
		state->melodicIndex = bank + SlipBytes_ReadLE32(bank + 12);
		state->melodicData = bank + SlipBytes_ReadLE32(bank + 16);
		state->melodicLoaded = 1;
		for (channel = 0; channel < 9; ++channel) {
			state->initialProgramMessage[0] = 0;
			state->initialProgramMessage[1] = channel;
			state->message = state->initialProgramMessage;
			HmiA002_Channel(state, channel, 1000, state->initialProgramMessage);
		}
	} else {
		state->bankToggle = 0;
		state->percussionBank = bank;
		state->percussionIndex = bank + SlipBytes_ReadLE32(bank + 12);
		state->percussionData = bank + SlipBytes_ReadLE32(bank + 16);
		state->percussionLoaded = 1;
	}
	return 0;
}

uint32_t HmiMusic_IsStopped(const HmiMusicState *state, uint32_t song) { return state->playing[song] == 0 ? 1u : 0u; }

static uint32_t HmiMusic_Abs(uint32_t value) { return (value & 0x80000000u) != 0 ? 0u - value : value; }

uint8_t *HmiMusic_Instrument(HmiMusicState *state, uint32_t instrument, uint8_t note) {
	uint8_t *best, *candidate;
	uint32_t distance;
	if (state->instrumentBankEnabled == 0 || state->instrumentPointers[instrument] == NULL)
		return NULL;
	best = state->instrumentPointers[instrument];
	distance = HmiMusic_Abs(SlipBytes_ReadLE32(best + 0x24) - note);
	/* Records have 0x74-byte headers followed by their +0x2c data length.
	 * The zero-length sentinel is tested before comparing its base note. */
	for (candidate = best + 0x74 + SlipBytes_ReadLE32(best + 0x2c); SlipBytes_ReadLE32(candidate + 0x2c) != 0;
	     candidate += 0x74 + SlipBytes_ReadLE32(candidate + 0x2c)) {
		if (HmiMusic_Abs(SlipBytes_ReadLE32(candidate + 0x24) - note) < distance) {
			distance = HmiMusic_Abs(SlipBytes_ReadLE32(candidate + 0x24) - note);
			best = candidate;
		}
	}
	return best;
}

uint32_t HmiMusic_Unregister(HmiMusicState *state, uint32_t song) {
	if (song < 8) {
		state->songs[song] = NULL;
	} else {
		return 10;
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
			return 14;
	for (slot = 0; slot < 8; ++slot)
		if (state->songs[slot] == NULL)
			break;
	if (slot == 8)
		return 11;
	for (i = 0; i < 32; ++i)
		state->cursors[slot][i] = NULL;
	state->songs[slot] = song;
	for (i = 0; i < 127; ++i)
		state->triggerCallbacks[slot][i] = NULL;
	state->branchCallbacks[slot] = NULL;
	state->loopCallbacks[slot] = NULL;
	counts = song + SlipBytes_ReadLE32(song + 0x20);
	state->routing[slot] = routing;
	state->activeTracks[slot] = SlipBytes_ReadLE32(song + 0x30);
	state->totalTracks[slot] = state->activeTracks[slot];
	for (i = 0; i < 4; ++i)
		song[0x380 + i] = (uint8_t)(descriptor->completionCallbackOffset >> (i * 8));
	song[0x384] = (uint8_t)descriptor->completionCallbackSelector;
	song[0x385] = (uint8_t)(descriptor->completionCallbackSelector >> 8);
	state->completionCallbacks[slot] = completion;
	for (track = 0; track < state->activeTracks[slot]; ++track) {
		const uint8_t *const header = song + 0x388 + offset;
		state->elapsed[slot][track] = 0;
		state->trackHeaders[slot][track] = header;
		state->cursors[slot][track] = header + 12;
		state->cursors[slot][track] += HmiMusic_ReadDelta(header + 12, &state->delta[slot][track]);
		offset += SlipBytes_ReadLE32(header + 4);
	}
	records = counts + state->activeTracks[slot];
	for (track = 0; track < state->activeTracks[slot]; ++track) {
		if (*counts != 0)
			state->branches[slot][track] = branchStorage;
		for (i = 0; i < *counts; ++i) {
			branchStorage[i].dosBytes = records + i * 24;
			branchStorage[i].controllerPairs = song + SlipBytes_ReadLE32(records + i * 24 + 12);
		}
		records += *counts * 24;

		if (*counts != 0)
			branchStorage += *counts;
		++counts;
	}
	for (track = 0; track < state->totalTracks[slot]; ++track) {
		if (routing[track] == 0xff) {
			uint32_t candidate, found = 0;
			const uint8_t *const desired = song + 0x80 + track * 20;
			for (candidate = 0; SlipBytes_ReadLE32(desired + candidate * 4) != 0 && found == 0 && candidate < 5;
			     ++candidate) {
				for (i = 0; i < 5; ++i) {
					const uint32_t want = SlipBytes_ReadLE32(desired + candidate * 4);
					const uint32_t have = state->driverIds[i];
					if ((want == 0xa000 && (have == 0xa000 || have == 0xa001 || have == 0xa008)) ||
					    (want == 0xa002 && (have == 0xa002 || have == 0xa009)) ||
					    (want != 0xa000 && want != 0xa002 && have == want)) {
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
				routing[track] = 0xff;
				--state->activeTracks[slot];
			}
		}
	}
	for (i = 0; i < 128; ++i)
		song[0x300 + i] = 1;
	*songIndex = slot;
	return 0;
}

uint32_t HmiMusic_Fade(HmiMusicState *state, HmiTimerState *timer, uint32_t song, uint32_t mode, uint32_t duration,
                       uint8_t from, uint8_t to, uint32_t divisor) {
	const uint32_t difference = (mode & 1u) != 0 ? (uint32_t)to - from : (uint32_t)from - to;
	const uint32_t rate = HmiTimer_GetRate(timer, state->timerHandles[song]);
	const uint32_t interval = 0x640000u / rate;
	const uint32_t steps = ((duration << 16) / interval) / divisor;
	if (steps == 0) {
		if ((mode & 4u) != 0)
			HmiMusic_StopSong(state, timer, song);
		else
			HmiMusic_SetSongVolume(state, song, to);
		return 0;
	}
	HmiMusic_SetSongVolume(state, song, from);
	state->fadeMode[song] = mode;
	state->fadeStep[song] = (difference << 16) / steps;
	state->fadeVolume[song] = (uint32_t)from << 16;
	state->fadeRemaining[song] = steps;
	return 0;
}

uint32_t HmiMusic_StopSong(HmiMusicState *state, HmiTimerState *timer, uint32_t song) {
	uint32_t handle;
	if (song >= 8)
		return 10;
	handle = state->timerHandles[song];
	if (handle != UINT32_MAX)
		HmiTimer_Remove(timer, handle);

	if (handle == UINT32_MAX)
		timer->accumulators[15] |= 0xff000000u;
	else
		timer->songForSlot[handle] = 0xff;
	state->timerHandles[song] = UINT32_MAX;
	if (state->playing[song] != 0) {
		HmiMusicSongDescriptor descriptor;
		descriptor.songData = state->songs[song];
		descriptor.completionCallbackOffset = SlipBytes_ReadLE32(descriptor.songData + 0x380);
		descriptor.completionCallbackSelector =
		    (uint16_t)((uint16_t)descriptor.songData[0x384] | ((uint16_t)descriptor.songData[0x385] << 8));
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
	scale = (percent << 16) / 100u;
	rate = (scale * SlipBytes_ReadLE32(state->songs[song] + 0x38u)) >> 16;
	if (rate == 0)
		rate = 1;
	HmiTimer_SetRate(timer, state->timerHandles[song], rate);
	return HmiTimer_GetRate(timer, state->timerHandles[song]);
}

uint32_t HmiMusic_ResetSong(HmiMusicState *state, uint32_t songIndex, const HmiMusicSongDescriptor *descriptor) {
	uint32_t offset = 0;
	uint32_t track;
	uint8_t *const song = descriptor->songData;
	const uint8_t *const tracks = song + 0x388u;
	const uint32_t callback = descriptor->completionCallbackOffset;
	state->songs[songIndex] = song;
	state->activeTracks[songIndex] = SlipBytes_ReadLE32(song + 0x30u);
	state->totalTracks[songIndex] = state->activeTracks[songIndex];
	song[0x380] = (uint8_t)callback;
	song[0x381] = (uint8_t)(callback >> 8);
	song[0x382] = (uint8_t)(callback >> 16);
	song[0x383] = (uint8_t)(callback >> 24);
	song[0x384] = (uint8_t)descriptor->completionCallbackSelector;
	song[0x385] = (uint8_t)(descriptor->completionCallbackSelector >> 8);
	for (track = 0; track < state->activeTracks[songIndex]; ++track) {
		uint32_t consumed;
		state->elapsed[songIndex][track] = 0;
		state->trackHeaders[songIndex][track] = tracks + offset;
		state->cursors[songIndex][track] = tracks + offset + 12u;
		consumed = HmiMusic_ReadDelta(state->cursors[songIndex][track], &state->delta[songIndex][track]);
		state->cursors[songIndex][track] += consumed;
		offset += SlipBytes_ReadLE32(tracks + offset + 4u);
	}
	for (track = 0; track < state->totalTracks[songIndex]; ++track) {
		if (state->routing[songIndex][track] == 0xffu) {
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
		if ((byte & 0x80u) != 0)
			finished = 1;
		byte &= 0x7fu;
		accumulated |= (uint32_t)byte << (shift & 31u);
		shift += 7u;
	} while (finished == 0);
	*value = accumulated;
	return count;
}
