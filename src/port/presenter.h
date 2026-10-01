#ifndef SLIPSTREAM5000_PRESENTER_H
#define SLIPSTREAM5000_PRESENTER_H
#include "sprite.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct SlipPresenterSequence {
	bool active;
	const uint8_t *cursor;
	const uint8_t *end;
	int16_t stackIndex;
	const uint8_t *returnStack[5];
	uint16_t delay;
	uint16_t elapsed;
} SlipPresenterSequence;

typedef struct SlipPresenterLayer {
	uint16_t frame, count;
	int16_t x, y;
	uint16_t resources[20];
} SlipPresenterLayer;

typedef struct SlipPresenter {
	bool active;
	uint16_t variant;
	uint16_t faceResource;
	SlipPresenterLayer layers[2][4];
	SlipPresenterSequence sequences[8];
	uint16_t overshoot;
} SlipPresenter;

extern SlipPresenter SlipPresenter_state;

bool SlipPresenter_Initialize(uint16_t variant, const char *const *archives, size_t archiveCount);
void SlipPresenter_Shutdown(void);
void SlipPresenter_Queue(const uint8_t *program, size_t bytes);
void SlipPresenter_Update(uint16_t delta);
void SlipPresenter_Draw(uint8_t *framebuffer, int pitch, int16_t x, int16_t y);

extern const uint8_t SlipPresenter_idle[80];
#endif
