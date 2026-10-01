#ifndef SLIPSTREAM5000_TEXT_LAYOUT_H
#define SLIPSTREAM5000_TEXT_LAYOUT_H

#include "font.h"
#include "sprite.h"
#include "text_format.h"
#include <stdbool.h>

typedef struct SlipTextLayout {
	bool measuring;
	uint16_t mode, color;
	uint16_t requestedSpacing, lineSpacing;
	int16_t left, right;
	SlipFont font;
	/* Host binding: NULL uses an already resident FONT view. */
	const SlipFontResourceCalls *fontResources;
	uint16_t fontResource;
	char line[161];
	uint16_t length, width;
	uint16_t spaceFraction, spaceStep;
} SlipTextLayout;

typedef struct SlipTextPosition {
	int16_t x, y;
} SlipTextPosition;

typedef struct SlipTextFontMetrics {
	uint16_t lineSpacing, glyphHeight, glyphWidth;
	uint8_t firstChar, lastChar;
} SlipTextFontMetrics;

void SlipText_FontMetrics(SlipTextLayout *, SlipTextFontMetrics *);

extern SlipTextLayout SlipText_state;
bool SlipText_FitsCharacter(SlipTextLayout *, const char *, uint8_t character, int16_t width);

void SlipText_BakeSprite(SlipTextLayout *state, const SlipSprite *sprite, const char *text, uint16_t color, int16_t y);

void SlipText_SelectFont(SlipTextLayout *state, const SlipFont *font);
void SlipText_SelectResourceFont(SlipTextLayout *, uint16_t resource, const SlipFontResourceCalls *);
void SlipText_SetColor(SlipTextLayout *state, uint16_t color);
void SlipText_SetStyle(SlipTextLayout *state, uint16_t mode, uint16_t spacing, int16_t left, int16_t right);

void SlipText_FlushLine(SlipTextLayout *state, SlipTextPosition position);
void SlipText_JustifyLine(SlipTextLayout *state, SlipTextPosition position);
void SlipText_MeasureBuffer(SlipTextLayout *state);
void SlipText_AppendCharacter(SlipTextLayout *state, uint8_t character, SlipTextPosition *position);
void SlipText_Draw(SlipTextLayout *state, const char *text, const SlipTextArgument *arguments,
                   SlipTextPosition *position);
uint32_t SlipText_CountLines(SlipTextLayout *state, const char *text, const SlipTextArgument *arguments,
                             SlipTextPosition position);
void SlipText_DrawCentered(SlipTextLayout *state, const char *text, const SlipTextArgument *arguments, int16_t x,
                           uint32_t top, uint32_t bottom);

#endif
