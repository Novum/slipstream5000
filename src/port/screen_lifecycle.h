#ifndef SLIP_SCREEN_LIFECYCLE_H
#define SLIP_SCREEN_LIFECYCLE_H
#include "raster.h"
#include "runtime.h"
#include "software_cursor.h"

enum SlipScreenMode {
	SLIP_SCREEN_AUTO = 0,
	SLIP_SCREEN_CHANGED_PAGES = 0x55,
	SLIP_SCREEN_SINGLE_PAGE = 0x56,
	SLIP_SCREEN_BANKED = 0x57
};

typedef struct SlipScreenClip {
	int16_t left, top, right, bottom;
} SlipScreenClip;

typedef struct SlipScreenLifecycle {
	uint32_t installed;
	uint32_t mode;
	uint8_t previousBiosMode;
	uint16_t signature, transparentColor;
	uint16_t drawHandle, previousHandle;
	uint8_t *drawPage, *previousPage;
	uint32_t rowOffsets[200];
	uint16_t pitch;
	void (*present)(void);
	bool spriteTarget;
	SlipScreenClip clip, cursorClip;
	SlipSoftwareCursor cursor;
	uint8_t palette[256][3], dirtyColors[256];
	uint32_t textureRowScroll;
	uint16_t perspectiveHandle;
	RasterPerspectiveEntry *perspectiveTable;
} SlipScreenLifecycle;

typedef struct SlipScreenLifecycleCalls {
	void *context;
	bool (*allocate)(void *, uint32_t, uint32_t, uint16_t *);
	uint8_t *(*lockPixels)(void *, uint16_t);
	RasterPerspectiveEntry *(*lockPerspective)(void *, uint16_t);
	void (*unlock)(void *, uint16_t);
	void (*release)(void *, uint16_t);
	uint8_t (*getBiosMode)(void *);
	void (*setBiosMode)(void *, uint8_t);
	void (*readDac)(void *, uint8_t[256][3]);
	void (*bindRows)(void *, uint8_t *, uint32_t, uint16_t);
	void (*setClip)(void *, SlipScreenClip);
	void (*registerExit)(void *, SlipRuntimeCleanup);
	void (*fatal)(void *, const char *);
	SlipRuntimeCleanup cleanup;
	void (*presentChangedPages)(void);
	void (*presentSinglePage)(void);
} SlipScreenLifecycleCalls;

void SlipScreen_InstallPerspective(SlipScreenLifecycle *, const SlipScreenLifecycleCalls *);
void SlipScreen_FreePerspective(SlipScreenLifecycle *, const SlipScreenLifecycleCalls *);
void SlipScreen_Install(SlipScreenLifecycle *, uint32_t mode, const SlipScreenLifecycleCalls *);
void SlipScreen_Cleanup(SlipScreenLifecycle *, const SlipScreenLifecycleCalls *);
#endif
