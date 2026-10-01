#include "byte_order.h"
#include "hmi_music.h"

#define DISPATCH_BYTE(address) state->dispatchGlobals[(address) - 0x85e05u]
#define SEND_DRIVER_MESSAGE(n) state->driverSend[driver](state, &DISPATCH_BYTE(0x86a35u), (n), driver)

uint32_t HmiMusic_SetSongVolume(HmiMusicState *state, uint32_t song, uint8_t volume) {
	uint32_t track;
	DISPATCH_BYTE(0x86b4eu + song * 4u) = volume;
	DISPATCH_BYTE(0x86b4fu + song * 4u) = 0;
	DISPATCH_BYTE(0x86b50u + song * 4u) = 0;
	DISPATCH_BYTE(0x86b51u + song * 4u) = 0;
	for (track = 0; track < 32; ++track) {
		if (state->cursors[song][track] != 0) {
			const uint8_t channel = state->trackHeaders[song][track][8];
			const uint32_t driver = state->routing[song][track];
			uint8_t mapped;
			if (SlipBytes_ReadLE32(&DISPATCH_BYTE(0x86a49u)) != 0)
				mapped = DISPATCH_BYTE(0x85e05u + driver * 128u + song * 16u + channel);
			else
				mapped = channel;
			DISPATCH_BYTE(0x86a3fu) = channel | 0xb0u;
			DISPATCH_BYTE(0x86a40u) = 7;
			DISPATCH_BYTE(0x86a41u) = DISPATCH_BYTE(0x86b6eu + driver * 16u + mapped);
			HmiMusic_Dispatch(state, song, state->routing[song][track], &DISPATCH_BYTE(0x86a3fu), 3);
		}
	}
	return 0;
}

uint32_t HmiMusic_SetMasterVolume(HmiMusicState *state, uint8_t volume) {
	uint32_t song;
	DISPATCH_BYTE(0x86b4du) = volume;
	for (song = 0; song < 8; ++song) {
		if (state->playing[song] != 0)
			HmiMusic_SetSongVolume(state, song, DISPATCH_BYTE(0x86b4eu + song * 4u));
	}
	return 0;
}

uint32_t HmiMusic_Cleanup(HmiMusicState *state, uint32_t song) {
	uint32_t track;
	for (track = 1; track < state->totalTracks[song]; ++track) {
		const uint32_t driver = state->routing[song][track];
		if (driver != UINT32_MAX && driver != 0xffu) {
			const uint8_t channel = state->trackHeaders[song][track][8];
			if (SlipBytes_ReadLE32(&DISPATCH_BYTE(0x86a49u)) == 0) {
				DISPATCH_BYTE(0x86a35u) = channel | 0xb0u;
				DISPATCH_BYTE(0x86a36u) = 0x7b;
				DISPATCH_BYTE(0x86a37u) = 0;
				SEND_DRIVER_MESSAGE(3);
				DISPATCH_BYTE(0x86a35u) = channel | 0xb0u;
				DISPATCH_BYTE(0x86a36u) = 0x79;
				DISPATCH_BYTE(0x86a37u) = 0;
				SEND_DRIVER_MESSAGE(3);
				DISPATCH_BYTE(0x86a35u) = channel | 0xe0u;
				DISPATCH_BYTE(0x86a36u) = 0x40;
				DISPATCH_BYTE(0x86a37u) = 0x40;
				SEND_DRIVER_MESSAGE(3);
				DISPATCH_BYTE(0x86a35u) = channel | 0xb0u;
				DISPATCH_BYTE(0x86a36u) = 7;
				DISPATCH_BYTE(0x86a37u) = 0;
				SEND_DRIVER_MESSAGE(3);
			} else {
				const uint32_t map = driver * 128u + song * 16u + channel;
				const uint8_t mapped = DISPATCH_BYTE(0x85e05u + map);
				uint8_t cache;
				DISPATCH_BYTE(0x85e05u + map) = 0xff;
				cache = DISPATCH_BYTE(0x867b5u + map);
				DISPATCH_BYTE(0x860d5u + driver * 16u + mapped) = 0xff;
				DISPATCH_BYTE(0x86125u + driver * 16u + mapped) = 0xff;
				DISPATCH_BYTE(0x86a35u) = mapped | 0xb0u;
				DISPATCH_BYTE(0x86a36u) = 0x7b;
				DISPATCH_BYTE(0x86a37u) = 0;
				SEND_DRIVER_MESSAGE(3);
				DISPATCH_BYTE(0x86a35u) = mapped | 0xb0u;
				DISPATCH_BYTE(0x86a36u) = 0x79;
				DISPATCH_BYTE(0x86a37u) = 0;
				SEND_DRIVER_MESSAGE(3);
				DISPATCH_BYTE(0x86a35u) = mapped | 0xe0u;
				DISPATCH_BYTE(0x86a36u) = 0x40;
				DISPATCH_BYTE(0x86a37u) = 0x40;
				SEND_DRIVER_MESSAGE(3);
				DISPATCH_BYTE(0x86a35u) = mapped | 0xb0u;
				DISPATCH_BYTE(0x86a36u) = 7;
				DISPATCH_BYTE(0x86a37u) = 0;
				SEND_DRIVER_MESSAGE(3);
				if (cache != 0xff) {
					const uint32_t offset = driver * 320u + channel * 20u + cache * 5u;
					DISPATCH_BYTE(0x86178u + offset) = 0xff;
					DISPATCH_BYTE(0x86176u + offset) = 0xff;
					DISPATCH_BYTE(0x86177u + offset) = 0xff;
					DISPATCH_BYTE(0x86179u + offset) = 0xff;
					DISPATCH_BYTE(0x86175u + offset) = 0xff;
					DISPATCH_BYTE(0x867b5u + map) = 0xff;
				}
			}
		}
	}
	return 1;
}

uint32_t HmiMusic_Dispatch(HmiMusicState *state, uint32_t song, uint32_t driver, uint8_t *message, uint32_t length) {
	const uint8_t original = message[0];
	const uint8_t channel = original & 15u;
	uint8_t mapped;
	uint8_t priority = 0;
	uint8_t victim = 0xff;
	uint32_t volume = UINT32_MAX;
	uint32_t i, cache, j;
	const uint32_t mapIndex = driver * 128u + song * 16u + channel;
	const uint32_t cacheBase = driver * 320u + channel * 20u;
	if (SlipBytes_ReadLE32(&DISPATCH_BYTE(0x86a49u)) == 0) {
		if ((original & 0xf0u) == 0xb0u) {
			if (message[1] == 7) {
				DISPATCH_BYTE(0x86a35u) = message[0];
				DISPATCH_BYTE(0x86a36u) = 7;
				DISPATCH_BYTE(0x86a37u) =
				    (uint8_t)((DISPATCH_BYTE(0x86b4du) *
				               ((message[2] * SlipBytes_ReadLE32(&DISPATCH_BYTE(0x86b4eu + song * 4u))) >> 7)) >>
				              7);
				DISPATCH_BYTE(0x86b6eu + driver * 16u + channel) = message[2];
				if (state->muted[song] != 0)
					DISPATCH_BYTE(0x86a37u) = 0;
			} else {
				DISPATCH_BYTE(0x86a35u) = message[0];
				DISPATCH_BYTE(0x86a36u) = message[1];
				DISPATCH_BYTE(0x86a37u) = message[2];
				DISPATCH_BYTE(0x86a38u) = message[3];
			}
			SEND_DRIVER_MESSAGE(length);
		} else {
			state->driverSend[driver](state, message, length, driver);
		}
		return 1;
	}
	mapped = DISPATCH_BYTE(0x85e05u + mapIndex);
retry:
	if (mapped == 0xff) {
		if (channel == 9) {
			DISPATCH_BYTE(0x85e05u + mapIndex) = 9;
			mapped = 9;
		} else {
			for (i = 0; i < 16; ++i) {
				while (DISPATCH_BYTE(0x86afdu + driver * 16u + i) == 0 && i < 16)
					++i;
				if (i < 16 && DISPATCH_BYTE(0x860d5u + driver * 16u + i) == 0xff) {
					DISPATCH_BYTE(0x85e05u + mapIndex) = (uint8_t)i;
					mapped = (uint8_t)i;
					DISPATCH_BYTE(0x860d5u + driver * 16u + i) = channel;
					DISPATCH_BYTE(0x86125u + driver * 16u + i) = (uint8_t)song;
					DISPATCH_BYTE(0x86085u + driver * 16u + i) = state->songs[song][0x40u + channel * 4u];
					cache = DISPATCH_BYTE(0x867b5u + mapIndex);
					if (cache == 0xff) {
						j = 0;
						goto allocateCachedVolumeChannel;
					}
					DISPATCH_BYTE(0x86b6eu + driver * 16u + i) = 127;
					DISPATCH_BYTE(0x86a35u) = (uint8_t)i | 0xb0u;
					DISPATCH_BYTE(0x86a36u) = 0x79;
					DISPATCH_BYTE(0x86a37u) = 0;
					SEND_DRIVER_MESSAGE(3);
					if (DISPATCH_BYTE(0x86178u + cacheBase + cache * 5u) != 0xff) {
						DISPATCH_BYTE(0x86a35u) = (uint8_t)i | 0xc0u;
						DISPATCH_BYTE(0x86a36u) = DISPATCH_BYTE(0x86178u + cacheBase + cache * 5u);
						SEND_DRIVER_MESSAGE(2);
					}
					if (DISPATCH_BYTE(0x86176u + cacheBase + cache * 5u) != 0xff) {
						DISPATCH_BYTE(0x86a35u) = (uint8_t)i | 0xe0u;
						DISPATCH_BYTE(0x86a36u) = 0;
						DISPATCH_BYTE(0x86a37u) = DISPATCH_BYTE(0x86176u + cacheBase + cache * 5u);
						SEND_DRIVER_MESSAGE(2); /* Original passes two, despite filling three bytes. */
					}
					if (DISPATCH_BYTE(0x86177u + cacheBase + cache * 5u) != 0xff) {
						DISPATCH_BYTE(0x86a35u) = (uint8_t)i | 0xb0u;
						DISPATCH_BYTE(0x86a36u) = 7;
						DISPATCH_BYTE(0x86a37u) = DISPATCH_BYTE(0x86177u + cacheBase + cache * 5u);
						SEND_DRIVER_MESSAGE(3);
					}
					if (DISPATCH_BYTE(0x86179u + cacheBase + cache * 5u) != 0xff) {
						DISPATCH_BYTE(0x86a35u) = (uint8_t)i | 0xb0u;
						DISPATCH_BYTE(0x86a36u) = 0x40;
						DISPATCH_BYTE(0x86a37u) = DISPATCH_BYTE(0x86179u + cacheBase + cache * 5u);
						SEND_DRIVER_MESSAGE(3);
					}
					goto retry;
				}
			}
			for (i = 0; i < 16; ++i) {
				while (DISPATCH_BYTE(0x86afdu + driver * 16u + i) == 0 && i < 16)
					++i;
				if (i < 16 && priority < DISPATCH_BYTE(0x86085u + driver * 16u + i) &&
				    DISPATCH_BYTE(0x86085u + driver * 16u + i) != 0xff) {
					priority = DISPATCH_BYTE(0x86085u + driver * 16u + i);
					victim = (uint8_t)i;
				}
			}
			if (victim == 0xff)
				goto event;
			if (priority <= SlipBytes_ReadLE32(state->songs[song] + 0x40u + channel * 4u)) {
				if (DISPATCH_BYTE(0x867b5u + mapIndex) != 0xff)
					goto event;
				j = 0;
				goto allocateCachedEventChannel;
			}
			DISPATCH_BYTE(0x85e05u + mapIndex) = victim;
			DISPATCH_BYTE(0x85e05u + driver * 128u + DISPATCH_BYTE(0x86125u + driver * 16u + victim) * 16u +
			              DISPATCH_BYTE(0x860d5u + driver * 16u + victim)) = 0xff;
			DISPATCH_BYTE(0x860d5u + driver * 16u + victim) = channel;
			DISPATCH_BYTE(0x86125u + driver * 16u + victim) = (uint8_t)song;
			mapped = victim;
			DISPATCH_BYTE(0x86085u + driver * 16u + victim) = state->songs[song][0x40u + channel * 4u];
			DISPATCH_BYTE(0x86b6eu + driver * 16u + victim) = 127;
			DISPATCH_BYTE(0x86a35u) = victim | 0xb0u;
			DISPATCH_BYTE(0x86a36u) = 0x7b;
			DISPATCH_BYTE(0x86a37u) = 0;
			SEND_DRIVER_MESSAGE(3);
			DISPATCH_BYTE(0x86a35u) = victim | 0xb0u;
			DISPATCH_BYTE(0x86a36u) = 0x79;
			DISPATCH_BYTE(0x86a37u) = 0;
			SEND_DRIVER_MESSAGE(3);
			if (DISPATCH_BYTE(0x867b5u + mapIndex) == 0xff) {
				for (j = 0; j < 4; ++j) {
					if (DISPATCH_BYTE(0x86175u + cacheBase + j * 5u) == 0xff) {
						DISPATCH_BYTE(0x86175u + cacheBase + j * 5u) = 1;
						DISPATCH_BYTE(0x867b5u + mapIndex) = (uint8_t)j;
						break;
					}
				}
			}
		}
		goto retry;
	}
	message[0] = mapped | (original & 0xf0u);
event:
	if (channel == 9) {
		if (original == 0xb9 && message[1] == 7) {
			volume = message[2];
			DISPATCH_BYTE(0x86b77u + driver * 16u) = message[2];
		}
	} else {
		cache = DISPATCH_BYTE(0x867b5u + mapIndex);
		if ((original & 0xf0u) == 0xb0u) {
			if (message[1] == 7) {
				DISPATCH_BYTE(0x86177u + cacheBase + cache * 5u) = message[2];
				volume = message[2];
				DISPATCH_BYTE(0x86b6eu + driver * 16u + mapped) = message[2];
			} else if (message[1] == 0x40) {
				DISPATCH_BYTE(0x86179u + cacheBase + cache * 5u) = message[2];
			}
		} else if ((original & 0xf0u) == 0xc0u) {
			DISPATCH_BYTE(0x86178u + cacheBase + cache * 5u) = message[1];
		} else if ((original & 0xf0u) == 0xe0u) {
			DISPATCH_BYTE(0x86176u + cacheBase + cache * 5u) = message[2];
		}
	}
	if (mapped == 0xff)
		return UINT32_MAX;
	if (volume != UINT32_MAX) {
		message[2] = state->muted[song] != 0
		                 ? 0
		                 : (uint8_t)((DISPATCH_BYTE(0x86b4du) *
		                              ((volume * SlipBytes_ReadLE32(&DISPATCH_BYTE(0x86b4eu + song * 4u))) >> 7)) >>
		                             7);
	}
	state->driverSend[driver](state, message, length, driver);
	message[0] = original;
	if (volume != UINT32_MAX)
		message[2] = (uint8_t)volume;
	return 0;
allocateCachedVolumeChannel:
	for (; j < 4; ++j) {
		if (DISPATCH_BYTE(0x86175u + cacheBase + j * 5u) == 0xff) {
			DISPATCH_BYTE(0x86175u + cacheBase + j * 5u) = 1;
			DISPATCH_BYTE(0x867b5u + mapIndex) = (uint8_t)j;
			break;
		}
	}
	goto retry;
allocateCachedEventChannel:
	for (; j < 4; ++j) {
		if (DISPATCH_BYTE(0x86175u + cacheBase + j * 5u) == 0xff) {
			DISPATCH_BYTE(0x86175u + cacheBase + j * 5u) = 1;
			DISPATCH_BYTE(0x867b5u + mapIndex) = (uint8_t)j;
			break;
		}
	}
	goto event;
}

#undef SEND_DRIVER_MESSAGE
#undef DISPATCH_BYTE
