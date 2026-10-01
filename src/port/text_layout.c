#include "text_layout.h"
#include "raster.h"
#include "runtime.h"

SlipTextLayout SlipText_state;

static uint16_t SlipText_CharacterWidth(SlipTextLayout *state, uint8_t character) {
	SlipFont font = state->fontResources
	                    ? state->fontResources->lock(state->fontResources->context, state->fontResource)
	                    : state->font;
	uint16_t width = 0, height;
	if (character >= font.firstChar && character <= font.lastChar)
		SlipFont_CharacterMetrics(&font, character, &width, &height);
	if (state->fontResources)
		state->fontResources->unlock(state->fontResources->context, state->fontResource);
	return width;
}

bool SlipText_FitsCharacter(SlipTextLayout *state, const char *text, uint8_t character, int16_t limit) {
	uint16_t width = SlipText_CharacterWidth(state, character);
	while (*text != 0)
		width = (uint16_t)(width + SlipText_CharacterWidth(state, (uint8_t)*text++));
	return (int16_t)width <= limit;
}

void SlipText_BakeSprite(SlipTextLayout *state, const SlipSprite *sprite, const char *text, uint16_t color, int16_t y) {
	SlipText_SetColor(state, color);
	RasterSurfaceBinding saved;
	Raster_BindSprite((uint8_t *)sprite->pixels, sprite->width, sprite->height, &saved);
	SlipText_SetStyle(state, 2, UINT16_MAX, 0, (int16_t)(sprite->width - 1));
	SlipTextPosition position = {0, y};
	SlipText_Draw(state, text, NULL, &position);
	Raster_RestoreScreen(&saved);
}

void SlipText_SelectFont(SlipTextLayout *state, const SlipFont *font) {
	state->fontResources = NULL;
	state->font = *font;
	if (state->requestedSpacing == UINT16_MAX)
		state->lineSpacing = (uint16_t)(font->glyphHeight + 1);
}

void SlipText_SelectResourceFont(SlipTextLayout *state, uint16_t resource, const SlipFontResourceCalls *calls) {
	state->fontResource = resource;
	state->fontResources = calls;
	if (state->requestedSpacing == UINT16_MAX) {
		state->font = calls->lock(calls->context, resource);
		state->lineSpacing = (uint16_t)(state->font.glyphHeight + 1);
		calls->unlock(calls->context, resource);
	}
}

void SlipText_FontMetrics(SlipTextLayout *state, SlipTextFontMetrics *metrics) {
	if (state->fontResources != NULL && state->fontResource == 0)
		return;
	SlipFont font = state->fontResources != NULL
	                    ? state->fontResources->lock(state->fontResources->context, state->fontResource)
	                    : state->font;
	metrics->lineSpacing = (uint16_t)(font.glyphHeight + 1);
	metrics->glyphHeight = font.glyphHeight;
	metrics->glyphWidth = font.glyphWidth;
	metrics->firstChar = font.firstChar;
	metrics->lastChar = font.lastChar;
	if (state->fontResources != NULL)
		state->fontResources->unlock(state->fontResources->context, state->fontResource);
}

void SlipText_SetColor(SlipTextLayout *state, uint16_t color) { state->color = color; }

void SlipText_SetStyle(SlipTextLayout *state, uint16_t mode, uint16_t spacing, int16_t left, int16_t right) {
	state->mode = mode;
	state->requestedSpacing = spacing;
	state->left = left;
	state->right = right;
	if (spacing == UINT16_MAX) {
		if (state->fontResources)
			state->font = state->fontResources->lock(state->fontResources->context, state->fontResource);
		spacing = (uint16_t)(state->font.glyphHeight + 1);
		if (state->fontResources)
			state->fontResources->unlock(state->fontResources->context, state->fontResource);
	}
	state->lineSpacing = spacing;
}

void SlipText_FlushLine(SlipTextLayout *state, SlipTextPosition position) {
	state->line[state->length] = 0;
	if (state->mode == 0) {

	} else if (state->mode == 2) {
		const int16_t remaining = (int16_t)(state->right - state->left + 1 - state->width);
		position.x = (int16_t)((remaining >> 1) + state->left);
	} else if (state->mode == 3) {
		position.x = (int16_t)(state->right - state->width);
	} else if (state->mode != 1) {
		SlipRuntime_Fatal("TextFlushBuffer: Format mode not implemented yet!");
	}
	if (!state->measuring) {
		if (state->color != UINT16_MAX) {
			if (state->fontResources)
				SlipFont_DrawResourceStringColor(state->fontResource, state->line, &position.x, position.y,
				                                 state->color, state->fontResources);
			else
				SlipFont_DrawStringColor(&state->font, state->line, &position.x, position.y, state->color);
		} else {
			if (state->fontResources)
				SlipFont_DrawResourceString(state->fontResource, state->line, &position.x, position.y,
				                            state->fontResources);
			else
				SlipFont_DrawString(&state->font, state->line, &position.x, position.y);
		}
	}
	state->width = 0;
	state->length = 0;
}

void SlipText_JustifyLine(SlipTextLayout *state, SlipTextPosition position) {
	state->line[state->length] = 0;
	const uint16_t remaining = (uint16_t)(state->right - position.x - state->width);
	if (remaining == 0) {

		if (state->color != UINT16_MAX) {
			if (state->fontResources)
				SlipFont_DrawResourceStringColor(state->fontResource, state->line, &position.x, position.y,
				                                 state->color, state->fontResources);
			else
				SlipFont_DrawStringColor(&state->font, state->line, &position.x, position.y, state->color);
		} else {
			if (state->fontResources)
				SlipFont_DrawResourceString(state->fontResource, state->line, &position.x, position.y,
				                            state->fontResources);
			else
				SlipFont_DrawString(&state->font, state->line, &position.x, position.y);
		}
	} else {
		const char *text = state->line;
		uint16_t spaces = 0;
		do {
			if (*text == ' ')
				++spaces;
			++text;
		} while (*text != 0);
		state->spaceStep = (uint16_t)(((uint32_t)remaining << 8) / spaces);
		state->spaceFraction = 0;
		uint16_t advance = state->spaceStep;
		text = state->line;
		for (;;) {
			const uint8_t character = (uint8_t)*text;
			advance = (uint16_t)((advance & 0xff00u) | character);
			if (character == 0)
				break;
			if (!state->measuring) {
				if (state->color != UINT16_MAX)
					advance = state->fontResources
					              ? SlipFont_DrawResourceCharacterColor(state->fontResource, character, position.x,
					                                                    position.y, state->color, state->fontResources)
					              : SlipFont_DrawCharacterColor(&state->font, character, position.x, position.y,
					                                            state->color);
				else
					advance = state->fontResources
					              ? SlipFont_DrawResourceCharacter(state->fontResource, character, position.x,
					                                               position.y, state->fontResources)
					              : SlipFont_DrawCharacter(&state->font, character, position.x, position.y);
			}
			position.x = (int16_t)(position.x + advance);
			if (*text == ' ') {
				state->spaceFraction = (uint16_t)(state->spaceFraction + state->spaceStep);
				advance = state->spaceFraction >> 8;
				position.x = (int16_t)(position.x + advance);
				state->spaceFraction &= 0xffu;
			}
			++text;
		}
	}
	state->width = 0;
	state->length = 0;
}

void SlipText_MeasureBuffer(SlipTextLayout *state) {
	uint16_t width = 0;
	for (uint32_t i = 0; i < state->length; ++i) {
		uint16_t advance, height;
		if (state->fontResources)
			SlipFont_ResourceCharacterMetrics(state->fontResource, (uint8_t)state->line[i], &advance, &height,
			                                  state->fontResources);
		else
			SlipFont_CharacterMetrics(&state->font, (uint8_t)state->line[i], &advance, &height);
		width = (uint16_t)(width + advance);
	}
	state->width = width;
}

static void SlipText_AppendLockedCharacter(SlipTextLayout *state, uint8_t character, SlipTextPosition *position) {
	if (character > state->font.lastChar)
		return;
	if ((int8_t)(character - state->font.firstChar) < 0)
		return;
	uint16_t advance, height;
	SlipFont_CharacterMetrics(&state->font, character, &advance, &height);
	if ((int16_t)(position->x + state->width + advance) >= state->right) {
		uint32_t trailing = 0;
		if (state->mode != 0 && state->length != 0) {
			for (uint32_t i = 0; i < state->length; ++i) {
				if (state->line[i] == ' ')
					trailing = 1;
				else
					++trailing;
			}
			state->length = (uint16_t)(state->length - trailing);
			SlipText_MeasureBuffer(state);
		}
		const uint32_t flushedLength = state->length;
		if (state->mode == 1)
			SlipText_JustifyLine(state, *position);
		else
			SlipText_FlushLine(state, *position);
		if (trailing != 0) {
			--trailing;
			if (trailing != 0) {
				state->length = (uint16_t)trailing;
				for (uint32_t i = 0; i < trailing; ++i)
					state->line[i] = state->line[flushedLength + 1 + i];
			}
			SlipText_MeasureBuffer(state);
		}
		position->x = state->left;
		position->y = (int16_t)(position->y + state->lineSpacing);
		if (state->length == 0 && character == ' ')
			return;
	}
	state->width = (uint16_t)(state->width + advance);
	state->line[state->length++] = (char)character;
}

void SlipText_AppendCharacter(SlipTextLayout *state, uint8_t character, SlipTextPosition *position) {
	if ((int16_t)state->length >= 160)
		SlipRuntime_Fatal("TextDoCharacter: Text line buffer full.");
	if (state->fontResources)
		state->font = state->fontResources->lock(state->fontResources->context, state->fontResource);
	SlipText_AppendLockedCharacter(state, character, position);
	if (state->fontResources)
		state->fontResources->unlock(state->fontResources->context, state->fontResource);
}

void SlipText_Draw(SlipTextLayout *state, const char *text, const SlipTextArgument *arguments,
                   SlipTextPosition *position) {
	if (state->mode == 1) {
		if (position->x < state->left)
			position->x = state->left;
	} else if (state->mode == 2) {
		position->x = state->left;
	}
	state->length = 0;
	state->width = 0;
	for (;;) {
		uint8_t character = (uint8_t)*text;
		if (character == 0)
			break;
		++text;
		if (character == '%') {
			const SlipTextExpansion *const expansion = SlipText_EvaluateControl(text, &arguments);
			const char *expanded = expansion->text;
			for (;;) {
				character = (uint8_t)*expanded++;
				if (character == 0)
					break;
				if (character == '\r') {
					SlipText_FlushLine(state, *position);
					position->x = state->left;
					position->y = (int16_t)(position->y + state->lineSpacing);
				} else if (character != '\n') {
					SlipText_AppendCharacter(state, character, position);
				}
			}
			text = expansion->continuation;
		} else if (character == '\r') {
			SlipText_FlushLine(state, *position);
			position->x = state->left;
			position->y = (int16_t)(position->y + state->lineSpacing);
		} else if (character != '\n') {
			SlipText_AppendCharacter(state, character, position);
		}
	}
	SlipText_FlushLine(state, *position);
}

uint32_t SlipText_CountLines(SlipTextLayout *state, const char *text, const SlipTextArgument *arguments,
                             SlipTextPosition position) {
	state->measuring = true;
	SlipText_Draw(state, text, arguments, &position);
	const uint32_t lines = (uint16_t)position.y / state->lineSpacing + 1u;
	state->measuring = false;
	return lines;
}

void SlipText_DrawCentered(SlipTextLayout *state, const char *text, const SlipTextArgument *arguments, int16_t x,
                           uint32_t top, uint32_t bottom) {
	SlipTextPosition position = {x, 0};
	const uint32_t lines = SlipText_CountLines(state, text, arguments, position);
	const uint32_t height = lines * state->lineSpacing;
	const uint32_t available = bottom - top + 1u;
	if (available < height)
		position.y = (int16_t)top;
	else
		position.y = (int16_t)(((available - height) >> 1) + top);
	SlipText_Draw(state, text, arguments, &position);
}
