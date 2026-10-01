#include "byte_order.h"
#include "hmi_music.h"
#include <assert.h>
#include <stddef.h>

static const uint8_t channelLengths[16] = {0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 2, 2, 3, 0};
static const uint8_t systemLengths[16] = {0, 1, 2, 1, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2};

static void HmiMusic_TimerTick(HmiTimerState *timer) { HmiMusic_Tick(timer->musicState, timer); }

uint32_t HmiMusic_StartSong(HmiMusicState *state, HmiTimerState *timer, uint32_t song) {
	uint32_t result;
	timer->musicState = state;
	result = HmiTimer_Register(timer, SlipBytes_ReadLE32(state->songs[song] + 0x38), HmiMusic_TimerTick,
	                           &state->timerHandles[song]);
	if (result != 0)
		return result;
	timer->songForSlot[state->timerHandles[song]] = (uint8_t)(song & 255u);
	state->playing[song] = 1;
	return 0;
}

uint32_t HmiMusic_BranchSong(HmiMusicState *state, uint32_t song, uint8_t branchId) {
	uint8_t track;
	uint32_t branch;
	branchId |= 0x80;
	for (track = 1; track < state->totalTracks[song]; ++track) {
		if (state->cursors[song][track] != NULL) {
			branch = 0;
			/* Original requires a matching record; no count or fallback search. */
			while (state->branches[song][track][branch].dosBytes[4] != branchId)
				++branch;
			state->cursors[song][track] =
			    state->trackHeaders[song][track] +
			    (uint32_t)(SlipBytes_ReadLE32(state->branches[song][track][branch].dosBytes) + 12u);
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

void HmiMusic_Tick(HmiMusicState *state, HmiTimerState *timer) {
	uint8_t track;
	uint32_t length, readDelta = 1, branch, id, remaining, other;
	uint32_t mode;
	if (state->playing[CURRENT_SONG] == 0 || state->paused[CURRENT_SONG] != 0)
		return;
	if (state->fadeRemaining[CURRENT_SONG] != 0 && state->fadeCountdown[CURRENT_SONG]-- == 0) {
		state->fadeCountdown[CURRENT_SONG] = 3;
		--state->fadeRemaining[CURRENT_SONG];
		mode = state->fadeMode[CURRENT_SONG];
		if (mode == 2 || mode == 4) {
			state->fadeVolume[CURRENT_SONG] -= state->fadeStep[CURRENT_SONG];
			HmiMusic_SetSongVolume(state, CURRENT_SONG, (uint8_t)((state->fadeVolume[CURRENT_SONG] >> 16) & 255u));
			if ((state->fadeMode[CURRENT_SONG] & 4u) != 0 && state->fadeRemaining[CURRENT_SONG] == 0)
				goto completion;
		} else if (mode == 1) {
			state->fadeVolume[CURRENT_SONG] += state->fadeStep[CURRENT_SONG];
			HmiMusic_SetSongVolume(state, CURRENT_SONG, (uint8_t)((state->fadeVolume[CURRENT_SONG] >> 16) & 255u));
		}
	}
	for (track = 0; track < state->totalTracks[CURRENT_SONG]; ++track) {
		++state->elapsed[CURRENT_SONG][track];
		if (TRACK_CURSOR == NULL || state->delta[CURRENT_SONG][track] > state->elapsed[CURRENT_SONG][track])
			continue;
		do {
			state->callbackPending = 0;
			readDelta = 1;
			length = *TRACK_CURSOR < 0xf0 ? channelLengths[*TRACK_CURSOR >> 4] : systemLengths[*TRACK_CURSOR & 15u];
			if (*TRACK_CURSOR == 0xff) {
				if (TRACK_CURSOR[1] == 0x2f) {
					TRACK_CURSOR = NULL;
					if (state->activeTracks[CURRENT_SONG] == 2 && state->cursors[CURRENT_SONG][0] != NULL) {
						--state->activeTracks[CURRENT_SONG];
						state->cursors[CURRENT_SONG][0] = NULL;
					}
					if (--state->activeTracks[CURRENT_SONG] == 0)
						goto completion;
					length = 3;
				} else if (TRACK_CURSOR[1] == 0x51) {
					length = 5;
				}
				goto advance;
			}
			if ((*TRACK_CURSOR & 0xf0u) == 0xb0) {
				switch (TRACK_CURSOR[1]) {
				case 103:
					state->songs[CURRENT_SONG][0x300u + TRACK_CURSOR[2]] = 0;
					break;
				case 104:
					state->songs[CURRENT_SONG][0x300u + TRACK_CURSOR[2]] = 1;
					break;
				case 108:
				case 110:
				case 113:
				case 116:
				case 120:
					break;
				case 109:
				case 115:
					id = TRACK_CURSOR[2];
					for (branch = 0; BRANCH_BYTES[4] != id; ++branch) {
					}
					BRANCH_BYTES[6] = TRACK_CURSOR[6];
					break;
				case 111:
				case 112:
					id = TRACK_CURSOR[2];
					for (branch = 0; BRANCH_BYTES[4] != id; ++branch) {
					}
					remaining = BRANCH_BYTES[6];
					if (remaining != 255 && remaining != 0) {
						--BRANCH_BYTES[6];
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
						for (other = 1; other < state->totalTracks[CURRENT_SONG]; ++other) {
							if (state->cursors[CURRENT_SONG][other] != NULL) {
								for (branch = 0; state->branches[CURRENT_SONG][other][branch].dosBytes[4] != id;
								     ++branch) {
								}
								state->cursors[CURRENT_SONG][other] =
								    state->trackHeaders[CURRENT_SONG][other] +
								    (uint32_t)(SlipBytes_ReadLE32(
								                   state->branches[CURRENT_SONG][other][branch].dosBytes) +
								               12u);
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
				case 114:
					id = TRACK_CURSOR[2] | 0x80u;
					for (branch = 0; BRANCH_BYTES[4] != id; ++branch) {
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
						for (other = 1; other < state->totalTracks[CURRENT_SONG]; ++other) {
							if (state->cursors[CURRENT_SONG][other] != NULL) {
								for (branch = 0; state->branches[CURRENT_SONG][other][branch].dosBytes[4] != id;
								     ++branch) {
								}
								state->cursors[CURRENT_SONG][other] =
								    state->trackHeaders[CURRENT_SONG][other] +
								    (uint32_t)(SlipBytes_ReadLE32(
								                   state->branches[CURRENT_SONG][other][branch].dosBytes) +
								               12u);
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
				case 117:
				case 118:
					id = TRACK_CURSOR[2];
					for (branch = 0; BRANCH_BYTES[4] != id; ++branch) {
					}
					remaining = BRANCH_BYTES[6];
					if (remaining != 255 && remaining != 0) {
						--BRANCH_BYTES[6];
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
						               (uint32_t)(SlipBytes_ReadLE32(BRANCH_BYTES) + 12u);
						HmiMusic_RestoreBranch(state, CURRENT_SONG, track, branch);
						length = 0;
					}
					break;
				case 119:
					remaining = TRACK_CURSOR[2];
					if (CURRENT_SONG * 127u + remaining == 8u * 127u) {
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
					if (state->triggerCallbacks[(CURRENT_SONG * 127u + remaining) / 127u]
					                           [(CURRENT_SONG * 127u + remaining) % 127u] != NULL) {
						state->callbackPending = 1;
						state->triggerCallbacks[(CURRENT_SONG * 127u + remaining) / 127u]
						                       [(CURRENT_SONG * 127u + remaining) % 127u](state, CURRENT_SONG, track,
						                                                                  (uint8_t)remaining);
						if (state->callbackPending == 0) {
							readDelta = 0;
							length = 0;
						} else
							state->callbackPending = 0;
					}
					break;
				case 121:
					id = TRACK_CURSOR[2];
					for (branch = 0; BRANCH_BYTES[4] != id; ++branch) {
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
						               (uint32_t)(SlipBytes_ReadLE32(BRANCH_BYTES) + 12u);
						HmiMusic_RestoreBranch(state, CURRENT_SONG, track, branch);
						length = 0;
					}
					break;
				default:
					goto dispatch;
				}
			} else {
			dispatch:
				if (track != 0)
					HmiMusic_Dispatch(state, CURRENT_SONG, state->routing[CURRENT_SONG][track], (uint8_t *)TRACK_CURSOR,
					                  length);
			}
		advance:
			if (state->callbackPending == 0)
				state->elapsed[CURRENT_SONG][track] = 0;
			if (TRACK_CURSOR == NULL)
				break;
			TRACK_CURSOR += length;
			if (readDelta != 0) {
				length = HmiMusic_ReadDelta(TRACK_CURSOR, &state->delta[CURRENT_SONG][track]);
				TRACK_CURSOR += length;
			}
		} while (state->delta[CURRENT_SONG][track] == 0);
	}
	return;
completion:
	/* Both original completion sites perform this sequence, rather than
	 * calling StopSong (which saves callback bytes before driver cleanup). */
	{
		HmiMusicSongDescriptor descriptor;
		void (*callback)(HmiMusicState *);
		state->playing[CURRENT_SONG] = 0;
		HmiMusic_Cleanup(state, CURRENT_SONG);
		if (state->timerHandles[CURRENT_SONG] != UINT32_MAX)
			HmiTimer_Remove(timer, state->timerHandles[CURRENT_SONG]);
		if (state->timerHandles[CURRENT_SONG] == UINT32_MAX)
			timer->accumulators[15] |= 0xff000000u;
		else
			timer->songForSlot[state->timerHandles[CURRENT_SONG]] = 0xff;
		state->timerHandles[CURRENT_SONG] = UINT32_MAX;
		descriptor.songData = state->songs[CURRENT_SONG];
		descriptor.completionCallbackOffset = SlipBytes_ReadLE32(descriptor.songData + 0x380);
		descriptor.completionCallbackSelector =
		    (uint16_t)((uint16_t)descriptor.songData[0x384] | ((uint16_t)descriptor.songData[0x385] << 8));
		callback = state->completionCallbacks[CURRENT_SONG];
		state->songs[CURRENT_SONG] = NULL;
		HmiMusic_ResetSong(state, CURRENT_SONG, &descriptor);
		if (callback != NULL)
			callback(state);
	}
}

#undef BRANCH_BYTES
#undef TRACK_CURSOR
#undef CURRENT_SONG
