#ifndef SLIP_MENU_RESOURCES_H
#define SLIP_MENU_RESOURCES_H
#include "resource_setup.h"
#include "software_cursor_pixels.h"

typedef struct SlipMenuResources {
	uint16_t smallFont, shadedFont, smallestFont;
} SlipMenuResources;

extern SlipMenuResources SlipMenu_resources;
extern const SlipCursorSprite SlipMenu_cursor;

typedef struct SlipMenuResourceCalls {
	void *context;
	bool (*load)(void *, const char *, uint16_t *);
	void (*resourceFailure)(void *);
	void (*selectCursor)(void *, bool useDefault, const SlipCursorSprite *);
	void (*setLoadError)(void *, SlipResourceLoadErrorHandler);
	void (*setReclaim)(void *, uint32_t);
	SlipResourceLoadErrorHandler memoryFailure;
} SlipMenuResourceCalls;

void SlipMenuResources_Initialize(SlipMenuResources *, const SlipMenuResourceCalls *);
#endif
