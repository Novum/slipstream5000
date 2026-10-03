#include "byte_order.h"
#include "hmi_music.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

static const uint8_t channelLengths[HMI_MIDI_STATUS_NIBBLE_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 2, 2, 3, 0};
static const uint8_t systemLengths[HMI_MIDI_STATUS_NIBBLE_COUNT] = {0, 1, 2, 1, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2};

static void HmiMusic_TimerTick(HmiTimerState *timer) { HmiMusic_Tick(timer->musicState, timer); }

uint32_t HmiMusic_StartSong(HmiMusicState *state, HmiTimerState *timer, uint32_t song) {
	uint32_t result;
	timer->musicState = state;
	result = HmiTimer_Register(timer, SlipBytes_ReadLE32(state->songs[song] + HMI_SONG_TEMPO_OFFSET),
	                           HmiMusic_TimerTick, &state->timerHandles[song]);
	if (result != 0)
		return result;
	timer->songForSlot[state->timerHandles[song]] = (uint8_t)(song & UINT8_MAX);
	state->playing[song] = 1;
	return 0;
}

uint32_t HmiMusic_BranchSong(HmiMusicState *state, uint32_t song, uint8_t branchId) {
	uint8_t track;
	uint32_t branch;
	branchId |= HMI_SONG_BRANCH_EXPLICIT_ID;
	for (track = HMI_SONG_FIRST_MUSIC_TRACK; track < state->totalTracks[song]; ++track) {
		if (state->cursors[song][track] != NULL) {
			branch = 0;
			/* Original requires a matching record; no count or fallback search. */
			while (state->branches[song][track][branch].dosBytes[HMI_SONG_BRANCH_ID_OFFSET] != branchId)
				++branch;
			state->cursors[song][track] = state->trackHeaders[song][track] +
			                              (uint32_t)(SlipBytes_ReadLE32(state->branches[song][track][branch].dosBytes) +
			                                         HMI_SONG_TRACK_HEADER_BYTES);
			state->cursors[song][track] += HmiMusic_ReadDelta(state->cursors[song][track], &state->delta[song][track]);
			state->elapsed[song][track] = 0;
			HmiMusic_RestoreBranch(state, song, track, branch);
		}
	}
	state->callbackPending = 0;
	return 0;
}

#define CURRENT_SONG timer->currentSong
#define TRACK_CURSOR state->cursors[CURRENT_SONG][track]
#define BRANCH_BYTES state->branches[CURRENT_SONG][track][branch].dosBytes

static void HmiMusic_CompleteSong(HmiMusicState *state, HmiTimerState *timer) {
	/* Read completion bytes after driver cleanup, as in the original completion path. */
	HmiMusicSongDescriptor descriptor;
	void (*callback)(HmiMusicState *);
	state->playing[CURRENT_SONG] = 0;
	HmiMusic_Cleanup(state, CURRENT_SONG);
	if (state->timerHandles[CURRENT_SONG] != UINT32_MAX)
		HmiTimer_Remove(timer, state->timerHandles[CURRENT_SONG]);
	if (state->timerHandles[CURRENT_SONG] == UINT32_MAX)
		timer->accumulators[HMI_MUSIC_TIMER_LAST_SLOT] |= HMI_MUSIC_TIMER_ALIAS_MASK;
	else
		timer->songForSlot[state->timerHandles[CURRENT_SONG]] = HMI_MUSIC_TIMER_NO_SONG;
	state->timerHandles[CURRENT_SONG] = UINT32_MAX;
	descriptor.songData = state->songs[CURRENT_SONG];
	descriptor.completionCallbackOffset = SlipBytes_ReadLE32(descriptor.songData + HMI_SONG_COMPLETION_CALLBACK_OFFSET);
	descriptor.completionCallbackSelector =
	    SlipBytes_ReadLE16(descriptor.songData + HMI_SONG_COMPLETION_SELECTOR_OFFSET);
	callback = state->completionCallbacks[CURRENT_SONG];
	state->songs[CURRENT_SONG] = NULL;
	HmiMusic_ResetSong(state, CURRENT_SONG, &descriptor);
	if (callback != NULL)
		callback(state);
}

void HmiMusic_Tick(HmiMusicState *state, HmiTimerState *timer) {
	uint8_t track;
	uint32_t length, readDelta = 1, branch, id, remaining, other;
	uint32_t mode;
	if (state->playing[CURRENT_SONG] == 0 || state->paused[CURRENT_SONG] != 0)
		return;
	if (state->fadeRemaining[CURRENT_SONG] != 0 && state->fadeCountdown[CURRENT_SONG]-- == 0) {
		state->fadeCountdown[CURRENT_SONG] = HMI_MUSIC_FADE_TICK_INTERVAL - 1;
		--state->fadeRemaining[CURRENT_SONG];
		mode = state->fadeMode[CURRENT_SONG];
		if (mode == HMI_MUSIC_FADE_DECREASE || mode == HMI_MUSIC_FADE_STOP) {
			state->fadeVolume[CURRENT_SONG] -= state->fadeStep[CURRENT_SONG];
			HmiMusic_SetSongVolume(state, CURRENT_SONG,
			                       (uint8_t)((state->fadeVolume[CURRENT_SONG] >> HMI_MUSIC_FRACTION_BITS) & UINT8_MAX));
			if ((state->fadeMode[CURRENT_SONG] & HMI_MUSIC_FADE_STOP) != 0 && state->fadeRemaining[CURRENT_SONG] == 0) {
				HmiMusic_CompleteSong(state, timer);
				return;
			}
		} else if (mode == HMI_MUSIC_FADE_INCREASE) {
			state->fadeVolume[CURRENT_SONG] += state->fadeStep[CURRENT_SONG];
			HmiMusic_SetSongVolume(state, CURRENT_SONG,
			                       (uint8_t)((state->fadeVolume[CURRENT_SONG] >> HMI_MUSIC_FRACTION_BITS) & UINT8_MAX));
		}
	}
	for (track = 0; track < state->totalTracks[CURRENT_SONG]; ++track) {
		++state->elapsed[CURRENT_SONG][track];
		if (TRACK_CURSOR == NULL || state->delta[CURRENT_SONG][track] > state->elapsed[CURRENT_SONG][track])
			continue;
		for (;;) {
			bool dispatchEvent = false;
			state->callbackPending = 0;
			readDelta = 1;
			length = *TRACK_CURSOR < HMI_MIDI_SYSTEM_STATUS
			             ? channelLengths[*TRACK_CURSOR >> HMI_MIDI_STATUS_NIBBLE_SHIFT]
			             : systemLengths[*TRACK_CURSOR & HMI_MIDI_CHANNEL_MASK];
			if (*TRACK_CURSOR == HMI_MIDI_META_STATUS) {
				if (TRACK_CURSOR[1] == HMI_MIDI_META_END_TRACK) {
					TRACK_CURSOR = NULL;
					if (state->activeTracks[CURRENT_SONG] == HMI_SONG_FINAL_TRACK_PAIR &&
					    state->cursors[CURRENT_SONG][HMI_SONG_CONTROL_TRACK] != NULL) {
						--state->activeTracks[CURRENT_SONG];
						state->cursors[CURRENT_SONG][HMI_SONG_CONTROL_TRACK] = NULL;
					}
					if (--state->activeTracks[CURRENT_SONG] == 0) {
						HmiMusic_CompleteSong(state, timer);
						return;
					}
					length = HMI_MIDI_META_END_TRACK_BYTES;
				} else if (TRACK_CURSOR[1] == HMI_MIDI_META_TEMPO) {
					length = HMI_MIDI_META_TEMPO_BYTES;
				}
			} else if ((*TRACK_CURSOR & HMI_MIDI_STATUS_MASK) == HMI_MIDI_CONTROL_CHANGE) {
				switch (TRACK_CURSOR[1]) {
				case HMI_SONG_CONTROL_DISABLE_CONTROLLER_RESTORE:
					state->songs[CURRENT_SONG][HMI_SONG_CONTROLLER_RESTORE_FLAGS_OFFSET + TRACK_CURSOR[2]] = 0;
					break;
				case HMI_SONG_CONTROL_ENABLE_CONTROLLER_RESTORE:
					state->songs[CURRENT_SONG][HMI_SONG_CONTROLLER_RESTORE_FLAGS_OFFSET + TRACK_CURSOR[2]] = 1;
					break;
				case HMI_SONG_CONTROL_IGNORED_108:
				case HMI_SONG_CONTROL_IGNORED_110:
				case HMI_SONG_CONTROL_IGNORED_113:
				case HMI_SONG_CONTROL_IGNORED_116:
				case HMI_SONG_CONTROL_IGNORED_120:
					break;
				case HMI_SONG_CONTROL_SET_LOOP_COUNT_A:
				case HMI_SONG_CONTROL_SET_LOOP_COUNT_B:
					id = TRACK_CURSOR[2];
					for (branch = 0; BRANCH_BYTES[HMI_SONG_BRANCH_ID_OFFSET] != id; ++branch) {
					}
					BRANCH_BYTES[HMI_SONG_BRANCH_REMAINING_OFFSET] = TRACK_CURSOR[HMI_SONG_LOOP_COUNT_EVENT_OFFSET];
					break;
				case HMI_SONG_CONTROL_LOOP_ALL_TRACKS_A:
				case HMI_SONG_CONTROL_LOOP_ALL_TRACKS_B:
					id = TRACK_CURSOR[2];
					for (branch = 0; BRANCH_BYTES[HMI_SONG_BRANCH_ID_OFFSET] != id; ++branch) {
					}
					remaining = BRANCH_BYTES[HMI_SONG_BRANCH_REMAINING_OFFSET];
					if (remaining != HMI_SONG_LOOP_FOREVER && remaining != 0) {
						--BRANCH_BYTES[HMI_SONG_BRANCH_REMAINING_OFFSET];
						--remaining;
					}
					if (state->loopCallbacks[CURRENT_SONG] != NULL) {
						state->callbackPending = 1;
						if (state->loopCallbacks[CURRENT_SONG](state, CURRENT_SONG, track, (uint8_t)id,
						                                       (uint8_t)remaining) == 0)
							remaining = 0;
						if (state->callbackPending == 0) {
							readDelta = 0;
							length = 0;
						} else
							state->callbackPending = 0;
					}
					if (remaining != 0) {
						for (other = HMI_SONG_FIRST_MUSIC_TRACK; other < state->totalTracks[CURRENT_SONG]; ++other) {
							if (state->cursors[CURRENT_SONG][other] != NULL) {
								for (branch = 0;
								     state->branches[CURRENT_SONG][other][branch].dosBytes[HMI_SONG_BRANCH_ID_OFFSET] !=
								     id;
								     ++branch) {
								}
								state->cursors[CURRENT_SONG][other] =
								    state->trackHeaders[CURRENT_SONG][other] +
								    (uint32_t)(SlipBytes_ReadLE32(
								                   state->branches[CURRENT_SONG][other][branch].dosBytes) +
								               HMI_SONG_TRACK_HEADER_BYTES);
								length = HmiMusic_ReadDelta(state->cursors[CURRENT_SONG][other],
								                            &state->delta[CURRENT_SONG][other]);
								state->cursors[CURRENT_SONG][other] += length;
								state->elapsed[CURRENT_SONG][other] = 0;
								readDelta = 0;
								HmiMusic_RestoreBranch(state, CURRENT_SONG, other, branch);
							}
						}
						length = 0;
					}
					break;
				case HMI_SONG_CONTROL_BRANCH_ALL_TRACKS:
					id = TRACK_CURSOR[2] | HMI_SONG_BRANCH_EXPLICIT_ID;
					for (branch = 0; BRANCH_BYTES[HMI_SONG_BRANCH_ID_OFFSET] != id; ++branch) {
					}
					remaining = 1;
					if (state->branchCallbacks[CURRENT_SONG] != NULL) {
						state->callbackPending = 1;
						if (state->branchCallbacks[CURRENT_SONG](state, CURRENT_SONG, track, (uint8_t)id) == 0)
							remaining = 0;
						if (state->callbackPending == 0) {
							readDelta = 0;
							length = 0;
						} else
							state->callbackPending = 0;
					}
					if (remaining != 0) {
						for (other = HMI_SONG_FIRST_MUSIC_TRACK; other < state->totalTracks[CURRENT_SONG]; ++other) {
							if (state->cursors[CURRENT_SONG][other] != NULL) {
								for (branch = 0;
								     state->branches[CURRENT_SONG][other][branch].dosBytes[HMI_SONG_BRANCH_ID_OFFSET] !=
								     id;
								     ++branch) {
								}
								state->cursors[CURRENT_SONG][other] =
								    state->trackHeaders[CURRENT_SONG][other] +
								    (uint32_t)(SlipBytes_ReadLE32(
								                   state->branches[CURRENT_SONG][other][branch].dosBytes) +
								               HMI_SONG_TRACK_HEADER_BYTES);
								length = HmiMusic_ReadDelta(state->cursors[CURRENT_SONG][other],
								                            &state->delta[CURRENT_SONG][other]);
								state->cursors[CURRENT_SONG][other] += length;
								state->elapsed[CURRENT_SONG][other] = 0;
								readDelta = 0;
								HmiMusic_RestoreBranch(state, CURRENT_SONG, other, branch);
							}
						}
						length = 0;
					}
					break;
				case HMI_SONG_CONTROL_LOOP_TRACK_A:
				case HMI_SONG_CONTROL_LOOP_TRACK_B:
					id = TRACK_CURSOR[2];
					for (branch = 0; BRANCH_BYTES[HMI_SONG_BRANCH_ID_OFFSET] != id; ++branch) {
					}
					remaining = BRANCH_BYTES[HMI_SONG_BRANCH_REMAINING_OFFSET];
					if (remaining != HMI_SONG_LOOP_FOREVER && remaining != 0) {
						--BRANCH_BYTES[HMI_SONG_BRANCH_REMAINING_OFFSET];
						--remaining;
					}
					if (state->loopCallbacks[CURRENT_SONG] != NULL) {
						state->callbackPending = 1;
						if (state->loopCallbacks[CURRENT_SONG](state, CURRENT_SONG, track, (uint8_t)id,
						                                       (uint8_t)remaining) == 0)
							remaining = 0;
						if (state->callbackPending == 0)
							length = 0;
						else
							state->callbackPending = 0;
					}
					if (remaining != 0) {
						TRACK_CURSOR = state->trackHeaders[CURRENT_SONG][track] +
						               (uint32_t)(SlipBytes_ReadLE32(BRANCH_BYTES) + HMI_SONG_TRACK_HEADER_BYTES);
						HmiMusic_RestoreBranch(state, CURRENT_SONG, track, branch);
						length = 0;
					}
					break;
				case HMI_SONG_CONTROL_TRIGGER:
					remaining = TRACK_CURSOR[2];
					if (CURRENT_SONG * HMI_MUSIC_TRIGGER_COUNT + remaining ==
					    HMI_MUSIC_SONG_COUNT * HMI_MUSIC_TRIGGER_COUNT) {
						HmiMusicFarPointer pointer = HmiMusic_ReadTriggerAlias(state);

						if (pointer.offset != 0 || pointer.selector != 0) {
							state->callbackPending = 1;
							pointer = HmiMusic_ReadTriggerAlias(state);
							if (state->callTriggerFar == NULL)
								SlipAssertFail("Unbound original trigger far target", __FILE__, __LINE__);
							state->callTriggerFar(state, pointer.offset, pointer.selector, CURRENT_SONG, track,
							                      (uint8_t)remaining);
							if (state->callbackPending == 0) {
								readDelta = 0;
								length = 0;
							} else
								state->callbackPending = 0;
						}
						break;
					}
					if (state->triggerCallbacks[(CURRENT_SONG * HMI_MUSIC_TRIGGER_COUNT + remaining) /
					                            HMI_MUSIC_TRIGGER_COUNT]
					                           [(CURRENT_SONG * HMI_MUSIC_TRIGGER_COUNT + remaining) %
					                            HMI_MUSIC_TRIGGER_COUNT] != NULL) {
						state->callbackPending = 1;
						state->triggerCallbacks[(CURRENT_SONG * HMI_MUSIC_TRIGGER_COUNT + remaining) /
						                        HMI_MUSIC_TRIGGER_COUNT]
						                       [(CURRENT_SONG * HMI_MUSIC_TRIGGER_COUNT + remaining) %
						                        HMI_MUSIC_TRIGGER_COUNT](state, CURRENT_SONG, track,
						                                                 (uint8_t)remaining);
						if (state->callbackPending == 0) {
							readDelta = 0;
							length = 0;
						} else
							state->callbackPending = 0;
					}
					break;
				case HMI_SONG_CONTROL_BRANCH_TRACK:
					id = TRACK_CURSOR[2];
					for (branch = 0; BRANCH_BYTES[HMI_SONG_BRANCH_ID_OFFSET] != id; ++branch) {
					}
					remaining = 1;
					if (state->branchCallbacks[CURRENT_SONG] != NULL) {
						state->callbackPending = 1;
						if (state->branchCallbacks[CURRENT_SONG](state, CURRENT_SONG, track, (uint8_t)id) == 0)
							remaining = 0;
						if (state->callbackPending == 0)
							length = 0;
						else
							state->callbackPending = 0;
					}
					if (remaining != 0) {
						TRACK_CURSOR = state->trackHeaders[CURRENT_SONG][track] +
						               (uint32_t)(SlipBytes_ReadLE32(BRANCH_BYTES) + HMI_SONG_TRACK_HEADER_BYTES);
						HmiMusic_RestoreBranch(state, CURRENT_SONG, track, branch);
						length = 0;
					}
					break;
				default:
					dispatchEvent = true;
					break;
				}
			} else {
				dispatchEvent = true;
			}
			if (dispatchEvent && track != HMI_SONG_CONTROL_TRACK)
				HmiMusic_Dispatch(state, CURRENT_SONG, state->routing[CURRENT_SONG][track], (uint8_t *)TRACK_CURSOR,
				                  length);
			if (state->callbackPending == 0)
				state->elapsed[CURRENT_SONG][track] = 0;
			if (TRACK_CURSOR == NULL)
				break;
			TRACK_CURSOR += length;
			if (readDelta != 0) {
				length = HmiMusic_ReadDelta(TRACK_CURSOR, &state->delta[CURRENT_SONG][track]);
				TRACK_CURSOR += length;
			}
			if (state->delta[CURRENT_SONG][track] != 0)
				break;
		}
	}
	return;
}

#undef BRANCH_BYTES
#undef TRACK_CURSOR
#undef CURRENT_SONG
