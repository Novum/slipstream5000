#include "hmi_music.h"

void HmiMusic_RestoreBranch(HmiMusicState *state, uint32_t song, uint32_t track, uint32_t branch) {

	uint8_t message[4] = {0};
	uint32_t controllerByteOffset;
	if (state->songs[song][0x36c] != 0) {
		message[0] = (*state->cursors[song][track] & 0x0fu) | 0xc0u;
		message[1] = state->branches[song][track][branch].dosBytes[5];
		HmiMusic_Dispatch(state, song, state->routing[song][track], message, 2);
	}
	message[0] = (*state->cursors[song][track] & 0x0fu) | 0xb0u;
	for (controllerByteOffset = 0; controllerByteOffset < state->branches[song][track][branch].dosBytes[7];
	     controllerByteOffset += 2) {
		message[1] = state->branches[song][track][branch].controllerPairs[controllerByteOffset];
		message[2] = state->branches[song][track][branch].controllerPairs[controllerByteOffset + 1];
		if (state->songs[song][0x300u + message[1]] != 0)
			HmiMusic_Dispatch(state, song, state->routing[song][track], message, 3);
	}
}
