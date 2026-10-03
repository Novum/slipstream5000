#ifndef SLIPSTREAM5000_PRESENTER_H
#define SLIPSTREAM5000_PRESENTER_H
#include "sprite.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
	SLIP_PRESENTER_VARIANT_COUNT = 2,
	SLIP_PRESENTER_LAYER_COUNT = 4,
	SLIP_PRESENTER_FRAME_CAPACITY = 20,
	SLIP_PRESENTER_SEQUENCE_CAPACITY = 8,
	SLIP_PRESENTER_RETURN_STACK_CAPACITY = 5,
	SLIP_PRESENTER_IDLE_PROGRAM_BYTES = 80
};

typedef struct SlipPresenterSequence {
	bool active;
	const uint8_t *cursor;
	const uint8_t *end;
	int16_t stackIndex;
	const uint8_t *returnStack[SLIP_PRESENTER_RETURN_STACK_CAPACITY];
	uint16_t delay;
	uint16_t elapsed;
} SlipPresenterSequence;

typedef struct SlipPresenterLayer {
	uint16_t frame, count;
	int16_t x, y;
	uint16_t resources[SLIP_PRESENTER_FRAME_CAPACITY];
} SlipPresenterLayer;

typedef struct SlipPresenter {
	bool active;
	uint16_t variant;
	uint16_t faceResource;
	SlipPresenterLayer layers[SLIP_PRESENTER_VARIANT_COUNT][SLIP_PRESENTER_LAYER_COUNT];
	SlipPresenterSequence sequences[SLIP_PRESENTER_SEQUENCE_CAPACITY];
	uint16_t overshoot;
} SlipPresenter;

extern SlipPresenter SlipPresenter_state;

bool SlipPresenter_Initialize(uint16_t variant, const char *const *archives, size_t archiveCount);
void SlipPresenter_Shutdown(void);
void SlipPresenter_Queue(const uint8_t *program, size_t bytes);
void SlipPresenter_Update(uint16_t delta);
void SlipPresenter_Draw(uint8_t *framebuffer, int pitch, int16_t x, int16_t y);

extern const uint8_t SlipPresenter_idle[SLIP_PRESENTER_IDLE_PROGRAM_BYTES];
#endif
