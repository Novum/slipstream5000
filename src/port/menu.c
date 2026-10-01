#include "menu.h"
#include "byte_order.h"
#include "config_controls.h"
#include "config_menu_host.h"
#include "config_settings.h"
#include "config_state_file.h"
#include "input_bios_host.h"
#include "input_navigation.h"
#include "material_host.h"
#include "menu_music.h"
#include "menu_resources.h"
#include "renderer_host.h"
#include "renderer_projection.h"
#include "renderer_state.h"
#include "screen_present_host.h"

#include "championship_screen.h"
#include "draw3d.h"
#include "font.h"
#include "frame_timer.h"
#include "game_errors.h"
#include "input.h"
#include "port_app_bridge.h"
#include "presenter.h"
#include "race_championship.h"
#include "race_hud.h"
#include "race_intro.h"
#include "race_player.h"
#include "race_records_host.h"
#include "race_results_host.h"
#include "race_session.h"
#include "raster.h"
#include "resource.h"
#include "resource_host.h"
#include "runtime.h"
#include "saved_games.h"
#include "shape3d.h"
#include "shape_format.h"
#include "sprite.h"
#include "sprite_effects.h"
#include "sprite_resource_host.h"
#include "string_table.h"
#include "text_layout.h"
#include "timed_values.h"
#include "track_view_render.h"
#include "vehicle_select.h"
#include "vehicle_selector_host.h"
#include "vga_dac.h"
#include "view3d.h"

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t g_palette[256];

static const char *kDefaultResPath = "E:\\SteamLibrary\\steamapps\\common\\Slipstream 5000\\SLIPSTRM.RES";

static SlipGameSoundState *menuSound;
static SlipSoundEffectLock menuSoundLock;
static SlipSoundEffectUnlock menuSoundUnlock;
static void *menuSoundContext;
static bool menuSelectActive;
static uint16_t menuSelect;
static SlipResourcePayload menuSelectPayload;
static uint32_t menuSelectVoice;

void SlipMenu_BindSoundHost(SlipGameSoundState *sound, SlipSoundEffectLock lock, SlipSoundEffectUnlock unlock,
                            void *context) {
	menuSound = sound;
	menuSoundLock = lock;
	menuSoundUnlock = unlock;
	menuSoundContext = context;
}

static void SlipMenuSound_Play(void) {
	if (!menuSelectActive || menuSelect == 0 || menuSound == NULL)
		return;
	if (menuSoundLock != NULL && !menuSoundLock(menuSoundContext))
		return;
	menuSelectVoice = SlipGameSound_Play(menuSound, menuSelectPayload.data, (uint32_t)menuSelectPayload.size);
	if (menuSoundUnlock != NULL)
		menuSoundUnlock(menuSoundContext);
}

static void SlipMenuSound_Release(void) {
	if (!menuSelectActive)
		return;
	menuSelectActive = false;
	if (menuSelect == 0)
		return;
	if (menuSoundLock != NULL && !menuSoundLock(menuSoundContext))
		return;
	if (menuSound != NULL)
		SlipGameSound_Stop(menuSound, menuSelectVoice);
	SlipResourceHost_Unlock(NULL, menuSelect);
	SlipResourceHost_Release(NULL, menuSelect);
	menuSelect = 0;
	if (menuSoundUnlock != NULL)
		menuSoundUnlock(menuSoundContext);
}

typedef struct MenuButton {
	const char *normalSpriteName;
	const char *highlightSpriteName;
	int x;
	int y;
	int width;
	int height;
} MenuButton;

typedef struct MenuPage {
	const char *title;
	const char *labelTags[6];
	char labels[6][64];
	int buttonCount;
	bool disabled[6];
} MenuPage;

typedef struct SpriteButton {
	const char *normalSpriteName;
	const char *highlightSpriteName;
	int x;
	int y;
	int width;
	int height;
} SpriteButton;

typedef struct GaragePricedItem {
	const char *name;
	int cost[3];
	int load;
} GaragePricedItem;

enum {
	kMaxMenuButtons = 6,
	kDriverCount = 10,
	kDriverActionCount = 3,
	kGarageActionCount = 4,
	kGarageWeaponPodActionCount = 3,
	kGarageWeaponGridActionCount = 12,
	kGarageTurboActionCount = 6,
	kGarageSystemsActionCount = 4,
	kGarageStatusZoomComplete = 0x4000,
	kGarageStatusZoomRate = 0x8000,
	kGarageStatusZoomFractionBits = 14,
	kGarageStatusZoomCenterX = 217,
	kGarageStatusZoomCenterY = 118,
	kGarageStatusPanelX = 137,
	kGarageStatusPanelY = 83,
	kGarageStatusPanelWidth = 162,
	kGarageStatusPanelHeight = 107,
	kGarageAffordablePriceColor = 0xfc,
	kGarageUnaffordablePriceColor = 0xfd,
	kGarageBorderLeftColor = 0x25,
	kGarageBorderTopColor = 0x2b,
	kGarageBorderBottomRightColor = 0x0a
};

typedef enum GaragePanelId {
	GARAGE_PANEL_NONE = -1,
	GARAGE_PANEL_WEAPON_PODS,
	GARAGE_PANEL_WEAPON_GRID,
	GARAGE_PANEL_TURBO,
	GARAGE_PANEL_SYSTEMS
} GaragePanelId;

typedef enum MenuPageId {
	MENU_PAGE_TOP,
	MENU_PAGE_ONE_PLAYER,
	MENU_PAGE_TWO_PLAYER,
	MENU_PAGE_TWO_PLAYER_RACE,
	MENU_PAGE_COUNT
} MenuPageId;

typedef enum OnePlayerRaceType {
	ONE_PLAYER_RACE_PRACTICE,
	ONE_PLAYER_RACE_SINGLE,
	ONE_PLAYER_RACE_CHAMPIONSHIP
} OnePlayerRaceType;

typedef enum MainMenuDispatchResult {
	MAIN_MENU_DISPATCH_RESTART = 0,
	MAIN_MENU_DISPATCH_RACE_SETUP = 1,
	MAIN_MENU_DISPATCH_SHOWCASE = 2,
	MAIN_MENU_DISPATCH_LOAD_GAME = 3,
	MAIN_MENU_DISPATCH_CONFIGURATION = 4
} MainMenuDispatchResult;

typedef enum AppMode {
	APP_MODE_MENU,
	APP_MODE_VEHICLE_SELECT,
	APP_MODE_VEHICLE_TO_DRIVER,
	APP_MODE_DRIVER_SELECT,
	APP_MODE_VEHICLE_VIEW,
	APP_MODE_TRACK_SELECT,
	APP_MODE_GARAGE,
	APP_MODE_RACE,
	APP_MODE_RESULTS
} AppMode;

static SlipRaceResultsScreen resultsScreen;

static bool returnToResultsAfterReplay;

typedef enum FocusSource { FOCUS_NONE, FOCUS_KEYBOARD, FOCUS_MOUSE } FocusSource;

typedef enum VehicleDriverTransitionPhase {
	VEHICLE_DRIVER_TRANSITION_FADE,
	VEHICLE_DRIVER_TRANSITION_BACKGROUND,
	VEHICLE_DRIVER_TRANSITION_ZOOM
} VehicleDriverTransitionPhase;

static int g_selectedDriver;
static uint16_t g_playerOneDriver;
static uint16_t g_playerTwoDriver;
static uint16_t g_excludedDriver;
static OnePlayerRaceType g_selectedRaceType;
static int g_selectedTrack;
static uint16_t g_trackHover;
static uint16_t g_trackGlobeGrow;

static bool g_sdlQuitRequested;
static bool g_mouseDriverPresent = true;
static const char *g_mainMenuResPath;
static SDL_Window *g_mainMenuWindow;
static SDL_Renderer *g_mainMenuRenderer;
static SlipMenuSdlPresentFrame g_mainMenuPresentFrame;
static void *g_mainMenuPresentFrameContext;
static int32_t SlipMainMenu_hiddenToggle;
static int32_t SlipMainMenu_demoSelection;
static int g_hoveredVehicle = -1;
static FocusSource g_vehicleFocus = FOCUS_NONE;
static SlipVehicleSelector vehicleResources = {
    .actionRects = {{208, 138, 257, 147}, {208, 150, 257, 159}, {208, 162, 257, 171}, {69, 24, 113, 74}}};
static SlipVehicleSelectorCalls vehicleResourceCalls;
static SlipVehicleSelectorHost vehicleResourceHost;
static bool vehicleResourcesActive;
static uint64_t g_screenStartMs;

enum {
	VEHICLE_FADE_COMPLETE = 0x4000,
	VEHICLE_FADE_RATE = 0x8000,
	VEHICLE_FADE_FRACTION_BITS = 14,
	DRIVER_ZOOM_COMPLETE = 0x4000,
	DRIVER_ZOOM_STEP = 0x400,
	DRIVER_CARD_X = 27,
	DRIVER_CARD_Y = 10
};

static int g_transitionDriver;
static int g_selectedDriverAction;
static int g_selectedGarageAction;
static int g_selectedGaragePanelItem;
static int g_selectedGarageWeaponPod;

static SlipRaceRacerState *g_garageRacer;
static int g_garageCash;
static int g_garageWeaponSlots[2];
static int g_garageWeaponLoads[2];
static int g_garageTurbo;
static uint32_t g_garageSystems;
static uint8_t *g_garageStatusPanel;
static uint16_t g_garageStatusCopy, g_garageStatusOriginal;
static uint16_t g_garageWeapons, g_garageTurboPanel, g_garageSystemsPanel;
static uint16_t g_garageFont, g_garageResultsFont, g_garageWeaponFont;
static uint16_t g_garageBackground, g_garageInactiveBackground, g_garageMarker;
static SlipStringTableSlot *g_garageStrings;
static uint16_t g_garageStatusZoom, g_garageStatusZoomTarget;
static int g_hoveredDriverAction = -1;

static int g_hoveredTrack = -1;
static int g_hoveredGarageAction = -1;
static int g_hoveredGaragePanelItem = -1;
static int g_garagePanel = -1;
static uint32_t g_vehicleZoomScale;
static uint16_t g_vehicleGrayMix;
static VehicleDriverTransitionPhase g_vehicleDriverTransitionPhase;

static FocusSource g_trackFocus = FOCUS_NONE;
static FocusSource g_garageActionFocus = FOCUS_NONE;
static FocusSource g_garagePanelFocus = FOCUS_NONE;
static AppMode g_trackReturnMode = APP_MODE_DRIVER_SELECT;
static char g_vehicleTitleLabel[64];
static char g_vehiclePlayerLabels[2][64];
char g_driverDescriptions[kDriverCount][512];
static char g_driverActionLabels[kDriverActionCount][64];
static char g_trackTitleLabel[64];
static char g_garageActionLabels[kGarageActionCount][64];
static char g_garageWeaponPodLabels[kGarageWeaponPodActionCount][64];

const VehicleViewParams g_vehicleViewParams[kDriverCount] = {
    {14640, 0, -4096, -17}, {14152, 0, -4096, -17}, {15128, 0, -4096, -17}, {13664, 0, -4096, -17},
    {15616, 0, -4096, -17}, {13176, 0, -4096, -17}, {17080, 0, -3072, -18}, {15616, 0, -4096, -20},
    {17568, 0, -4096, -17}, {12200, 0, -4096, -17}};

const char *g_trackResourceNames[kDriverCount] = {"CHICAGO", "HAWAII", "TOKYO",  "NORWAY", "CAVE",
                                                  "CAN",     "AMAZON", "LONDON", "EGYPT",  "NEWYORK"};

const uint8_t g_trackMenuToResourceIndex[kDriverCount] = {5, 0, 6, 7, 3, 8, 4, 1, 2, 9};

static const char *g_trackLabels[kDriverCount] = {"Arizona", "Chicago", "Amazon", "London", "Norway",
                                                  "Egypt",   "France",  "Hawaii", "Tokyo",  "New York"};

const SlipView3DVec32 g_trackFirstSpawnRecords[kDriverCount] = {
    {4582847, 1012426, 1567396}, {3682271, 1080070, 4728334}, {4937029, 8281, 4844685},  {1708968, 12087, 4985545},
    {4695002, 1079880, 4558001}, {4364242, 1086182, 6066072}, {3599152, 27783, 5704161}, {3204053, 1582791, 8776302},
    {2858526, 857513, 4863366},  {4754313, 14503, 8991532}};

const uint16_t g_trackPlayerStartRecordIndices[kDriverCount] = {0, 5, 1, 6, 2, 7, 3, 8, 4, 9};

const int16_t g_trackGlobeFlagCoords[kDriverCount][2] = {{-90, 43},  {-160, 25}, {152, 44}, {14, 66}, {5, 54},
                                                         {-105, 39}, {-59, 1},   {0, 59},   {22, 33}, {-76, 43}};

const int16_t g_trackGlobeOrientation[kDriverCount][6] = {
    {-5307, -3864, -15012, 15491, -782, -5275}, {5368, -3077, -15171, 15342, -1078, 5647},
    {15123, 1551, 6108, -6109, 7504, 13221},    {-15227, 1, 6046, -5106, -8772, -12861},
    {-15227, 1, 6046, -5106, -8772, -12861},    {-10173, 8, -12842, 10343, -9706, -8200},
    {-14602, 1464, -7284, 6162, -6590, -13676}, {-15189, 4139, 4536, -6100, -8766, -12425},
    {-15636, 51, 4889, -4841, -2441, -15460},   {-10173, 8, -12842, 10343, -9706, -8200}};

static const GaragePricedItem g_garageSystemsItems[kGarageSystemsActionCount - 1] = {
    {"Charger", {300, 300, 300}, 0}, {"Targetter", {500, 500, 500}, 0}, {"Loader", {700, 700, 700}, 0}};

static MenuButton g_menuButtons[kMaxMenuButtons] = {
    {"MAINBT_1.SPR", "MAINBTH1.SPR", 0, 0, 0, 0}, {"MAINBT_2.SPR", "MAINBTH2.SPR", 0, 0, 0, 0},
    {"MAINBT_3.SPR", "MAINBTH3.SPR", 0, 0, 0, 0}, {"MAINBT_4.SPR", "MAINBTH4.SPR", 0, 0, 0, 0},
    {"MAINBT_5.SPR", "MAINBTH5.SPR", 0, 0, 0, 0}, {"MAINBT_6.SPR", "MAINBTH6.SPR", 0, 0, 0, 0}};

/* User-requested port UI exception: Serial, Modem and IPX are unavailable. */
static MenuPage g_menuPages[MENU_PAGE_COUNT] = {
    {"Main Menu", {"OPT1", "OPT2", "OPT3", "OPT4", "OPT5", NULL}, {{0}}, 5, {false}},
    {"One Player", {"OP11", "OP12", "OP13", NULL}, {{0}}, 3, {false}},
    {"Two Players", {"LNK1", "LNK2", "LNK3", "LNK4", NULL}, {{0}}, 4, {false, true, true, true}},
    {"Two Player Race", {"OP21", "OP22", NULL}, {{0}}, 2, {false}}};

static SpriteButton g_trackButtons[kDriverCount] = {
    {"TRKBT_0.SPR", "TRKBTH0.SPR", 0, 0, 0, 0}, {"TRKBT_1.SPR", "TRKBTH1.SPR", 0, 0, 0, 0},
    {"TRKBT_2.SPR", "TRKBTH2.SPR", 0, 0, 0, 0}, {"TRKBT_3.SPR", "TRKBTH3.SPR", 0, 0, 0, 0},
    {"TRKBT_4.SPR", "TRKBTH4.SPR", 0, 0, 0, 0}, {"TRKBT_5.SPR", "TRKBTH5.SPR", 0, 0, 0, 0},
    {"TRKBT_6.SPR", "TRKBTH6.SPR", 0, 0, 0, 0}, {"TRKBT_7.SPR", "TRKBTH7.SPR", 0, 0, 0, 0},
    {"TRKBT_8.SPR", "TRKBTH8.SPR", 0, 0, 0, 0}, {"TRKBT_9.SPR", "TRKBTH9.SPR", 0, 0, 0, 0}};

static SpriteButton g_trackTitlePanel = {"CH_TRACK.SPR", NULL, 0, 0, 0, 0};

static SpriteButton g_driverActionButtons[kDriverActionCount] = {
    {"DRIVER0A.SPR", NULL, 0, 0, 0, 0}, {"DRIVER0B.SPR", NULL, 0, 0, 0, 0}, {"DRIVER0C.SPR", NULL, 0, 0, 0, 0}};

static SpriteButton g_garageActionButtons[kGarageActionCount] = {{NULL, NULL, 13, 83, 101, 23},
                                                                 {NULL, NULL, 13, 111, 101, 23},
                                                                 {NULL, NULL, 13, 139, 101, 23},
                                                                 {NULL, NULL, 13, 167, 101, 23}};

static SpriteButton g_garageWeaponTitleButton = {NULL, NULL, 13, 83, 101, 23};

static SpriteButton g_garageWeaponPodButtons[kGarageWeaponPodActionCount] = {
    {NULL, NULL, 13, 111, 101, 23}, {NULL, NULL, 13, 139, 101, 23}, {NULL, NULL, 13, 167, 101, 23}};

static SpriteButton g_garageWeaponGridButtons[kGarageWeaponGridActionCount] = {
    {NULL, NULL, 24, 99, 61, 24},   {NULL, NULL, 92, 99, 61, 24},   {NULL, NULL, 160, 99, 61, 24},
    {NULL, NULL, 228, 99, 61, 24},  {NULL, NULL, 24, 129, 61, 24},  {NULL, NULL, 92, 129, 61, 24},
    {NULL, NULL, 160, 129, 61, 24}, {NULL, NULL, 228, 129, 61, 24}, {NULL, NULL, 24, 159, 61, 24},
    {NULL, NULL, 92, 159, 61, 24},  {NULL, NULL, 160, 159, 61, 24}, {NULL, NULL, 228, 159, 61, 24}};

static SpriteButton g_garageTurboButtons[kGarageTurboActionCount] = {
    {NULL, NULL, 23, 101, 83, 38}, {NULL, NULL, 113, 101, 83, 38}, {NULL, NULL, 203, 101, 83, 38},
    {NULL, NULL, 23, 144, 83, 38}, {NULL, NULL, 113, 144, 83, 38}, {NULL, NULL, 203, 144, 83, 38}};

static SpriteButton g_garageSystemsButtons[kGarageSystemsActionCount] = {{NULL, NULL, 24, 112, 61, 54},
                                                                         {NULL, NULL, 92, 112, 61, 54},
                                                                         {NULL, NULL, 160, 112, 61, 54},
                                                                         {NULL, NULL, 228, 112, 61, 54}};

static int g_garageTurboMarkerCenters[kGarageTurboActionCount][2] = {{64, 119}, {154, 119}, {244, 119},
                                                                     {64, 162}, {154, 162}, {244, 162}};

static int g_garageSystemsMarkerCenters[kGarageSystemsActionCount - 1][2] = {{54, 138}, {122, 138}, {190, 138}};

static bool SlipMenu_FileExists(const char *path) {
	FILE *const fp = fopen(path, "rb");
	if (fp == NULL) {
		return false;
	}
	fclose(fp);
	return true;
}

static const char *SlipMenu_FindResPathInternal(int argc, char **argv) {
	const char *envPath;

	if (argc > 1 && SlipMenu_FileExists(argv[1])) {
		return argv[1];
	}

	envPath = getenv("SLIPSTREAM5000_RES");
	if (envPath != NULL && SlipMenu_FileExists(envPath)) {
		return envPath;
	}

	if (SlipMenu_FileExists("SLIPSTRM.RES")) {
		return "SLIPSTRM.RES";
	}

	if (SlipMenu_FileExists(kDefaultResPath)) {
		return kDefaultResPath;
	}

	return NULL;
}

bool SlipMenu_DrawSpriteFromRes(const char *resPath, const char *name, bool applyPalette) {
	SlipResourcePayload payload;
	SlipSprite sprite;
	bool ok;

	ok = SlipResource_LoadByName(&resPath, 1u, name, &payload) != 0;
	if (!ok) {
		fprintf(stderr, "Could not load resource %s\n", name);
		return false;
	}

	ok = SlipSprite_FromPayload(&payload, &sprite) != 0;
	if (!ok) {
		fprintf(stderr, "Could not parse sprite %s\n", name);
		return false;
	}

	if (applyPalette) {
		SlipSprite_ApplyPalette(&sprite);
	}
	SlipSprite_Draw(&sprite, g_framebuffer, SLIPSTREAM_SCREEN_WIDTH, sprite.x, sprite.y);
	return true;
}

static bool SlipMenu_DrawSpriteFromResAt(const char *resPath, const char *name, int x, int y, bool applyPalette) {
	SlipResourcePayload payload;
	SlipSprite sprite;
	bool ok;

	ok = SlipResource_LoadByName(&resPath, 1u, name, &payload) != 0;
	if (!ok) {
		fprintf(stderr, "Could not load resource %s\n", name);
		return false;
	}

	ok = SlipSprite_FromPayload(&payload, &sprite) != 0;
	if (!ok) {
		fprintf(stderr, "Could not parse sprite %s\n", name);
		return false;
	}

	if (applyPalette) {
		SlipSprite_ApplyPalette(&sprite);
	}
	SlipSprite_Draw(&sprite, g_framebuffer, SLIPSTREAM_SCREEN_WIDTH, x, y);
	return true;
}

bool SlipMenu_ApplyPaletteResource(const char *const *archives, size_t archiveCount, const char *name) {
	SlipResourcePayload payload = {0};
	uint16_t start;
	uint16_t count;

	if (!SlipResource_LoadByName(archives, archiveCount, name, &payload) || payload.size < 4u) {
		return false;
	}

	start = SlipBytes_ReadLE16(payload.data);
	count = SlipBytes_ReadLE16(payload.data + 2u);
	if ((size_t)start + count > 256u || payload.size - 4u < (size_t)count * 3u) {
		return false;
	}

	SlipVgaDac_WriteRange(start, count, payload.data + 4u);
	return true;
}

static bool SlipMenu_LoadMenuButtonRect(const char *resPath, MenuButton *button) {
	SlipResourcePayload payload;
	SlipSprite sprite;
	bool ok;

	ok = SlipResource_LoadByName(&resPath, 1u, button->normalSpriteName, &payload) != 0;
	if (!ok) {
		return false;
	}

	ok = SlipSprite_FromPayload(&payload, &sprite) != 0;
	if (ok) {
		button->x = sprite.x;
		button->y = sprite.y;
		button->width = sprite.width;
		button->height = sprite.height;
	}

	return ok;
}

static bool SlipMenu_LoadSpriteButtonRect(const char *resPath, SpriteButton *button) {
	SlipResourcePayload payload;
	SlipSprite sprite;
	bool ok;

	ok = SlipResource_LoadByName(&resPath, 1u, button->normalSpriteName, &payload) != 0;
	if (!ok) {
		return false;
	}

	ok = SlipSprite_FromPayload(&payload, &sprite) != 0;
	if (ok) {
		button->x = sprite.x;
		button->y = sprite.y;
		button->width = sprite.width;
		button->height = sprite.height;
	}

	return ok;
}

static MenuPageId g_menuBakedPage = (MenuPageId)-1;
static const SlipStringTableResources menuStringResources = {.load = SlipResourceHost_Load,
                                                             .lock = SlipResourceHost_Lock,
                                                             .unlock = SlipResourceHost_Unlock,
                                                             .release = SlipResourceHost_Release};

/* Host metadata inspection for UI descriptors. The translated screen entry
 * owns its string slot; inspecting labels must not allocate arena resources. */
static bool SlipMenu_InspectStringMetadata(const char *resPath, const char name[8], SlipResourcePayload *payload) {
	char filename[13] = "        .ST0";
	memcpy(filename, name, 8);
	filename[11] = (char)(uint8_t)(SlipConfig_Language() + '0');
	return SlipResource_LoadByName(&resPath, 1u, filename, payload) != 0;
}

bool SlipMenu_LoadMainMenuModel(const char *resPath) {
	SlipResourcePayload strings;
	SlipResourcePayload viewCarStrings;
	SlipResourcePayload vehicleSelectStrings;
	SlipResourcePayload trackStrings;
	SlipResourcePayload garageStrings;
	bool ok;
	int itemIndex;
	int page;

	ok = SlipMenu_InspectStringMetadata(resPath, "MAINMENU", &strings);
	if (!ok) {
		fprintf(stderr, "Could not inspect MAINMENU strings\n");
		return false;
	}

	for (page = 0; page < MENU_PAGE_COUNT; ++page) {
		for (itemIndex = 0; itemIndex < g_menuPages[page].buttonCount; ++itemIndex) {
			const char *const tag = g_menuPages[page].labelTags[itemIndex];
			if (!SlipStringTable_FindText(&strings, tag, g_menuPages[page].labels[itemIndex],
			                              sizeof(g_menuPages[page].labels[itemIndex]))) {
				snprintf(g_menuPages[page].labels[itemIndex], sizeof(g_menuPages[page].labels[itemIndex]), "%s", tag);
			}
		}
	}

	if (SlipMenu_InspectStringMetadata(resPath, "VIEWCAR ", &viewCarStrings)) {
		for (itemIndex = 0; itemIndex < kDriverCount; ++itemIndex) {
			char tag[5];
			snprintf(tag, sizeof(tag), "CAR%d", itemIndex);
			if (!SlipStringTable_FindText(&viewCarStrings, tag, g_driverDescriptions[itemIndex],
			                              sizeof(g_driverDescriptions[itemIndex]))) {
				g_driverDescriptions[itemIndex][0] = '\0';
			}
		}
	}

	if (SlipMenu_InspectStringMetadata(resPath, "CH_TEAM ", &vehicleSelectStrings)) {
		static const char *actionTags[kDriverActionCount] = {"OPT1", "OPT2", "OPT3"};
		for (itemIndex = 0; itemIndex < kDriverActionCount; ++itemIndex) {
			if (!SlipStringTable_FindText(&vehicleSelectStrings, actionTags[itemIndex], g_driverActionLabels[itemIndex],
			                              sizeof(g_driverActionLabels[itemIndex]))) {
				snprintf(g_driverActionLabels[itemIndex], sizeof(g_driverActionLabels[itemIndex]), "%s",
				         actionTags[itemIndex]);
			}
		}
		if (!SlipStringTable_FindText(&vehicleSelectStrings, "TITL", g_vehicleTitleLabel,
		                              sizeof(g_vehicleTitleLabel))) {
			snprintf(g_vehicleTitleLabel, sizeof(g_vehicleTitleLabel), "SELECT YOUR VEHICLE");
		}
		if (!SlipStringTable_FindText(&vehicleSelectStrings, "PLY1", g_vehiclePlayerLabels[0],
		                              sizeof(g_vehiclePlayerLabels[0]))) {
			snprintf(g_vehiclePlayerLabels[0], sizeof(g_vehiclePlayerLabels[0]), "PLAYER ONE");
		}
		if (!SlipStringTable_FindText(&vehicleSelectStrings, "PLY2", g_vehiclePlayerLabels[1],
		                              sizeof(g_vehiclePlayerLabels[1]))) {
			snprintf(g_vehiclePlayerLabels[1], sizeof(g_vehiclePlayerLabels[1]), "PLAYER TWO");
		}
	} else {
		snprintf(g_driverActionLabels[0], sizeof(g_driverActionLabels[0]), "Accept");
		snprintf(g_driverActionLabels[1], sizeof(g_driverActionLabels[1]), "Cancel");
		snprintf(g_driverActionLabels[2], sizeof(g_driverActionLabels[2]), "View");
		snprintf(g_vehicleTitleLabel, sizeof(g_vehicleTitleLabel), "SELECT YOUR VEHICLE");
		snprintf(g_vehiclePlayerLabels[0], sizeof(g_vehiclePlayerLabels[0]), "PLAYER ONE");
		snprintf(g_vehiclePlayerLabels[1], sizeof(g_vehiclePlayerLabels[1]), "PLAYER TWO");
	}

	if (SlipMenu_InspectStringMetadata(resPath, "CHTRACK ", &trackStrings)) {
		if (!SlipStringTable_FindText(&trackStrings, "TITL", g_trackTitleLabel, sizeof(g_trackTitleLabel))) {
			snprintf(g_trackTitleLabel, sizeof(g_trackTitleLabel), "Choose Track");
		}
	} else {
		snprintf(g_trackTitleLabel, sizeof(g_trackTitleLabel), "Choose Track");
	}

	if (SlipMenu_InspectStringMetadata(resPath, "GARAGE  ", &garageStrings)) {
		static const char *garageTags[kGarageActionCount] = {"BUT1", "BUT2", "BUT3", "BUT4"};
		for (itemIndex = 0; itemIndex < kGarageActionCount; ++itemIndex) {
			if (!SlipStringTable_FindText(&garageStrings, garageTags[itemIndex], g_garageActionLabels[itemIndex],
			                              sizeof(g_garageActionLabels[itemIndex]))) {
				snprintf(g_garageActionLabels[itemIndex], sizeof(g_garageActionLabels[itemIndex]), "%s",
				         garageTags[itemIndex]);
			}
		}
		if (!SlipStringTable_FindText(&garageStrings, "WEP1", g_garageWeaponPodLabels[0],
		                              sizeof(g_garageWeaponPodLabels[0]))) {
			snprintf(g_garageWeaponPodLabels[0], sizeof(g_garageWeaponPodLabels[0]), "Left Pod");
		}
		if (!SlipStringTable_FindText(&garageStrings, "WEP2", g_garageWeaponPodLabels[1],
		                              sizeof(g_garageWeaponPodLabels[1]))) {
			snprintf(g_garageWeaponPodLabels[1], sizeof(g_garageWeaponPodLabels[1]), "Right Pod");
		}
		if (!SlipStringTable_FindText(&garageStrings, "REP3", g_garageWeaponPodLabels[2],
		                              sizeof(g_garageWeaponPodLabels[2]))) {
			snprintf(g_garageWeaponPodLabels[2], sizeof(g_garageWeaponPodLabels[2]), "Ok");
		}
	} else {
		snprintf(g_garageActionLabels[0], sizeof(g_garageActionLabels[0]), "Weapons");
		snprintf(g_garageActionLabels[1], sizeof(g_garageActionLabels[1]), "Turbo");
		snprintf(g_garageActionLabels[2], sizeof(g_garageActionLabels[2]), "Systems");
		snprintf(g_garageActionLabels[3], sizeof(g_garageActionLabels[3]), "Start Race");
		snprintf(g_garageWeaponPodLabels[0], sizeof(g_garageWeaponPodLabels[0]), "Left Pod");
		snprintf(g_garageWeaponPodLabels[1], sizeof(g_garageWeaponPodLabels[1]), "Right Pod");
		snprintf(g_garageWeaponPodLabels[2], sizeof(g_garageWeaponPodLabels[2]), "Ok");
	}

	for (itemIndex = 0; itemIndex < kMaxMenuButtons; ++itemIndex) {
		if (!SlipMenu_LoadMenuButtonRect(resPath, &g_menuButtons[itemIndex])) {
			fprintf(stderr, "Could not inspect button sprite %s\n", g_menuButtons[itemIndex].normalSpriteName);
			return false;
		}
	}
	for (itemIndex = 0; itemIndex < kDriverActionCount; ++itemIndex) {
		if (!SlipMenu_LoadSpriteButtonRect(resPath, &g_driverActionButtons[itemIndex])) {
			fprintf(stderr, "Could not inspect driver action sprite %s\n",
			        g_driverActionButtons[itemIndex].normalSpriteName);
			return false;
		}
	}
	for (itemIndex = 0; itemIndex < kDriverCount; ++itemIndex) {
		if (!SlipMenu_LoadSpriteButtonRect(resPath, &g_trackButtons[itemIndex])) {
			fprintf(stderr, "Could not inspect track button sprite %s\n", g_trackButtons[itemIndex].normalSpriteName);
			return false;
		}
	}
	if (!SlipMenu_LoadSpriteButtonRect(resPath, &g_trackTitlePanel)) {
		fprintf(stderr, "Could not inspect track title sprite %s\n", g_trackTitlePanel.normalSpriteName);
		return false;
	}

	return true;
}

void SlipMenu_MakeDriverSpriteName(char *dst, size_t dstSize, const char *prefix, int driver, const char *suffix) {
	snprintf(dst, dstSize, "%s%d%s", prefix, driver, suffix);
}

size_t SlipMenu_BuildArchiveList(const char *resPath, char secondaryPath[512], const char *archives[2]) {
	const char *slash;
	const char *backslash;
	const char *end;
	size_t dirLen;

	archives[0] = resPath;
	archives[1] = NULL;
	if (resPath == NULL) {
		return 0;
	}

	slash = strrchr(resPath, '/');
	backslash = strrchr(resPath, '\\');
	end = slash > backslash ? slash : backslash;
	if (end == NULL) {
		snprintf(secondaryPath, 512, "SLIPCD.RES");
	} else {
		dirLen = (size_t)(end - resPath) + 1u;
		if (dirLen >= 512) {
			return 1;
		}
		memcpy(secondaryPath, resPath, dirLen);
		snprintf(secondaryPath + dirLen, 512 - dirLen, "SLIPCD.RES");
	}

	if (SlipMenu_FileExists(secondaryPath)) {
		archives[1] = secondaryPath;
		return 2;
	}
	return 1;
}

static void SlipMenu_DrawSpriteButtonLabelRow(const SlipFont *font, const SpriteButton *button, const char *label,
                                              int color, int row) {
	const int textWidth = SlipFont_MeasureText(font, label);
	const int x = button->x + (button->width - textWidth) / 2;

	SlipFont_DrawText(font, g_framebuffer, SLIPSTREAM_SCREEN_WIDTH, x, button->y + row, label, color);
}

static bool SlipMenu_DrawVehicleResource(uint16_t resource, int x, int y, bool resourcePosition, bool applyPalette) {
	if (SlipResourceHost_Lock(NULL, resource) == NULL)
		return false;
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	SlipSprite sprite;
	bool ok = SlipSprite_FromPayload(&payload, &sprite) != 0;
	if (ok) {
		if (applyPalette)
			SlipSprite_ApplyPalette(&sprite);
		SlipSprite_DrawClipped(&sprite, g_framebuffer, SLIPSTREAM_SCREEN_WIDTH, resourcePosition ? sprite.x : x,
		                       resourcePosition ? sprite.y : y);
	}
	SlipResourceHost_Unlock(NULL, resource);
	return ok;
}

static bool SlipMenu_ApplyVehicleDriverFadePalette(const char *resPath, uint16_t mix) {
	(void)resPath;
	const uint16_t resource = vehicleResources.assets.background;
	if (SlipResourceHost_Lock(NULL, resource) == NULL)
		return false;
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	SlipSprite sprite;
	if (!SlipSprite_FromPayload(&payload, &sprite) || sprite.paletteOffset == 0 ||
	    (size_t)sprite.paletteOffset + 4u > payload.size) {
		SlipResourceHost_Unlock(NULL, resource);
		return false;
	}
	const uint8_t *const packet = payload.data + sprite.paletteOffset;
	const uint16_t count = SlipBytes_ReadLE16(packet + 2u);
	const size_t bytes = (size_t)count * 3u + 4u;
	if (bytes > sizeof(vehicleResources.sourcePalette) || payload.size - sprite.paletteOffset < bytes) {
		SlipResourceHost_Unlock(NULL, resource);
		return false;
	}
	memcpy(vehicleResources.sourcePalette, packet, bytes);
	memcpy(vehicleResources.grayPalette, packet, bytes);
	SlipResourceHost_Unlock(NULL, resource);
	uint8_t *rgb = vehicleResources.grayPalette + 4u;
	for (unsigned paletteEntry = 0; paletteEntry < count; ++paletteEntry) {
		uint8_t gray = (uint8_t)(((uint16_t)rgb[0] * 0x55u) >> 8);
		gray = (uint8_t)(gray + (((uint16_t)rgb[1] * 0x55u) >> 8));
		gray = (uint8_t)(gray + (((uint16_t)rgb[2] * 0x55u) >> 8));
		rgb[0] = rgb[1] = rgb[2] = gray;
		rgb += 3;
	}

	const uint16_t blended = SlipPalette_Blend(vehicleResources.sourcePalette, vehicleResources.grayPalette,
	                                           (int16_t)mix, &SlipSpriteHost_effectResources);
	const uint8_t *const blendedData = SlipResourceHost_Lock(NULL, blended);
	SlipVgaDac_WriteRange(SlipBytes_ReadLE16(blendedData), SlipBytes_ReadLE16(blendedData + 2u), blendedData + 4u);
	SlipResourceHost_Unlock(NULL, blended);
	SlipResourceHost_Release(NULL, blended);
	return true;
}

static bool SlipMenu_ApplyVehicleDriverBackdropPalette(void) {
	const uint16_t resource = vehicleResources.assets.driverBackground;
	if (SlipResourceHost_Lock(NULL, resource) == NULL)
		return false;
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	SlipSprite backdrop;
	bool ok = SlipSprite_FromPayload(&payload, &backdrop) != 0;
	if (ok)
		SlipSprite_ApplyPalette(&backdrop);
	SlipResourceHost_Unlock(NULL, resource);
	return ok;
}

static bool SlipMenu_DrawVehicleDriverBackdrop(void) {
	return SlipMenu_DrawVehicleResource(vehicleResources.assets.driverBackground, 0, 0, false, false);
}

#define MENU_FADE_STEPS 0x46u
#define MENU_FADE_RATE 0x46u

typedef struct MenuFade {
	int32_t fadeValue;
	uint64_t hostStartMs;
	uint32_t hostTicksApplied;
	uint32_t hostRate;
} MenuFade;

static MenuFade g_menuFade;
static SlipTimedValues menuTimedValues;
static int32_t *menuTimedValueOutputs[1];
static int g_menuFadeOutSelection = -1;

/* Native frame-clock service for the original timer callback registration. */
static bool SlipMenu_RegisterTimedClock(void *context, uint32_t rate) {
	MenuFade *const clock = context;
	clock->hostRate = rate;
	clock->hostStartMs = SlipSdl_TicksMs();
	clock->hostTicksApplied = 0;
	return true;
}

static void SlipMenu_RemoveTimedClock(void *context) { ((MenuFade *)context)->hostRate = 0; }

static const SlipTimedValueTimerCalls menuTimedValueTimer = {&g_menuFade, SlipMenu_RegisterTimedClock,
                                                             SlipMenu_RemoveTimedClock};

static void SlipMenuFade_Start(int32_t from, int32_t to, uint32_t ticks, uint32_t mode) {
	SlipTimedValues_Start(&menuTimedValues, 0x3d434, &g_menuFade.fadeValue, from, to, ticks, mode);
}

static void SlipMenuFade_Tick(void) { SlipTimedValues_Tick(&menuTimedValues); }

static uint16_t g_menuButtonCopies[kMaxMenuButtons][2];
static uint16_t g_mainMenuBackground;
static uint16_t g_mainMenuFont;
static SlipStringTableSlot *g_mainMenuStrings;

static SlipFont SlipMenu_LockFont(void *context, uint16_t resource) {
	SlipResourceHost_Lock(context, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	SlipFont font;
	if (!SlipFont_FromPayload(&payload, &font))
		SlipGame_ResourceFailure();
	return font;
}

static const SlipFontResourceCalls mainMenuFontCalls = {.lock = SlipMenu_LockFont, .unlock = SlipResourceHost_Unlock};

static void SlipMenu_BakeResourceSprite(uint16_t resource, const char *text, int16_t row) {
	SlipText_SetColor(&SlipText_state, UINT16_MAX);
	SlipResourceHost_Lock(NULL, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	SlipSprite sprite;
	if (!SlipSprite_FromPayload(&payload, &sprite))
		SlipGame_ResourceFailure();
	SlipResourceHost_Unlock(NULL, resource);
	SlipResourceHost_LockWritable(NULL, resource);
	payload = SlipResourceHost_Payload(resource);
	if (!SlipSprite_FromPayload(&payload, &sprite))
		SlipGame_ResourceFailure();
	RasterSurfaceBinding saved;
	Raster_BindSprite((uint8_t *)sprite.pixels, sprite.width, sprite.height, &saved);
	SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, 0, (int16_t)(sprite.width - 1));
	SlipTextPosition position = {0, row};
	SlipText_Draw(&SlipText_state, text, NULL, &position);
	SlipResourceHost_Unlock(NULL, resource);
	Raster_RestoreScreen(&saved);
}

static bool SlipMenu_PageSetup(const char *resPath, MenuPageId pageId) {
	(void)resPath;
	const MenuPage *const page = &g_menuPages[pageId];
	SlipStringTable_SetLanguage(&SlipStringTable_state, (uint8_t)SlipConfig_Language());
	if (!SlipStringTable_Load(&SlipStringTable_state, "MAINMENU", &menuStringResources, &g_mainMenuStrings) ||
	    !SlipResourceHost_Load(NULL, "MENUFONT.FNT", &g_mainMenuFont))
		SlipGame_ResourceFailure();
	SlipText_SelectResourceFont(&SlipText_state, g_mainMenuFont, &mainMenuFontCalls);
	SlipText_SetColor(&SlipText_state, UINT16_MAX);
	if (!SlipResourceHost_Load(NULL, "MAINMENU.SPR", &g_mainMenuBackground))
		SlipGame_ResourceFailure();
	SlipResourceHost_Lock(NULL, g_mainMenuBackground);
	SlipResourcePayload background = SlipResourceHost_Payload(g_mainMenuBackground);
	SlipSprite sprite;
	if (!SlipSprite_FromPayload(&background, &sprite))
		SlipGame_ResourceFailure();
	SlipSprite_ApplyPalette(&sprite);
	SlipResourceHost_Unlock(NULL, g_mainMenuBackground);
	g_menuBakedPage = pageId;

	for (int buttonIndex = 0; buttonIndex < page->buttonCount; ++buttonIndex) {
		MenuButton *const button = &g_menuButtons[buttonIndex];
		const char *names[2] = {button->normalSpriteName, button->highlightSpriteName};
		for (int variant = 0; variant < 2; ++variant) {
			uint16_t source;
			if (!SlipResourceHost_Load(NULL, names[variant], &source))
				SlipGame_ResourceFailure();
			uint16_t copy = source;
			if (!SlipResourceHost_Copy(NULL, &copy))
				SlipGame_ResourceFailure();
			SlipResourceHost_Release(NULL, source);
			g_menuButtonCopies[buttonIndex][variant] = copy;
		}
		const uint16_t normal = g_menuButtonCopies[buttonIndex][0];
		SlipResourceHost_Lock(NULL, normal);
		SlipResourcePayload payload = SlipResourceHost_Payload(normal);
		if (!SlipSprite_FromPayload(&payload, &sprite))
			SlipGame_ResourceFailure();
		button->x = sprite.x;
		button->y = sprite.y;
		button->width = sprite.width;
		button->height = sprite.height;
		SlipResourceHost_Unlock(NULL, normal);
		const uint8_t *const tag = (const uint8_t *)page->labelTags[buttonIndex];
		const uint32_t label = (uint32_t)tag[0] << 24 | (uint32_t)tag[1] << 16 | (uint32_t)tag[2] << 8 | tag[3];
		const char *const text = SlipStringTable_Get(g_mainMenuStrings, label, &menuStringResources);
		SlipMenu_BakeResourceSprite(normal, text, 2);
		SlipMenu_BakeResourceSprite(g_menuButtonCopies[buttonIndex][1], text, 2);
		SlipStringTable_Unlock(g_mainMenuStrings, &menuStringResources);
	}
	if (pageId == MENU_PAGE_TWO_PLAYER) {

		uint16_t blocker;
		if (!SlipResourceHost_Load(NULL, "CNFBLOCK.SPR", &blocker))
			SlipGame_ResourceFailure();
		SlipResourceHost_Lock(NULL, blocker);
		SlipResourcePayload blockerPayload = SlipResourceHost_Payload(blocker);
		SlipSprite blockerSprite;
		if (!SlipSprite_FromPayload(&blockerPayload, &blockerSprite))
			SlipGame_ResourceFailure();
		for (int buttonIndex = 0; buttonIndex < page->buttonCount; ++buttonIndex) {
			if (!page->disabled[buttonIndex])
				continue;
			const uint16_t normal = g_menuButtonCopies[buttonIndex][0];
			SlipResourceHost_Lock(NULL, normal);
			SlipResourcePayload payload = SlipResourceHost_Payload(normal);
			SlipSprite sprite;
			if (!SlipSprite_FromPayload(&payload, &sprite))
				SlipGame_ResourceFailure();
			RasterSurfaceBinding saved;
			Raster_BindSprite((uint8_t *)sprite.pixels, sprite.width, sprite.height, &saved);
			SlipSprite_DrawClipped(&blockerSprite, (uint8_t *)sprite.pixels, sprite.width, 0, 0);
			Raster_RestoreScreen(&saved);
			SlipResourceHost_Unlock(NULL, normal);
		}
		SlipResourceHost_Unlock(NULL, blocker);
		SlipResourceHost_Release(NULL, blocker);
	}
	return true;
}

static bool SlipMainMenu_DrawFrame(const char *resPath, MenuPageId pageId, int hoveredButton) {
	const MenuPage *const page = &g_menuPages[pageId];
	const uint32_t hostTicks = (uint32_t)((SlipSdl_TicksMs() - g_menuFade.hostStartMs) * 0x1234dcu /
	                                      (1000u * (0x1234dcu / g_menuFade.hostRate)));
	int32_t level;
	int buttonIndex;

	while (g_menuFade.hostTicksApplied < hostTicks) {
		SlipMenuFade_Tick();
		++g_menuFade.hostTicksApplied;
	}
	level = g_menuFade.fadeValue;

	Raster_Clear(0, sizeof(g_framebuffer));
	Raster_SetClipRect(0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1);

	(void)resPath;
	SlipResourceHost_Lock(NULL, g_mainMenuBackground);
	SlipResourcePayload background = SlipResourceHost_Payload(g_mainMenuBackground);
	SlipSprite backgroundSprite;
	if (!SlipSprite_FromPayload(&background, &backgroundSprite))
		SlipGame_ResourceFailure();
	SlipSprite_DrawClipped(&backgroundSprite, g_framebuffer, SLIPSTREAM_SCREEN_WIDTH, backgroundSprite.x,
	                       backgroundSprite.y);
	SlipResourceHost_Unlock(NULL, g_mainMenuBackground);

	if (SlipMainMenu_hiddenToggle != 0) {
		enum { CHEAT_MESSAGE_COLOR = 0xff, CHEAT_MESSAGE_CENTERED_MODE = 2, CHEAT_MESSAGE_Y = 190 };

		static const char cheatMessage[] = "Cheating will get you everywhere.";
		SlipText_SelectResourceFont(&SlipText_state, SlipMenu_resources.smallFont, &mainMenuFontCalls);
		SlipText_SetColor(&SlipText_state, CHEAT_MESSAGE_COLOR);
		SlipText_SetStyle(&SlipText_state, CHEAT_MESSAGE_CENTERED_MODE, UINT16_MAX, 0, SLIPSTREAM_SCREEN_WIDTH - 1);
		SlipTextPosition position = {0, CHEAT_MESSAGE_Y};
		SlipText_Draw(&SlipText_state, cheatMessage, NULL, &position);
	}

	for (buttonIndex = 0; buttonIndex < page->buttonCount; ++buttonIndex) {
		SlipResourcePayload copyPayload;
		SlipSprite sprite;
		bool selected = g_menuFadeOutSelection == buttonIndex;

		const int variant = !page->disabled[buttonIndex] && (buttonIndex == hoveredButton || selected) ? 1 : 0;
		uint32_t buttonLevel = (uint32_t)level;

		if (g_menuFadeOutSelection >= 0 && selected) {
			buttonLevel = 0xffffu;
		}
		const uint16_t resource = g_menuButtonCopies[buttonIndex][variant];
		SlipResourceHost_Lock(NULL, resource);
		copyPayload = SlipResourceHost_Payload(resource);
		if (!SlipSprite_FromPayload(&copyPayload, &sprite))
			SlipGame_ResourceFailure();
		SlipSprite_DrawDissolve(&sprite, g_framebuffer, SLIPSTREAM_SCREEN_WIDTH, sprite.x, sprite.y,
		                        (uint16_t)buttonLevel);
		SlipResourceHost_Unlock(NULL, resource);
	}

	return true;
}

static int SlipMenu_FocusedItemIndex(FocusSource focus, int selectedItem, int hoveredItem) {
	if (focus == FOCUS_MOUSE) {
		return hoveredItem;
	}
	if (focus == FOCUS_KEYBOARD) {
		return selectedItem;
	}
	return -1;
}

static void SlipMenu_VehicleDoorAnimationBegin(void) {
	vehicleResources.animation.doorElapsed = 0;
	SlipFrameTimer_Reset();
}

static int SlipMenu_VehicleZoneAt(const SlipResourcePayload *zones, int x, int y) {
	const uint8_t *data;
	uint16_t rows;
	uint16_t offset;
	size_t spanOffset;

	if (zones == NULL || zones->data == NULL || zones->size < 2 || y < 0) {
		return -1;
	}

	data = zones->data;
	rows = SlipBytes_ReadLE16(data);
	if (y >= (int)rows || zones->size < 2u + (size_t)rows * 2u) {
		return -1;
	}

	offset = SlipBytes_ReadLE16(data + 2u + (size_t)y * 2u);
	if (offset >= zones->size) {
		return -1;
	}

	spanOffset = offset;
	while (spanOffset + 3u <= zones->size) {
		const int zoneId = data[spanOffset];
		const int spanX = SlipBytes_ReadLE16(data + spanOffset + 1u);
		const int vehicle = zoneId == 0 ? -1 : zoneId - 1;

		spanOffset += 3u;
		if (spanX == 0xffff || x < spanX) {
			return vehicle >= 0 && vehicle < kDriverCount ? vehicle : -1;
		}
	}

	return -1;
}

static int SlipMenu_HitTestVehicleZone(const char *resPath, int x, int y) {
	SlipResourcePayload zones;
	int hit = -1;

	(void)resPath;
	const uint16_t resource = vehicleResources.zones;
	if (SlipResourceHost_Lock(NULL, resource) != NULL) {
		zones = SlipResourceHost_Payload(resource);
		hit = SlipMenu_VehicleZoneAt(&zones, x, y);
		SlipResourceHost_Unlock(NULL, resource);
	}

	if (hit + 1 == g_excludedDriver)
		hit = -1;

	return hit;
}

static bool SlipMenu_DrawVehicleSelect(const char *resPath, bool confirmed) {
	SlipFrameTimer_Update((uint32_t)SlipSdl_TicksMs());

	SlipVehicleSelection_Update(&vehicleResources.animation, SlipRace_gameMode, g_excludedDriver);

	if (!confirmed) {
		SlipInputPointerPosition pointer = SlipInput_Pointer();
		const int activeVehicle = SlipMenu_HitTestVehicleZone(resPath, pointer.x, pointer.y);
		vehicleResources.animation.selectedVehicle = (uint16_t)(activeVehicle + 1);
	}

	SlipVehicleSelector_Draw(&vehicleResources, &vehicleResourceCalls);

	return true;
}

static int SlipMenu_HitTestDriverAction(int x, int y);

static bool driverCardResourcesActive;
static SlipSelectorCardSetupResult driverCardSetupExit;

static bool SlipMenu_BakeDriverCardLabels(const char *resPath, int driver) {
	(void)resPath;
	(void)driver;
	if (driverCardResourcesActive)
		return true;
	driverCardSetupExit = SlipVehicleSelector_CardSetup(&vehicleResources, &vehicleResourceCalls,
	                                                    SlipResourceHost_Modify, SlipResourceHost_ModifyReturnedSI);
	if (driverCardSetupExit.modify.exit == SLIP_RESOURCE_MODIFY_DISPLACED_RETURN) {
		SlipResourceHost_ReportModifyContinuation(
		    driverCardSetupExit.continuationReturnAddress, driverCardSetupExit.continuationOperand,
		    driverCardSetupExit.continuationReturnAddress == 0x45d71 ? vehicleResources.grayPalette : NULL,
		    driverCardSetupExit.modify);
		return false;
	}
	driverCardResourcesActive = true;
	return true;
}

static bool SlipMenu_DrawDriverSelect(const char *resPath, int driver) {
	(void)resPath;
	(void)driver;
	void *const context = vehicleResourceCalls.context;
	vehicleResourceCalls.drawSprite(context, vehicleResources.assets.driverBackground, 0, 0);
	vehicleResourceCalls.drawSprite(context, vehicleResources.card, 27, 10);
	g_hoveredDriverAction = SlipMenu_HitTestDriverAction(SlipInput_Pointer().x, SlipInput_Pointer().y);
	if (g_hoveredDriverAction >= 0 && g_hoveredDriverAction < kDriverActionCount)
		vehicleResourceCalls.drawSprite(context, vehicleResources.hover[g_hoveredDriverAction], 0x7fff, 0);
	return true;
}

static bool SlipMenu_DrawVehicleToDriverTransition(const char *resPath, int driver) {

	if (g_vehicleDriverTransitionPhase == VEHICLE_DRIVER_TRANSITION_FADE) {
		uint16_t frameStep;
		uint32_t fadeStep;

		if (!SlipMenu_DrawVehicleSelect(resPath, true)) {
			return false;
		}
		if (!SlipMenu_ApplyVehicleDriverFadePalette(resPath, g_vehicleGrayMix)) {
			return false;
		}

		frameStep = (uint16_t)SlipFrameTimer_Values().stepQ14;
		fadeStep = (uint16_t)(((uint32_t)frameStep * VEHICLE_FADE_RATE) >> VEHICLE_FADE_FRACTION_BITS);
		if ((int16_t)g_vehicleGrayMix < VEHICLE_FADE_COMPLETE) {
			g_vehicleGrayMix = (uint16_t)(g_vehicleGrayMix + fadeStep);
			if ((int16_t)g_vehicleGrayMix > VEHICLE_FADE_COMPLETE)
				g_vehicleGrayMix = VEHICLE_FADE_COMPLETE;
		}
		if (g_vehicleGrayMix == VEHICLE_FADE_COMPLETE) {
			g_vehicleDriverTransitionPhase = VEHICLE_DRIVER_TRANSITION_BACKGROUND;
		}

		return true;
	}

	if (g_vehicleDriverTransitionPhase == VEHICLE_DRIVER_TRANSITION_BACKGROUND) {
		if (!SlipMenu_BakeDriverCardLabels(resPath, driver))
			return false;
		vehicleResourceCalls.drawSprite(vehicleResourceCalls.context, vehicleResources.assets.driverBackground, 0, 0);
		g_vehicleDriverTransitionPhase = VEHICLE_DRIVER_TRANSITION_ZOOM;
		return true;
	}

	if (g_vehicleZoomScale == 0) {
		vehicleResourceCalls.drawSprite(vehicleResourceCalls.context, vehicleResources.assets.driverBackground, 0, 0);
		vehicleResourceCalls.setNavigation(vehicleResourceCalls.context, &SlipVehicleSelector_driverNavigation);
		RasterSurfaceBounds bounds = Raster_GetSurfaceBounds();
		Raster_SetClipRect((int16_t)bounds.left, (int16_t)bounds.top, (int16_t)bounds.right, (int16_t)bounds.bottom);
	}
	vehicleResourceCalls.zoom(vehicleResourceCalls.context, vehicleResources.card, (int16_t)g_vehicleZoomScale,
	                          vehicleResources.zoomCenter, (SlipSelectorPoint){27, 10});
	g_vehicleZoomScale += DRIVER_ZOOM_STEP;
	return true;
}

bool SlipMenu_DrawVehiclePanel(const char *resPath, int driver) {
	char spriteName[32];

	SlipMenu_MakeDriverSpriteName(spriteName, sizeof(spriteName), "VIEWCAR", driver, ".SPR");
	if (!SlipMenu_DrawSpriteFromRes(resPath, spriteName, true)) {
		return false;
	}
	return true;
}

typedef struct VehicleArtActor VehicleArtActor;
typedef struct VehicleArtSlot VehicleArtSlot;

static uint16_t SlipMenu_TrackSelectResourceTrack(uint16_t menuTrackOneBased) {

	if (menuTrackOneBased == 0 || menuTrackOneBased > kDriverCount) {
		return 0;
	}
	return (uint16_t)(g_trackMenuToResourceIndex[menuTrackOneBased - 1u] + 1u);
}

static uint16_t trackButtonResources[2][SLIP_CONFIG_TRACK_COUNT];
static uint16_t trackBlockerResource;
static uint16_t trackFontResource;
static uint16_t trackTitleResource;
static SlipStringTableSlot *trackMenuStrings;
static uint32_t SlipMenu_TrackLimit(void);

static void SlipMenu_TrackButtonSetup(void) {

	if (!SlipResourceHost_Load(NULL, "STARFONT.FNT", &trackFontResource))
		SlipGame_ResourceFailure();
	SlipText_SelectResourceFont(&SlipText_state, trackFontResource, &mainMenuFontCalls);
	SlipText_SetColor(&SlipText_state, UINT16_MAX);
	SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, 0, SLIPSTREAM_SCREEN_WIDTH - 1);
	SlipStringTable_SetLanguage(&SlipStringTable_state, (uint8_t)SlipConfig_Language());
	if (!SlipStringTable_Load(&SlipStringTable_state, "CHTRACK ", &menuStringResources, &trackMenuStrings))
		SlipGame_ResourceFailure();
	if (!SlipResourceHost_Load(NULL, "CHTRACKB.SPR", &trackBlockerResource))
		SlipGame_ResourceFailure();
	if (!SlipResourceHost_Load(NULL, "CH_TRACK.SPR", &trackTitleResource))
		SlipGame_ResourceFailure();
	SlipResourceModifyResult title = SlipResourceHost_Modify(NULL, trackTitleResource);

	if (title.exit == SLIP_RESOURCE_MODIFY_DISPLACED_RETURN)
		SlipRuntime_Fatal("DOS ResModify bug: malformed failure continuation at 0005ae71 is unsupported");
	trackTitleResource = SlipResourceHost_ModifyReturnedSI(title);
	const char *const titleText = SlipStringTable_Get(trackMenuStrings, 0x5449544c, &menuStringResources);
	SlipMenu_BakeResourceSprite(trackTitleResource, titleText, 3);
	SlipStringTable_Unlock(trackMenuStrings, &menuStringResources);

	for (unsigned variant = 0; variant < 2; ++variant) {
		for (unsigned trackIndex = 0; trackIndex < SLIP_CONFIG_TRACK_COUNT; ++trackIndex) {
			uint16_t resource;
			const char *const name = variant == 0 ? g_trackButtons[trackIndex].normalSpriteName
			                                      : g_trackButtons[trackIndex].highlightSpriteName;
			if (!SlipResourceHost_Load(NULL, name, &resource))
				SlipGame_ResourceFailure();
			SlipResourceModifyResult modified = SlipResourceHost_Modify(NULL, resource);

			if (modified.exit == SLIP_RESOURCE_MODIFY_DISPLACED_RETURN)
				SlipRuntime_Fatal("DOS ResModify bug: malformed failure continuation at 0005aee0 is unsupported");
			resource = SlipResourceHost_ModifyReturnedSI(modified);
			trackButtonResources[variant][trackIndex] = resource;

			SlipResourceHost_Lock(NULL, resource);
			SlipResourcePayload payload = SlipResourceHost_Payload(resource);
			SlipSprite sprite;
			if (!SlipSprite_FromPayload(&payload, &sprite))
				SlipGame_ResourceFailure();
			g_trackButtons[trackIndex].x = sprite.x;
			g_trackButtons[trackIndex].y = sprite.y;
			g_trackButtons[trackIndex].width = sprite.width;
			g_trackButtons[trackIndex].height = sprite.height;
			SlipResourceHost_Unlock(NULL, resource);
			SlipMenu_BakeResourceSprite(resource, g_trackLabels[trackIndex], 3);
		}
	}

	const uint32_t firstBlocked = SlipMenu_TrackLimit();
	const uint32_t blockedCount = SLIP_CONFIG_TRACK_COUNT - firstBlocked;
	if (blockedCount != 0) {
		for (unsigned variant = 0; variant < 2; ++variant) {
			uint32_t remaining = blockedCount;
			uint32_t trackIndex = firstBlocked;
			do {
				const uint16_t resource = trackButtonResources[variant][trackIndex];
				SlipResourceHost_Lock(NULL, resource);
				SlipResourcePayload payload = SlipResourceHost_Payload(resource);
				SlipSprite sprite;
				if (!SlipSprite_FromPayload(&payload, &sprite))
					SlipGame_ResourceFailure();
				SlipResourceHost_Unlock(NULL, resource);
				SlipResourceHost_LockWritable(NULL, resource);
				payload = SlipResourceHost_Payload(resource);
				if (!SlipSprite_FromPayload(&payload, &sprite))
					SlipGame_ResourceFailure();
				RasterSurfaceBinding saved;
				Raster_BindSprite((uint8_t *)sprite.pixels, sprite.width, sprite.height, &saved);
				SlipResourceHost_Lock(NULL, trackBlockerResource);
				SlipResourcePayload blockerPayload = SlipResourceHost_Payload(trackBlockerResource);
				SlipSprite blocker;
				if (!SlipSprite_FromPayload(&blockerPayload, &blocker))
					SlipGame_ResourceFailure();
				SlipSprite_DrawClipped(&blocker, g_screenBufferBase, g_screenPitch, 0, 0);
				SlipResourceHost_Unlock(NULL, trackBlockerResource);
				SlipResourceHost_Unlock(NULL, resource);
				Raster_RestoreScreen(&saved);
				++trackIndex;
			} while (--remaining != 0);
		}
	}
}

static void SlipMenu_TrackButtonRelease(void) {
	for (unsigned variant = 0; variant < 2; ++variant)
		for (unsigned trackIndex = 0; trackIndex < SLIP_CONFIG_TRACK_COUNT; ++trackIndex)
			SlipResourceHost_Release(NULL, trackButtonResources[variant][trackIndex]);
	SlipResourceHost_Release(NULL, trackBlockerResource);
	SlipResourceHost_Release(NULL, trackTitleResource);
	SlipResourceHost_Release(NULL, trackFontResource);
	SlipStringTable_Release(trackMenuStrings, &menuStringResources);
}

static bool SlipMenu_DrawTrackSelect(const char *resPath) {
	const uint16_t resourceTrack = SlipMenu_TrackSelectResourceTrack(g_trackHover);
	int trackIndex;

	Raster_Clear(0, sizeof(g_framebuffer));
	Raster_SetClipRect(0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1);

	if (!SlipMenu_DrawSpriteFromRes(resPath, "STARS.SPR", true)) {
		return false;
	}
	SlipTrackGlobe_Draw(resPath, resourceTrack, g_trackGlobeGrow);
	if (!SlipMenu_DrawVehicleResource(trackTitleResource, 0, 0, true, false)) {
		return false;
	}

	for (trackIndex = 0; trackIndex < kDriverCount; ++trackIndex) {
		const unsigned variant = trackIndex + 1 == g_trackHover ? 1u : 0u;
		if (!SlipMenu_DrawVehicleResource(trackButtonResources[variant][trackIndex], 0, 0, true, false)) {
			return false;
		}
	}

	return true;
}

static bool SlipMenu_DrawTrackSelectFrame(const char *resPath, int selectedTrack, int hoveredTrack, FocusSource focus) {
	uint16_t menuTrackOneBased;
	uint32_t growStep;
	bool drewFrame;

	TrackView_TrackGlobeFrameTimerUpdate();
	if (g_trackGlobeGrow != 0x4000u) {
		growStep = 0x3000u * (uint16_t)SlipFrameTimer_Step();
		g_trackGlobeGrow = (uint16_t)(g_trackGlobeGrow + (growStep >> 14));
		if ((int16_t)g_trackGlobeGrow > 0x4000) {
			g_trackGlobeGrow = 0x4000u;
		}
	}

	menuTrackOneBased = (uint16_t)(SlipMenu_FocusedItemIndex(focus, selectedTrack, hoveredTrack) + 1);
	if (menuTrackOneBased != g_trackHover) {
		g_trackHover = menuTrackOneBased;
		g_trackGlobeGrow = 0;
	}

	drewFrame = SlipMenu_DrawTrackSelect(resPath);
	SlipTrackGlobe_UpdateMatrix(resPath, SlipMenu_TrackSelectResourceTrack(g_trackHover));
	return drewFrame;
}

static bool SlipMenu_DrawGarageSprite(uint16_t resource, int x, int y);

enum {
	SLIP_GARAGE_FIRST_ACTION_TAG = 0x42555431,
	SLIP_GARAGE_WEAPON_TITLE_TAG = 0x57455030,
	SLIP_GARAGE_LEFT_WEAPON_POD_TAG = 0x57455031,
	SLIP_GARAGE_RIGHT_WEAPON_POD_TAG = 0x57455032,
	SLIP_GARAGE_WEAPON_PODS_ACCEPT_TAG = 0x52455033,
	SLIP_GARAGE_CANCEL_TAG = 0x43414e43,
	SLIP_GARAGE_EXIT_TAG = 0x45584954,
	SLIP_GARAGE_CURRENT_SYSTEM_FITTED = 1
};

static void SlipMenu_DrawGarageTaggedButton(const SpriteButton *button, bool active, uint32_t tag) {
	const int16_t left = (int16_t)button->x;
	const int16_t top = (int16_t)button->y;
	const int16_t right = (int16_t)(button->x + button->width - 1);
	const int16_t bottom = (int16_t)(button->y + button->height - 1);
	Raster_SetClipRect(left, top, right, bottom);
	Raster_DrawLineClipped(kGarageBorderLeftColor, left, top, left, bottom);
	Raster_DrawLineClipped(kGarageBorderTopColor, left, top, right, top);
	Raster_DrawLineClipped(kGarageBorderBottomRightColor, left, bottom, right, bottom);
	Raster_DrawLineClipped(kGarageBorderBottomRightColor, right, top, right, bottom);
	Raster_SetClipRect((int16_t)(left + 1), (int16_t)(top + 1), (int16_t)(right - 1), (int16_t)(bottom - 1));
	SlipMenu_DrawGarageSprite(active ? g_garageBackground : g_garageInactiveBackground, 0, 0);
	Raster_SetClipRect(left, top, right, bottom);
	if (tag != 0) {
		SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, left, right);
		const char *const text = SlipStringTable_Get(g_garageStrings, tag, &menuStringResources);
		SlipTextPosition position = {0, (int16_t)(top + 5)};
		SlipText_Draw(&SlipText_state, text, NULL, &position);
		SlipStringTable_Unlock(g_garageStrings, &menuStringResources);
	}
}

static int SlipMenu_GarageWeaponItemIndexFromId(int weaponId) {
	const int item = weaponId - 1;

	if (item < 0 || item >= kGarageWeaponGridActionCount - 1) {
		return -1;
	}
	return item;
}

static const SlipRacePlayerWeaponRecord *SlipMenu_GarageWeaponRecordFromItem(int item) {
	if (item < 0 || item >= kGarageWeaponGridActionCount - 1) {
		return NULL;
	}
	return &SlipRacePlayer_records[item + 1];
}

static uint32_t SlipMenu_GarageWeaponPrice(const SlipRacePlayerWeaponRecord *weapon) {
	return weapon->priceByMode[SlipConfig_CurrentMode()];
}

static int SlipMenu_GarageWeaponLoadedCount(int item) {
	const SlipRacePlayerWeaponRecord *const weapon = SlipMenu_GarageWeaponRecordFromItem(item);
	int load = (int)weapon->initialLoad;

	if ((g_garageSystems & 4u) != 0) {
		load *= 2;
		if (load > 9) {
			load = 9;
		}
	}
	return load;
}

static void SlipMenu_LoadGarageResource(const char *name, uint16_t *resource) {
	if (!SlipResourceHost_Load(NULL, name, resource))
		SlipGame_ResourceFailure();
}

static void SlipMenu_InitializeGarageResources(int driver) {
	SlipMenu_LoadGarageResource("GARBOX1.SPR", &g_garageStatusOriginal);
	g_garageStatusCopy = g_garageStatusOriginal;
	if (!SlipResourceHost_Copy(NULL, &g_garageStatusCopy))
		SlipGame_ResourceFailure();
	SlipMenu_LoadGarageResource("GARBOX2.SPR", &g_garageWeapons);
	SlipMenu_LoadGarageResource("GARBOX3.SPR", &g_garageTurboPanel);
	SlipMenu_LoadGarageResource("GARBOX4.SPR", &g_garageSystemsPanel);
	SlipMenu_LoadGarageResource("CNFFONT.FNT", &g_garageFont);
	SlipMenu_LoadGarageResource("RESULTS.FNT", &g_garageResultsFont);
	SlipMenu_LoadGarageResource("GARWEAP.FNT", &g_garageWeaponFont);
	SlipConfigHost_calls.language(NULL);
	if (!SlipStringTable_Load(&SlipStringTable_state, "GARAGE  ", &menuStringResources, &g_garageStrings))
		SlipGame_ResourceFailure();
	char name[13];
	SlipMenu_MakeDriverSpriteName(name, sizeof(name), "GARAGE", driver, "B.SPR");
	SlipMenu_LoadGarageResource(name, &g_garageInactiveBackground);
	SlipMenu_MakeDriverSpriteName(name, sizeof(name), "GARAGE", driver, "A.SPR");
	SlipMenu_LoadGarageResource(name, &g_garageBackground);
	SlipConfigHost_calls.palette(NULL, g_garageBackground);
	SlipMenu_LoadGarageResource("GARGOT.SPR", &g_garageMarker);
}

static void SlipMenu_ReleaseGarageResources(void) {
	SlipResourceHost_Release(NULL, g_garageFont);
	SlipResourceHost_Release(NULL, g_garageResultsFont);
	SlipResourceHost_Release(NULL, g_garageWeaponFont);
	SlipResourceHost_Release(NULL, g_garageBackground);
	SlipResourceHost_Release(NULL, g_garageInactiveBackground);
	SlipResourceHost_Release(NULL, g_garageStatusCopy);
	SlipResourceHost_Release(NULL, g_garageStatusOriginal);
	SlipResourceHost_Release(NULL, g_garageWeapons);
	SlipResourceHost_Release(NULL, g_garageTurboPanel);
	SlipResourceHost_Release(NULL, g_garageSystemsPanel);
	SlipResourceHost_Release(NULL, g_garageMarker);
	SlipStringTable_Release(g_garageStrings, &menuStringResources);
}

static bool SlipMenu_DrawGarageSprite(uint16_t resource, int x, int y) {
	SlipResourceHost_Lock(NULL, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	SlipSprite sprite;
	bool parsed = SlipSprite_FromPayload(&payload, &sprite) != 0;
	if (parsed)
		SlipSprite_DrawClipped(&sprite, g_screenBufferBase, g_screenPitch, x, y);
	SlipResourceHost_Unlock(NULL, resource);
	return parsed;
}

static void SlipMenu_PrepareGarageStatus(void) {
	SlipResourceHost_Lock(NULL, g_garageStatusCopy);
	SlipResourcePayload panelPayload = SlipResourceHost_Payload(g_garageStatusCopy);
	SlipSprite panel;
	SlipSprite_FromPayload(&panelPayload, &panel);
	SlipResourceHost_Unlock(NULL, g_garageStatusCopy);
	SlipResourceHost_LockWritable(NULL, g_garageStatusCopy);
	panelPayload = SlipResourceHost_Payload(g_garageStatusCopy);
	SlipSprite_FromPayload(&panelPayload, &panel);
	g_garageStatusPanel = (uint8_t *)panel.pixels;
	RasterSurfaceBinding savedSurface;
	Raster_BindSprite(g_garageStatusPanel, panel.width, panel.height, &savedSurface);
	SlipMenu_DrawGarageSprite(g_garageStatusOriginal, 0, 0);
	SlipText_SelectResourceFont(&SlipText_state, g_garageWeaponFont, &SlipRaceHud_fontResources);
	SlipText_SetColor(&SlipText_state, UINT16_MAX);
	SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, 15, 148);
	SlipTextArgument cash = {.dword = &g_garageRacer->bonusScore};
	SlipTextPosition position = {15, 14};
	SlipText_Draw(&SlipText_state, "Cash: $%ld", &cash, &position);
	char weaponLabels[2][24];
	uint32_t weaponIds[2] = {g_garageRacer->primaryWeaponIndex, g_garageRacer->secondaryWeaponIndex};
	uint32_t ammunition[2] = {g_garageRacer->primaryWeaponAmmo, g_garageRacer->secondaryWeaponAmmo};
	for (unsigned slot = 0; slot < 2; ++slot) {
		if ((int32_t)weaponIds[slot] < 0)
			strcpy(weaponLabels[slot], "Empty");
		else
			SlipRacePlayer_BuildWeaponLabel(SlipRacePlayer_records, weaponIds[slot], ammunition[slot],
			                                weaponLabels[slot]);
	}
	const char *const turbo = (int32_t)g_garageRacer->powerupRecord < 0
	                              ? "Not Fitted"
	                              : SlipRacePowerup_GetName(g_garageRacer->powerupRecord);
	SlipTextArgument turboArgument = {.text = turbo};
	position = (SlipTextPosition){15, 88};
	SlipText_Draw(&SlipText_state, "Turbo: %s", &turboArgument, &position);
	static const char *const systems[3] = {"Charger", "Targetter", "Loader"};
	uint32_t fitted = g_garageRacer->powerupFlags;
	for (unsigned system = 0; system < 3; ++system) {
		SlipTextArgument arguments[2] = {
		    {.text = systems[system]},
		    {.text = (fitted & SLIP_GARAGE_CURRENT_SYSTEM_FITTED) != 0 ? "Fitted" : "Not Fitted"}};
		position = (SlipTextPosition){(int16_t)(3 - system), (int16_t)(59 + system * 7)};
		SlipText_Draw(&SlipText_state, "%s: %s", arguments, &position);
		fitted >>= 1;
	}
	const int16_t left[2] = {18, 86};
	const int16_t right[2] = {77, 146};
	for (unsigned slot = 0; slot < 2; ++slot) {
		Raster_SetClipRect(left[slot], 25, right[slot], 49);
		SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, left[slot], right[slot]);
		if (weaponIds[slot] != 0) {
			/* The wrapped -1 table entry is the 'GE  ' bytes of GARAGE at4ba7e. */
			static const uint16_t sourceCoordinatesByBiasedWeapon[13][2] = {
			    {0x4547, 0x2020}, {0, 0},     {24, 99},   {92, 99},  {160, 99}, {228, 99}, {24, 129},
			    {92, 129},        {160, 129}, {228, 129}, {24, 159}, {92, 159}, {160, 159}};
			const uint16_t *const source = sourceCoordinatesByBiasedWeapon[weaponIds[slot] + 1u];
			const int x = left[slot] - ((int)source[0] - 12);
			const int y = 25 - ((int)source[1] - 82);
			SlipMenu_DrawGarageSprite(g_garageWeapons, x, y);
		}
		SlipText_SelectResourceFont(&SlipText_state, g_garageWeaponFont, &SlipRaceHud_fontResources);
		SlipText_SetColor(&SlipText_state, UINT16_MAX);
		char *label = weaponLabels[slot];
		const size_t length = strlen(label);
		position = (SlipTextPosition){left[slot], 28};
		if (label[length - 1u] == ']') {
			const size_t suffixOffset = length - 3u;
			const char saved = label[suffixOffset];
			label[suffixOffset] = 0;
			SlipText_Draw(&SlipText_state, label, NULL, &position);
			label[suffixOffset] = saved;
			label += suffixOffset;
			SlipText_SetStyle(&SlipText_state, 3, UINT16_MAX, left[slot], right[slot]);
			position.y = 40;
		}
		SlipText_Draw(&SlipText_state, label, NULL, &position);
	}
	SlipResourceHost_Unlock(NULL, g_garageStatusCopy);
	Raster_RestoreScreen(&savedSurface);
}

static void SlipMenu_DrawGarageGargotAtCenter(const char *resPath, int centerX, int centerY) {
	(void)resPath;
	SlipMenu_DrawGarageSprite(g_garageMarker, centerX - 24, centerY - 18);
}

static void SlipMenu_DrawGarageRectText(const SpriteButton *button, const char *text, const SlipTextArgument *arguments,
                                        int row) {
	SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, (int16_t)button->x, (int16_t)(button->x + button->width - 1));
	SlipTextPosition position = {(int16_t)button->x, (int16_t)(button->y + row)};
	SlipText_Draw(&SlipText_state, text, arguments, &position);
}

static bool SlipMenu_DrawGarageView(const char *resPath, int driver, int selectedAction, int hoveredAction, int panel,
                                    int selectedPanelItem, int hoveredPanelItem) {
	int activeItem;
	int itemIndex;

	if (panel == GARAGE_PANEL_NONE) {

		SlipFrameTimer_Update((uint32_t)SlipSdl_TicksMs());
		const uint16_t step =
		    (uint16_t)(((uint32_t)(uint16_t)SlipFrameTimer_Values().stepQ14 * kGarageStatusZoomRate) >>
		               kGarageStatusZoomFractionBits);
		if (g_garageStatusZoomTarget != g_garageStatusZoom) {
			if ((int16_t)g_garageStatusZoomTarget > (int16_t)g_garageStatusZoom) {
				g_garageStatusZoom = (uint16_t)(g_garageStatusZoom + step);
				if ((int16_t)g_garageStatusZoom > (int16_t)g_garageStatusZoomTarget)
					g_garageStatusZoom = g_garageStatusZoomTarget;
			} else {
				g_garageStatusZoom = (uint16_t)(g_garageStatusZoom - step);
				if ((int16_t)g_garageStatusZoom < (int16_t)g_garageStatusZoomTarget)
					g_garageStatusZoom = g_garageStatusZoomTarget;
			}
		}
	}
	Raster_Clear(0, sizeof(g_framebuffer));
	Raster_SetClipRect(0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1);

	if (!SlipMenu_DrawGarageSprite(g_garageBackground, 0, 0)) {
		return false;
	}

	if (panel == GARAGE_PANEL_WEAPON_PODS) {

		SlipMenu_DrawGarageSprite(g_garageStatusCopy, kGarageStatusPanelX, kGarageStatusPanelY);
		SlipText_SelectResourceFont(&SlipText_state, g_garageFont, &SlipRaceHud_fontResources);
		SlipText_SetColor(&SlipText_state, UINT16_MAX);
		{
			activeItem = SlipMenu_FocusedItemIndex(g_garagePanelFocus, selectedPanelItem, hoveredPanelItem);
			SlipMenu_DrawGarageTaggedButton(&g_garageWeaponTitleButton, true, SLIP_GARAGE_WEAPON_TITLE_TAG);
			static const uint32_t tags[kGarageWeaponPodActionCount] = {
			    SLIP_GARAGE_LEFT_WEAPON_POD_TAG, SLIP_GARAGE_RIGHT_WEAPON_POD_TAG, SLIP_GARAGE_WEAPON_PODS_ACCEPT_TAG};
			for (itemIndex = 0; itemIndex < kGarageWeaponPodActionCount; ++itemIndex) {
				SlipMenu_DrawGarageTaggedButton(&g_garageWeaponPodButtons[itemIndex], itemIndex == activeItem,
				                                tags[itemIndex]);
			}
		}
		return true;
	}

	if (panel == GARAGE_PANEL_WEAPON_GRID) {
		if (!SlipMenu_DrawGarageSprite(g_garageWeapons, 12, 82))
			return false;
		SlipText_SelectResourceFont(&SlipText_state, g_garageWeaponFont, &SlipRaceHud_fontResources);
		SlipText_SetColor(&SlipText_state, UINT16_MAX);
		for (itemIndex = 0; itemIndex < kGarageWeaponGridActionCount - 1; ++itemIndex) {
			const SlipRacePlayerWeaponRecord *const weapon = SlipMenu_GarageWeaponRecordFromItem(itemIndex);
			SlipMenu_DrawGarageRectText(&g_garageWeaponGridButtons[itemIndex], weapon->displayName, NULL, 1);
		}
		const SpriteButton *const cancel = &g_garageWeaponGridButtons[kGarageWeaponGridActionCount - 1];
		SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, (int16_t)cancel->x, (int16_t)(cancel->x + cancel->width - 1));
		const char *const label = SlipStringTable_Get(g_garageStrings, SLIP_GARAGE_CANCEL_TAG, &menuStringResources);
		SlipTextPosition position = {(int16_t)cancel->x, (int16_t)(cancel->y + 10)};
		SlipText_Draw(&SlipText_state, label, NULL, &position);
		SlipStringTable_Unlock(g_garageStrings, &menuStringResources);
		activeItem = SlipMenu_FocusedItemIndex(g_garagePanelFocus, selectedPanelItem, hoveredPanelItem);
		if (activeItem >= 0 && activeItem < kGarageWeaponGridActionCount - 1) {
			const SlipRacePlayerWeaponRecord *const weapon = SlipMenu_GarageWeaponRecordFromItem(activeItem);
			const uint32_t cost = SlipMenu_GarageWeaponPrice(weapon);
			int16_t costWord = (int16_t)cost;
			SlipTextArgument argument = {.word = &costWord};
			SlipText_SelectResourceFont(&SlipText_state, g_garageResultsFont, &SlipRaceHud_fontResources);
			SlipText_SetColor(&SlipText_state, kGarageAffordablePriceColor);
			const SpriteButton *const button = &g_garageWeaponGridButtons[activeItem];
			SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, (int16_t)button->x,
			                  (int16_t)(button->x + button->width - 1));
			if ((int32_t)cost > g_garageCash)
				SlipText_SetColor(&SlipText_state, kGarageUnaffordablePriceColor);
			SlipTextPosition pricePosition = {(int16_t)button->x, (int16_t)(button->y + 10)};
			SlipText_Draw(&SlipText_state, "$%d", &argument, &pricePosition);
		}
		return true;
	}

	if (panel == GARAGE_PANEL_TURBO) {
		if (!SlipMenu_DrawGarageSprite(g_garageTurboPanel, 13, 83))
			return false;
		SlipText_SelectResourceFont(&SlipText_state, g_garageWeaponFont, &SlipRaceHud_fontResources);
		SlipText_SetColor(&SlipText_state, UINT16_MAX);
		for (itemIndex = 0; itemIndex < kGarageTurboActionCount - 1; ++itemIndex)
			SlipMenu_DrawGarageRectText(&g_garageTurboButtons[itemIndex], SlipRacePowerup_GetName(itemIndex), NULL, 2);
		const SpriteButton *const cancel = &g_garageTurboButtons[kGarageTurboActionCount - 1];
		SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, (int16_t)cancel->x, (int16_t)(cancel->x + cancel->width - 1));
		const char *const label = SlipStringTable_Get(g_garageStrings, SLIP_GARAGE_CANCEL_TAG, &menuStringResources);
		SlipText_DrawCentered(&SlipText_state, label, NULL, (int16_t)cancel->x, (uint32_t)cancel->y,
		                      (uint32_t)(cancel->y + cancel->height - 1));
		SlipStringTable_Unlock(g_garageStrings, &menuStringResources);
		if (g_garageTurbo >= 0 && g_garageTurbo < kGarageTurboActionCount - 1)
			SlipMenu_DrawGarageGargotAtCenter(resPath, g_garageTurboMarkerCenters[g_garageTurbo][0],
			                                  g_garageTurboMarkerCenters[g_garageTurbo][1]);
		activeItem = SlipMenu_FocusedItemIndex(g_garagePanelFocus, selectedPanelItem, hoveredPanelItem);
		if (activeItem >= 0 && activeItem < kGarageTurboActionCount - 1) {
			const SpriteButton *const button = &g_garageTurboButtons[activeItem];
			SlipText_SelectResourceFont(&SlipText_state, g_garageResultsFont, &SlipRaceHud_fontResources);
			SlipText_SetColor(&SlipText_state, kGarageAffordablePriceColor);
			SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, (int16_t)button->x,
			                  (int16_t)(button->x + button->width - 1));
			if (activeItem != g_garageTurbo) {
				const int cost = SlipRacePowerup_GetPrice(activeItem);
				int16_t costWord = (int16_t)cost;
				SlipTextArgument argument = {.word = &costWord};
				if (cost > g_garageCash)
					SlipText_SetColor(&SlipText_state, kGarageUnaffordablePriceColor);
				SlipTextPosition position = {(int16_t)button->x, (int16_t)(button->y + 10)};
				SlipText_Draw(&SlipText_state, "$%d", &argument, &position);
			}
		}
		return true;
	}

	if (panel == GARAGE_PANEL_SYSTEMS) {
		if (!SlipMenu_DrawGarageSprite(g_garageSystemsPanel, 12, 101))
			return false;
		SlipText_SelectResourceFont(&SlipText_state, g_garageWeaponFont, &SlipRaceHud_fontResources);
		SlipText_SetColor(&SlipText_state, UINT16_MAX);
		for (itemIndex = 0; itemIndex < kGarageSystemsActionCount - 1; ++itemIndex)
			SlipMenu_DrawGarageRectText(&g_garageSystemsButtons[itemIndex], g_garageSystemsItems[itemIndex].name, NULL,
			                            2);
		const SpriteButton *const cancel = &g_garageSystemsButtons[kGarageSystemsActionCount - 1];
		SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, (int16_t)cancel->x, (int16_t)(cancel->x + cancel->width - 1));
		const char *const label = SlipStringTable_Get(g_garageStrings, SLIP_GARAGE_EXIT_TAG, &menuStringResources);
		SlipText_DrawCentered(&SlipText_state, label, NULL, (int16_t)cancel->x, (uint32_t)cancel->y,
		                      (uint32_t)(cancel->y + cancel->height - 1));
		SlipStringTable_Unlock(g_garageStrings, &menuStringResources);
		uint32_t fitted = (uint32_t)g_garageSystems;
		for (itemIndex = 0; itemIndex < kGarageSystemsActionCount - 1; ++itemIndex) {
			if ((fitted & SLIP_GARAGE_CURRENT_SYSTEM_FITTED) != 0)
				SlipMenu_DrawGarageGargotAtCenter(resPath, g_garageSystemsMarkerCenters[itemIndex][0],
				                                  g_garageSystemsMarkerCenters[itemIndex][1]);
			fitted >>= 1;
		}
		activeItem = SlipMenu_FocusedItemIndex(g_garagePanelFocus, selectedPanelItem, hoveredPanelItem);
		if (activeItem >= 0 && activeItem < kGarageSystemsActionCount - 1) {
			const SpriteButton *const button = &g_garageSystemsButtons[activeItem];
			SlipText_SelectResourceFont(&SlipText_state, g_garageResultsFont, &SlipRaceHud_fontResources);
			SlipText_SetColor(&SlipText_state, kGarageAffordablePriceColor);
			SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, (int16_t)button->x,
			                  (int16_t)(button->x + button->width - 1));
			if ((g_garageSystems & (SLIP_GARAGE_CURRENT_SYSTEM_FITTED << activeItem)) == 0) {
				const int cost = g_garageSystemsItems[activeItem].cost[0];
				int16_t costWord = (int16_t)cost;
				SlipTextArgument argument = {.word = &costWord};
				if (cost > g_garageCash)
					SlipText_SetColor(&SlipText_state, kGarageUnaffordablePriceColor);
				SlipTextPosition position = {(int16_t)button->x, (int16_t)(button->y + 10)};
				SlipText_Draw(&SlipText_state, "$%d", &argument, &position);
			}
		}
		return true;
	}

	SlipText_SelectResourceFont(&SlipText_state, g_garageFont, &SlipRaceHud_fontResources);
	SlipText_SetColor(&SlipText_state, UINT16_MAX);
	for (itemIndex = 0; itemIndex < kGarageActionCount; ++itemIndex) {
		{
			SlipMenu_DrawGarageTaggedButton(
			    &g_garageActionButtons[itemIndex],
			    itemIndex == SlipMenu_FocusedItemIndex(g_garageActionFocus, selectedAction, hoveredAction),
			    SLIP_GARAGE_FIRST_ACTION_TAG + (uint32_t)itemIndex);
		}
	}
	Raster_SetClipRect(0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1);

	SlipSprite_Zoom(g_garageStatusCopy, (int16_t)g_garageStatusZoom, kGarageStatusZoomCenterX, kGarageStatusZoomCenterY,
	                kGarageStatusPanelX, kGarageStatusPanelY, &SlipSpriteHost_effectResources);
	return true;
}

static int SlipMenu_HitTestTrackButton(int x, int y) {
	int buttonIndex;

	for (buttonIndex = 0; buttonIndex < kDriverCount; ++buttonIndex) {
		const SpriteButton *const button = &g_trackButtons[buttonIndex];
		if (x >= button->x && x < button->x + button->width && y >= button->y && y < button->y + button->height) {
			return buttonIndex;
		}
	}

	return -1;
}

static int SlipMenu_HitTestDriverAction(int x, int y) {
	const int localX = x - 0x1b;
	const int localY = y - 0x0a;
	int actionIndex;

	for (actionIndex = 0; actionIndex < 4; ++actionIndex) {
		const SlipSelectorRectangle *const zone = &vehicleResources.actionRects[actionIndex];
		if (localX >= zone->left && localX <= zone->right && localY >= zone->top && localY <= zone->bottom) {
			return actionIndex;
		}
	}

	return -1;
}

static int SlipMenu_HitTestGarageAction(int x, int y) {
	int actionIndex;

	for (actionIndex = 0; actionIndex < kGarageActionCount; ++actionIndex) {
		const SpriteButton *const button = &g_garageActionButtons[actionIndex];
		if (x >= button->x && x < button->x + button->width && y >= button->y && y < button->y + button->height) {
			return actionIndex;
		}
	}

	return -1;
}

static int SlipMenu_HitTestSpriteButtons(const SpriteButton *buttons, int buttonCount, int x, int y) {
	int buttonIndex;

	for (buttonIndex = 0; buttonIndex < buttonCount; ++buttonIndex) {
		const SpriteButton *const button = &buttons[buttonIndex];
		if (x >= button->x && x < button->x + button->width && y >= button->y && y < button->y + button->height) {
			return buttonIndex;
		}
	}

	return -1;
}

static const SpriteButton *SlipMenu_GaragePanelButtons(int panel, int *buttonCount) {
	if (panel == GARAGE_PANEL_WEAPON_PODS) {
		*buttonCount = kGarageWeaponPodActionCount;
		return g_garageWeaponPodButtons;
	}
	if (panel == GARAGE_PANEL_WEAPON_GRID) {
		*buttonCount = kGarageWeaponGridActionCount;
		return g_garageWeaponGridButtons;
	}
	if (panel == GARAGE_PANEL_TURBO) {
		*buttonCount = kGarageTurboActionCount;
		return g_garageTurboButtons;
	}
	if (panel == GARAGE_PANEL_SYSTEMS) {
		*buttonCount = kGarageSystemsActionCount;
		return g_garageSystemsButtons;
	}

	*buttonCount = 0;
	return NULL;
}

static int SlipMenu_HitTestGaragePanelItem(int panel, int x, int y) {
	int buttonCount;
	const SpriteButton *const buttons = SlipMenu_GaragePanelButtons(panel, &buttonCount);

	if (buttons == NULL) {
		return -1;
	}

	return SlipMenu_HitTestSpriteButtons(buttons, buttonCount, x, y);
}

static int SlipMenu_EventKeycode(const SDL_Event *event) { return event->key.key; }

static SlipInputCode SlipMenu_SdlKeyToDosScan(int key) {
	switch (key) {
	case SDLK_ESCAPE:
		return SLIP_INPUT_SCAN_ESCAPE;
	case '1':
		return SLIP_INPUT_SCAN_1;
	case '2':
		return SLIP_INPUT_SCAN_2;
	case '3':
		return SLIP_INPUT_SCAN_3;
	case '4':
		return SLIP_INPUT_SCAN_4;
	case '5':
		return SLIP_INPUT_SCAN_5;
	case '6':
		return SLIP_INPUT_SCAN_6;
	case '7':
		return SLIP_INPUT_SCAN_7;
	case '8':
		return SLIP_INPUT_SCAN_8;
	case '9':
		return SLIP_INPUT_SCAN_9;
	case '0':
		return SLIP_INPUT_SCAN_0;
	case SDLK_BACKSPACE:
		return SLIP_INPUT_SCAN_BACKSPACE;
	case SDLK_TAB:
		return SLIP_INPUT_SCAN_TAB;
	case 'q':
	case 'Q':
		return SLIP_INPUT_SCAN_Q;
	case 'w':
	case 'W':
		return SLIP_INPUT_SCAN_W;
	case 'e':
	case 'E':
		return SLIP_INPUT_SCAN_E;
	case 'r':
	case 'R':
		return SLIP_INPUT_SCAN_R;
	case 't':
	case 'T':
		return SLIP_INPUT_SCAN_T;
	case 'y':
	case 'Y':
		return SLIP_INPUT_SCAN_Y;
	case 'u':
	case 'U':
		return SLIP_INPUT_SCAN_U;
	case 'i':
	case 'I':
		return SLIP_INPUT_SCAN_I;
	case 'o':
	case 'O':
		return SLIP_INPUT_SCAN_O;
	case 'p':
	case 'P':
		return SLIP_INPUT_SCAN_P;
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		return SLIP_INPUT_SCAN_ENTER;
	case SDLK_LCTRL:
	case SDLK_RCTRL:
		return SLIP_INPUT_SCAN_CONTROL;
	case 'a':
	case 'A':
		return SLIP_INPUT_SCAN_A;
	case 's':
	case 'S':
		return SLIP_INPUT_SCAN_S;
	case 'd':
	case 'D':
		return SLIP_INPUT_SCAN_D;
	case 'f':
	case 'F':
		return SLIP_INPUT_SCAN_F;
	case 'g':
	case 'G':
		return SLIP_INPUT_SCAN_G;
	case 'h':
	case 'H':
		return SLIP_INPUT_SCAN_H;
	case 'j':
	case 'J':
		return SLIP_INPUT_SCAN_J;
	case 'k':
	case 'K':
		return SLIP_INPUT_SCAN_K;
	case 'l':
	case 'L':
		return SLIP_INPUT_SCAN_L;
	case 'z':
	case 'Z':
		return SLIP_INPUT_SCAN_Z;
	case 'x':
	case 'X':
		return SLIP_INPUT_SCAN_X;
	case 'c':
	case 'C':
		return SLIP_INPUT_SCAN_C;
	case 'v':
	case 'V':
		return SLIP_INPUT_SCAN_V;
	case 'b':
	case 'B':
		return SLIP_INPUT_SCAN_B;
	case 'n':
	case 'N':
		return SLIP_INPUT_SCAN_N;
	case 'm':
	case 'M':
		return SLIP_INPUT_SCAN_M;
	case SDLK_LALT:
	case SDLK_RALT:
		return SLIP_INPUT_SCAN_ALT;
	case SDLK_SPACE:
		return SLIP_INPUT_SCAN_SPACE;
	case SDLK_LSHIFT:
		return 0x2a;
	case SDLK_INSERT:
		return 0x52;
	case SDLK_DELETE:
		return 0x53;
	case SDLK_PAGEUP:
		return 0x49;
	case SDLK_PAGEDOWN:
		return 0x51;
	case SDLK_KP_PLUS:
		return 0x4e;
	case SDLK_KP_MINUS:
		return 0x4a;
	case SDLK_F1:
		return 0x3b;
	case SDLK_F2:
		return 0x3c;
	case SDLK_F3:
		return 0x3d;
	case SDLK_F4:
		return 0x3e;
	case SDLK_F5:
		return 0x3f;
	case SDLK_F6:
		return 0x40;
	case SDLK_F7:
		return 0x41;
	case SDLK_F8:
		return 0x42;
	case SDLK_F9:
		return 0x43;
	case SDLK_F10:
		return 0x44;
	case SDLK_UP:
		return SLIP_INPUT_SCAN_UP;
	case SDLK_DOWN:
		return SLIP_INPUT_SCAN_DOWN;
	case SDLK_LEFT:
		return SLIP_INPUT_SCAN_LEFT;
	case SDLK_RIGHT:
		return SLIP_INPUT_SCAN_RIGHT;
	case SDLK_RSHIFT:
		return 0x36;
	case SDLK_CAPSLOCK:
		return 0x3a;
	case SDLK_NUMLOCKCLEAR:
		return 0x45;
	case SDLK_HOME:
		return 0x47;
	case SDLK_END:
		return 0x4f;
	case SDLK_KP_DIVIDE:
		return 0x35;
	case SDLK_KP_MULTIPLY:
		return 0x37;
	case SDLK_KP_0:
		return 0x52;
	case SDLK_KP_1:
		return 0x4f;
	case SDLK_KP_2:
		return 0x50;
	case SDLK_KP_3:
		return 0x51;
	case SDLK_KP_4:
		return 0x4b;
	case SDLK_KP_5:
		return 0x4c;
	case SDLK_KP_6:
		return 0x4d;
	case SDLK_KP_7:
		return 0x47;
	case SDLK_KP_8:
		return 0x48;
	case SDLK_KP_9:
		return 0x49;
	case SDLK_KP_PERIOD:
		return 0x53;
	case SDLK_F11:
		return 0x57;
	case SDLK_F12:
		return 0x58;
	default:
		return SLIP_INPUT_SCAN_NONE;
	}
}

static SlipInputCode SlipMenu_SdlMouseButtonToInputCode(uint8_t button) {
	switch (button) {
	case SDL_BUTTON_LEFT:
		return SLIP_INPUT_MOUSE_LEFT;
	case SDL_BUTTON_RIGHT:
		return SLIP_INPUT_MOUSE_RIGHT;
	default:
		return SLIP_INPUT_SCAN_NONE;
	}
}

static bool SlipMenu_WindowToLogical(SDL_Renderer *renderer, float windowX, float windowY, int *x, int *y) {
	float logicalX;
	float logicalY;

	if (!SDL_RenderCoordinatesFromWindow(renderer, windowX, windowY, &logicalX, &logicalY)) {
		return false;
	}

	/*
	 * The logical presentation is the upscaled 4:3 CRT space (960x720);
	 * map back to the 320x200 framebuffer (the 3.0x / 3.6x stretch).
	 */
	*x = (int)(logicalX / (float)SLIP_PRESENT_SCALE);
	*y = (int)(logicalY * (float)SLIPSTREAM_SCREEN_HEIGHT / (float)SLIP_OUT_HEIGHT);
	return true;
}

static void SlipSdlInput_ApplyEvent(const SDL_Event *event, SDL_Renderer *renderer, bool *quitRequested) {
	SlipInputCode inputCode = SLIP_INPUT_SCAN_NONE;

	if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
		bool keyDown = event->type == SDL_EVENT_KEY_DOWN;

		inputCode = SlipMenu_SdlKeyToDosScan(SlipMenu_EventKeycode(event));
		if (inputCode >= 0 && inputCode < 256) {
			bool extended = false;
			switch (SlipMenu_EventKeycode(event)) {
			case SDLK_RCTRL:
			case SDLK_RALT:
			case SDLK_INSERT:
			case SDLK_DELETE:
			case SDLK_HOME:
			case SDLK_END:
			case SDLK_PAGEUP:
			case SDLK_PAGEDOWN:
			case SDLK_UP:
			case SDLK_DOWN:
			case SDLK_LEFT:
			case SDLK_RIGHT:
			case SDLK_KP_ENTER:
			case SDLK_KP_DIVIDE:
				extended = true;
				break;
			}
			SlipInputBiosHost_ApplyKey((uint8_t)inputCode, keyDown, extended, SlipInput_pressed, SlipInput_held);
		}
	}
	if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
		bool inputDown = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
		int mouseX;
		int mouseY;

		inputCode = SlipMenu_SdlMouseButtonToInputCode(event->button.button);
		if (inputCode >= 0) {
			if (inputDown && !SlipInput_held[inputCode]) {
				SlipInput_pressed[inputCode] = true;
			}
			SlipInput_held[inputCode] = inputDown;
		}
		if (SlipMenu_WindowToLogical(renderer, event->button.x, event->button.y, &mouseX, &mouseY)) {
			SlipInput_pointerX = mouseX * 0x10000;
			SlipInput_pointerY = mouseY * 0x10000;
		}
	}
	if (event->type == SDL_EVENT_MOUSE_MOTION) {
		int mouseX;
		int mouseY;

		if (SlipMenu_WindowToLogical(renderer, event->motion.x, event->motion.y, &mouseX, &mouseY)) {
			SlipInput_pointerX = mouseX * 0x10000;
			SlipInput_pointerY = mouseY * 0x10000;
		}
	}
	if (event->type == SDL_EVENT_QUIT || event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
		g_sdlQuitRequested = true;
		*quitRequested = true;
	}
}

static void SlipMenu_SetStatusWindowTitle(SDL_Window *window, const char *status) {
	char title[128];

	snprintf(title, sizeof(title), "Slipstream 5000 SDL Port - %s", status);
	SDL_SetWindowTitle(window, title);
	fprintf(stderr, "%s\n", status);
}

static void SlipMenu_SetDriverWindowTitle(SDL_Window *window, const char *status, int driver) {
	char title[128];

	snprintf(title, sizeof(title), "%s - driver %d", status, driver + 1);
	SlipMenu_SetStatusWindowTitle(window, title);
}

typedef enum MainMenuFirstLabel {
	MAIN_MENU_FIRST_LABEL_OPT1 = 0x4f505431u,
	MAIN_MENU_FIRST_LABEL_OP11 = 0x4f503131u,
	MAIN_MENU_FIRST_LABEL_LNK1 = 0x4c4e4b31u,
	MAIN_MENU_FIRST_LABEL_OP21 = 0x4f503231u
} MainMenuFirstLabel;

static void SlipInput_UpdatePointer(void) {
	if (!g_mouseDriverPresent) {
		SlipInput_UpdateNavigation(SlipInput_pressed);
	}
}

static uint16_t SlipMenu_HitTestRectList(MenuPageId menuPage, int mouseX, int mouseY) {
	SlipInputRectangle rectangles[kMaxMenuButtons];
	const uint16_t count = (uint16_t)g_menuPages[menuPage].buttonCount;
	for (uint16_t buttonIndex = 0; buttonIndex < count; ++buttonIndex) {
		const MenuButton *const button = &g_menuButtons[buttonIndex];
		rectangles[buttonIndex] =
		    (SlipInputRectangle){(int16_t)button->x, (int16_t)button->y, (int16_t)(button->x + button->width - 1),
		                         (int16_t)(button->y + button->height - 1)};
	}
	const uint16_t hit = (uint16_t)SlipInput_HitTest(rectangles, count, (int16_t)mouseX, (int16_t)mouseY);
	return hit != 0 && g_menuPages[menuPage].disabled[hit - 1] ? 0 : hit;
}

static int SlipMainMenu_Show(MainMenuFirstLabel firstLabel, uint16_t buttonCount) {
	MenuPageId menuPage;
	SlipInputNavigationTable navigationTable;

	enum { MAIN_MENU_IDLE_MILLISECONDS = 30000 };

	int32_t timeout = MAIN_MENU_IDLE_MILLISECONDS;
	int16_t selection = 0;
	uint16_t hoveredItem = 0;
	static const SlipInputCode hiddenSequence[] = {SLIP_INPUT_SCAN_R, SLIP_INPUT_SCAN_E, SLIP_INPUT_SCAN_F,
	                                               SLIP_INPUT_SCAN_I, SLIP_INPUT_SCAN_N, SLIP_INPUT_SCAN_E,
	                                               SLIP_INPUT_SCAN_R, SLIP_INPUT_SCAN_Y, SLIP_INPUT_SCAN_NONE};
	const SlipInputCode *hiddenSequenceCursor = hiddenSequence;
	bool quitRequested = false;
	int buttonIndex;

	switch (firstLabel) {
	case MAIN_MENU_FIRST_LABEL_OPT1:
		menuPage = MENU_PAGE_TOP;
		break;
	case MAIN_MENU_FIRST_LABEL_OP11:
		menuPage = MENU_PAGE_ONE_PLAYER;
		break;
	case MAIN_MENU_FIRST_LABEL_LNK1:
		menuPage = MENU_PAGE_TWO_PLAYER;
		break;
	case MAIN_MENU_FIRST_LABEL_OP21:
		menuPage = MENU_PAGE_TWO_PLAYER_RACE;
		break;
	default:
		return -1;
	}

	if (!SlipMenu_LoadMainMenuModel(g_mainMenuResPath))
		return -1;

	if (buttonCount != (uint16_t)g_menuPages[menuPage].buttonCount ||
	    !SlipMenu_PageSetup(g_mainMenuResPath, menuPage)) {
		return -1;
	}
	navigationTable.itemCount = buttonCount;
	navigationTable.currentItem = 0;
	for (buttonIndex = 0; buttonIndex < buttonCount; ++buttonIndex) {
		int previous = buttonIndex - 1;
		while (previous >= 0 && g_menuPages[menuPage].disabled[previous])
			--previous;
		int next = buttonIndex + 1;
		while (next < buttonCount && g_menuPages[menuPage].disabled[next])
			++next;
		navigationTable.up[buttonIndex] = (int16_t)previous;
		navigationTable.down[buttonIndex] = next == buttonCount ? -1 : (int16_t)next;
		navigationTable.left[buttonIndex] = -1;
		navigationTable.right[buttonIndex] = -1;
		navigationTable.centers[buttonIndex][0] =
		    (int16_t)(g_menuButtons[buttonIndex].x + (g_menuButtons[buttonIndex].width - 1) / 2);
		navigationTable.centers[buttonIndex][1] =
		    (int16_t)(g_menuButtons[buttonIndex].y + (g_menuButtons[buttonIndex].height - 1) / 2);
	}
	SlipInput_SetNavigation(&navigationTable);

	SlipMenu_SetStatusWindowTitle(g_mainMenuWindow, g_menuPages[menuPage].title);
	g_menuFadeOutSelection = -1;

	if (!SlipTimedValues_Initialize(&menuTimedValues, menuTimedValueOutputs, MENU_FADE_RATE, 1, &menuTimedValueTimer))
		SlipGame_UnexpectedFailure();
	SlipMenuFade_Start(0, 0xffff, MENU_FADE_STEPS, 0);
	SlipMenuSound_Play();
	SlipFrameTimer_Reset();

	do {
		SDL_Event event;
		SlipInputCode inputCode;

		SlipFrameTimer_Update((uint32_t)SlipSdl_TicksMs());
		while (SlipSdl_PollEvent(&event)) {
			SlipSdlInput_ApplyEvent(&event, g_mainMenuRenderer, &quitRequested);
		}
		/* User-requested network-page keyboard focus skips unavailable entries. */
		if (menuPage == MENU_PAGE_TWO_PLAYER &&
		    (SlipInput_pressed[SLIP_INPUT_SCAN_UP] || SlipInput_pressed[SLIP_INPUT_SCAN_DOWN] ||
		     SlipInput_pressed[SLIP_INPUT_SCAN_LEFT] || SlipInput_pressed[SLIP_INPUT_SCAN_RIGHT])) {
			SlipInput_UpdateNavigation(SlipInput_pressed);
			const uint16_t current = navigationTable.currentItem;
			SlipInput_pointerX = (int32_t)navigationTable.centers[current][0] * 0x10000;
			SlipInput_pointerY = (int32_t)navigationTable.centers[current][1] * 0x10000;
		}
		SlipInput_UpdatePointer();
		if (quitRequested) {
			selection = -1;
			break;
		}

		if (selection == 0) {
			hoveredItem = SlipMenu_HitTestRectList(menuPage, SlipInput_Pointer().x, SlipInput_Pointer().y);
		}

		SlipMainMenu_DrawFrame(g_mainMenuResPath, menuPage, hoveredItem == 0 ? -1 : (int)hoveredItem - 1);
		SlipMenu_PresentFrame();
		inputCode = SlipInput_PopMenuPressed(SlipInput_pressed);
		if (inputCode == SLIP_INPUT_SCAN_ESCAPE) {
			break;
		}
		if ((inputCode == SLIP_INPUT_SCAN_ENTER || inputCode == SLIP_INPUT_MOUSE_LEFT) && hoveredItem != 0) {
			selection = (int16_t)hoveredItem;
			g_menuFadeOutSelection = selection - 1;
			SlipMenuFade_Start(0xffff, 0, MENU_FADE_STEPS, SLIP_TIMED_VALUE_PRESERVE_OUTPUT);
			SlipMenuSound_Play();
		}
		if (inputCode != SLIP_INPUT_SCAN_NONE) {
			if (*hiddenSequenceCursor == inputCode) {
				++hiddenSequenceCursor;
				if (*hiddenSequenceCursor == SLIP_INPUT_SCAN_NONE) {
					SlipMainMenu_hiddenToggle ^= 1;
					hiddenSequenceCursor = hiddenSequence;
				}
			} else {
				hiddenSequenceCursor = hiddenSequence;
			}
		}

		timeout -= (int32_t)SlipFrameTimer_Values().deltaMilliseconds;
		if (timeout < 0) {
			selection = -1;
			break;
		}
	} while (selection == 0 || SlipTimedValues_Active(&menuTimedValues, 0x3d434));

	if (selection > 0 && !quitRequested) {
		SlipMainMenu_DrawFrame(g_mainMenuResPath, menuPage, hoveredItem == 0 ? -1 : (int)hoveredItem - 1);
		SlipMenu_PresentFrame();
		SlipMainMenu_DrawFrame(g_mainMenuResPath, menuPage, hoveredItem == 0 ? -1 : (int)hoveredItem - 1);
	}

	SlipResourceHost_Release(NULL, g_mainMenuBackground);
	SlipInput_ClearNavigation();
	SlipTimedValues_Shutdown(&menuTimedValues, &menuTimedValueTimer);
	for (buttonIndex = 0; buttonIndex < buttonCount; ++buttonIndex) {
		SlipResourceHost_Release(NULL, g_menuButtonCopies[buttonIndex][0]);
		SlipResourceHost_Release(NULL, g_menuButtonCopies[buttonIndex][1]);
	}
	SlipResourceHost_Release(NULL, g_mainMenuFont);
	SlipStringTable_Release(g_mainMenuStrings, &menuStringResources);
	g_menuFadeOutSelection = -1;
	return selection;
}

typedef struct LinkMenuDispatchResult {
	int returnToLinkMenu;
	int secondPlayerEnabled;
} LinkMenuDispatchResult;

static LinkMenuDispatchResult SlipLinkMenu_DispatchSelection(int selection) {
	if (selection == 1) {
		return (LinkMenuDispatchResult){0, 1};
	}
	return (LinkMenuDispatchResult){1, 0};
}

static int SlipRaceLink_Setup(void) {
	if (SlipRace_gameMode == 0 || SlipRace_gameMode == 1) {
		return 0;
	}
	return 1;
}

static int SlipMainMenu_RunFlow(void) {
	static const SlipRaceType onePlayerRaceTypes[4] = {0, SLIP_RACE_TYPE_PRACTICE, SLIP_RACE_TYPE_SINGLE,
	                                                   SLIP_RACE_TYPE_CHAMPIONSHIP};
	static const SlipRaceType twoPlayerRaceTypes[3] = {0, SLIP_RACE_TYPE_SINGLE, SLIP_RACE_TYPE_SINGLE};
	int selection;

	SlipConfig_mode = -1;
	SlipConfig_damageOverride = -1;
	if (SlipMainMenu_demoSelection != 0) {
		SlipRace_type = SLIP_RACE_TYPE_SINGLE;
		SlipRace_secondPlayerEnabled = 1;
		SlipRace_gameMode = 0;
		return SlipMainMenu_demoSelection;
	}

	for (;;) {
		selection = SlipMainMenu_Show(MAIN_MENU_FIRST_LABEL_OPT1, 5);
		if (selection == -1 || selection == 0) {
			return selection;
		}
		if (selection == 1) {
			selection = SlipMainMenu_Show(MAIN_MENU_FIRST_LABEL_OP11, 3);
			if (selection == -1) {
				return selection;
			}
			if (selection == 0) {
				continue;
			}
			SlipRace_type = onePlayerRaceTypes[selection];
			SlipRace_racerCount = 10;
			SlipRace_gameMode = 0;
			SlipRace_secondPlayerEnabled = 1;
			return 1;
		}
		if (selection == 2) {
			for (;;) {
				LinkMenuDispatchResult linkResult;

				selection = SlipMainMenu_Show(MAIN_MENU_FIRST_LABEL_LNK1, 4);
				if (selection == -1) {
					return selection;
				}
				if (selection == 0) {
					break;
				}
				SlipRace_gameMode = (uint32_t)selection;
				linkResult = SlipLinkMenu_DispatchSelection(selection);
				if (linkResult.returnToLinkMenu != 0) {
					continue;
				}
				SlipRace_secondPlayerEnabled = (uint16_t)linkResult.secondPlayerEnabled;
				if (linkResult.secondPlayerEnabled == 0) {
					SlipRace_type = SLIP_RACE_TYPE_SINGLE;
				} else {
					selection = SlipMainMenu_Show(MAIN_MENU_FIRST_LABEL_OP21, 2);
					if (selection == -1) {
						return selection;
					}
					if (selection == 0) {
						continue;
					}
					SlipRace_racerCount = selection == 1 ? 2u : 10u;
					SlipRace_type = twoPlayerRaceTypes[selection];
				}
				if (SlipRaceLink_Setup() == 0) {
					return 1;
				}
			}
			continue;
		}
		if (selection == 3 || selection == 4) {
			return selection;
		}
		return 2;
	}
}

static void SlipMenu_RaceConfiguration(void) {
	const uint16_t font = SlipConfigHost_calls.currentFont(SlipConfigHost_calls.context);
	uint8_t palette[256][3];
	SlipVgaDac_ReadRange(0, 256, palette[0]);
	SDL_ShowCursor();
	SlipConfigMenu_state.allowDifficulty = 0;
	SlipConfigMenu_Run(&SlipConfigMenu_state, &SlipConfigHost_calls);
	SlipConfigMenu_state.allowDifficulty = 1;
	SDL_HideCursor();
	SlipVgaDac_WriteRange(0, 256, palette[0]);
	SlipConfigHost_calls.selectFont(SlipConfigHost_calls.context, font);
	SlipRaceSession_ApplyConfigurationValues();
	SlipRaceHud_ResetConsole(&SlipRaceSession_hudState);
}

static void SlipMenu_ReleaseVehicleResources(void) {
	if (!vehicleResourcesActive)
		return;
	SlipVehicleSelector_Release(&vehicleResources, &vehicleResourceCalls);
	vehicleResourcesActive = false;
}

static void SlipMenu_EnterVehicleSelect(SDL_Window *window, AppMode *mode, OnePlayerRaceType raceType,
                                        int *hoveredButton, bool *redraw, uint16_t excludedDriver) {

	g_excludedDriver = excludedDriver;
	vehicleResourceHost = (SlipVehicleSelectorHost){.smallFontHandle = SlipMenu_resources.smallFont};
	vehicleResourceCalls.context = &vehicleResourceHost;
	SlipVehicleSelector_BindNativeCalls(&vehicleResourceCalls);
	SlipVehicleSelector_Setup(&vehicleResources, excludedDriver, NULL, SlipMenu_resources.smallFont,
	                          (uint16_t)SlipConfig_language, &SlipStringTable_state, &vehicleResourceCalls);
	vehicleResourcesActive = true;
	vehicleResources.animation.backgroundRedraws = 2;
	g_selectedRaceType = raceType;
	g_selectedDriver = 0;
	g_hoveredVehicle = -1;
	g_vehicleFocus = FOCUS_NONE;
	SlipMenu_VehicleDoorAnimationBegin();
	*mode = APP_MODE_VEHICLE_SELECT;
	*hoveredButton = -1;
	*redraw = true;
	g_screenStartMs = SlipSdl_TicksMs();
	SlipMenu_SetStatusWindowTitle(window, "Select your vehicle");
}

static void SlipMenu_EnterVehicleToDriver(SDL_Window *window, AppMode *mode, int driver, bool *redraw) {
	g_selectedDriver = driver;
	g_transitionDriver = driver;
	g_vehicleGrayMix = 0;
	g_vehicleDriverTransitionPhase = VEHICLE_DRIVER_TRANSITION_FADE;
	g_vehicleZoomScale = 0;
	*mode = APP_MODE_VEHICLE_TO_DRIVER;
	*redraw = true;
	SlipMenu_SetDriverWindowTitle(window, "Vehicle selection", driver);
}

static void SlipMenu_EnterDriverSelect(SDL_Window *window, AppMode *mode, int *hoveredButton, bool *redraw) {
	vehicleResources.voice = 0;
	g_selectedDriverAction = 0;
	g_hoveredDriverAction = -1;
	*mode = APP_MODE_DRIVER_SELECT;
	*hoveredButton = -1;
	*redraw = true;
	g_screenStartMs = SlipSdl_TicksMs();
	SlipMenu_SetDriverWindowTitle(window, "Driver selection", g_selectedDriver);
}

static void SlipMenu_EnterVehicleView(SDL_Window *window, AppMode *mode, bool *redraw) {
	*mode = APP_MODE_VEHICLE_VIEW;
	g_hoveredDriverAction = -1;
	*redraw = true;
	g_screenStartMs = SlipSdl_TicksMs();
	TrackView_VehicleViewResetActorState();
	SlipMenu_SetDriverWindowTitle(window, "Vehicle view", g_selectedDriver);
}

static void SlipMenu_DriverViewReturn(void) {
	RasterSurfaceBounds bounds = Raster_GetSurfaceBounds();
	Raster_SetClipRect((int16_t)bounds.left, (int16_t)bounds.top, (int16_t)bounds.right, (int16_t)bounds.bottom);
	vehicleResourceCalls.spritePalette(vehicleResourceCalls.context, vehicleResources.card);
	vehicleResourceCalls.spritePalette(vehicleResourceCalls.context, vehicleResources.assets.driverBackground);
}

static void SlipMenu_TrackSelectBegin(void) {

	g_trackHover = 0;
	g_trackGlobeGrow = 0;
	TrackView_TrackGlobeResetActorState();
}

static void SlipMenu_EnterTrackSelect(SDL_Window *window, AppMode *mode, AppMode returnMode, bool *redraw) {
	*mode = APP_MODE_TRACK_SELECT;
	g_trackReturnMode = returnMode;
	g_hoveredDriverAction = -1;
	g_selectedTrack = 0;
	g_hoveredTrack = -1;
	g_trackFocus = FOCUS_NONE;
	*redraw = true;
	SlipMenu_TrackButtonSetup();
	SlipMenu_TrackSelectBegin();
	SlipMenu_SetStatusWindowTitle(window, "Choose Track");
}

static void SlipMenu_StartSelectedRace(const char *resPath, AppMode *mode) {
	SlipMenuMusic_Stop();
	SlipMenuSound_Release();
	const uint16_t trackIndex = (uint16_t)(g_trackMenuToResourceIndex[g_selectedTrack] + 1u);

	*mode = APP_MODE_RACE;

	if (SlipRace_type != SLIP_RACE_TYPE_PRACTICE)
		SlipRaceRecording_Install(SlipRace_gameMode == 0 ? 6 : 12, 100000, &SlipRaceSession_recordingHost);
	SlipRaceSession_StartNew(resPath, trackIndex, &SlipRace_racerTable, (uint32_t)SlipConfig_environmentDetail,
	                         (uint32_t)SlipConfig_shading, (uint32_t)SlipConfig_textures, (uint32_t)SlipConfig_shadows);
}

static void SlipMenu_EnterGarageView(SDL_Window *window, AppMode *mode, bool *redraw, uint32_t playerNumber);

static uint32_t SlipMenu_TrackLimit(void) {
	if (SlipRace_gameMode != 0)
		return SLIP_CONFIG_TRACK_COUNT;
	return SlipConfig_TrackProgress();
}

static void SlipMenu_AcceptTrackSelection(const char *resPath, SDL_Window *window, AppMode *mode, int track,
                                          bool *redraw) {

	if (SlipMainMenu_hiddenToggle == 0) {
		const uint16_t limit = (uint16_t)SlipMenu_TrackLimit();
		if ((int16_t)(uint16_t)(track + 1) > (int16_t)limit)
			return;
	}
	SlipMenu_TrackButtonRelease();
	g_selectedTrack = track;

	SlipRace_racerTable.racerCount =
	    g_selectedRaceType == ONE_PLAYER_RACE_PRACTICE ? 1u : (uint16_t)SlipRace_racerCount;
	SlipRace_BuildRacerTable(&SlipRace_racerTable, g_playerOneDriver, g_playerTwoDriver);
	if (g_selectedRaceType == ONE_PLAYER_RACE_PRACTICE) {

		SlipMenu_SetStatusWindowTitle(window, "Start Race");
		SlipMenu_StartSelectedRace(resPath, mode);
		*redraw = true;
		return;
	}
	SlipMenu_EnterGarageView(window, mode, redraw, 1u);
}

static void SlipMenu_EnterGarageView(SDL_Window *window, AppMode *mode, bool *redraw, uint32_t playerNumber) {

	const uint16_t racerType = playerNumber == 1 ? 0 : 1;
	g_garageRacer = SlipRace_racerTable.records;
	for (uint16_t remaining = SlipRace_racerTable.racerCount; remaining != 0; --remaining) {
		if (g_garageRacer->racerType == racerType)
			break;
		++g_garageRacer;
	}

	g_selectedDriver = g_garageRacer->tuningIndex - 1;
	SlipMenu_InitializeGarageResources(g_selectedDriver);
	*mode = APP_MODE_GARAGE;
	g_garagePanel = GARAGE_PANEL_NONE;
	g_selectedGarageAction = 0;
	g_selectedGaragePanelItem = 0;
	g_selectedGarageWeaponPod = 0;

	g_garageCash = (int)g_garageRacer->bonusScore;
	g_garageWeaponSlots[0] = (int32_t)g_garageRacer->primaryWeaponIndex;
	g_garageWeaponSlots[1] = (int32_t)g_garageRacer->secondaryWeaponIndex;
	g_garageWeaponLoads[0] = (int)g_garageRacer->primaryWeaponAmmo;
	g_garageWeaponLoads[1] = (int)g_garageRacer->secondaryWeaponAmmo;
	g_garageTurbo = (int)g_garageRacer->powerupRecord;
	g_garageSystems = g_garageRacer->powerupFlags;
	g_hoveredGarageAction = -1;
	g_hoveredGaragePanelItem = -1;
	g_garageActionFocus = FOCUS_NONE;
	g_garagePanelFocus = FOCUS_NONE;
	*redraw = true;

	g_garageStatusZoom = 0;
	g_garageStatusZoomTarget = kGarageStatusZoomComplete;
	SlipMenu_PrepareGarageStatus();
	SlipFrameTimer_Reset();
	SlipMenu_SetStatusWindowTitle(window, "Garage");
}

static void SlipMenu_SelectGarageAction(const char *resPath, SDL_Window *window, AppMode *mode, int action,
                                        bool *redraw) {
	g_selectedGarageAction = action;
	g_hoveredGarageAction = -1;
	g_hoveredGaragePanelItem = -1;
	g_selectedGaragePanelItem = 0;
	g_garageActionFocus = FOCUS_NONE;
	g_garagePanelFocus = FOCUS_NONE;
	if (action == 0) {
		g_garagePanel = GARAGE_PANEL_WEAPON_PODS;
		SlipMenu_PrepareGarageStatus();
		SlipMenu_SetStatusWindowTitle(window, g_garageActionLabels[action]);
	} else if (action == 1) {
		g_garagePanel = GARAGE_PANEL_TURBO;
		SlipMenu_SetStatusWindowTitle(window, g_garageActionLabels[action]);
	} else if (action == 2) {
		g_garagePanel = GARAGE_PANEL_SYSTEMS;
		SlipMenu_SetStatusWindowTitle(window, g_garageActionLabels[action]);
	} else {

		SlipMenu_ReleaseGarageResources();
		g_garagePanel = GARAGE_PANEL_NONE;
		SlipMenu_SetStatusWindowTitle(window, g_garageActionLabels[3]);
		if (mode != NULL) {

			if (SlipRace_gameMode == 1 && g_garageRacer->racerType == 0)
				SlipMenu_EnterGarageView(window, mode, redraw, 2u);
			else
				SlipMenu_StartSelectedRace(resPath, mode);
		}
	}
	*redraw = true;
}

static void SlipMenu_LeaveGaragePanel(SDL_Window *window, bool *redraw) {

	if (g_garagePanel == GARAGE_PANEL_SYSTEMS) {
		g_garageRacer->powerupFlags = g_garageSystems;
		g_garageRacer->bonusScore = (uint32_t)g_garageCash;
	}
	g_garagePanel = GARAGE_PANEL_NONE;
	g_hoveredGarageAction = -1;
	g_hoveredGaragePanelItem = -1;
	g_selectedGaragePanelItem = 0;
	g_garageActionFocus = FOCUS_NONE;
	g_garagePanelFocus = FOCUS_NONE;
	*redraw = true;
	SlipMenu_PrepareGarageStatus();

	SlipFrameTimer_Reset();
	SlipMenu_SetStatusWindowTitle(window, "Garage");
}

static void SlipMenu_ReturnToGarageWeaponPods(SDL_Window *window) {
	SlipMenu_PrepareGarageStatus();
	g_garagePanel = GARAGE_PANEL_WEAPON_PODS;
	g_selectedGaragePanelItem = g_selectedGarageWeaponPod;
	g_hoveredGaragePanelItem = -1;
	g_garagePanelFocus = FOCUS_NONE;
	SlipMenu_SetStatusWindowTitle(window, g_garageActionLabels[0]);
}

static void SlipMenu_SelectGaragePanelItem(SDL_Window *window, int item, bool *redraw) {
	if (g_garagePanel == GARAGE_PANEL_WEAPON_PODS) {
		if (item == 0 || item == 1) {
			g_selectedGarageWeaponPod = item;
			g_garagePanel = GARAGE_PANEL_WEAPON_GRID;
			g_selectedGaragePanelItem = 0;
			g_hoveredGaragePanelItem = -1;
			g_garagePanelFocus = FOCUS_NONE;
			SlipMenu_SetStatusWindowTitle(window, g_garageWeaponPodLabels[item]);
		} else {
			SlipMenu_LeaveGaragePanel(window, redraw);
			return;
		}
	} else if (g_garagePanel == GARAGE_PANEL_WEAPON_GRID) {
		if (item == kGarageWeaponGridActionCount - 1) {
			SlipMenu_ReturnToGarageWeaponPods(window);
		} else {
			const SlipRacePlayerWeaponRecord *const weapon = SlipMenu_GarageWeaponRecordFromItem(item);
			const uint32_t cost = SlipMenu_GarageWeaponPrice(weapon);

			if (SlipMainMenu_hiddenToggle != 0 || g_garageCash >= (int32_t)cost) {
				if (SlipMainMenu_hiddenToggle == 0)
					g_garageCash -= (int)cost;
				g_garageWeaponSlots[g_selectedGarageWeaponPod] = item + 1;
				g_garageWeaponLoads[g_selectedGarageWeaponPod] = SlipMenu_GarageWeaponLoadedCount(item);

				g_garageRacer->bonusScore = (uint32_t)g_garageCash;
				if (g_selectedGarageWeaponPod == 0) {
					g_garageRacer->primaryWeaponIndex = (uint32_t)g_garageWeaponSlots[0];
					g_garageRacer->primaryWeaponAmmo = (uint32_t)g_garageWeaponLoads[0];
				} else {
					g_garageRacer->secondaryWeaponIndex = (uint32_t)g_garageWeaponSlots[1];
					g_garageRacer->secondaryWeaponAmmo = (uint32_t)g_garageWeaponLoads[1];
				}
				SlipMenu_ReturnToGarageWeaponPods(window);
			}
		}
	} else if (g_garagePanel == GARAGE_PANEL_TURBO) {
		if (item == kGarageTurboActionCount - 1) {
			SlipMenu_LeaveGaragePanel(window, redraw);
			return;
		} else {
			const int cost = SlipRacePowerup_GetPrice(item);
			g_selectedGaragePanelItem = item;

			if (SlipMainMenu_hiddenToggle != 0 || (g_garageTurbo != item && g_garageCash >= cost)) {
				if (SlipMainMenu_hiddenToggle == 0)
					g_garageCash -= cost;
				g_garageTurbo = item;

				g_garageRacer->bonusScore = (uint32_t)g_garageCash;
				g_garageRacer->powerupRecord = (uint32_t)g_garageTurbo;

				SlipMenu_LeaveGaragePanel(window, redraw);
				return;
			}
		}
	} else if (g_garagePanel == GARAGE_PANEL_SYSTEMS) {
		if (item == kGarageSystemsActionCount - 1) {
			SlipMenu_LeaveGaragePanel(window, redraw);
			return;
		} else {
			const int cost = g_garageSystemsItems[item].cost[0];
			g_selectedGaragePanelItem = item;

			if ((g_garageSystems & (1u << item)) == 0 && (SlipMainMenu_hiddenToggle != 0 || g_garageCash >= cost)) {
				if (SlipMainMenu_hiddenToggle == 0)
					g_garageCash -= cost;
				g_garageSystems |= 1u << item;
			}
		}
	}
	g_hoveredGaragePanelItem = -1;
	*redraw = true;
}

bool SlipMenu_PollInput(void) {
	SDL_Event event;
	bool quit = false;
	while (SlipSdl_PollEvent(&event))
		SlipSdlInput_ApplyEvent(&event, g_mainMenuRenderer, &quit);
	if (quit)
		g_sdlQuitRequested = true;
	return !g_sdlQuitRequested;
}

void SlipMenu_PresentFrame(void) {

	SlipScreenHost_Present();
	if (g_mainMenuPresentFrame != NULL)
		g_mainMenuPresentFrame(g_mainMenuPresentFrameContext);

	Raster_SetScreenBufferRows(g_framebuffer, SLIPSTREAM_SCREEN_WIDTH);
}

static bool SlipMenu_CampaignSampleSize(void *context, const char *name, uint32_t *size) {
	TrackViewResourceHandleRegistry *const registry = context;
	if (registry->hostResources) {
		uint16_t resource;
		return SlipResourceHost_Find(NULL, name, &resource) && SlipResourceHost_Size(NULL, resource, size);
	}
	SlipResourcePayload payload = {0};
	if (!SlipResource_LoadByName(registry->archives, registry->archiveCount, name, &payload))
		return false;
	*size = (uint32_t)payload.size;
	SlipResource_ReleaseHandle(&payload);
	return true;
}

static bool SlipMenu_CampaignSampleLoad(void *context, const char *name, uint16_t *handle) {
	TrackViewResourceHandleRegistry *const registry = context;
	if (registry->hostResources) {
		if (!SlipResourceHost_Load(NULL, name, handle))
			return false;
		SlipResourceHost_Lock(NULL, *handle);
		return true;
	}
	uint32_t nativeHandle;
	if (!TrackView_LoadNamedResource(context, name, &nativeHandle))
		return false;
	*handle = (uint16_t)nativeHandle;
	return true;
}

static void SlipMenu_CampaignSampleRelease(void *context, uint16_t handle) {
	TrackViewResourceHandleRegistry *const registry = context;
	if (registry->hostResources) {
		SlipResourceHost_Unlock(NULL, handle);
		SlipResourceHost_Release(NULL, handle);
	} else
		TrackView_ReleaseResource(context, handle);
}

static uint32_t SlipMenu_CampaignVoiceStopped(void *context, uint32_t voice) {
	(void)context;
	if (menuSound == NULL)
		return 1;
	if (menuSoundLock != NULL && !menuSoundLock(menuSoundContext))
		return 0;
	const uint32_t result = SlipGameSound_IsStopped(menuSound, voice);
	if (menuSoundUnlock != NULL)
		menuSoundUnlock(menuSoundContext);
	return result;
}

static uint32_t SlipMenu_CampaignVoicePlay(void *context, uint16_t handle, uint32_t *sampleBytes) {
	SlipResourcePayload sample = {0};
	TrackViewResourceHandleRegistry *const registry = context;
	if (registry->hostResources) {
		uint32_t bytes;
		(void)SlipResourceHost_Size(NULL, handle, &bytes);
		const uint8_t *const data = SlipResourceHost_Lock(NULL, handle);
		SlipResourceHost_Unlock(NULL, handle);
		sample.data = (uint8_t *)data;
		sample.size = bytes;
	} else if (!TrackView_LoadResourceHandlePayload(context, handle, &sample))
		SlipRuntime_Fatal("Could not resolve ANN speech resource.");

	if (sampleBytes != NULL)
		*sampleBytes = (uint32_t)sample.size;
	if (menuSound == NULL)
		return 0;
	if (menuSoundLock != NULL && !menuSoundLock(menuSoundContext))
		return 0;
	const uint32_t voice = SlipGameSound_PlayAlternate(menuSound, sample.data, (uint32_t)sample.size);
	if (menuSoundUnlock != NULL)
		menuSoundUnlock(menuSoundContext);
	return voice;
}

static void SlipMenu_CampaignResetConsole(void *context) {
	(void)context;
	SlipRaceHud_ResetConsole(&SlipRaceSession_hudState);
}

static void SlipMenu_CampaignPresenterQueue(void *context, const uint8_t *program, size_t size) {
	(void)context;
	SlipPresenter_Queue(program, size);
}

static SlipView3DMatrix campaignGlobe = {{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}};

bool SlipMenu_CampaignPresenter(uint16_t track, uint32_t afterPreview) {
	static const char *const names[11] = {NULL,         "CHIINT.ANN", "HAWINT.ANN", "TOKINT.ANN",
	                                      "NORINT.ANN", "CAVINT.ANN", "COLINT.ANN", "AMAINT.ANN",
	                                      "LONINT.ANN", "EGYINT.ANN", "NYCINT.ANN"};
	static const char *const titles[10] = {"Chicago", "Hawaii", "Tokyo",  "Norway", "France",
	                                       "Arizona", "Amazon", "London", "Egypt",  "New York"};
	char secondaryPath[512], name[13];
	const char *archives[2];
	const size_t count = SlipMenu_BuildArchiveList(g_mainMenuResPath, secondaryPath, archives);
	TrackViewResourceHandleRegistry registry = {.archives = archives, .archiveCount = count, .hostResources = true};
	uint16_t titleResource, titleFontResource, starsResource, scriptResource, globeResource, flagResource;
	SlipResourcePayload script;
	SlipStringTableSlot *strings;
	SlipRaceIntroScript state = {.cursor = 16};
	SlipRaceIntroResources resources = {&registry, SlipMenu_CampaignSampleSize, SlipMenu_CampaignSampleLoad,
	                                    SlipMenu_CampaignSampleRelease};
	SlipRaceIntroScriptHost host = {
	    &registry, SlipMenu_CampaignVoiceStopped, SlipMenu_CampaignVoicePlay, SlipMenu_CampaignResetConsole, NULL,
	    NULL};
	if (track == 0 || track > 10 || count == 0)
		SlipRuntime_Fatal("Invalid campaign presenter track.");

	SlipStringTable_SetLanguage(&SlipStringTable_state, (uint8_t)SlipConfig_language);
	SlipRenderer_Initialize(&SlipRendererHost_state, 150, &SlipRendererHost_lifecycleCalls);
	SlipDraw3D_SetMinimumDepth(0x0c);
	SlipDraw3D_SetMaximumDepth(0x7fffffff);
	SlipDraw3D_ResetLighting();
	SlipDraw3D_SetAmbientLight(0x1800);
	SlipDraw3D_SetLightVector(0, (int16_t)0xc000, 0, 0x2800);
	SlipDraw3D_SetDepthFade(0, 0, 0);
	SlipRenderer_SetFlags(&SlipRendererHost_state, SLIP_RENDER_ALTERNATE_TEXTURE_RASTER);
	const SlipView3DMatrix cameraIdentity = {{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}};
	SlipRenderer_SetCamera(&SlipRendererHost_state, (SlipView3DVec32){0, 0, 0}, &cameraIdentity);
	SlipShape3D_Initialize();
	uint16_t materialResource;
	if (!SlipResourceHost_Load(NULL, "GLOBE.MAT", &materialResource))
		SlipGame_ResourceFailure();
	const uint8_t *const materialSource = SlipResourceHost_Lock(NULL, materialResource);
	SlipMaterial_Install(&SlipMaterialHost_install, materialSource, &SlipMaterialHost_installCalls);
	SlipMaterial_MakeResident(&SlipMaterialHost_residency, &SlipMaterialHost_residencyCalls);
	SlipResourceHost_Unlock(NULL, materialResource);
	SlipResourceHost_Release(NULL, materialResource);
	if (!SlipResourceHost_Load(NULL, "STARFONT.FNT", &titleFontResource))
		SlipGame_ResourceFailure();
	SlipText_SelectResourceFont(&SlipText_state, titleFontResource, &mainMenuFontCalls);
	if (!SlipResourceHost_Load(NULL, "CH_TRACK.SPR", &titleResource))
		SlipGame_ResourceFailure();
	SlipResourceModifyResult modifiedTitle = SlipResourceHost_Modify(NULL, titleResource);
	if (modifiedTitle.exit == SLIP_RESOURCE_MODIFY_DISPLACED_RETURN) {
		SlipResourceHost_ReportModifyContinuation(0x57496, 0x54388, NULL, modifiedTitle);
		return false;
	}
	titleResource = SlipResourceHost_ModifyReturnedSI(modifiedTitle);
	SlipText_SetColor(&SlipText_state, UINT16_MAX);
	SlipResourceHost_Lock(NULL, titleResource);
	SlipResourcePayload title = SlipResourceHost_Payload(titleResource);
	SlipSprite titleSprite;
	if (!SlipSprite_FromPayload(&title, &titleSprite))
		SlipRuntime_Fatal("Invalid campaign title host view.");
	SlipResourceHost_Unlock(NULL, titleResource);
	SlipResourceHost_LockWritable(NULL, titleResource);
	title = SlipResourceHost_Payload(titleResource);
	if (!SlipSprite_FromPayload(&title, &titleSprite))
		SlipRuntime_Fatal("Invalid campaign title host view.");
	RasterSurfaceBinding savedTitleSurface;
	Raster_BindSprite((uint8_t *)titleSprite.pixels, titleSprite.width, titleSprite.height, &savedTitleSurface);
	SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, 0, (int16_t)(titleSprite.width - 1));
	SlipTextPosition titlePosition = {0, 3};
	SlipText_Draw(&SlipText_state, titles[track - 1], NULL, &titlePosition);
	SlipResourceHost_Unlock(NULL, titleResource);
	Raster_RestoreScreen(&savedTitleSurface);
	SlipResourceHost_Release(NULL, titleFontResource);

	strcpy(name, names[track]);
	name[5] = afterPreview != 0 ? '1' : 'T';

	if (!SlipResourceHost_Load(NULL, name, &scriptResource))
		SlipGame_ResourceFailure();
	SlipResourceHost_LockWritable(NULL, scriptResource);
	script = SlipResourceHost_Payload(scriptResource);
	if (script.size < 16)
		SlipRuntime_Fatal("Invalid campaign ANN host view.");
	char stringBase[9];
	memcpy(stringBase, script.data, 8);
	stringBase[8] = 0;
	if (!SlipStringTable_Load(&SlipStringTable_state, stringBase, &menuStringResources, &strings))
		SlipGame_ResourceFailure();
	if (!SlipRaceIntro_PreloadResource(scriptResource, (uint16_t)SlipConfig_language, &resources))
		SlipGame_ResourceFailure();

	if (!SlipPresenter_Initialize(SlipBytes_ReadLE16(script.data + 8), archives, count))
		SlipGame_ResourceFailure();
	SlipPresenter_Queue(SlipPresenter_idle, sizeof(SlipPresenter_idle));

	if (!SlipResourceHost_Load(NULL, "STARS.SPR", &starsResource) ||
	    !SlipResourceHost_Load(NULL, "GLOBE.SHP", &globeResource) ||
	    !SlipResourceHost_Load(NULL, "FLAG.SHP", &flagResource))
		SlipGame_ResourceFailure();
	SlipResourceHost_Lock(NULL, starsResource);
	SlipResourcePayload stars = SlipResourceHost_Payload(starsResource);
	SlipSprite starSprite;
	if (!SlipSprite_FromPayload(&stars, &starSprite))
		SlipRuntime_Fatal("Invalid campaign stars host view.");
	SlipSprite_ApplyPalette(&starSprite);
	SlipResourceHost_Unlock(NULL, starsResource);
	Raster_SetClipRect(0, 0, 319, 199);

	uint32_t captionEdgeOrSpeechSize = SLIPSTREAM_SCREEN_HEIGHT - 1;
	SlipFrameTimer_Reset();
	for (;;) {
		SDL_Event event;
		bool quit = false;
		while (SlipSdl_PollEvent(&event))
			SlipSdlInput_ApplyEvent(&event, g_mainMenuRenderer, &quit);
		if (quit) {
			g_sdlQuitRequested = true;
			break;
		}
		SlipFrameTimer_Update((uint32_t)SlipSdl_TicksMs());
		SlipTrackGlobe_UpdateGivenMatrix(g_mainMenuResPath, track, &campaignGlobe, captionEdgeOrSpeechSize);
		vehicleResourceCalls.drawSprite(NULL, starsResource, 0, 0);
		SlipTrackGlobe_DrawGivenResources(g_mainMenuResPath, track, 0x4000, &campaignGlobe, globeResource,
		                                  flagResource);
		SlipPresenter_Draw(g_framebuffer, 320, 182, 25);
		vehicleResourceCalls.drawSprite(NULL, titleResource, INT16_MAX, 0);
		if (state.caption != 0 &&
		    !(SlipConfig_language != 0 && menuSound != NULL && menuSound->digitalCard != 0 && SlipConfig_speech != 0)) {

			SlipText_SelectResourceFont(&SlipText_state, SlipMenu_resources.smallFont, &mainMenuFontCalls);
			SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, 184, 308);
			const char *const caption = SlipStringTable_Get(strings, state.caption, &menuStringResources);
			SlipText_SetColor(&SlipText_state, 0x84);
			SlipText_DrawCentered(&SlipText_state, caption, NULL, 184, 127, 196);
			SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, 185, 309);
			SlipText_SetColor(&SlipText_state, 0x8d);
			SlipText_DrawCentered(&SlipText_state, caption, NULL, 185, 127, 196);
			SlipStringTable_Unlock(strings, &menuStringResources);
			captionEdgeOrSpeechSize = 309;
		}
		SlipMenu_PresentFrame();
		const uint16_t delta = (uint16_t)SlipFrameTimer_Values().deltaMilliseconds;
		const SlipRaceIntroScriptResult result =
		    SlipRaceIntro_StepPresenter(&state, script.data, script.size, delta, &host, (uint16_t)SlipConfig_language,
		                                SlipMenu_CampaignPresenterQueue, &captionEdgeOrSpeechSize);
		if (result == SLIP_RACE_INTRO_SCRIPT_INVALID)
			SlipRuntime_Fatal("Invalid presenter ANN command.");
		if (result == SLIP_RACE_INTRO_SCRIPT_FINISHED)
			break;
		SlipPresenter_Update(delta);
		if (SlipInput_TestAndClear(SlipInput_pressed, SLIP_INPUT_SCAN_ENTER) ||
		    SlipInput_TestAndClear(SlipInput_pressed, SLIP_INPUT_MOUSE_LEFT))
			break;
	}

	SlipResourceHost_Release(NULL, globeResource);
	SlipResourceHost_Release(NULL, flagResource);
	SlipResourceHost_Release(NULL, titleResource);
	SlipResourceHost_Release(NULL, starsResource);
	if (state.voice != 0 && menuSound != NULL) {
		if (menuSoundLock == NULL || menuSoundLock(menuSoundContext)) {
			SlipGameSound_Stop(menuSound, state.voice);
			if (menuSoundUnlock != NULL)
				menuSoundUnlock(menuSoundContext);
		}
	}
	SlipRaceIntro_ReleaseResource(scriptResource, &resources);
	SlipResourceHost_Unlock(NULL, scriptResource);
	SlipResourceHost_Release(NULL, scriptResource);
	SlipStringTable_Release(strings, &menuStringResources);
	SlipPresenter_Shutdown();
	SlipShape3D_Shutdown();
	SlipRenderer_Shutdown(&SlipRendererHost_state, &SlipRendererHost_lifecycleCalls);
	return true;
}

static uint32_t championshipStage;

static void SlipCampaign_Stage(SDL_Window *window, AppMode *mode, bool *redraw) {
	static const uint8_t stages[10] = {6, 1, 7, 8, 4, 9, 5, 2, 3, 10};
	const uint16_t track = stages[championshipStage - 1];
	char secondaryPath[512];
	const char *archives[2];
	const size_t count = SlipMenu_BuildArchiveList(g_mainMenuResPath, secondaryPath, archives);
	TrackViewResourceHandleRegistry registry = {.archives = archives, .archiveCount = count, .hostResources = true};
	SlipRaceIntroResources resources = {&registry, SlipMenu_CampaignSampleSize, SlipMenu_CampaignSampleLoad,
	                                    SlipMenu_CampaignSampleRelease};
	SlipRaceIntroScriptHost host = {
	    &registry, SlipMenu_CampaignVoiceStopped, SlipMenu_CampaignVoicePlay, SlipMenu_CampaignResetConsole, NULL,
	    NULL};
	SlipRacePlayer_track = track;
	g_selectedTrack = (int)championshipStage - 1;
	SDL_HideCursor();
	if (!SlipMenu_CampaignPresenter(track, 0))
		return;
	if (g_sdlQuitRequested)
		return;
	SlipMenuMusic_Stop();
	SlipRaceSession_PlayIntro(g_mainMenuResPath, track, (uint16_t)(g_selectedDriver + 1), (uint16_t)SlipConfig_language,
	                          (uint32_t)SlipConfig_environmentDetail, (uint32_t)SlipConfig_shading,
	                          (uint32_t)SlipConfig_textures, (uint32_t)SlipConfig_shadows,
	                          (uint32_t)SlipConfig_windowSize, SlipInput_held, SlipInput_pressed, &host, &resources);
	if (g_sdlQuitRequested)
		return;
	SlipMenuMusic_Start();
	SlipMenuMusic_Branch((uint32_t)(g_selectedDriver + 1));
	if (!SlipMenu_CampaignPresenter(track, 1))
		return;
	if (g_sdlQuitRequested)
		return;
	SDL_ShowCursor();
	SlipMenu_EnterGarageView(window, mode, redraw, 1u);
}

static void SlipCampaign_Continue(SDL_Window *window, AppMode *mode, bool *redraw) {
	championshipStage = 1; /* No command-line stage override is bound. */
	SlipRace_racerTable.racerCount = (uint16_t)SlipRace_racerCount;
	SlipRace_BuildRacerTable(&SlipRace_racerTable, (uint16_t)(g_selectedDriver + 1), 0);
	SlipCampaign_Stage(window, mode, redraw);
}

static void SlipDriverSelect_Action(SDL_Window *window, AppMode *mode, int action, int *hoveredButton, bool *redraw) {
	if (driverCardResourcesActive && menuSound != NULL)
		SlipGameSound_Stop(menuSound, vehicleResources.voice);
	if (action == 0) {
		if (driverCardResourcesActive)
			SlipVehicleSelector_CardRelease(&vehicleResources, &vehicleResourceCalls);
		driverCardResourcesActive = false;
		SlipMenu_ReleaseVehicleResources();
		/* The SDL event already consumed the key/click that accepted the driver. */
		SlipInput_pressed[SLIP_INPUT_SCAN_ENTER] = false;
		SlipInput_pressed[SLIP_INPUT_MOUSE_LEFT] = false;
		if (g_selectedRaceType == ONE_PLAYER_RACE_CHAMPIONSHIP)
			SlipCampaign_Continue(window, mode, redraw);
		else {

			if (g_excludedDriver == 0) {
				g_playerOneDriver = (uint16_t)(g_selectedDriver + 1);
				g_playerTwoDriver = 0;
				if (SlipRace_gameMode == 1) {
					SlipMenu_EnterVehicleSelect(window, mode, g_selectedRaceType, hoveredButton, redraw,
					                            g_playerOneDriver);
					return;
				}
			} else {
				g_playerTwoDriver = (uint16_t)(g_selectedDriver + 1);
			}
			g_selectedDriver = g_playerOneDriver - 1;
			SlipMenu_EnterTrackSelect(window, mode, APP_MODE_DRIVER_SELECT, redraw);
		}
	} else if (action == 1) {

		SlipMenuMusic_Branch(0);
		if (driverCardResourcesActive) {
			void *const context = vehicleResourceCalls.context;
			RasterSurfaceBounds bounds = Raster_GetSurfaceBounds();
			Raster_SetClipRect((int16_t)bounds.left, (int16_t)bounds.top, (int16_t)bounds.right,
			                   (int16_t)bounds.bottom);
			for (int16_t scale = 0x4000; scale >= 0; scale -= 0x400) {
				vehicleResourceCalls.poll(context);
				vehicleResourceCalls.drawSprite(context, vehicleResources.assets.driverBackground, 0, 0);
				vehicleResourceCalls.zoom(context, vehicleResources.card, scale, vehicleResources.zoomCenter,
				                          (SlipSelectorPoint){27, 10});
				vehicleResourceCalls.present(context);
			}
			if (vehicleResources.speech != 0) {
				SlipResourceHost_Unlock(NULL, vehicleResources.speech);
				SlipResourceHost_Release(NULL, vehicleResources.speech);
			}
			SlipResourceHost_Release(NULL, vehicleResources.card);
			for (unsigned hoverSpriteIndex = 0; hoverSpriteIndex < 3; ++hoverSpriteIndex)
				SlipResourceHost_Release(NULL, vehicleResources.hover[hoverSpriteIndex]);
			driverCardResourcesActive = false;
			vehicleResources.confirmed = 0;
		}
		*mode = APP_MODE_VEHICLE_SELECT;
		g_hoveredVehicle = -1;
		g_vehicleFocus = FOCUS_NONE;
		SlipMenu_VehicleDoorAnimationBegin();
		*hoveredButton = -1;
		g_screenStartMs = SlipSdl_TicksMs();
		*redraw = true;
		SlipMenu_SetStatusWindowTitle(window, "Select your vehicle");
	} else {
		SlipMenu_EnterVehicleView(window, mode, redraw);
	}
}

/* Diagnostic entry exercises the same Accept dispatch as keyboard/mouse input. */
bool SlipMenu_DebugAcceptSplitDrivers(void) {
	AppMode mode = APP_MODE_DRIVER_SELECT;
	int hovered = -1;
	bool redraw = false;
	g_selectedRaceType = ONE_PLAYER_RACE_SINGLE;
	SlipRace_gameMode = 1;
	g_excludedDriver = 0;
	g_selectedDriver = 3;
	SlipDriverSelect_Action(NULL, &mode, 0, &hovered, &redraw);
	if (mode != APP_MODE_VEHICLE_SELECT || g_excludedDriver != 4 || g_playerOneDriver != 4 || g_playerTwoDriver != 0)
		return false;
	g_selectedDriver = 7;
	mode = APP_MODE_DRIVER_SELECT;
	SlipDriverSelect_Action(NULL, &mode, 0, &hovered, &redraw);
	if (mode != APP_MODE_TRACK_SELECT || g_playerTwoDriver != 8)
		return false;
	SlipRaceRacerTable racers = {0};
	racers.racerCount = 2;
	SlipRace_BuildRacerTable(&racers, g_playerOneDriver, g_playerTwoDriver);
	return racers.records[0].tuningIndex == 4 && racers.records[1].tuningIndex == 8 &&
	       racers.records[0].racerType == 0 && racers.records[1].racerType == 1;
}

bool SlipMenu_DebugAcceptChampionship(void) {
	AppMode mode = APP_MODE_DRIVER_SELECT;
	int hovered = -1;
	bool redraw = false;
	g_selectedRaceType = ONE_PLAYER_RACE_CHAMPIONSHIP;
	SlipRace_type = SLIP_RACE_TYPE_CHAMPIONSHIP;
	SlipRace_gameMode = 0;
	SlipRace_racerCount = 10;
	g_selectedDriver = 0;
	SlipDriverSelect_Action(NULL, &mode, 0, &hovered, &redraw);
	return mode == APP_MODE_GARAGE && !g_sdlQuitRequested;
}

/* Diagnostic branch coverage; these are the same render calls used by the UI. */
static bool SlipMenu_DebugRenderGaragePanel(const char *resPath, int panel, int focusedItem, const char *label) {
	memset(g_framebuffer, 0, sizeof(g_framebuffer));
	const FocusSource savedFocus = g_garagePanelFocus;
	g_garagePanelFocus = FOCUS_KEYBOARD;
	bool rendered = SlipMenu_DrawGarageView(resPath, g_selectedDriver, 0, -1, panel, focusedItem, focusedItem);
	g_garagePanelFocus = savedFocus;
	if (!rendered)
		return false;
	uint32_t hash = 2166136261u;
	for (size_t pixelIndex = 0; pixelIndex < sizeof(g_framebuffer); ++pixelIndex)
		hash = (hash ^ g_framebuffer[pixelIndex]) * 16777619u;
	printf("garage_render panel=%s focused=%d font=%u resource_backed=%u pixels_fnv1a=%08x\n", label, focusedItem,
	       SlipText_state.fontResource, SlipText_state.fontResources == &SlipRaceHud_fontResources, hash);
	return SlipText_state.fontResources == &SlipRaceHud_fontResources &&
	       SlipText_state.fontResource == g_garageResultsFont;
}

/* Diagnostic only: exercise the two real garage continuations and purchases. */
bool SlipMenu_DebugSplitGarage(const char *resPath) {
	if (!SlipMenu_DebugAcceptSplitDrivers())
		return false;
	AppMode mode = APP_MODE_TRACK_SELECT;
	bool redraw = false;
	SlipRace_racerCount = 2;
	SlipRace_type = SLIP_RACE_TYPE_SINGLE;
	SlipMenu_AcceptTrackSelection(resPath, NULL, &mode, 0, &redraw);
	if (mode != APP_MODE_GARAGE || g_garageRacer != &SlipRace_racerTable.records[0] || g_garageCash != 3000 ||
	    g_selectedDriver != 3)
		return false;
	SlipMenu_SelectGarageAction(resPath, NULL, &mode, 0, &redraw);
	SlipMenu_SelectGaragePanelItem(NULL, 0, &redraw);
	if (!SlipMenu_DebugRenderGaragePanel(resPath, GARAGE_PANEL_WEAPON_GRID, 0, "weapons"))
		return false;
	SlipMenu_SelectGaragePanelItem(NULL, 0, &redraw);
	const uint32_t firstCash = SlipRace_racerTable.records[0].bonusScore;
	const uint32_t firstAmmo = SlipRace_racerTable.records[0].primaryWeaponAmmo;
	if (firstCash >= 3000 || firstAmmo == 0 || SlipRace_racerTable.records[0].primaryWeaponIndex != 1)
		return false;

	SlipMainMenu_hiddenToggle = 1;
	SlipMenu_SelectGaragePanelItem(NULL, 0, &redraw);
	SlipMenu_SelectGaragePanelItem(NULL, 0, &redraw);
	SlipMainMenu_hiddenToggle = 0;
	if (g_garageCash != (int)firstCash || g_garageRacer->bonusScore != firstCash)
		return false;
	/* Signed insufficient-cash rejection and free turbo purchase. */
	g_garageCash = -1;
	g_garageRacer->bonusScore = UINT32_MAX;
	SlipMenu_SelectGaragePanelItem(NULL, 0, &redraw);
	SlipMenu_SelectGaragePanelItem(NULL, 1, &redraw);
	if (g_garageCash != -1 || g_garageRacer->primaryWeaponIndex != 1 || g_garagePanel != GARAGE_PANEL_WEAPON_GRID)
		return false;
	SlipMenu_SelectGarageAction(resPath, NULL, &mode, 1, &redraw);
	if (!SlipMenu_DebugRenderGaragePanel(resPath, GARAGE_PANEL_TURBO, 0, "turbo"))
		return false;
	SlipMainMenu_hiddenToggle = 1;
	SlipMenu_SelectGaragePanelItem(NULL, 4, &redraw);
	SlipMainMenu_hiddenToggle = 0;
	if (g_garageCash != -1 || g_garageRacer->bonusScore != UINT32_MAX || g_garageRacer->powerupRecord != 4 ||
	    g_garagePanel != GARAGE_PANEL_NONE)
		return false;
	SlipMenu_SelectGarageAction(resPath, NULL, &mode, 1, &redraw);
	SlipMenu_SelectGaragePanelItem(NULL, 4, &redraw);
	if (g_garagePanel != GARAGE_PANEL_TURBO || g_garageCash != -1)
		return false;
	if (!SlipMenu_DebugRenderGaragePanel(resPath, GARAGE_PANEL_TURBO, 4, "turbo-equipped"))
		return false;
	SlipMenu_SelectGaragePanelItem(NULL, 0, &redraw);
	if (g_garagePanel != GARAGE_PANEL_TURBO || g_garageRacer->powerupRecord != 4)
		return false;
	SlipMenu_SelectGaragePanelItem(NULL, 5, &redraw);
	if (g_garagePanel != GARAGE_PANEL_NONE)
		return false;
	SlipMenu_SelectGarageAction(resPath, NULL, &mode, 2, &redraw);
	if (!SlipMenu_DebugRenderGaragePanel(resPath, GARAGE_PANEL_SYSTEMS, 0, "systems"))
		return false;
	const uint32_t originalSystems = g_garageRacer->powerupFlags;
	SlipMainMenu_hiddenToggle = 1;
	SlipMenu_SelectGaragePanelItem(NULL, 0, &redraw);
	SlipMainMenu_hiddenToggle = 0;
	if (g_garageCash != -1 || g_garageSystems != (originalSystems | 1u) ||
	    g_garageRacer->powerupFlags != originalSystems)
		return false;
	if (!SlipMenu_DebugRenderGaragePanel(resPath, GARAGE_PANEL_SYSTEMS, 0, "systems-fitted"))
		return false;
	SlipMenu_SelectGaragePanelItem(NULL, 3, &redraw);
	if (g_garageRacer->powerupFlags != (originalSystems | 1u) || g_garageRacer->bonusScore != UINT32_MAX)
		return false;
	g_garageCash = (int)firstCash;
	g_garageRacer->bonusScore = firstCash;
	SlipMenu_SelectGarageAction(resPath, NULL, &mode, 3, &redraw);
	if (mode != APP_MODE_GARAGE || g_garageRacer != &SlipRace_racerTable.records[1] || g_garageCash != 3000 ||
	    g_selectedDriver != 7 || g_garageWeaponSlots[0] != -1)
		return false;
	SlipMenu_SelectGarageAction(resPath, NULL, &mode, 0, &redraw);
	SlipMenu_SelectGaragePanelItem(NULL, 1, &redraw);
	SlipMenu_SelectGaragePanelItem(NULL, 1, &redraw);
	const uint32_t secondCash = SlipRace_racerTable.records[1].bonusScore;
	const uint32_t secondAmmo = SlipRace_racerTable.records[1].secondaryWeaponAmmo;
	SlipMenu_SelectGarageAction(resPath, NULL, &mode, 3, &redraw);
	return mode == APP_MODE_RACE && firstCash == SlipRace_racerTable.records[0].bonusScore &&
	       firstAmmo == SlipRace_racerTable.records[0].primaryWeaponAmmo && secondCash < 3000 &&
	       secondCash == SlipRace_racerTable.records[1].bonusScore && secondAmmo != 0 &&
	       secondAmmo == SlipRace_racerTable.records[1].secondaryWeaponAmmo &&
	       SlipRace_racerTable.records[0].primaryWeaponIndex == 1 &&
	       SlipRace_racerTable.records[1].secondaryWeaponIndex == 2;
}

static bool haveMainMenu;
static bool redraw = true;
static AppMode appMode = APP_MODE_MENU;
static int hoveredButton = -1;

const char *SlipMenu_FindResPath(int argc, char **argv) { return SlipMenu_FindResPathInternal(argc, argv); }

void SlipMenu_Init(const char *resPath, SDL_Window *window, SDL_Renderer *renderer,
                   SlipMenuSdlPresentFrame presentFrame, void *presentFrameContext) {
	g_mainMenuResPath = resPath;
	g_mainMenuWindow = window;
	g_mainMenuRenderer = renderer;
	g_mainMenuPresentFrame = presentFrame;
	g_mainMenuPresentFrameContext = presentFrameContext;
	haveMainMenu = false;
	redraw = true;
	appMode = APP_MODE_MENU;
	hoveredButton = -1;
	g_sdlQuitRequested = false;
	Raster_SetScreenBufferRows(g_framebuffer, SLIPSTREAM_SCREEN_WIDTH);
	if (resPath != NULL && SlipMenu_LoadMainMenuModel(resPath)) {
		haveMainMenu = true;
	}
	if (!haveMainMenu) {
		fprintf(stderr, "Pass SLIPSTRM.RES as argv[1] or set SLIPSTREAM5000_RES.\n");
	}
	SlipMenuMusic_SetSetting((uint16_t)SlipConfig_music);
	if (haveMainMenu)
		SlipMenuMusic_Start();
}

void SlipMenu_HandleEvent(const char *resPath, SDL_Window *window, SDL_Renderer *renderer, const SDL_Event *event,
                          bool *running) {
	SlipInputCode scanCode = SLIP_INPUT_SCAN_NONE;
	SlipInputCode mouseInputCode = SLIP_INPUT_SCAN_NONE;
	bool dispatchInputToRace = haveMainMenu && appMode == APP_MODE_RACE;
	bool quitRequested = false;

	SlipSdlInput_ApplyEvent(event, renderer, &quitRequested);
	if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
		scanCode = SlipMenu_SdlKeyToDosScan(SlipMenu_EventKeycode(event));
	}
	if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
		mouseInputCode = SlipMenu_SdlMouseButtonToInputCode(event->button.button);
	}

	if (haveMainMenu && (appMode == APP_MODE_MENU || appMode == APP_MODE_RESULTS)) {
		if (quitRequested) {
			*running = false;
		}
		return;
	}
	if (quitRequested) {
		*running = false;
	} else if (event->type == SDL_EVENT_KEY_DOWN) {
		if (scanCode < 0 || !SlipInput_pressed[scanCode]) {
			return;
		}

		if (scanCode == SLIP_INPUT_SCAN_ESCAPE) {
			if (haveMainMenu && appMode == APP_MODE_RACE) {
				redraw = true;
			} else if (haveMainMenu && appMode == APP_MODE_GARAGE) {
				if (g_garagePanel >= 0) {
					SlipMenu_LeaveGaragePanel(window, &redraw);
				} else {
					appMode = APP_MODE_TRACK_SELECT;
					SlipMenu_TrackButtonSetup();
					g_hoveredGarageAction = -1;
					g_hoveredGaragePanelItem = -1;
					g_trackFocus = FOCUS_NONE;
					redraw = true;
					SlipMenu_SetStatusWindowTitle(window, "Choose Track");
				}
			} else if (haveMainMenu && appMode == APP_MODE_TRACK_SELECT) {
				SlipMenu_TrackButtonRelease();
				if (g_selectedRaceType != ONE_PLAYER_RACE_CHAMPIONSHIP) {

					SlipMenu_EnterVehicleSelect(window, &appMode, g_selectedRaceType, &hoveredButton, &redraw, 0);
				} else {
					appMode = g_trackReturnMode;
					g_hoveredTrack = -1;
					redraw = true;
					if (appMode == APP_MODE_VEHICLE_VIEW) {
						SlipMenu_SetDriverWindowTitle(window, "Vehicle view", g_selectedDriver);
					} else {
						appMode = APP_MODE_DRIVER_SELECT;
						g_hoveredDriverAction = -1;
						g_trackFocus = FOCUS_NONE;
						SlipMenu_SetDriverWindowTitle(window, "Driver selection", g_selectedDriver);
					}
				}
			} else if (haveMainMenu && appMode == APP_MODE_VEHICLE_VIEW) {
				SlipMenu_DriverViewReturn();
				appMode = APP_MODE_DRIVER_SELECT;
				g_hoveredDriverAction = -1;
				redraw = true;
				SlipMenu_SetDriverWindowTitle(window, "Driver selection", g_selectedDriver);
			} else if (haveMainMenu && appMode == APP_MODE_DRIVER_SELECT) {
				SlipDriverSelect_Action(window, &appMode, 1, &hoveredButton, &redraw);
			} else if (haveMainMenu && appMode == APP_MODE_VEHICLE_TO_DRIVER) {

			} else if (haveMainMenu && appMode == APP_MODE_VEHICLE_SELECT) {
				vehicleResources.animation.selectedVehicle = 0;
				SlipMenu_ReleaseVehicleResources();
				if (g_excludedDriver != 0) {

					SlipMenu_EnterVehicleSelect(window, &appMode, g_selectedRaceType, &hoveredButton, &redraw, 0);
				} else {
					appMode = APP_MODE_MENU;
					hoveredButton = -1;
					redraw = true;
				}
			} else {
				*running = false;
			}
		} else if (haveMainMenu && appMode == APP_MODE_VEHICLE_SELECT &&
		           (scanCode == SLIP_INPUT_SCAN_ENTER || scanCode == SLIP_INPUT_SCAN_SPACE)) {
			const int vehicle = SlipMenu_FocusedItemIndex(g_vehicleFocus, g_selectedDriver, g_hoveredVehicle);
			if (vehicle >= 0) {
				g_selectedDriver = vehicle;
			}
			if (g_selectedDriver + 1 != g_excludedDriver)
				SlipMenu_EnterVehicleToDriver(window, &appMode, g_selectedDriver, &redraw);
		} else if (haveMainMenu && appMode == APP_MODE_DRIVER_SELECT &&
		           (scanCode == SLIP_INPUT_SCAN_DOWN || scanCode == SLIP_INPUT_SCAN_UP)) {
			if (scanCode == SLIP_INPUT_SCAN_DOWN) {
				g_selectedDriverAction = (g_selectedDriverAction + 1) % kDriverActionCount;
			} else {
				g_selectedDriverAction = (g_selectedDriverAction + kDriverActionCount - 1) % kDriverActionCount;
			}
			g_hoveredDriverAction = -1;
			redraw = true;
		} else if (haveMainMenu && appMode == APP_MODE_DRIVER_SELECT &&
		           (scanCode == SLIP_INPUT_SCAN_ENTER || scanCode == SLIP_INPUT_SCAN_SPACE)) {
			int action = g_hoveredDriverAction >= 0 ? g_hoveredDriverAction : g_selectedDriverAction;
			if (action == 3)
				action = 0;
			SlipDriverSelect_Action(window, &appMode, action, &hoveredButton, &redraw);
		} else if (haveMainMenu && appMode == APP_MODE_VEHICLE_VIEW &&
		           (scanCode == SLIP_INPUT_SCAN_ENTER || scanCode == SLIP_INPUT_SCAN_SPACE)) {
			SlipMenu_DriverViewReturn();
			appMode = APP_MODE_DRIVER_SELECT;
			g_hoveredDriverAction = -1;
			redraw = true;
			SlipMenu_SetDriverWindowTitle(window, "Driver selection", g_selectedDriver);
		} else if (haveMainMenu && appMode == APP_MODE_TRACK_SELECT && scanCode == SLIP_INPUT_SCAN_ENTER) {
			const int track = SlipMenu_FocusedItemIndex(g_trackFocus, g_selectedTrack, g_hoveredTrack);
			if (track >= 0) {
				SlipMenu_AcceptTrackSelection(resPath, window, &appMode, track, &redraw);
			}
		} else if (haveMainMenu && appMode == APP_MODE_GARAGE &&
		           (scanCode == SLIP_INPUT_SCAN_ENTER || scanCode == SLIP_INPUT_SCAN_SPACE)) {
			if (g_garagePanel >= 0) {
				const int item =
				    SlipMenu_FocusedItemIndex(g_garagePanelFocus, g_selectedGaragePanelItem, g_hoveredGaragePanelItem);
				if (item >= 0) {
					SlipMenu_SelectGaragePanelItem(window, item, &redraw);
				}
			} else {
				const int action =
				    SlipMenu_FocusedItemIndex(g_garageActionFocus, g_selectedGarageAction, g_hoveredGarageAction);
				if (action >= 0) {
					SlipMenu_SelectGarageAction(resPath, window, &appMode, action, &redraw);
				}
			}
		}
		if (!dispatchInputToRace) {
			SlipInput_pressed[scanCode] = false;
		}
	}

	else if (haveMainMenu && appMode == APP_MODE_VEHICLE_SELECT && event->type == SDL_EVENT_MOUSE_MOTION) {
		int x;
		int y;
		int hit;
		if (SlipMenu_WindowToLogical(renderer, event->motion.x, event->motion.y, &x, &y)) {
			hit = SlipMenu_HitTestVehicleZone(resPath, x, y);
			if (g_vehicleFocus != FOCUS_MOUSE || hit != g_hoveredVehicle) {
				g_vehicleFocus = FOCUS_MOUSE;
				g_hoveredVehicle = hit;
				redraw = true;
			}
		}
	} else if (haveMainMenu && appMode == APP_MODE_TRACK_SELECT && event->type == SDL_EVENT_MOUSE_MOTION) {
		int x;
		int y;
		int hit;
		if (SlipMenu_WindowToLogical(renderer, event->motion.x, event->motion.y, &x, &y)) {
			hit = SlipMenu_HitTestTrackButton(x, y);
			if (g_trackFocus != FOCUS_MOUSE || hit != g_hoveredTrack) {
				g_trackFocus = FOCUS_MOUSE;
				g_hoveredTrack = hit;
				redraw = true;
			}
		}
	} else if (haveMainMenu && appMode == APP_MODE_GARAGE && g_garagePanel < 0 &&
	           event->type == SDL_EVENT_MOUSE_MOTION) {
		int x;
		int y;
		int hit;
		if (SlipMenu_WindowToLogical(renderer, event->motion.x, event->motion.y, &x, &y)) {
			hit = SlipMenu_HitTestGarageAction(x, y);
			if (g_garageActionFocus != FOCUS_MOUSE || hit != g_hoveredGarageAction) {
				g_garageActionFocus = FOCUS_MOUSE;
				g_hoveredGarageAction = hit;
				redraw = true;
			}
		}
	} else if (haveMainMenu && appMode == APP_MODE_GARAGE && g_garagePanel >= 0 &&
	           event->type == SDL_EVENT_MOUSE_MOTION) {
		int x;
		int y;
		int hit;
		if (SlipMenu_WindowToLogical(renderer, event->motion.x, event->motion.y, &x, &y)) {
			hit = SlipMenu_HitTestGaragePanelItem(g_garagePanel, x, y);
			if (g_garagePanelFocus != FOCUS_MOUSE || hit != g_hoveredGaragePanelItem) {
				g_garagePanelFocus = FOCUS_MOUSE;
				g_hoveredGaragePanelItem = hit;
				redraw = true;
			}
		}
	} else if (haveMainMenu && appMode == APP_MODE_VEHICLE_SELECT && event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
	           mouseInputCode == SLIP_INPUT_MOUSE_LEFT) {
		int x;
		int y;
		int hit;
		if (SlipMenu_WindowToLogical(renderer, event->button.x, event->button.y, &x, &y)) {
			hit = SlipMenu_HitTestVehicleZone(resPath, x, y);
			if (hit >= 0) {
				g_selectedDriver = hit;
				g_hoveredVehicle = hit;
				g_vehicleFocus = FOCUS_MOUSE;
				SlipMenu_EnterVehicleToDriver(window, &appMode, hit, &redraw);
			}
		}
	} else if (haveMainMenu && appMode == APP_MODE_DRIVER_SELECT && event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
	           mouseInputCode == SLIP_INPUT_MOUSE_LEFT) {
		int x;
		int y;
		int hit;
		if (SlipMenu_WindowToLogical(renderer, event->button.x, event->button.y, &x, &y)) {
			hit = SlipMenu_HitTestDriverAction(x, y);
			if (hit == 3) {
				if (vehicleResources.speech != 0 && menuSound != NULL) {
					SlipGameSound_Stop(menuSound, vehicleResources.voice);
					uint32_t bytes;
					SlipResourceHost_Size(NULL, vehicleResources.speech, &bytes);
					vehicleResources.voice = SlipGameSound_PlayAlternate(menuSound, vehicleResources.speechData, bytes);
				}
				return;
			}
			if (hit >= 0) {
				g_hoveredDriverAction = hit;
				g_selectedDriverAction = hit;
				SlipDriverSelect_Action(window, &appMode, hit, &hoveredButton, &redraw);
			}
		}
	} else if (haveMainMenu && appMode == APP_MODE_TRACK_SELECT && event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
	           mouseInputCode == SLIP_INPUT_MOUSE_LEFT) {
		int x;
		int y;
		int hit;
		if (SlipMenu_WindowToLogical(renderer, event->button.x, event->button.y, &x, &y)) {
			hit = SlipMenu_HitTestTrackButton(x, y);
			if (hit >= 0) {
				g_trackFocus = FOCUS_MOUSE;
				g_hoveredTrack = hit;
				SlipMenu_AcceptTrackSelection(resPath, window, &appMode, hit, &redraw);
			}
		}
	} else if (haveMainMenu && appMode == APP_MODE_GARAGE && g_garagePanel < 0 &&
	           event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && mouseInputCode == SLIP_INPUT_MOUSE_LEFT) {
		int x;
		int y;
		int hit;
		if (SlipMenu_WindowToLogical(renderer, event->button.x, event->button.y, &x, &y)) {
			hit = SlipMenu_HitTestGarageAction(x, y);
			if (hit >= 0) {
				g_garageActionFocus = FOCUS_MOUSE;
				g_hoveredGarageAction = hit;
				SlipMenu_SelectGarageAction(resPath, window, &appMode, hit, &redraw);
			}
		}
	} else if (haveMainMenu && appMode == APP_MODE_GARAGE && g_garagePanel >= 0 &&
	           event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && mouseInputCode == SLIP_INPUT_MOUSE_LEFT) {
		int x;
		int y;
		int hit;
		if (SlipMenu_WindowToLogical(renderer, event->button.x, event->button.y, &x, &y)) {
			hit = SlipMenu_HitTestGaragePanelItem(g_garagePanel, x, y);
			if (hit >= 0) {
				g_garagePanelFocus = FOCUS_MOUSE;
				g_hoveredGaragePanelItem = hit;
				SlipMenu_SelectGaragePanelItem(window, hit, &redraw);
			}
		}
	}
	if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && mouseInputCode >= 0 && !dispatchInputToRace) {
		SlipInput_pressed[mouseInputCode] = false;
	}
}

static bool SlipMenuSound_Initialize(const char *resPath) {
	(void)resPath;
	if (menuSelectActive)
		return true;
	menuSelectActive = true;
	if (menuSound == NULL || menuSound->digitalCard == 0) {
		menuSelect = 0;
		return true;
	}
	if (!SlipResourceHost_Load(NULL, "SELECT.SMP", &menuSelect))
		SlipGame_ResourceFailure();
	uint32_t bytes;
	SlipResourceHost_Size(NULL, menuSelect, &bytes);
	menuSelectPayload.data = SlipResourceHost_Lock(NULL, menuSelect);
	menuSelectPayload.size = bytes;
	menuSelectVoice = 0;
	return true;
}

static bool SlipMainMenu_Attract(const char *resPath) {
	const uint16_t savedLaps = SlipRacePlayer_lapCount;
	SlipRacePlayer_lapCount = 1;
	SlipRace_demoChaseEnabled = 1;
	SlipRacePlayer_demoMode = 1;
	bool running = true;
	do {
		SlipRacePlayer_demoAiEnabled = 1;
		SlipRace_type = SLIP_RACE_TYPE_SINGLE;
		SlipRace_gameMode = 0;
		uint16_t track, driver;
		do {
			track = (uint16_t)(SlipRandom_Range(9) + 1);
		} while (track == SlipRacePlayer_track);
		SlipRacePlayer_track = track;
		do {
			driver = (uint16_t)(SlipRandom_Range(9) + 1);
		} while (driver == g_playerOneDriver);
		g_playerOneDriver = driver;
		g_playerTwoDriver = 0;
		SlipRace_racerTable.racerCount = (uint16_t)SlipRace_racerCount;
		SlipRace_BuildRacerTable(&SlipRace_racerTable, driver, 0);
		SDL_HideCursor();
		SlipMenuMusic_Stop();
		SlipMenuSound_Release();
		SlipRaceSession_StartNew(resPath, track, &SlipRace_racerTable, SlipConfig_environmentDetail, SlipConfig_shading,
		                         SlipConfig_textures, SlipConfig_shadows);
		appMode = APP_MODE_RACE;
		for (;;) {
			running = SlipMenu_PollInput();
			if (!running)
				break;
			SlipInputPointerPosition pointer = SlipInput_Pointer();
			const SlipRaceFrameResult result =
			    SlipRaceSession_RunFrame((uint32_t)SlipSdl_TicksMs(), SlipInput_held, SlipInput_pressed,
			                             SlipConfig_windowSize, pointer.x, pointer.y);
			if (result == SLIP_RACE_FRAME_EXIT_PROGRAM) {
				running = false;
				break;
			}
			if (result == SLIP_RACE_FRAME_ENDED)
				break;
		}
		appMode = APP_MODE_MENU;
	} while (running && SlipRaceSession_exitRequested == 0);
	SlipRacePlayer_lapCount = savedLaps;
	SlipRacePlayer_demoAiEnabled = 0;
	SlipRace_demoChaseEnabled = 0;
	SlipRacePlayer_demoMode = 0;
	SDL_ShowCursor();
	return running;
}

bool SlipMenu_UpdateAndDraw(const char *resPath, SDL_Window *window) {
	if (g_sdlQuitRequested) {
		return false;
	}
	if (haveMainMenu && appMode == APP_MODE_MENU) {
		int menuResult;

		if (!SlipMenuSound_Initialize(resPath))
			return false;
		menuResult = SlipMainMenu_RunFlow();

		if (g_sdlQuitRequested) {
			return false;
		}

		switch (menuResult) {
		case -1:
			SlipMenuMusic_Stop();
			SlipMenuSound_Release();
			SDL_HideCursor();
			if (!SlipMainMenu_Attract(resPath))
				return false;
			SlipMenuMusic_Start();
			redraw = true;
			break;
		case MAIN_MENU_DISPATCH_RESTART:
			SlipMenuMusic_Stop();
			SlipMenuSound_Release();
			SDL_HideCursor();
			if (!SlipSdl_RunStartupIntro(resPath))
				return false;
			SDL_ShowCursor();
			SlipMenuMusic_Start();
			hoveredButton = -1;
			redraw = true;
			break;
		case MAIN_MENU_DISPATCH_RACE_SETUP:
			g_selectedRaceType = (OnePlayerRaceType)(SlipRace_type - SLIP_RACE_TYPE_PRACTICE);
			SlipMenu_EnterVehicleSelect(window, &appMode, g_selectedRaceType, &hoveredButton, &redraw, 0);
			break;
		case MAIN_MENU_DISPATCH_SHOWCASE: {
			char secondaryPath[512];
			const char *archives[2];
			const size_t archiveCount = SlipMenu_BuildArchiveList(resPath, secondaryPath, archives);
			SlipView3DMaths maths = {0};
			if (!SlipView3D_LoadMathsFromArchives(&maths, archives, archiveCount))
				return false;
			SlipLapRecordsFrameCalls calls;
			SlipLapRecordsHost_BindRuntime(&calls, &maths);
			SlipLapRecords_Show(&SlipLapRecordsHost_screen, &SlipConfig_lapRecords, 0, &SlipStringTable_state,
			                    &SlipLapRecordsHost_lifecycle, &calls);
			SlipView3D_FreeMaths(&maths);
			hoveredButton = -1;
			redraw = true;
			break;
		}
		case MAIN_MENU_DISPATCH_LOAD_GAME: {
			bool loaded = SlipSavedGames_Run(resPath, &SlipRace_racerTable, &championshipStage);
			SlipMenuMusic_Stop();
			SlipMenuSound_Release();
			if (loaded) {
				SlipRace_type = SLIP_RACE_TYPE_CHAMPIONSHIP;
				SlipRace_gameMode = 0;
				for (uint16_t racerIndex = 0; racerIndex < SlipRace_racerTable.racerCount; ++racerIndex) {
					const SlipRaceRacerState *const racer = &SlipRace_racerTable.records[racerIndex];
					if (racer->racerType == 0) {
						g_playerOneDriver = racer->tuningIndex;
						g_playerTwoDriver = 0;
						g_selectedDriver = g_playerOneDriver - 1;
						g_selectedRaceType = ONE_PLAYER_RACE_CHAMPIONSHIP;
						goto championshipStandings;
					}
				}
			}
			SlipMenuMusic_Start();
			hoveredButton = -1;
			redraw = true;
			break;
		}
		case MAIN_MENU_DISPATCH_CONFIGURATION:
			SlipConfigMenu_Run(&SlipConfigMenu_state, &SlipConfigHost_calls);

			if (SlipConfig_Music() == 0)
				SlipMenuMusic_StopRetainingSong();
			else
				SlipMenuMusic_Resume();

			SlipRandom_Stir(SlipDebug_BiosTickLow());
			SlipMenuMusic_Start();
			return true;
		}
	}
	if (haveMainMenu && appMode == APP_MODE_VEHICLE_TO_DRIVER) {
		if (g_vehicleZoomScale > DRIVER_ZOOM_COMPLETE) {
			SlipMenu_EnterDriverSelect(window, &appMode, &hoveredButton, &redraw);
		}
	}

	if (haveMainMenu) {

		bool raceFrame = appMode == APP_MODE_RACE || appMode == APP_MODE_RESULTS;
		if (appMode == APP_MODE_VEHICLE_SELECT) {
			SlipMenu_DrawVehicleSelect(resPath, false);
		} else if (appMode == APP_MODE_VEHICLE_TO_DRIVER) {
			if (!SlipMenu_DrawVehicleToDriverTransition(resPath, g_transitionDriver))
				return false; /* Host cannot execute the displaced guest RET. */
		} else if (appMode == APP_MODE_DRIVER_SELECT) {
			SlipMenu_DrawDriverSelect(resPath, g_selectedDriver);
		} else if (appMode == APP_MODE_VEHICLE_VIEW) {
			TrackView_DrawVehicleView(resPath, g_selectedDriver);
		} else if (appMode == APP_MODE_TRACK_SELECT) {
			SlipMenu_DrawTrackSelectFrame(resPath, g_selectedTrack, g_hoveredTrack, g_trackFocus);
		} else if (appMode == APP_MODE_RACE) {

			const uint64_t nowMs = SlipSdl_TicksMs();
			const SlipRaceFrameResult raceResult =
			    SlipRaceSession_RunFrame((uint32_t)nowMs, SlipInput_held, SlipInput_pressed,
			                             (uint32_t)SlipConfig_windowSize, SlipInput_Pointer().x, SlipInput_Pointer().y);

			switch (raceResult) {
			case SLIP_RACE_FRAME_OPEN_CONFIGURATION:
				SlipMenu_RaceConfiguration();
				SlipRaceSession_ConfigurationReturn();
				redraw = true;
				break;
			case SLIP_RACE_FRAME_ENDED:
				SDL_ShowCursor();
				if (returnToResultsAfterReplay ||
				    (SlipRace_type != SLIP_RACE_TYPE_PRACTICE && SlipRaceSession_exitRequested == 0)) {
					if (!returnToResultsAfterReplay) {

						SlipRaceResults_Begin(&resultsScreen, SlipRaceSession_track, &SlipRace_racerTable,
						                      g_playerOneDriver, resPath, menuSound);
					} else {

						SlipRaceResults_BeginDisplay(&resultsScreen, resPath, menuSound);
					}
					returnToResultsAfterReplay = false;
					appMode = APP_MODE_RESULTS;
					break;
				}

				if (SlipRace_type != SLIP_RACE_TYPE_PRACTICE)
					SlipRaceRecording_Release();
				SlipRandom_Stir(SlipDebug_BiosTickLow());
				SlipMenuMusic_Start();
				appMode = APP_MODE_MENU;
				hoveredButton = -1;
				redraw = true;
				break;
			case SLIP_RACE_FRAME_EXIT_PROGRAM:
				return false;
			case SLIP_RACE_FRAME_CONTINUE:
				break;
			}
		} else if (appMode == APP_MODE_RESULTS) {
			const SlipRaceResultsAction action = SlipRaceResults_Frame(&resultsScreen);
			if (action != SLIP_RESULTS_WAIT) {
				SlipRaceResults_EndDisplay(&resultsScreen, menuSound);
				if (action == SLIP_RESULTS_REPLAY) {
					SDL_HideCursor();
					returnToResultsAfterReplay = true;
					SlipRaceSession_Replay(resPath, SlipRacePlayer_track, &SlipRace_racerTable,
					                       SlipConfig_environmentDetail, SlipConfig_shading, SlipConfig_textures,
					                       SlipConfig_shadows);
					appMode = APP_MODE_RACE;
				} else {

					if (SlipRace_type == SLIP_RACE_TYPE_CHAMPIONSHIP)
						SlipRaceRecording_Release();
					SDL_HideCursor();
					SlipLapRecordsHost_Update(SlipRacePlayer_track, &SlipRace_racerTable, &SlipRaceSession_maths);
					SDL_ShowCursor();
					if (SlipRace_type != SLIP_RACE_TYPE_CHAMPIONSHIP) {

						SlipRaceRecording_Release();
						SlipRandom_Stir(SlipDebug_BiosTickLow());
						SlipMenuMusic_Start();
						appMode = APP_MODE_MENU;
						hoveredButton = -1;
						redraw = true;
					} else {
						SlipChampionship_AwardRace(&SlipRace_racerTable);

					championshipStandings:;
						static const uint32_t championshipRounds[3] = {6, 8, 10};
						const uint32_t rounds = championshipRounds[SlipConfig_CurrentMode()];
						if (championshipStage != rounds) {
							for (;;) {
								const SlipChampionshipAction standings =
								    SlipChampionship_Screen((uint16_t)rounds, &SlipRace_racerTable);
								if (g_sdlQuitRequested)
									return false;
								if (standings == SLIP_CHAMPIONSHIP_CONTINUE ||
								    !SlipSavedGames_Save(resPath, &SlipRace_racerTable, championshipStage))
									break;
							}
							++championshipStage;
							SlipMenuMusic_Start();
							if (!SlipMenuSound_Initialize(resPath))
								SlipGame_ResourceFailure();
							for (uint16_t index = 0; index < SlipRace_racerTable.racerCount; ++index) {
								SlipRaceRacerState *const racer = &SlipRace_racerTable.records[index];
								racer->racePosition = (uint16_t)(11 - racer->racePosition);
							}
							SlipCampaign_Stage(window, &appMode, &redraw);
							return true;
						} else {
							SlipChampionship_FinalScreen(&SlipRace_racerTable);
						}

						SlipRaceRecording_Release();
						SlipRandom_Stir(SlipDebug_BiosTickLow());
						SlipMenuMusic_Start();
						appMode = APP_MODE_MENU;
						hoveredButton = -1;
						redraw = true;
					}
				}
			}
		} else if (appMode == APP_MODE_GARAGE) {
			SlipInputNavigationTable *const navigation =
			    &SlipInput_garageNavigation[g_garagePanel < 0 ? 4 : g_garagePanel];
			if (SlipInput_navigation.active != navigation)
				SlipInput_SetNavigation(navigation);
			SlipInput_UpdatePointer();
			SlipInputPointerPosition pointer = SlipInput_Pointer();
			if (g_garagePanel < 0) {
				g_hoveredGarageAction = SlipMenu_HitTestGarageAction(pointer.x, pointer.y);
				g_garageActionFocus = FOCUS_MOUSE;
			} else {
				g_hoveredGaragePanelItem = SlipMenu_HitTestGaragePanelItem(g_garagePanel, pointer.x, pointer.y);
				g_garagePanelFocus = FOCUS_MOUSE;
			}
			SlipMenu_DrawGarageView(resPath, g_selectedDriver, g_selectedGarageAction, g_hoveredGarageAction,
			                        g_garagePanel, g_selectedGaragePanelItem, g_hoveredGaragePanelItem);
		}
		if (!raceFrame)
			SlipMenu_PresentFrame();
		redraw = false;
	}
	return true;
}
