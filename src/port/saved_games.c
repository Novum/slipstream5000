#include "saved_games.h"
#include "championship_save_file.h"
#include "config_menu_host.h"
#include "frame_timer.h"
#include "game_errors.h"
#include "host_file.h"
#include "input_bios_host.h"
#include "input_zone.h"
#include "menu.h"
#include "menu_resources.h"
#include "port_app_bridge.h"
#include "resource_host.h"
#include "save_slot_animation.h"
#include "text_edit.h"
#include "text_layout.h"
#include <stdio.h>
#include <string.h>

enum {
	SAVED_PATH_CAPACITY = 4096,
	SAVED_CANCEL = SLIP_SAVE_SLOT_COUNT + 1,
	SAVED_CURSOR_BLINK_MS = 1000,
	SAVED_SLOT_NAME_Y = 64,
	SAVED_CANCEL_LABEL_Y = 95,
	SAVED_STATUS_TOP = 54,
	SAVED_STATUS_BOTTOM = 79,
	SAVED_NAME_LEFT = 106,
	SAVED_NAME_TOP = 68,
	SAVED_NAME_CURSOR_BOTTOM = 77,
	SAVED_NAME_MAXIMUM_WIDTH = 100,
	SAVED_NAME_CLIP_LEFT = 99,
	SAVED_NAME_CLIP_RIGHT = 216,
	SAVED_NAME_PROMPT_Y = 57,
	SAVED_NAME_TEXT_COLOUR = 255,
	SAVED_TOGGLE_SELECTION_BASE = 0x80,
	SAVED_TOGGLE_FIRST = SAVED_TOGGLE_SELECTION_BASE + 1,
	SAVED_TOGGLE_LAST = SAVED_TOGGLE_SELECTION_BASE + SLIP_SAVE_SLOT_COUNT,
	SAVED_TEXT_COLOR = 240,
	SAVED_TAG_CANCEL = 0x43414e43,
	SAVED_TAG_EMPTY = 0x4e4f4741,
	SAVED_TAG_CHOOSE = 0x43485345,
	SAVED_TAG_LOADING = 0x4c4f4144,
	SAVED_TAG_CHOOSE_SAVE = 0x43485331,
	SAVED_TAG_NAME = 0x454e5452
};

static struct {
	uint16_t labels[SLIP_SAVE_SLOT_COUNT], frames[SLIP_SAVE_SLOT_LAST_FRAME][SLIP_SAVE_SLOT_COUNT];
	uint16_t zones, background;
	SlipStringTableSlot *strings;
	SlipSaveSlotAnimation animation;
} saved;

static SlipInputNavigationTable savedGamesNavigation = {
    .itemCount = SAVED_CANCEL,
    .currentItem = 0,
    .up = {-1, -1, -1, -1, -1, -1, -1},
    .down = {-1, -1, -1, -1, -1, -1, -1},
    .left = {3, -1, 1, 2, 0, 4, 5},
    .right = {4, 2, 3, 0, 5, 6, -1},
    .centers = {{160, 99}, {13, 90}, {48, 96}, {77, 104}, {237, 103}, {266, 96}, {301, 89}}};
static const SlipStringTableResources strings = {.load = SlipResourceHost_Load,
                                                 .lock = SlipResourceHost_Lock,
                                                 .unlock = SlipResourceHost_Unlock,
                                                 .release = SlipResourceHost_Release};

static bool SlipSavedGamesHost_Path(const char *name, char path[SAVED_PATH_CAPACITY]) {
	char *const preferencePath = SlipHostFile_PreferencePath(name);
	if (preferencePath == NULL)
		return false;
	const bool fits = SDL_strlcpy(path, preferencePath, SAVED_PATH_CAPACITY) < SAVED_PATH_CAPACITY;
	SDL_free(preferencePath);
	return fits;
}

void SlipSavedGamesHost_ImportLegacySave(const char *resourcePath) {
	char *const destination = SlipHostFile_PreferencePath("SLIPSTRM.SAV");
	if (destination == NULL)
		return;
	SDL_PathInfo info;
	if (SDL_GetPathInfo(destination, &info)) {
		SDL_free(destination);
		return;
	}
	const char *slash = strrchr(resourcePath, '/');
	const char *const backslash = strrchr(resourcePath, '\\');
	if (backslash != NULL && (slash == NULL || backslash > slash))
		slash = backslash;
	char *source = NULL;
	SDL_asprintf(&source, "%.*sSLIPSTRM.SAV", slash != NULL ? (int)(slash + 1 - resourcePath) : 0, resourcePath);
	size_t bytes = 0;
	void *const data = source != NULL ? SDL_LoadFile(source, &bytes) : NULL;
	SDL_free(source);
	if (data != NULL) {
		FILE *const file = SlipHostFile_OpenStream(destination, "wbx");
		if (file != NULL) {
			const bool written = fwrite(data, 1, bytes, file) == bytes;
			const bool closed = fclose(file) == 0;
			if (!written || !closed) {
				SDL_RemovePath(destination);
				fprintf(stderr, "Could not import existing saved games into the preferences folder.\n");
			}
		}
		SDL_free(data);
	}
	SDL_free(destination);
}

/* Native filesystem binding: store saved games in the application's preferences folder. */
static bool SlipSavedGamesHost_LoadFile(void *context, const char *name, uint16_t *resource) {
	(void)context;
	char path[SAVED_PATH_CAPACITY];
	if (!SlipSavedGamesHost_Path(name, path))
		return false;
	FILE *const file = SlipHostFile_OpenStream(path, "rb");
	if (file == NULL)
		return false;
	bool ok = fseek(file, 0, SEEK_END) == 0;
	const long bytes = ok ? ftell(file) : -1;
	if (bytes < 0 || fseek(file, 0, SEEK_SET) != 0 || !SlipResourceHost_Allocate(NULL, (uint32_t)bytes, 0, resource)) {
		fclose(file);
		return false;
	}
	uint8_t *const data = SlipResourceHost_LockWritable(NULL, *resource);
	ok = fread(data, 1, (size_t)bytes, file) == (size_t)bytes;
	SlipResourceHost_Unlock(NULL, *resource);
	if (fclose(file) != 0)
		ok = false;
	if (!ok)
		SlipResourceHost_Release(NULL, *resource);
	return ok;
}

static SlipChampionshipSaveDirectory *SlipSavedGamesHost_LockDirectory(void *context, uint16_t resource) {
	return (SlipChampionshipSaveDirectory *)SlipResourceHost_LockWritable(context, resource);
}

static char *SlipSavedGamesHost_LockNames(void *context, uint16_t resource) {
	return (char *)SlipResourceHost_LockWritable(context, resource);
}

static SlipInputPointerPosition SlipSavedGamesHost_Pointer(void *context) {
	(void)context;
	return SlipInput_Pointer();
}

static void SlipSavedGames_Initialize(void) {
	if (!SlipResourceHost_LoadSequence(NULL, "RES_GL*.SPR", 1, SLIP_SAVE_SLOT_COUNT, saved.labels))
		SlipGame_FileFailure();
	char pattern[] = "RES_G0*.SPR";

	enum { SLIP_SAVE_FRAME_DIGIT_OFFSET = sizeof("RES_G") - 1 };

	for (unsigned frame = 0; frame < SLIP_SAVE_SLOT_LAST_FRAME; ++frame) {
		pattern[SLIP_SAVE_FRAME_DIGIT_OFFSET] = (char)('0' + frame);
		if (!SlipResourceHost_LoadSequence(NULL, pattern, 1, SLIP_SAVE_SLOT_COUNT, saved.frames[frame]))
			SlipGame_FileFailure();
	}
	for (unsigned slot = 0; slot < SLIP_SAVE_SLOT_COUNT; ++slot) {
		saved.animation.currentFrame[slot] = SLIP_SAVE_SLOT_LAST_FRAME;
		saved.animation.targetFrame[slot] = SLIP_SAVE_SLOT_LAST_FRAME;
	}
	saved.animation.remainingMilliseconds = SLIP_SAVE_SLOT_FRAME_PERIOD;
	if (!SlipResourceHost_Load(NULL, "RESGAMEZ.ZON", &saved.zones) ||
	    !SlipResourceHost_Load(NULL, "RES_GAME.SPR", &saved.background))
		SlipGame_FileFailure();
	SlipConfigHost_calls.palette(NULL, saved.background);
	SlipInput_SetNavigation(&savedGamesNavigation);
}

static void SlipSavedGames_Close(void) {
	SlipInput_ClearNavigation();
	SlipResourceHost_Release(NULL, saved.zones);
	SlipResourceHost_Release(NULL, saved.background);
	SlipResourceHost_ReleaseSequence(NULL, saved.labels, SLIP_SAVE_SLOT_COUNT);
	for (unsigned frame = 0; frame < SLIP_SAVE_SLOT_LAST_FRAME; ++frame)
		SlipResourceHost_ReleaseSequence(NULL, saved.frames[frame], SLIP_SAVE_SLOT_COUNT);
}

/* Typed host sprite view for the original locked handle. */
static void SlipSavedGamesHost_DrawSprite(uint16_t resource, bool positioned) {
	SlipResourceHost_Lock(NULL, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	SlipSprite sprite;
	if (!SlipSprite_FromPayload(&payload, &sprite))
		SlipGame_FileFailure();
	SlipSprite_DrawClipped(&sprite, g_framebuffer, SLIPSTREAM_SCREEN_WIDTH, positioned ? sprite.x : 0,
	                       positioned ? sprite.y : 0);
	SlipResourceHost_Unlock(NULL, resource);
}

static void SlipSavedGames_DrawSlot(uint16_t selection, uint16_t names) {
	SlipSavedGamesHost_DrawSprite(saved.labels[selection - 1], true);
	if (names != 0) {
		const char *text = (const char *)SlipResourceHost_Lock(NULL, names);
		for (uint16_t slotIndex = 1; slotIndex < selection; ++slotIndex)
			text += strlen(text) + 1;
		SlipTextPosition position = {0, SAVED_SLOT_NAME_Y};
		SlipText_Draw(&SlipText_state, text, NULL, &position);
		SlipResourceHost_Unlock(NULL, names);
	}
}

static void SlipSavedGames_Draw(void) {
	SlipSavedGamesHost_DrawSprite(saved.background, false);
	for (unsigned slot = 0; slot < SLIP_SAVE_SLOT_COUNT; ++slot) {
		const int16_t frame = saved.animation.currentFrame[slot];
		if (frame > 0)
			SlipSavedGamesHost_DrawSprite(saved.frames[frame - 1][slot], true);
	}
	SlipText_SetColor(&SlipText_state, SAVED_TEXT_COLOR);
	SlipText_SetStyle(&SlipText_state, SLIP_TEXT_CENTERED, UINT16_MAX, 0, SLIPSTREAM_SCREEN_WIDTH - 1);
	const char *const text = SlipStringTable_Get(saved.strings, SAVED_TAG_CANCEL, &strings);
	SlipTextPosition position = {0, SAVED_CANCEL_LABEL_Y};
	SlipText_Draw(&SlipText_state, text, NULL, &position);
	SlipStringTable_Unlock(saved.strings, &strings);
}

bool SlipSavedGames_Run(const char *resourcePath, SlipRaceRacerTable *racers, uint32_t *stage) {
	SlipConfigHost_calls.language(NULL);
	if (!SlipStringTable_Load(&SlipStringTable_state, "SAVED   ", &strings, &saved.strings))
		SlipGame_ResourceFailure();
	SlipConfigHost_calls.selectFont(NULL, SlipMenu_resources.smallFont);
	SlipSavedGames_Initialize();
	SlipChampionshipSaveDirectoryCalls files = {.context = (void *)resourcePath,
	                                            .load = SlipSavedGamesHost_LoadFile,
	                                            .lockDirectory = SlipSavedGamesHost_LockDirectory,
	                                            .allocate = SlipResourceHost_Allocate,
	                                            .lockNames = SlipSavedGamesHost_LockNames,
	                                            .unlock = SlipResourceHost_Unlock,
	                                            .release = SlipResourceHost_Release};
	SlipChampionshipSaveNames names = SlipChampionshipSave_LoadNames(&files);
	if (names.slotCount != 0 && names.slotCount != SLIP_SAVE_SLOT_COUNT)
		SlipGame_FileFailure();
	const SlipInputZoneCalls zones = {NULL, SlipResourceHost_Lock, SlipSavedGamesHost_Pointer, SlipResourceHost_Unlock};
	uint16_t selection = SAVED_CANCEL;
	SlipFrameTimer_Reset();
	for (;;) {
		SlipFrameTimer_Update((uint32_t)SlipSdl_TicksMs());
		SlipSaveSlotAnimation_Update(&saved.animation);
		SlipSavedGames_Draw();
		selection = SlipInput_Zone(saved.zones, &zones);
		if (names.resource != 0 && selection >= 1 && selection <= SLIP_SAVE_SLOT_COUNT)
			SlipSavedGames_DrawSlot(selection, names.resource);
		else {
			const uint32_t tag = names.resource == 0 ? SAVED_TAG_EMPTY : SAVED_TAG_CHOOSE;
			const char *const text = SlipStringTable_Get(saved.strings, tag, &strings);
			SlipText_DrawCentered(&SlipText_state, text, NULL, 0, SAVED_STATUS_TOP, SAVED_STATUS_BOTTOM);
			SlipStringTable_Unlock(saved.strings, &strings);
		}
		SlipMenu_PresentFrame();
		if (SlipInput_TestAndClear(SlipInput_pressed, SLIP_INPUT_SCAN_ESCAPE) || !SlipMenu_PollInput()) {
			selection = SAVED_CANCEL;
			break;
		}
		if (!SlipInput_TestAndClear(SlipInput_pressed, SLIP_INPUT_SCAN_ENTER) &&
		    !SlipInput_TestAndClear(SlipInput_pressed, SLIP_INPUT_MOUSE_LEFT))
			continue;
		if (names.resource == 0) {
			if (selection < SAVED_TOGGLE_FIRST || selection > SAVED_TOGGLE_LAST) {
				selection = SAVED_CANCEL;
				break;
			}
			saved.animation.targetFrame[selection - SAVED_TOGGLE_FIRST] ^= SLIP_SAVE_SLOT_LAST_FRAME;
		}
		if (selection < 1 || selection > SAVED_CANCEL)
			continue;
		if (selection == SAVED_CANCEL)
			break;
		if (!SlipChampionshipSave_Load((uint16_t)(selection - 1), racers, stage, &files))
			continue;
		saved.animation.targetFrame[selection - 1] = 0;
		bool animating;
		do {
			SlipFrameTimer_Update((uint32_t)SlipSdl_TicksMs());
			animating = SlipSaveSlotAnimation_Update(&saved.animation);
			SlipSavedGames_Draw();
			const char *const text = SlipStringTable_Get(saved.strings, SAVED_TAG_LOADING, &strings);
			SlipText_DrawCentered(&SlipText_state, text, NULL, 0, SAVED_STATUS_TOP, SAVED_STATUS_BOTTOM);
			SlipStringTable_Unlock(saved.strings, &strings);
			SlipMenu_PresentFrame();
		} while (animating);
		break;
	}
	SlipSavedGames_Close();
	if (names.resource != 0)
		SlipResourceHost_Release(NULL, names.resource);
	SlipStringTable_Release(saved.strings, &strings);
	return selection != SAVED_CANCEL;
}

typedef struct SlipSavedGamesFileHost {
	const SlipRaceRacerTable *racers;
	uint32_t stage;
	FILE *file;
	SlipChampionshipSaveDirectoryCalls directory;
} SlipSavedGamesFileHost;

static bool SlipSavedGamesHost_Open(void *context, const char *name, uint8_t mode, int32_t *file) {
	SlipSavedGamesFileHost *const host = context;
	char path[SAVED_PATH_CAPACITY];
	if (!SlipSavedGamesHost_Path(name, path))
		return false;
	host->file = SlipHostFile_OpenStream(path, mode == SLIP_SAVE_OPEN_READ ? "rb" : "r+b");
	*file = 0;
	return host->file != NULL;
}

static bool SlipSavedGamesHost_Create(void *context, const char *name, int32_t *file) {
	SlipSavedGamesFileHost *const host = context;
	char path[SAVED_PATH_CAPACITY];
	if (!SlipSavedGamesHost_Path(name, path))
		return false;
	host->file = SlipHostFile_OpenStream(path, "wb");
	*file = 0;
	return host->file != NULL;
}

static bool SlipSavedGamesHost_Write(void *context, int32_t file, int32_t offset, const void *data, uint16_t bytes) {
	(void)file;
	SlipSavedGamesFileHost *const host = context;
	if (offset != -1 && fseek(host->file, offset, SEEK_SET) != 0)
		return false;
	return fwrite(data, 1, bytes, host->file) == bytes;
}

static bool SlipSavedGamesHost_Close(void *context, int32_t file) {
	(void)file;
	SlipSavedGamesFileHost *const host = context;
	bool ok = fclose(host->file) == 0;
	host->file = NULL;
	return ok;
}

static bool SlipSavedGamesHost_Size(void *context, const char *name, uint32_t *bytes) {
	(void)context;
	char path[SAVED_PATH_CAPACITY];
	if (!SlipSavedGamesHost_Path(name, path))
		return false;
	return SlipFile_Size(path, bytes, &SlipResourceHost_fileCalls);
}

static bool SlipSavedGamesHost_Rewrite(void *context, const char *name, const SlipChampionshipSaveDirectory *data,
                                       uint32_t bytes) {
	(void)context;
	char path[SAVED_PATH_CAPACITY];
	if (!SlipSavedGamesHost_Path(name, path))
		return false;
	FILE *const file = SlipHostFile_OpenStream(path, "wb");
	if (file == NULL)
		return false;
	bool ok = fwrite(data, 1, bytes, file) == bytes;
	return fclose(file) == 0 && ok;
}

static SlipChampionshipSavePayload *SlipSavedGamesHost_LockPayload(void *context, uint16_t resource) {
	return (SlipChampionshipSavePayload *)SlipResourceHost_LockWritable(context, resource);
}

static const SlipChampionshipSavePayload *SlipSavedGamesHost_ReadPayload(void *context, uint16_t resource) {
	return SlipSavedGamesHost_LockPayload(context, resource);
}

static bool SlipSavedGamesHost_Pack(void *context, SlipChampionshipSavePayloadResult *result) {
	SlipSavedGamesFileHost *const host = context;
	const SlipChampionshipSavePayloadCalls calls = {NULL, SlipResourceHost_Allocate, SlipSavedGamesHost_LockPayload,
	                                                SlipResourceHost_Unlock};
	return SlipChampionshipSave_Pack(host->racers, host->stage, &calls, result);
}

static bool SlipSavedGames_Invalidate(void *context, uint16_t slot) {
	SlipSavedGamesFileHost *const host = context;
	const SlipChampionshipSaveDirectoryCalls *const calls = &host->directory;
	uint16_t resource;
	if (!calls->load(calls->context, "SLIPSTRM.SAV", &resource))
		return false;
	SlipChampionshipSaveDirectory *const directory = calls->lockDirectory(calls->context, resource);
	bool ok = directory->header.version == SLIP_SAVE_DIRECTORY_VERSION &&
	          (int16_t)slot < (int16_t)directory->header.slotCount;
	if (ok)
		directory->slots[slot].payloadOffset = 0;
	calls->unlock(calls->context, resource);
	calls->release(calls->context, resource);
	return ok;
}

static bool SlipSavedGames_Name(uint16_t selection, char name[SLIP_SAVE_NAME_BYTES]) {
	saved.animation.targetFrame[selection - 1] ^= SLIP_SAVE_SLOT_LAST_FRAME;
	name[0] = 0;
	bool cursorVisible = true;
	uint16_t cursor = 0, cursorMilliseconds = SAVED_CURSOR_BLINK_MS;
	SDL_HideCursor();
	SlipInput_SetBiosMode(1);
	SlipFrameTimer_Reset();
	bool accepted = false;
	for (;;) {
		SlipFrameTimer_Update((uint32_t)SlipSdl_TicksMs());
		SlipSaveSlotAnimation_Update(&saved.animation);
		SlipSavedGames_Draw();
		SlipSavedGames_DrawSlot(selection, 0);
		const char *const text = SlipStringTable_Get(saved.strings, SAVED_TAG_NAME, &strings);
		SlipTextPosition position = {0, SAVED_NAME_PROMPT_Y};
		SlipText_Draw(&SlipText_state, text, NULL, &position);
		SlipStringTable_Unlock(saved.strings, &strings);
		const uint16_t elapsed = (uint16_t)SlipFrameTimer_Values().deltaMilliseconds;
		bool borrow = cursorMilliseconds < elapsed;
		cursorMilliseconds = (uint16_t)(cursorMilliseconds - elapsed);
		if (borrow) {
			cursorMilliseconds = (uint16_t)(cursorMilliseconds + SAVED_CURSOR_BLINK_MS);
			cursorVisible = !cursorVisible;
		}
		const uint8_t character = SlipInput_ReadCharacter(SlipInput_pressed, SlipInput_held, &SlipInputBiosHost_calls);
		if (character != 0) {
			if (character == '\r') {
				accepted = true;
				break;
			}
			if (SlipText_FitsCharacter(&SlipText_state, name, character, SAVED_NAME_MAXIMUM_WIDTH))
				cursor = (uint16_t)SlipText_Edit(name, cursor, SLIP_SAVE_NAME_BYTES - 1, character);
		}
		SlipText_SetColor(&SlipText_state, SAVED_NAME_TEXT_COLOUR);
		SlipText_SetStyle(&SlipText_state, SLIP_TEXT_AT_POSITION, UINT16_MAX, SAVED_NAME_CLIP_LEFT,
		                  SAVED_NAME_CLIP_RIGHT);
		position = (SlipTextPosition){SAVED_NAME_LEFT, SAVED_NAME_TOP};
		SlipText_Draw(&SlipText_state, name, NULL, &position);
		if (cursorVisible) {
			const char replaced = name[cursor];
			name[cursor] = 0;
			const int16_t x = (int16_t)(SAVED_NAME_LEFT + SlipFont_MeasureText(&SlipText_state.font, name));
			name[cursor] = replaced;
			Raster_DrawLineClipped(SAVED_NAME_TEXT_COLOUR, x, SAVED_NAME_TOP, x, SAVED_NAME_CURSOR_BOTTOM);
		}
		SlipMenu_PresentFrame();
		if (!SlipMenu_PollInput() || SlipInput_TestAndClear(SlipInput_pressed, SLIP_INPUT_SCAN_ESCAPE))
			break;
	}
	SDL_ShowCursor();
	SlipInput_SetBiosMode(0);
	if (!accepted)
		saved.animation.targetFrame[selection - 1] ^= SLIP_SAVE_SLOT_LAST_FRAME;
	return accepted;
}

bool SlipSavedGames_Save(const char *resourcePath, const SlipRaceRacerTable *racers, uint32_t stage) {
	SlipConfigHost_calls.language(NULL);
	if (!SlipStringTable_Load(&SlipStringTable_state, "SAVED   ", &strings, &saved.strings))
		SlipGame_ResourceFailure();
	SlipConfigHost_calls.selectFont(NULL, SlipMenu_resources.smallFont);
	Raster_SetClipRect(0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1);
	SlipSavedGames_Initialize();
	SlipSavedGamesFileHost host = {.racers = racers,
	                               .stage = stage,
	                               .directory = {.context = (void *)resourcePath,
	                                             .load = SlipSavedGamesHost_LoadFile,
	                                             .lockDirectory = SlipSavedGamesHost_LockDirectory,
	                                             .allocate = SlipResourceHost_Allocate,
	                                             .lockNames = SlipSavedGamesHost_LockNames,
	                                             .unlock = SlipResourceHost_Unlock,
	                                             .release = SlipResourceHost_Release}};
	SlipChampionshipSaveNames names = SlipChampionshipSave_LoadNames(&host.directory);
	if (names.slotCount == 0) {
		if (!SlipResourceHost_Allocate(NULL, SLIP_SAVE_SLOT_COUNT * sizeof("[Unused Slot]") + 1, 0, &names.resource))
			SlipGame_MemoryFailure();
		char *destination = SlipSavedGamesHost_LockNames(NULL, names.resource);
		for (unsigned slotIndex = 0; slotIndex < SLIP_SAVE_SLOT_COUNT; ++slotIndex) {
			memcpy(destination, "[Unused Slot]", sizeof("[Unused Slot]"));
			destination += sizeof("[Unused Slot]");
		}
		*destination = 0;
		SlipResourceHost_Unlock(NULL, names.resource);
	} else if (names.slotCount != SLIP_SAVE_SLOT_COUNT)
		SlipGame_FileFailure();
	const SlipInputZoneCalls zones = {NULL, SlipResourceHost_Lock, SlipSavedGamesHost_Pointer, SlipResourceHost_Unlock};
	SlipFrameTimer_Reset();
	bool cancelled = true;
	for (;;) {
		SlipFrameTimer_Update((uint32_t)SlipSdl_TicksMs());
		SlipSaveSlotAnimation_Update(&saved.animation);
		SlipSavedGames_Draw();
		const uint16_t selection = SlipInput_Zone(saved.zones, &zones);
		if (selection >= 1 && selection <= SLIP_SAVE_SLOT_COUNT)
			SlipSavedGames_DrawSlot(selection, names.resource);
		else {
			const char *const text = SlipStringTable_Get(saved.strings, SAVED_TAG_CHOOSE_SAVE, &strings);
			SlipText_DrawCentered(&SlipText_state, text, NULL, 0, SAVED_STATUS_TOP, SAVED_STATUS_BOTTOM);
			SlipStringTable_Unlock(saved.strings, &strings);
		}
		SlipText_SetStyle(&SlipText_state, SLIP_TEXT_CENTERED, UINT16_MAX, SAVED_NAME_CLIP_LEFT, SAVED_NAME_CLIP_RIGHT);
		SlipText_SetColor(&SlipText_state, SAVED_TEXT_COLOR);
		SlipMenu_PresentFrame();
		if (!SlipMenu_PollInput())
			break;
		if (!SlipInput_TestAndClear(SlipInput_pressed, SLIP_INPUT_SCAN_ENTER) &&
		    !SlipInput_TestAndClear(SlipInput_pressed, SLIP_INPUT_MOUSE_LEFT))
			continue;
		if (selection == 0 || selection >= SAVED_TOGGLE_SELECTION_BASE)
			continue;
		if (selection == SAVED_CANCEL)
			break;
		char name[SLIP_SAVE_NAME_BYTES] = {0};
		if (!SlipSavedGames_Name(selection, name))
			continue;
		SlipChampionshipSaveStoreCalls store = {.file = {&host, SlipSavedGamesHost_Open, SlipSavedGamesHost_Create,
		                                                 SlipSavedGamesHost_Write, SlipSavedGamesHost_Close},
		                                        .directory = host.directory,
		                                        .context = &host,
		                                        .invalidate = SlipSavedGames_Invalidate,
		                                        .pack = SlipSavedGamesHost_Pack,
		                                        .size = SlipSavedGamesHost_Size,
		                                        .lockPayload = SlipSavedGamesHost_ReadPayload,
		                                        .rewrite = SlipSavedGamesHost_Rewrite};
		(void)SlipChampionshipSave_Store((uint16_t)(selection - 1), name, &store);
		cancelled = false;
		break;
	}
	SlipResourceHost_Release(NULL, names.resource);
	SlipSavedGames_Close();
	SlipStringTable_Release(saved.strings, &strings);
	return cancelled;
}
