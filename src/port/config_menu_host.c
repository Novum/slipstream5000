#include "config_menu_host.h"
#include "config_controls.h"
#include "config_settings.h"
#include "config_state_file.h"
#include "controller_sdl.h"
#include "frame_timer.h"
#include "game_errors.h"
#include "joystick_calibration.h"
#include "maths_host.h"
#include "menu.h"
#include "menu_music.h"
#include "menu_resources.h"
#include "port_app_bridge.h"
#include "race_display.h"
#include "race_player.h"
#include "resource_host.h"
#include "runtime.h"
#include "screen_present_host.h"
#include <stdlib.h>
#include <string.h>

static char configurationPath[1024];
static uint16_t boundSprite;
static RasterSurfaceBinding savedSurface;
static const SlipStringTableResources strings = {.load = SlipResourceHost_Load,
                                                 .lock = SlipResourceHost_Lock,
                                                 .unlock = SlipResourceHost_Unlock,
                                                 .release = SlipResourceHost_Release};

static void SlipConfigHost_Error(void *context) {
	(void)context;
	SlipGame_ResourceFailure();
}

static SlipSprite SlipConfigHost_SpriteView(uint16_t handle) {
	SlipResourcePayload payload = SlipResourceHost_Payload(handle);
	SlipSprite sprite;
	if (!SlipSprite_FromPayload(&payload, &sprite))
		SlipConfigHost_Error(NULL);
	return sprite;
}

static SlipConfigMenuRectangle SlipConfigHost_Surface(void *context) {
	(void)context;
	RasterSurfaceBounds bounds = Raster_GetSurfaceBounds();
	return (SlipConfigMenuRectangle){(int16_t)bounds.left, (int16_t)bounds.top, (int16_t)bounds.right,
	                                 (int16_t)bounds.bottom};
}

static void SlipConfigHost_Clip(void *context, SlipConfigMenuRectangle bounds) {
	(void)context;
	Raster_SetClipRect(bounds.left, bounds.top, bounds.right, bounds.bottom);
}

static void SlipConfigHost_Line(void *context, uint16_t color, int16_t left, int16_t top, int16_t right,
                                int16_t bottom) {
	(void)context;
	Raster_DrawLineSolid(color, left, top, right, bottom);
}

static void SlipConfigHost_Sprite(void *context, uint16_t handle, int16_t x, int16_t y) {
	SlipResourceHost_Lock(context, handle);
	SlipSprite view = SlipConfigHost_SpriteView(handle);
	SlipSprite_DrawClipped(&view, g_screenBufferBase, g_screenPitch, x, y);
	SlipResourceHost_Unlock(context, handle);
}

static void SlipConfigHost_UnclippedSprite(void *context, uint16_t handle, int16_t x, int16_t y) {
	SlipResourceHost_Lock(context, handle);
	SlipSprite view = SlipConfigHost_SpriteView(handle);
	SlipSprite_Draw(&view, g_screenBufferBase, g_screenPitch, x, y);
	SlipResourceHost_Unlock(context, handle);
}

static void SlipConfigHost_Font(void *context, uint16_t handle) {
	SlipText_state.fontResource = handle;
	bool heightNeeded = SlipText_state.requestedSpacing == UINT16_MAX;
	if (heightNeeded)
		SlipResourceHost_Lock(context, handle);
	SlipResourcePayload payload = SlipResourceHost_Payload(handle);
	SlipFont view;
	if (!SlipFont_FromPayload(&payload, &view))
		SlipConfigHost_Error(context);
	SlipText_SelectFont(&SlipText_state, &view);
	if (heightNeeded)
		SlipResourceHost_Unlock(context, handle);
}

static uint16_t SlipConfigHost_GetFont(void *context) {
	(void)context;
	return SlipText_state.fontResource;
}

static void SlipConfigHost_Color(void *context, uint16_t value) {
	(void)context;
	SlipText_SetColor(&SlipText_state, value);
}

static void SlipConfigHost_Style(void *context, uint16_t mode, uint16_t spacing, int16_t left, int16_t right) {
	(void)context;
	SlipText_SetStyle(&SlipText_state, mode, spacing, left, right);
}

static const char *SlipConfigHost_String(void *context, SlipStringTableSlot *slot, uint32_t tag) {
	(void)context;
	return SlipStringTable_Get(slot, tag, &strings);
}

static void SlipConfigHost_UnlockStrings(void *context, SlipStringTableSlot *slot) {
	(void)context;
	SlipStringTable_Unlock(slot, &strings);
}

static void SlipConfigHost_Text(void *context, const char *value, const SlipTextArgument *arguments,
                                SlipTextPosition position) {
	(void)context;
	SlipText_Draw(&SlipText_state, value, arguments, &position);
}

static void SlipConfigHost_Palette(void *context, uint16_t handle) {
	SlipResourceHost_Lock(context, handle);
	SlipSprite view = SlipConfigHost_SpriteView(handle);
	SlipSprite_ApplyPalette(&view);
	SlipResourceHost_Unlock(context, handle);
}

static void SlipConfigHost_Language(void *context) {
	(void)context;
	SlipStringTable_SetLanguage(&SlipStringTable_state, (uint8_t)SlipConfig_language);
}

static bool SlipConfigHost_LoadStrings(void *context, const char name[8], SlipStringTableSlot **slot) {
	(void)context;
	return SlipStringTable_Load(&SlipStringTable_state, name, &strings, slot);
}

static void SlipConfigHost_ReleaseStrings(void *context, SlipStringTableSlot *slot) {
	(void)context;
	SlipStringTable_Release(slot, &strings);
}

static void SlipConfigHost_Navigation(void *context, SlipConfigMenuTable table) {
	(void)context;
	SlipInput_SetNavigation(&SlipInput_configurationNavigation[table]);
}

static void SlipConfigHost_ClearNavigation(void *context) {
	(void)context;
	SlipInput_ClearNavigation();
}

static void SlipConfigHost_ResetTimer(void *context) {
	(void)context;
	SlipFrameTimer_Reset();
}

static void SlipConfigHost_UpdateTimer(void *context) {
	(void)context;
	SlipFrameTimer_Update((uint32_t)SlipSdl_TicksMs());
}

static SlipConfigMenuPoint SlipConfigHost_Pointer(void *context) {
	(void)context;
	SlipInputPointerPosition point = SlipInput_Pointer();
	return (SlipConfigMenuPoint){(int16_t)point.x, (int16_t)point.y};
}

static uint32_t SlipConfigHost_HitTest(void *context, SlipConfigMenuTable table, SlipConfigMenuPoint point) {
	(void)context;

	static const SlipInputRectangle bindings[10] = {
	    {55, 69, 187, 77},   {193, 69, 256, 77}, {87, 87, 150, 95},   {55, 99, 118, 107},   {122, 99, 185, 107},
	    {87, 111, 150, 119}, {193, 87, 256, 95}, {193, 99, 256, 107}, {193, 111, 256, 119}, {122, 131, 185, 139}};
	static const SlipInputRectangle button = {83, 75, 141, 90};
	const SlipInputRectangle *const rectangles[9] = {SlipConfigMenu_mainRectangles,
	                                                 SlipConfigMenu_difficultyRectangles,
	                                                 SlipConfigMenu_generalRectangles,
	                                                 SlipConfigMenu_detailRectangles,
	                                                 SlipConfigMenu_soundRectangles,
	                                                 SlipConfigMenu_controlsRectangles,
	                                                 bindings,
	                                                 &button,
	                                                 &button};
	static const uint16_t counts[9] = {7, 4, 7, 8, 6, 7, 10, 1, 1};
	if (table == SLIP_CONFIG_GENERAL_TABLE && point.x >= 25 && point.x <= 281 && point.y >= 43 && point.y <= 59)
		return 8;
	return SlipInput_HitTest(rectangles[table], counts[table], point.x, point.y);
}

static bool SlipConfigHost_Pressed(void *context, SlipInputCode code) {
	(void)context;
	return SlipInput_TestAndClear(SlipInput_pressed, code);
}

static void SlipConfigHost_Present(void *context) {
	(void)context;
	SlipMenu_PresentFrame();
}

static void SlipConfigHost_Poll(void *context) {
	(void)context;
	if (!SlipMenu_PollInput()) {
		/* Native window-close event terminates the host, outside game input. */
		SlipRuntime_Shutdown();
		SDL_Quit();
		exit(0);
	}
}

static void SlipConfigHost_Save(void *context) {
	(void)context;
	SlipConfigFile_Save(configurationPath);
}

static void SlipConfigHost_ApplyMusic(void *context) {
	(void)context;
	SlipMenuMusic_SetSetting(SlipConfig_music);
}

static uint32_t SlipConfigHost_MousePresent(void *context) {
	(void)context;
	return 1; /* SDL mouse backend */
}

static int32_t SlipConfigHost_NextInput(void *context) {
	(void)context;
	return SlipInput_PopPressed(SlipInput_pressed);
}

static const char *SlipConfigHost_InputName(void *context, uint32_t code) {
	(void)context;
	return SlipInput_Name(code);
}

static void SlipConfigHost_ShowPointer(void *context) {
	(void)context;
	SDL_ShowCursor();
}

static void SlipConfigHost_HidePointer(void *context) {
	(void)context;
	SDL_HideCursor();
}

static void SlipConfigHost_CycleMovement(void *context, uint32_t player) {
	SlipConfigControls_CycleMovement(player, SlipConfigHost_MousePresent, context);
}

static uint16_t SlipConfigHost_JoystickPresence(void *context) {
	(void)context;
	return SlipControllerSdl_Presence();
}

static bool SlipConfigHost_ReadJoystick(void *context, uint32_t joystick, uint16_t *x, uint16_t *y) {
	return SlipControllerSdl_Read(context, joystick, x, y);
}

static bool SlipConfigHost_Calibration(void *context, uint32_t operation, uint32_t joystick) {
	return SlipJoystick_Calibrate(operation, joystick, SlipConfigHost_ReadJoystick, context);
}

static void SlipConfigHost_ReadCalibration(void *context, uint32_t joystick, SlipJoystickCalibration *record) {
	(void)context;
	SlipJoystick_GetCalibration(joystick, record);
}

static SlipConfigControlDimensions SlipConfigHost_Dimensions(void *context, uint16_t handle) {
	SlipResourceHost_Lock(context, handle);
	SlipSprite view = SlipConfigHost_SpriteView(handle);
	SlipResourceHost_Unlock(context, handle);
	return (SlipConfigControlDimensions){view.width, view.height};
}

static void SlipConfigHost_BindSprite(void *context, uint16_t handle, uint16_t offset, uint16_t pitch,
                                      SlipConfigControlDimensions size) {
	(void)pitch; /* Original callers supply the sprite width as the pitch. */
	uint8_t *const data = (uint8_t *)SlipResourceHost_Lock(context, handle);
	boundSprite = handle;
	Raster_BindSprite(data + offset, size.width, size.height, &savedSurface);
}

static void SlipConfigHost_Fill(void *context, uint16_t value, SlipConfigMenuRectangle rectangle) {
	(void)context;
	Raster_FillRectClipped((uint8_t)value, rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
}

static void SlipConfigHost_Restore(void *context) {
	if (boundSprite != 0)
		SlipResourceHost_Unlock(context, boundSprite);
	Raster_RestoreScreen(&savedSurface);
}

/* Remaining declarations and callback tables below are signature adapters:
 * each entry invokes the translated callee named in config_menu.h. */
static uint32_t SlipConfigHost_GetDifficulty(void *context) {
	(void)context;
	return SlipConfig_CurrentMode();
}

static uint32_t SlipConfigHost_GetDamage(void *context) {
	(void)context;
	return SlipConfig_DamageEnabled();
}

static uint32_t SlipConfigHost_GetRearMonitor(void *context) {
	(void)context;
	return SlipConfig_RearMonitor();
}

static uint32_t SlipConfigHost_GetWeaponsMonitor(void *context) {
	(void)context;
	return SlipConfig_WeaponsMonitor();
}

static const char *SlipConfigHost_GetLanguage(void *context) {
	(void)context;
	return SlipConfig_LanguageName();
}

static uint32_t SlipConfigHost_GetTrackMap(void *context) {
	(void)context;
	return SlipConfig_TrackMapEnabled();
}

static uint32_t SlipConfigHost_GetSpeedDisplay(void *context) {
	(void)context;
	return SlipConfig_SpeedDisplay();
}

static const char *SlipConfigHost_GetEnvironment(void *context) {
	(void)context;
	return SlipConfig_EnvironmentName();
}

static uint32_t SlipConfigHost_GetClouds(void *context) {
	(void)context;
	return SlipConfig_CloudsEnabled();
}

static uint32_t SlipConfigHost_GetShading(void *context) {
	(void)context;
	return SlipConfig_Shading();
}

static uint32_t SlipConfigHost_GetTextures(void *context) {
	(void)context;
	return SlipConfig_Textures();
}

static uint32_t SlipConfigHost_GetWindow(void *context) {
	(void)context;
	return SlipConfig_WindowSize();
}

static uint16_t SlipConfigHost_GetShadows(void *context) {
	(void)context;
	return SlipConfig_Shadows();
}

static uint32_t SlipConfigHost_GetEffects(void *context) {
	(void)context;
	return SlipConfig_SoundEffects();
}

static uint32_t SlipConfigHost_GetEngines(void *context) {
	(void)context;
	return SlipConfig_EngineSounds();
}

static uint32_t SlipConfigHost_GetSpeech(void *context) {
	(void)context;
	return SlipConfig_Speech();
}

static uint32_t SlipConfigHost_GetMusic(void *context) {
	(void)context;
	return SlipConfig_Music();
}

static uint32_t SlipConfigHost_GetReverseAccelerator(void *context) {
	(void)context;
	return SlipRace_GetReverseAccelerator();
}

static const SlipConfigMenuDrawCalls drawCalls = {
    .surface = SlipConfigHost_Surface,
    .clip = SlipConfigHost_Clip,
    .line = SlipConfigHost_Line,
    .clippedSprite = SlipConfigHost_Sprite,
    .unclippedSprite = SlipConfigHost_UnclippedSprite,
    .font = SlipConfigHost_Font,
    .textColor = SlipConfigHost_Color,
    .style = SlipConfigHost_Style,
    .string = SlipConfigHost_String,
    .unlockStrings = SlipConfigHost_UnlockStrings,
    .text = SlipConfigHost_Text,
    .difficulty = SlipConfigHost_GetDifficulty,
    .damage = SlipConfigHost_GetDamage,
    .rearMonitor = SlipConfigHost_GetRearMonitor,
    .weaponsMonitor = SlipConfigHost_GetWeaponsMonitor,
    .language = SlipConfigHost_GetLanguage,
    .trackMap = SlipConfigHost_GetTrackMap,
    .speedDisplay = SlipConfigHost_GetSpeedDisplay,
    .environment = SlipConfigHost_GetEnvironment,
    .clouds = SlipConfigHost_GetClouds,
    .shading = SlipConfigHost_GetShading,
    .textures = SlipConfigHost_GetTextures,
    .window = SlipConfigHost_GetWindow,
    .shadows = SlipConfigHost_GetShadows,
    .effects = SlipConfigHost_GetEffects,
    .engines = SlipConfigHost_GetEngines,
    .speech = SlipConfigHost_GetSpeech,
    .music = SlipConfigHost_GetMusic,
    .reverseAccelerator = SlipConfigHost_GetReverseAccelerator,
};
static const SlipConfigControlsDrawCalls controlsDrawCalls = {.draw = &drawCalls,
                                                              .dimensions = SlipConfigHost_Dimensions,
                                                              .bindSprite = SlipConfigHost_BindSprite,
                                                              .fill = SlipConfigHost_Fill,
                                                              .inputName = SlipConfigHost_InputName,
                                                              .restore = SlipConfigHost_Restore};

static void SlipConfigHost_DrawMain(void *context, SlipConfigMenuState *state) {
	(void)context;
	SlipConfigMenu_DrawMain(state, &drawCalls);
}

static void SlipConfigHost_DrawDifficulty(void *context, SlipConfigMenuState *state) {
	(void)context;
	SlipConfigMenu_DrawDifficulty(state, &drawCalls);
}

static void SlipConfigHost_DrawGeneral(void *context, SlipConfigMenuState *state) {
	(void)context;
	SlipConfigMenu_DrawGeneral(state, &drawCalls);
	const SlipConfigMenuRectangle rectangle = {25, 43, 281, 59};
	SlipMenu_DrawPanel(rectangle, state->generalSelection == 8 ? state->background : state->inactiveBackground,
	                   state->generalStrings, 0, &drawCalls);
	SlipText_SetStyle(&SlipText_state, 0, UINT16_MAX, 25, 200);
	SlipTextPosition label = {31, 46};
	SlipText_Draw(&SlipText_state, "High Res", NULL, &label);
	SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, 204, 281);
	SlipTextPosition value = {0, 47};
	SlipText_Draw(&SlipText_state, SlipRaceDisplay_highRes ? "On" : "Off", NULL, &value);
}

static void SlipConfigHost_DrawDetail(void *context, SlipConfigMenuState *state) {
	(void)context;
	SlipConfigMenu_DrawDetail(state, &drawCalls);
}

static void SlipConfigHost_DrawSound(void *context, SlipConfigMenuState *state) {
	(void)context;
	SlipConfigMenu_DrawSound(state, &drawCalls);
}

static void SlipConfigHost_DrawControls(void *context, SlipConfigMenuState *state) {
	(void)context;
	SlipConfigMenu_DrawControls(state, &drawCalls);
}

static void SlipConfigHost_DrawBindings(void *context, const SlipConfigControlsState *state) {
	(void)context;
	SlipConfigControls_Draw(state, SlipMenu_resources.smallFont, SlipConfigMenu_state.controlsStrings,
	                        &controlsDrawCalls);
}

static void SlipConfigHost_DrawConflict(void *context, const SlipConfigConflictState *state) {
	(void)context;
	SlipConfigControls_DrawWarning(state, SlipConfigMenu_state.font, SlipMenu_resources.smallFont,
	                               SlipConfigMenu_state.controlsStrings, &controlsDrawCalls);
}

static void SlipConfigHost_DrawCalibration(void *context, const SlipConfigCalibrationState *state) {
	(void)context;
	SlipConfigControls_DrawCalibration(state, SlipConfigMenu_state.font, SlipMenu_resources.smallFont,
	                                   SlipConfigMenu_state.controlsStrings, &controlsDrawCalls);
}

static const SlipConfigControlsCalls controlsCalls = {.menu = &SlipConfigHost_calls,
                                                      .draw = &drawCalls,
                                                      .copy = SlipResourceHost_Copy,
                                                      .nextInput = SlipConfigHost_NextInput,
                                                      .inputName = SlipConfigHost_InputName,
                                                      .showPointer = SlipConfigHost_ShowPointer,
                                                      .hidePointer = SlipConfigHost_HidePointer,
                                                      .cycleMovement = SlipConfigHost_CycleMovement,
                                                      .drawBindings = SlipConfigHost_DrawBindings};
static const SlipConfigDialogCalls dialogCalls = {.controls = &controlsCalls,
                                                  .calibration = SlipConfigHost_Calibration,
                                                  .readCalibration = SlipConfigHost_ReadCalibration,
                                                  .drawConflict = SlipConfigHost_DrawConflict,
                                                  .drawCalibration = SlipConfigHost_DrawCalibration};

static void SlipConfigHost_ChangeDifficulty(void *context) {
	(void)context;
	SlipConfig_CycleMode();
}

static void SlipConfigHost_ChangeDamage(void *context) {
	(void)context;
	SlipConfig_ToggleDamage();
}

static void SlipConfigHost_ChangeRear(void *context) {
	(void)context;
	SlipConfig_ToggleRearMonitor();
}

static void SlipConfigHost_ChangeWeapons(void *context) {
	(void)context;
	SlipConfig_ToggleWeaponsMonitor();
}

static void SlipConfigHost_ChangeMap(void *context) {
	(void)context;
	SlipConfig_ToggleTrackMap();
}

static void SlipConfigHost_ChangeSpeed(void *context) {
	(void)context;
	SlipConfig_ToggleSpeedDisplay();
}

static void SlipConfigHost_ChangeEnvironment(void *context) {
	(void)context;
	SlipConfig_CycleEnvironmentDetail();
}

static void SlipConfigHost_ChangeClouds(void *context) {
	(void)context;
	SlipConfig_ToggleClouds();
}

static void SlipConfigHost_ChangeShading(void *context) {
	(void)context;
	SlipConfig_CycleShading();
}

static void SlipConfigHost_ChangeTextures(void *context) {
	(void)context;
	SlipConfig_CycleTextures();
}

static void SlipConfigHost_ChangeWindow(void *context) {
	(void)context;
	SlipConfig_ToggleWindowSize();
}

static void SlipConfigHost_ChangeShadows(void *context) {
	(void)context;
	SlipConfig_ToggleShadows();
}

static void SlipConfigHost_ChangeEffects(void *context) {
	(void)context;
	SlipConfig_ToggleSoundEffects();
}

static void SlipConfigHost_ChangeEngines(void *context) {
	(void)context;
	SlipConfig_CycleEngineSounds();
}

static void SlipConfigHost_ChangeSpeech(void *context) {
	(void)context;
	SlipConfig_ToggleSpeech();
}

static void SlipConfigHost_ChangeMusic(void *context) {
	(void)context;
	SlipConfig_CycleMusic();
}

static void SlipConfigHost_ChangeLanguage(void *context) {
	(void)context;
	SlipConfig_CycleLanguage();
}

static void SlipConfigHost_ChangeReverse(void *context) {
	(void)context;
	SlipRace_ToggleReverseAccelerator();
}

static void SlipConfigHost_LanguageOption(void *context) {
	(void)context;
	SlipConfigMenu_CycleLanguage(&SlipConfigMenu_state, &SlipConfigHost_calls);
}

static void SlipConfigHost_MusicOption(void *context) {
	(void)context;
	SlipConfigMenu_CycleMusic(&SlipConfigHost_calls);
}

const SlipConfigMenuCalls SlipConfigHost_calls = {
    .load = SlipResourceHost_Load,
    .release = SlipResourceHost_Release,
    .selectFont = SlipConfigHost_Font,
    .currentFont = SlipConfigHost_GetFont,
    .textColor = SlipConfigHost_Color,
    .palette = SlipConfigHost_Palette,
    .language = SlipConfigHost_Language,
    .strings = SlipConfigHost_LoadStrings,
    .releaseStrings = SlipConfigHost_ReleaseStrings,
    .resourceError = SlipConfigHost_Error,
    .navigation = SlipConfigHost_Navigation,
    .clearNavigation = SlipConfigHost_ClearNavigation,
    .resetTimer = SlipConfigHost_ResetTimer,
    .updateTimer = SlipConfigHost_UpdateTimer,
    .pointer = SlipConfigHost_Pointer,
    .hitTest = SlipConfigHost_HitTest,
    .pressed = SlipConfigHost_Pressed,
    .drawMain = SlipConfigHost_DrawMain,
    .drawDifficulty = SlipConfigHost_DrawDifficulty,
    .present = SlipConfigHost_Present,
    .poll = SlipConfigHost_Poll,
    .cycleDifficulty = SlipConfigHost_ChangeDifficulty,
    .toggleDamage = SlipConfigHost_ChangeDamage,
    .save = SlipConfigHost_Save,
    .drawGeneral = SlipConfigHost_DrawGeneral,
    .toggleHighRes = SlipRaceDisplay_Toggle,
    .drawDetail = SlipConfigHost_DrawDetail,
    .drawSound = SlipConfigHost_DrawSound,
    .generalOptions = {SlipConfigHost_ChangeRear, SlipConfigHost_ChangeWeapons, SlipConfigHost_LanguageOption,
                       SlipConfigHost_ChangeMap, SlipConfigHost_ChangeSpeed},
    .detailOptions = {SlipConfigHost_ChangeEnvironment, SlipConfigHost_ChangeClouds, SlipConfigHost_ChangeShading,
                      SlipConfigHost_ChangeTextures, SlipConfigHost_ChangeWindow, SlipConfigHost_ChangeShadows},
    .soundOptions = {SlipConfigHost_ChangeEffects, SlipConfigHost_ChangeEngines, SlipConfigHost_ChangeSpeech,
                     SlipConfigHost_MusicOption},
    .cycleLanguage = SlipConfigHost_ChangeLanguage,
    .cycleMusic = SlipConfigHost_ChangeMusic,
    .applyMusic = SlipConfigHost_ApplyMusic,
    .drawControls = SlipConfigHost_DrawControls,
    .controlDialogs = &dialogCalls,
    .reverseAccelerator = SlipConfigHost_ChangeReverse};

static void SlipConfigHost_MemoryFailure(SlipResourceHandle *record) {
	(void)record;
	SlipGame_MemoryFailure();
}

static void SlipConfigHost_SetLoadError(void *context, SlipResourceLoadErrorHandler handler) {
	(void)context;
	SlipResource_SetLoadErrorHandler(handler);
}

static void SlipConfigHost_SetReclaim(void *context, uint32_t enabled) {
	(void)context;
	SlipResource_SetReclaimEnabled(enabled);
}

bool SlipConfigHost_Install(const char *resourcePath) {
	char secondary[512];
	const char *archives[2];
	const size_t count = SlipMenu_BuildArchiveList(resourcePath, secondary, archives);
	SlipResourceHost_Initialize(16u * 1024u * 1024u);
	SlipResourceHost_OpenArchives(archives[0], count > 1 ? archives[1] : NULL);

	SlipScreenHost_Initialize();

	if (!SlipMathsHost_Initialize())
		return false;
	/* Native path binding: configuration is alongside the supplied game archive. */
	size_t directory = 0;
	for (size_t index = 0; resourcePath[index] != 0; ++index)
		if (resourcePath[index] == '/' || resourcePath[index] == '\\')
			directory = index + 1;
	if (directory + sizeof("SLIPSTRM.CFG") > sizeof(configurationPath))
		SlipConfigHost_Error(NULL);
	memcpy(configurationPath, resourcePath, directory);
	memcpy(configurationPath + directory, "SLIPSTRM.CFG", sizeof("SLIPSTRM.CFG"));
	return true;
}

void SlipConfigHost_LoadConfiguration(void) {
	const SlipConfigDeviceCalls devices = {NULL, SlipConfigHost_JoystickPresence, SlipConfigHost_MousePresent};
	SlipConfigFile_Load(configurationPath, &devices);
	const SlipMenuResourceCalls resources = {.load = SlipResourceHost_Load,
	                                         .resourceFailure = SlipConfigHost_Error,
	                                         .selectCursor = SlipScreenHost_SelectCursor,
	                                         .setLoadError = SlipConfigHost_SetLoadError,
	                                         .setReclaim = SlipConfigHost_SetReclaim,
	                                         .memoryFailure = SlipConfigHost_MemoryFailure};
	SlipMenuResources_Initialize(&SlipMenu_resources, &resources);
}

/* Combined host setup for standalone configuration diagnostics. */
void SlipConfigHost_Initialize(const char *resourcePath) {
	if (!SlipConfigHost_Install(resourcePath))
		SlipConfigHost_Error(NULL);
	SlipConfigHost_LoadConfiguration();
}
