#ifndef SLIPSTREAM5000_SCREEN_PRESENT_HOST_H
#define SLIPSTREAM5000_SCREEN_PRESENT_HOST_H
#include "software_cursor_pixels.h"
/* Native storage/service binding; OS presentation follows the page presenter. */
void SlipScreenHost_Present(void);
void SlipScreenHost_Initialize(void);
void SlipScreenHost_SelectCursor(void *, bool useDefault, const SlipCursorSprite *);
#endif
