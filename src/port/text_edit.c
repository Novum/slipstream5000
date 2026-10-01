#include "text_edit.h"
#include "input.h"

uint32_t SlipText_Length(const char *text) {
	uint32_t length = 0;
	while (text[length] != '\0')
		++length;
	return length;
}

bool SlipText_Insert(char *text, uint16_t cursor, uint16_t maximumLength, uint8_t character) {
	if (maximumLength == SlipText_Length(text))
		return true;
	uint32_t count = SlipText_Length(text + cursor) + 1;
	while (count != 0) {
		text[cursor + count] = text[cursor + count - 1];
		--count;
	}
	text[cursor] = (char)character;
	return false;
}

void SlipText_Delete(char *text, uint32_t cursor) {
	if (text[cursor] != '\0') {
		const uint32_t count = SlipText_Length(text + cursor);
		for (uint32_t i = 0; i < count; ++i)
			text[cursor + i] = text[cursor + i + 1];
	}
}

uint32_t SlipText_Edit(char *text, uint16_t cursor, uint16_t maximumLength, uint8_t character) {
	uint32_t position = cursor;
	switch (character) {
	case SLIP_CHARACTER_UP:
	case SLIP_CHARACTER_DOWN:
	case SLIP_CHARACTER_ENTER:
		break;
	case SLIP_CHARACTER_LEFT:
		if (position != 0)
			--position;
		break;
	case SLIP_CHARACTER_RIGHT:
		if ((int32_t)position < (int32_t)SlipText_Length(text))
			++position;
		break;
	case SLIP_CHARACTER_DELETE:
		SlipText_Delete(text, position);
		break;
	case SLIP_CHARACTER_BACKSPACE:
		if (position != 0) {
			--position;
			SlipText_Delete(text, position);
		}
		break;
	default:
		if (!SlipText_Insert(text, (uint16_t)position, maximumLength, character))
			++position;
		break;
	}
	return position;
}
