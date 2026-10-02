#include "font.h"
#include "byte_order.h"
#include "raster/overlay.h"
#include "raster/raster.h"
#include "runtime.h"

#include <stddef.h>
#include <string.h>

void SlipFont_DrawResourceString(uint16_t resource, const char *text, int16_t *x, int16_t y,
                                 const SlipFontResourceCalls *calls) {
	SlipFont font = calls->lock(calls->context, resource);
	SlipFont_DrawString(&font, text, x, y);
	calls->unlock(calls->context, resource);
}

void SlipFont_DrawResourceStringColor(uint16_t resource, const char *text, int16_t *x, int16_t y, uint16_t color,
                                      const SlipFontResourceCalls *calls) {
	SlipFont font = calls->lock(calls->context, resource);
	SlipFont_DrawStringColor(&font, text, x, y, color);
	calls->unlock(calls->context, resource);
}

uint16_t SlipFont_DrawResourceCharacter(uint16_t resource, uint8_t character, int16_t x, int16_t y,
                                        const SlipFontResourceCalls *calls) {
	SlipFont font = calls->lock(calls->context, resource);
	const uint16_t advance = SlipFont_DrawCharacter(&font, character, x, y);
	calls->unlock(calls->context, resource);
	return advance;
}

uint16_t SlipFont_DrawResourceCharacterColor(uint16_t resource, uint8_t character, int16_t x, int16_t y, uint16_t color,
                                             const SlipFontResourceCalls *calls) {
	SlipFont font = calls->lock(calls->context, resource);
	const uint16_t advance = SlipFont_DrawCharacterColor(&font, character, x, y, color);
	calls->unlock(calls->context, resource);
	return advance;
}

uint16_t SlipFont_MeasureResource(uint16_t resource, const char *text, const SlipFontResourceCalls *calls) {
	SlipFont font = calls->lock(calls->context, resource);
	const uint16_t width = (uint16_t)SlipFont_MeasureText(&font, text);
	calls->unlock(calls->context, resource);
	return width;
}

void SlipFont_ResourceCharacterMetrics(uint16_t resource, uint8_t character, uint16_t *advance, uint16_t *height,
                                       const SlipFontResourceCalls *calls) {
	SlipFont font = calls->lock(calls->context, resource);
	SlipFont_CharacterMetrics(&font, character, advance, height);
	calls->unlock(calls->context, resource);
}

void SlipFont_CharacterMetrics(const SlipFont *font, uint8_t character, uint16_t *advance, uint16_t *height) {
	*height = font->glyphHeight;
	const uint8_t index = (uint8_t)(character - font->firstChar);
	const uint8_t *const entry = font->data + font->tableOffset + (uint32_t)index * 4;
	*advance = SlipBytes_ReadLE16(entry + 2);
}

int SlipFont_FromPayload(const SlipResourcePayload *payload, SlipFont *font) {
	uint16_t tableOffset;
	size_t glyphCount;

	if (payload == NULL || payload->data == NULL || payload->size < 14) {
		return 0;
	}

	if (memcmp(payload->data, "FONT", 4) != 0) {
		return 0;
	}

	tableOffset = SlipBytes_ReadLE16(payload->data + 10);
	glyphCount = (size_t)(payload->data[9] - payload->data[8] + 1u);
	if (tableOffset + glyphCount * 4u > payload->size) {
		return 0;
	}

	font->data = payload->data;
	font->glyphWidth = SlipBytes_ReadLE16(payload->data + 4);
	font->glyphHeight = SlipBytes_ReadLE16(payload->data + 6);
	font->firstChar = payload->data[8];
	font->lastChar = payload->data[9];
	font->tableOffset = tableOffset;

	return font->glyphWidth != 0 && font->glyphHeight != 0;
}

int SlipFont_MeasureText(const SlipFont *font, const char *text) {
	uint16_t width = 0;
	for (;;) {
		const uint8_t character = (uint8_t)*text++;
		if (character == 0)
			return width;
		if (character > font->lastChar)
			continue;
		if (character < font->firstChar)
			continue;
		const uint16_t offset = (uint16_t)((uint16_t)(character - font->firstChar) * 4u + font->tableOffset);
		width = (uint16_t)(width + SlipBytes_ReadLE16(font->data + offset + 2u));
	}
}

void SlipFont_DrawText(const SlipFont *font, uint8_t *dst, int dstPitch, int x, int y, const char *text, int color) {
	SlipFont_DrawTextClipped(font, dst, dstPitch, x, y, text, color, 0, 0, SLIPSTREAM_SCREEN_WIDTH - 1,
	                         SLIPSTREAM_SCREEN_HEIGHT - 1);
}

void SlipFont_DrawTextClipped(const SlipFont *font, uint8_t *dst, int dstPitch, int x, int y, const char *text,
                              int color, int clipMinX, int clipMinY, int clipMaxX, int clipMaxY) {
	RasterSurfaceBinding saved = {g_screenBufferBase, g_screenPitch, g_clipMinX, g_clipMinY, g_clipMaxX, g_clipMaxY};
	Raster_SetScreenBufferRows(dst, dstPitch);
	Raster_SetClipRect((int16_t)clipMinX, (int16_t)clipMinY, (int16_t)clipMaxX, (int16_t)clipMaxY);
	int16_t position = (int16_t)x;
	if (color < 0)
		SlipFont_DrawString(font, text, &position, (int16_t)y);
	else
		SlipFont_DrawStringColor(font, text, &position, (int16_t)y, (uint16_t)color);
	Raster_SetScreenBufferRows(saved.screenBuffer, saved.screenPitch);
	Raster_SetClipRect(saved.minX, saved.minY, saved.maxX, saved.maxY);
}

static uint16_t lastCharacterAdvance;
static uint16_t lastColorCharacterAdvance;

void SlipFont_DrawString(const SlipFont *font, const char *text, int16_t *x, int16_t y) {
	if (*x < g_clipMinX || y < g_clipMinY)
		return;
	if ((int16_t)(y + font->glyphHeight - 1) > g_clipMaxY)
		return;
	for (;;) {
		const uint8_t character = (uint8_t)*text++;
		if (character == 0)
			return;
		if (character < font->firstChar)
			continue;
		const uint8_t index = (uint8_t)(character - font->firstChar);
		if (index > font->lastChar)
			continue;
		const uint8_t *const entry = font->data + font->tableOffset + (uint32_t)index * 4;
		const uint16_t advance = SlipBytes_ReadLE16(entry + 2);
		const uint8_t *glyph = font->data + SlipBytes_ReadLE16(entry);
		uint8_t *destination = g_screenRowPtrs[(uint16_t)y] + (uint16_t)(*x);
		uint16_t rows = font->glyphHeight;
		do {
			uint32_t columns = font->glyphWidth;
			do {
				const uint8_t pixel = *glyph++;
				if (pixel != 0) {
					*destination = pixel;
					RasterOverlay_MarkWritten(destination, 1);
				}
				++destination;
			} while (--columns != 0);
			destination += g_screenPitch - font->glyphWidth;
		} while (--rows != 0);
		*x = (int16_t)(*x + advance);
	}
}

void SlipFont_DrawStringColor(const SlipFont *font, const char *text, int16_t *x, int16_t y, uint16_t color) {
	if (*x < g_clipMinX || y < g_clipMinY)
		return;
	if ((int16_t)(y + font->glyphHeight - 1) > g_clipMaxY)
		return;
	for (;;) {
		const uint8_t character = (uint8_t)*text++;
		if (character == 0)
			return;
		if (character < font->firstChar)
			continue;
		const uint8_t index = (uint8_t)(character - font->firstChar);
		if (index > font->lastChar)
			continue;
		const uint8_t *const entry = font->data + font->tableOffset + (uint32_t)index * 4;
		const uint16_t advance = SlipBytes_ReadLE16(entry + 2);
		const uint8_t *glyph = font->data + SlipBytes_ReadLE16(entry);
		uint8_t *destination = g_screenRowPtrs[(uint16_t)y] + (uint16_t)(*x);
		uint16_t rows = font->glyphHeight;
		do {
			uint32_t columns = font->glyphWidth;
			do {
				const uint8_t pixel = *glyph++;
				if (pixel != 0) {
					*destination = (uint8_t)color;
					RasterOverlay_MarkWritten(destination, 1);
				}
				++destination;
			} while (--columns != 0);
			destination += g_screenPitch - font->glyphWidth;
		} while (--rows != 0);
		*x = (int16_t)(*x + advance);
	}
}

uint16_t SlipFont_DrawCharacter(const SlipFont *font, uint8_t character, int16_t x, int16_t y) {
	if (x < g_clipMinX || y < g_clipMinY)
		return lastCharacterAdvance;

	if (x > g_clipMaxX)
		return lastCharacterAdvance;
	if ((int16_t)(y + font->glyphHeight - 1) > g_clipMaxY)
		return lastCharacterAdvance;
	if (character < font->firstChar)
		return lastCharacterAdvance;
	const uint8_t index = (uint8_t)(character - font->firstChar);
	if (index > font->lastChar)
		return lastCharacterAdvance;
	const uint8_t *const entry = font->data + font->tableOffset + (uint32_t)index * 4;
	lastCharacterAdvance = SlipBytes_ReadLE16(entry + 2);
	const uint8_t *glyph = font->data + SlipBytes_ReadLE16(entry);
	uint8_t *destination = g_screenRowPtrs[(uint16_t)y] + (uint16_t)(x);
	uint16_t rows = font->glyphHeight;
	do {
		uint32_t columns = font->glyphWidth;
		do {
			const uint8_t pixel = *glyph++;
			if (pixel != 0) {
				*destination = pixel;
				RasterOverlay_MarkWritten(destination, 1);
			}
			++destination;
		} while (--columns != 0);
		destination += g_screenPitch - font->glyphWidth;
	} while (--rows != 0);
	return lastCharacterAdvance;
}

uint16_t SlipFont_DrawCharacterColor(const SlipFont *font, uint8_t character, int16_t x, int16_t y, uint16_t color) {
	if (x < g_clipMinX || y < g_clipMinY)
		return lastColorCharacterAdvance;

	if (x > g_clipMaxX)
		return lastColorCharacterAdvance;
	if ((int16_t)(y + font->glyphHeight - 1) > g_clipMaxY)
		return lastColorCharacterAdvance;
	if (character < font->firstChar)
		return lastColorCharacterAdvance;
	const uint8_t index = (uint8_t)(character - font->firstChar);
	if (index > font->lastChar)
		return lastColorCharacterAdvance;
	const uint8_t *const entry = font->data + font->tableOffset + (uint32_t)index * 4;
	lastColorCharacterAdvance = SlipBytes_ReadLE16(entry + 2);
	const uint8_t *glyph = font->data + SlipBytes_ReadLE16(entry);
	uint8_t *destination = g_screenRowPtrs[(uint16_t)y] + (uint16_t)(x);
	uint16_t rows = font->glyphHeight;
	do {
		uint32_t columns = font->glyphWidth;
		do {
			const uint8_t pixel = *glyph++;
			if (pixel != 0) {
				*destination = (uint8_t)color;
				RasterOverlay_MarkWritten(destination, 1);
			}
			++destination;
		} while (--columns != 0);
		destination += g_screenPitch - font->glyphWidth;
	} while (--rows != 0);
	return lastColorCharacterAdvance;
}

void SlipFont_DrawCenteredLine(const SlipFont *font, uint8_t *dst, int pitch, const char *line, int color, int left,
                               int right, int y) {
	const int x = left + ((right - left + 1 - SlipFont_MeasureText(font, line)) >> 1);

	SlipFont_DrawTextClipped(font, dst, pitch, x, y, line, color, g_clipMinX, g_clipMinY, g_clipMaxX, g_clipMaxY);
}
