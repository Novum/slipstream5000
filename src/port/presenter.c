#include "presenter.h"
#include "byte_order.h"
#include "resource_host.h"
#include "runtime.h"
#include <string.h>

SlipPresenter SlipPresenter_state = {

    .layers = {{{.count = 15, .x = 26, .y = 45},
                {.count = 20, .x = 31, .y = 40},
                {.count = 4, .x = 31, .y = 40},
                {.count = 6, .x = 28, .y = 25}},
               {{.count = 15, .x = 31, .y = 51},
                {.count = 20, .x = 32, .y = 45},
                {.count = 4, .x = 32, .y = 45},
                {.count = 6, .x = 31, .y = 30}}}};

typedef enum SlipPresenterOpcode {
	PRESENTER_DELAY = 0,
	PRESENTER_JUMP = 1,
	PRESENTER_CALL = 2,
	PRESENTER_FORK = 3,
	PRESENTER_ADD_FRAME = 4,
	PRESENTER_END = 5,
	PRESENTER_SET_FRAME = 6,
	PRESENTER_RANDOM_DELAY = 7,
	PRESENTER_WAIT_TIME = 8
} SlipPresenterOpcode;

const uint8_t SlipPresenter_idle[80] = {
    0x06, 0x00, 0x01, 0x00, 0x07, 0x00, 0x06, 0x00, 0x02, 0x00, 0x00, 0x00, 0x06, 0x00, 0x03, 0x00,
    0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x03, 0x00, 0x49, 0x7a, 0x05, 0x00, 0x05, 0x00,
    0x06, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x32, 0x00, 0x04, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x32, 0x00, 0x04, 0x00, 0x02, 0x00, 0xff, 0xff, 0x00, 0x00, 0x32, 0x00, 0x04, 0x00,
    0x02, 0x00, 0xff, 0xff, 0x07, 0x00, 0x2c, 0x01, 0x88, 0x13, 0x01, 0x00, 0x49, 0x7a, 0x05, 0x00};

static const uint8_t *SlipPresenter_Target(uint32_t address) {
	if (address < 0x57a29u || address >= 0x57a29u + sizeof(SlipPresenter_idle))
		SlipRuntime_Fatal("Unmapped presenter program pointer.");
	return SlipPresenter_idle + address - 0x57a29u;
}

void SlipPresenter_Shutdown(void) {
	SlipPresenter *const state = &SlipPresenter_state;

	if (!state->active)
		return;
	state->active = false;
	SlipResourceHost_Release(NULL, state->faceResource);
	for (unsigned layer = 0; layer < 4; ++layer)
		for (unsigned frame = 0; frame < state->layers[state->variant != 0][layer].count; ++frame)
			SlipResourceHost_Release(NULL, state->layers[state->variant != 0][layer].resources[frame]);
}

bool SlipPresenter_Initialize(uint16_t variant, const char *const *archives, size_t archiveCount) {
	(void)archives;
	(void)archiveCount;
	SlipPresenter *const state = &SlipPresenter_state;

	static const char *const names[2][4] = {{"MOUTH_?.SPR", "EYES_?.SPR", "LIDS_?.SPR", "BROWS_?.SPR"},
	                                        {"FMOUTH_?.SPR", "FEYES_?.SPR", "FLIDS_?.SPR", "FBROWS_?.SPR"}};
	SlipPresenter_Shutdown();
	state->active = true;
	state->variant = variant;
	for (unsigned i = 0; i < 8; ++i)
		state->sequences[i].active = false;

	for (unsigned i = 0; i < 4; ++i) {
		memset(state->layers[state->variant != 0][i].resources, 0,
		       sizeof(state->layers[state->variant != 0][i].resources));
	}
	bool loaded = true;
	for (unsigned i = 0; i < 4 && loaded; ++i) {
		char name[16];
		strcpy(name, names[variant != 0][i]);
		char *const letter = strchr(name, '?');
		for (unsigned j = 0; j < state->layers[variant != 0][i].count; ++j) {
			*letter = (char)('A' + j);
			if (!SlipResourceHost_Load(NULL, name, &state->layers[variant != 0][i].resources[j])) {
				loaded = false;
				break;
			}
		}
	}
	if (loaded)
		loaded = SlipResourceHost_Load(NULL, variant == 0 ? "FACE.SPR" : "FFACE.SPR", &state->faceResource);
	if (!loaded) {

		for (unsigned i = 0; i < 4; ++i)
			for (unsigned j = 0; j < state->layers[variant != 0][i].count; ++j)
				if (state->layers[variant != 0][i].resources[j] != 0)
					SlipResourceHost_Release(NULL, state->layers[variant != 0][i].resources[j]);
		state->active = false;
		return false;
	}

	SlipRuntime_RegisterExit(SlipPresenter_Shutdown);
	return true;
}

void SlipPresenter_Queue(const uint8_t *program, size_t bytes) {
	SlipPresenter *const state = &SlipPresenter_state;
	for (unsigned i = 0; i < 8; ++i) {
		SlipPresenterSequence *const sequence = &state->sequences[i];
		if (!sequence->active) {
			sequence->cursor = program;
			sequence->end = program + bytes;
			sequence->stackIndex = 5;
			sequence->active = true;
			sequence->delay = 0;
			sequence->elapsed = UINT16_MAX;
			return;
		}
	}
}

static void SlipPresenter_SetFrame(SlipPresenter *state, uint16_t layer, uint16_t frame) {
	if (layer >= 4)
		SlipRuntime_Fatal("Invalid presenter layer.");
	state->layers[state->variant != 0][layer].frame =
	    frame < state->layers[state->variant != 0][layer].count ? frame : 0;
}

void SlipPresenter_Update(uint16_t delta) {
	SlipPresenter *const state = &SlipPresenter_state;
	for (unsigned i = 0; i < 8; ++i) {
		SlipPresenterSequence *const sequence = &state->sequences[i];
		if (!sequence->active)
			continue;
		sequence->elapsed = sequence->elapsed == UINT16_MAX ? 0 : (uint16_t)(sequence->elapsed + delta);
		if (sequence->delay != 0) {
			sequence->delay = (uint16_t)(sequence->delay - delta);
			if ((int16_t)sequence->delay > 0)
				continue;
			if ((int16_t)sequence->delay < 0) {
				state->overshoot = (uint16_t)(0 - sequence->delay);
				sequence->delay = 0;
			}
		}
		const uint8_t *cursor = sequence->cursor;
		bool yielded = false;
		while (sequence->active && !yielded) {
			if (cursor > sequence->end || sequence->end - cursor < 2)
				SlipRuntime_Fatal("Truncated presenter program.");
			const uint16_t opcode = SlipBytes_ReadLE16(cursor);
			cursor += 2;
			const size_t operands = opcode == PRESENTER_DELAY || opcode == PRESENTER_WAIT_TIME
			                            ? 2
			                            : (opcode >= 1 && opcode <= 7 && opcode != 5 ? 4 : 0);
			if ((size_t)(sequence->end - cursor) < operands)
				SlipRuntime_Fatal("Truncated presenter command.");
			switch (opcode) {
			case PRESENTER_JUMP:
				cursor = SlipPresenter_Target(SlipBytes_ReadLE32(cursor));
				sequence->end = SlipPresenter_idle + sizeof(SlipPresenter_idle);
				break;
			case PRESENTER_CALL:
				if (--sequence->stackIndex < 0) {
					sequence->active = false;
					break;
				}
				sequence->returnStack[sequence->stackIndex] = cursor + 4;
				cursor = SlipPresenter_Target(SlipBytes_ReadLE32(cursor));
				sequence->end = SlipPresenter_idle + sizeof(SlipPresenter_idle);
				break;
			case PRESENTER_FORK: {
				const uint8_t *const target = SlipPresenter_Target(SlipBytes_ReadLE32(cursor));
				SlipPresenter_Queue(target, (size_t)(SlipPresenter_idle + sizeof(SlipPresenter_idle) - target));
				cursor += 4;
				break;
			}
			case PRESENTER_ADD_FRAME:
			case PRESENTER_SET_FRAME: {
				uint16_t layer = SlipBytes_ReadLE16(cursor), value = SlipBytes_ReadLE16(cursor + 2);
				if (layer >= 4)
					SlipRuntime_Fatal("Invalid presenter layer.");
				if (opcode == PRESENTER_ADD_FRAME)
					value = (uint16_t)(value + state->layers[state->variant != 0][layer].frame);
				SlipPresenter_SetFrame(state, layer, value);
				cursor += 4;
				break;
			}
			case PRESENTER_WAIT_TIME:
				if (SlipBytes_ReadLE16(cursor) < sequence->elapsed) {
					cursor += 2;
					break;
				}
				sequence->cursor = cursor - 2;
				yielded = true;
				break;
			case PRESENTER_DELAY:
			case PRESENTER_RANDOM_DELAY: {
				uint16_t wait = SlipBytes_ReadLE16(cursor);
				if (opcode == PRESENTER_RANDOM_DELAY) {
					const uint16_t span = (uint16_t)(SlipBytes_ReadLE16(cursor + 2) - wait);
					wait = (uint16_t)(wait + (((uint32_t)(uint16_t)SlipRandom_Next() * span) >> 16));
				}
				cursor += operands;
				wait = (uint16_t)(wait - state->overshoot);
				if ((int16_t)wait <= 0) {
					state->overshoot = wait;
					break;
				}
				sequence->delay = wait;
				sequence->cursor = cursor;
				yielded = true;
				break;
			}
			default:
				sequence->active = false;
				break;
			}
		}
	}
}

static void SlipPresenter_DrawResource(uint16_t resource, uint8_t *framebuffer, int pitch, int16_t x, int16_t y) {
	const uint8_t *const bytes = SlipResourceHost_Lock(NULL, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	payload.data = (uint8_t *)bytes;
	SlipSprite sprite;
	if (!SlipSprite_FromPayload(&payload, &sprite))
		SlipRuntime_Fatal("Invalid presenter sprite host view.");
	SlipSprite_DrawClipped(&sprite, framebuffer, pitch, x, y);
	SlipResourceHost_Unlock(NULL, resource);
}

void SlipPresenter_Draw(uint8_t *framebuffer, int pitch, int16_t x, int16_t y) {
	SlipPresenter *const state = &SlipPresenter_state;
	SlipPresenter_DrawResource(state->faceResource, framebuffer, pitch, x, y);
	for (unsigned i = 0; i < 4; ++i) {
		const SlipPresenterLayer *const layer = &state->layers[state->variant != 0][i];
		SlipPresenter_DrawResource(layer->resources[layer->frame], framebuffer, pitch, (int16_t)(x + layer->x),
		                           (int16_t)(y + layer->y));
	}
}
