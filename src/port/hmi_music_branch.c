#include "hmi_music.h"

void HmiMusic_RestoreBranch(HmiMusicState *state, uint32_t song, uint32_t track, uint32_t branch) {

	uint8_t message[HMI_MIDI_MESSAGE_STORAGE_BYTES] = {0};
	uint32_t controllerByteOffset;
	if (state->songs[song][HMI_SONG_RESTORE_PROGRAM_ENABLED_OFFSET] != 0) {
		message[0] = (*state->cursors[song][track] & HMI_MIDI_CHANNEL_MASK) | HMI_MIDI_PROGRAM_CHANGE;
		message[1] = state->branches[song][track][branch].dosBytes[HMI_SONG_BRANCH_PROGRAM_OFFSET];
		HmiMusic_Dispatch(state, song, state->routing[song][track], message, HMI_MIDI_PROGRAM_MESSAGE_BYTES);
	}
	message[0] = (*state->cursors[song][track] & HMI_MIDI_CHANNEL_MASK) | HMI_MIDI_CONTROL_CHANGE;
	for (controllerByteOffset = 0;
	     controllerByteOffset < state->branches[song][track][branch].dosBytes[HMI_SONG_BRANCH_CONTROLLER_BYTES_OFFSET];
	     controllerByteOffset += HMI_SONG_CONTROLLER_PAIR_BYTES) {
		message[1] = state->branches[song][track][branch].controllerPairs[controllerByteOffset];
		message[2] = state->branches[song][track][branch].controllerPairs[controllerByteOffset + 1];
		if (state->songs[song][HMI_SONG_CONTROLLER_RESTORE_FLAGS_OFFSET + message[1]] != 0)
			HmiMusic_Dispatch(state, song, state->routing[song][track], message, HMI_MIDI_CONTROL_MESSAGE_BYTES);
	}
}
