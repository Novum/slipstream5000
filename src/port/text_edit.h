#ifndef SLIPSTREAM5000_TEXT_EDIT_H
#define SLIPSTREAM5000_TEXT_EDIT_H

#include <stdbool.h>
#include <stdint.h>

uint32_t SlipText_Length(const char *text);

bool SlipText_Insert(char *text, uint16_t cursor, uint16_t maximumLength, uint8_t character);
void SlipText_Delete(char *text, uint32_t cursor);
uint32_t SlipText_Edit(char *text, uint16_t cursor, uint16_t maximumLength, uint8_t character);

#endif
