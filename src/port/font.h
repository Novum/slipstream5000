#ifndef SLIPSTREAM5000_FONT_H
#define SLIPSTREAM5000_FONT_H

#include "resource.h"

#include <stdint.h>

typedef struct SlipFont {
	const uint8_t *data;
	uint16_t glyphWidth;
	uint16_t glyphHeight;
	uint8_t firstChar;
	uint8_t lastChar;
	uint16_t tableOffset;
} SlipFont;

/* Typed resource ABI for the original text metric callees. The lock returns
 * a decoded view of the FONT file while retaining the resource lock. */
typedef struct SlipFontResourceCalls {
	void *context;
	SlipFont (*lock)(void *, uint16_t resource);
	void (*unlock)(void *, uint16_t resource);
} SlipFontResourceCalls;

uint16_t SlipFont_MeasureResource(uint16_t resource, const char *text, const SlipFontResourceCalls *);
void SlipFont_ResourceCharacterMetrics(uint16_t resource, uint8_t character, uint16_t *advance, uint16_t *height,
                                       const SlipFontResourceCalls *);
void SlipFont_DrawResourceString(uint16_t resource, const char *text, int16_t *x, int16_t y,
                                 const SlipFontResourceCalls *);
void SlipFont_DrawResourceStringColor(uint16_t resource, const char *text, int16_t *x, int16_t y, uint16_t color,
                                      const SlipFontResourceCalls *);

uint16_t SlipFont_DrawResourceCharacter(uint16_t resource, uint8_t character, int16_t x, int16_t y,
                                        const SlipFontResourceCalls *);
uint16_t SlipFont_DrawResourceCharacterColor(uint16_t resource, uint8_t character, int16_t x, int16_t y, uint16_t color,
                                             const SlipFontResourceCalls *);

int SlipFont_FromPayload(const SlipResourcePayload *payload, SlipFont *font);
int SlipFont_MeasureText(const SlipFont *font, const char *text);
void SlipFont_CharacterMetrics(const SlipFont *font, uint8_t character, uint16_t *advance, uint16_t *height);
void SlipFont_DrawText(const SlipFont *font, uint8_t *dst, int dstPitch, int x, int y, const char *text, int color);
void SlipFont_DrawTextClipped(const SlipFont *font, uint8_t *dst, int dstPitch, int x, int y, const char *text,
                              int color, int clipMinX, int clipMinY, int clipMaxX, int clipMaxY);

void SlipFont_DrawString(const SlipFont *font, const char *text, int16_t *x, int16_t y);
void SlipFont_DrawStringColor(const SlipFont *font, const char *text, int16_t *x, int16_t y, uint16_t color);
uint16_t SlipFont_DrawCharacter(const SlipFont *font, uint8_t character, int16_t x, int16_t y);
uint16_t SlipFont_DrawCharacterColor(const SlipFont *font, uint8_t character, int16_t x, int16_t y, uint16_t color);

void SlipFont_DrawCenteredLine(const SlipFont *font, uint8_t *dst, int pitch, const char *line, int color, int left,
                               int right, int y);

#endif
