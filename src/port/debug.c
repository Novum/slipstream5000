#include "debug.h"
#include "byte_order.h"
#include "gpu/renderer.h"
#include "race_display.h"
#ifdef SLIP_REPLAY_HARNESS
#include "../testing/ui_capture.h"
#endif
#include "capture_stream.h"
#include "config_menu_host.h"
#include "config_settings.h"
#include "fixed_point.h"
#include "frame_timer.h"
#include "input.h"
#include "menu.h"
#include "menu_music.h"
#include "menu_resources.h"
#include "renderer_host.h"
#include "resource.h"
#include "resource_host.h"
#include "screen_present_host.h"
#include "shape_format.h"
#include "vga_dac.h"
#include <SDL3/SDL.h>
#include <limits.h>

#include "port_app_bridge.h"
#include "presenter.h"
#include "race.h"
#include "race_camera.h"
#include "race_collision.h"
#include "race_drone.h"
#include "race_effects.h"
#include "race_hud.h"
#include "race_physics.h"
#include "race_recording.h"
#include "race_results.h"
#include "race_session.h"
#include "race_voice.h"
#include "raster/raster.h"
#include "runtime.h"
#include "sound_effects.h"
#include "string_table.h"
#include "timed_effects.h"
#include "track_view_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	SLIP_DEBUG_TRACK_RECORD_BASE_TOKEN = 0x04000000,
	SLIP_DEBUG_RACE_MAXIMUM_FRAMES = 90000,
	SLIP_DEBUG_RACE_DEFAULT_TICK_MS = 14,
	SLIP_DEBUG_RACE_MAXIMUM_TICK_MS = 1000,
	SLIP_DEBUG_RACE_TRACE_INTERVAL_FRAMES = 1000,
	SLIP_DEBUG_DIGITAL_DRIVER_VERSION = 0xe015,
	SLIP_DEBUG_RESOURCE_CAPACITY_BYTES = 32u * 1024u * 1024u,
	SLIP_DEBUG_GPU_MAXIMUM_DIMENSION = 8192,
	SLIP_DEBUG_PRESENTER_TICK_MS = 16,
	SLIP_DEBUG_PRESENTER_DELAY_MS = 14,
	SLIP_DEBUG_PRESENTER_DEFAULT_CAPTURE_FRAME = 3000
};

static SlipRaceFrameResult SlipDebug_RunRaceFixtureFrame(uint32_t tick, const SlipRaceControlBinding bindings[2],
                                                         const bool held[SLIP_INPUT_CODE_COUNT],
                                                         bool pressed[SLIP_INPUT_CODE_COUNT], bool reverseAccelerator,
                                                         uint32_t windowSize, int mouseX, int mouseY) {
	SlipRace_controlBindings[0] = bindings[0];
	SlipRace_controlBindings[1] = bindings[1];
	SlipRace_reverseAccelerator = reverseAccelerator ? 1 : 0;
	return SlipRaceSession_RunFrame(tick, held, pressed, windowSize, mouseX, mouseY);
}

typedef struct SlipDebugRecordingClock {
	SlipRaceRecording *state;
	uint32_t tickIndex, resets, frames;
	uint32_t callbackCursor;
} SlipDebugRecordingClock;

static uint32_t SlipDebug_RecordingTick(void *context) {
	SlipDebugRecordingClock *const clock = context;
	const uint32_t ticks[] = {UINT32_MAX, 0, 1, 2, 1};
	const uint32_t index = clock->tickIndex++;
	return ticks[index < 5 ? index : 4];
}

static void SlipDebug_RecordingReset(void *context) {
	SlipDebugRecordingClock *const clock = context;
	++clock->resets;
	clock->callbackCursor = clock->state->playbackFrame;
}

static void SlipDebug_RecordingFrame(void *context) {
	SlipDebugRecordingClock *const clock = context;
	++clock->frames;
	clock->callbackCursor = clock->state->playbackFrame;
}

static uint32_t SlipDebug_ReplayTick(void *context) {
	uint32_t *const tick = context;
	*tick += 14;
	return *tick;
}

static bool SlipDebug_HarnessFrame(uint32_t frame) {
	if (getenv("SLIP_HARNESS_PIPE") == NULL)
		return false;
	uint32_t states[SLIP_CAPTURE_STATE_WORD_COUNT] = {0};
	uint32_t count = 0;
	const uint16_t players[2] = {SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject};
	for (unsigned i = 0; i < 2; ++i) {
		if (!players[i])
			continue;
		const SlipObject *const object = &SlipObject_table[players[i] / SLIP_OBJECT_DOS_STRIDE];
		SlipObjectPosition position;
		SlipObject_Position(SlipObject_table, SLIP_OBJECT_TABLE_DOS_BYTES, players[i], &position);
		const SlipRaceRacerState *const racer = SlipRacePlayer_RacerState(players[i]);
		uint32_t *const state = &states[i * 8];
		state[0] = position.positionX;
		state[1] = position.positionY;
		state[2] = position.positionZ;
		state[3] = (uint32_t)(int32_t)object->direction.x;
		state[4] = (uint32_t)(int32_t)object->direction.y;
		state[5] = (uint32_t)(int32_t)object->direction.z;
		state[6] = racer->lapNumber;
		state[7] = racer->trackComponent;
		++count;
	}
	SlipVgaDac_Commit();
	SlipRandomState random = SlipRandom_GetState();
	return SlipHarness_WriteFrame(frame, SlipFrameTimer_Values().deltaMilliseconds, count, random.stateWords,
	                              random.stateTail, states, g_displayFramebuffer, &g_vgaDacPalette[0][0]) != 0;
}

/* Isolated collision response injection for --verify-door-events. */
static bool SlipDebug_DoorCollision(uint32_t flags, uint16_t object) {
	(void)flags;
	(void)object;
	return true;
}

static uintptr_t SlipDebug_reclaimEvent[7];
static uintptr_t SlipDebug_nativeFreeData[3];
static unsigned SlipDebug_collisionEventCount;
static uint32_t SlipDebug_collisionEvents[2][6];
static SlipRaceCollisionBody *SlipDebug_collisionBody;

static uint32_t SlipDebug_CollisionEvent(uint32_t code, uint32_t payload, uint32_t value, uint32_t flags,
                                         uint16_t object, uintptr_t data, uint32_t frame) {
	(void)data;
	if (SlipDebug_collisionEventCount < 2) {
		uint32_t *const event = SlipDebug_collisionEvents[SlipDebug_collisionEventCount++];
		event[0] = code;
		event[1] = payload;
		event[2] = value;
		event[3] = flags;
		event[4] = object;
		event[5] = frame;
	}
	SlipDebug_collisionBody->otherObject = 0x87654321;
	return 0xabcd0001;
}

static uint32_t SlipDebug_pointMask;
static unsigned SlipDebug_pointClassifications, SlipDebug_pointProjections;

static uint32_t SlipDebug_PointMask(SlipDraw3DVec32 point, const SlipDraw3DProjectState *state) {
	(void)point;
	(void)state;
	++SlipDebug_pointClassifications;
	return SlipDebug_pointMask;
}

static void SlipDebug_PointProject(SlipDraw3DVec32 point, int32_t *x, int32_t *y, void *context) {
	(void)context;
	++SlipDebug_pointProjections;
	*x = point.x;
	*y = point.y;
}

static uint32_t SlipDebug_ReclaimEvent(uint32_t code, uint32_t payload, uint32_t value, uint32_t flags, uint16_t object,
                                       uintptr_t data, uint32_t frame) {
	SlipDebug_reclaimEvent[0] = code;
	SlipDebug_reclaimEvent[1] = payload;
	SlipDebug_reclaimEvent[2] = value;
	SlipDebug_reclaimEvent[3] = flags;
	SlipDebug_reclaimEvent[4] = object;
	SlipDebug_reclaimEvent[5] = data;
	SlipDebug_reclaimEvent[6] = frame;
	if ((uint16_t)code == SLIP_OBJECT_EVENT_FREE)
		SlipDebug_nativeFreeData[0] = data;
	else if ((uint16_t)code == 1)
		SlipDebug_nativeFreeData[1] = data;
	else if ((uint16_t)code == SLIP_OBJECT_EVENT_FREED)
		SlipDebug_nativeFreeData[2] = data;
	return code;
}

static unsigned SlipDebug_crossUpdateCount;
static uint16_t SlipDebug_crossUpdateLifetime;
static uint32_t SlipDebug_crossUpdateSlot30;

static uint32_t SlipDebug_CrossUpdate(uint16_t object) {
	++SlipDebug_crossUpdateCount;
	SlipDebug_crossUpdateLifetime = SlipObject_CrossEffectState(object)->remainingLifetime;
	SlipDebug_crossUpdateSlot30 = (uint32_t)SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE].drawData;
	return 0xa1b2c3d4;
}

/* Diagnostic adapter: execute the actual deferred wrapper and list traversal. */
static bool SlipDebug_DrawDeferredSprite(TrackViewRawBspContext *context, uint32_t object) {
	SlipDraw3DListState *const state = &SlipDraw3D_listState;
	SlipDraw3DListNode *const nodes = SlipDraw3D_listPool;
	SlipView3DVec32 view;
	const uint32_t payload = 0xabcd0000u | (uint16_t)object;
	if (!SlipObject_ViewPosition(context->objectTable, context->objectTableBytes, (uint16_t)object, &view) ||
	    !SlipDraw3D_ListPushFrame(state, nodes, 2 * sizeof(*nodes)) || !TrackView_QueueTimedEffect(context, payload))
		return false;
	if (nodes[0].callback != TrackView_DrawTimedEffect || nodes[0].payload != payload ||
	    nodes[0].sortKey != (uint32_t)view.z)
		return false;
	bool drawn = SlipDraw3D_ListTraverse(state, nodes, 2 * sizeof(*nodes), context) != 0;
	return SlipDraw3D_ListPopFrame(state) && drawn;
}

static void SlipDebug_PrintDebrisState(const char *phase, uint16_t object) {
	const SlipObject *const entry = &SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE];
	const SlipRaceDebrisState *const state = &entry->debrisEffect;
	printf("debris_state %s %u %u %u %u %u %u %u %u %u %u %u %u", phase, object, state->elapsed,
	       (uint16_t)state->rotationRateX, (uint16_t)state->rotationRateY, (uint16_t)state->rotationRateZ,
	       (uint32_t)entry->position.x, (uint32_t)entry->position.y, (uint32_t)entry->position.z,
	       (uint32_t)entry->speed, (uint16_t)entry->direction.x, (uint16_t)entry->direction.y,
	       (uint16_t)entry->direction.z);
	for (unsigned i = 0; i < 9; ++i)
		printf(" %u", (uint16_t)entry->matrix.m[i]);
	printf(" %u\n", entry->flags);
}

static unsigned SlipDebug_CountDebris(void) {
	unsigned count = 0;
	for (uint16_t object = SlipObject_Next(UINT16_MAX); object != UINT16_MAX; object = SlipObject_Next(object))
		count += SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE].eventCallback == SlipRaceSession_DebrisEvent;
	return count;
}

static int SlipDebug_VerifyRaceRender(int argc, char **argv) {
	bool verifyDoorShapes = argc == 4 && strcmp(argv[1], "--verify-door-shapes") == 0;
	bool verifyDoors = verifyDoorShapes || (argc == 4 && strcmp(argv[1], "--verify-track-doors") == 0);
	bool verifyRefuelRepair = argc == 3 && strcmp(argv[1], "--verify-refuel-repair") == 0;
	bool verifyRefuel = argc == 4 && strcmp(argv[1], "--verify-track-refuel") == 0;
	bool verifyFinishDelay = argc == 3 && strcmp(argv[1], "--verify-race-finish-delay") == 0;
	bool verifyFinish = argc == 3 && strcmp(argv[1], "--verify-race-finish") == 0;
	bool verifySplitLap = (argc == 4 || argc == 5 || argc == 6) && strcmp(argv[1], "--verify-split-laps") == 0;
	bool verifyLapRun =
	    verifySplitLap || (argc == 4 || argc == 5 || argc == 6) && strcmp(argv[1], "--verify-race-laps") == 0;
	const char *const traceLapFramesSetting = getenv("SLIP_TRACE_RACE_FRAMES");
	bool traceLapFrames = traceLapFramesSetting != NULL && strcmp(traceLapFramesSetting, "1") == 0;
	bool verifyQuit = (argc == 3 || argc == 4) && strcmp(argv[1], "--verify-race-quit") == 0;
	bool verifyMusic = argc == 3 && strcmp(argv[1], "--verify-race-music") == 0;
	bool verifyMenuMusicClose = argc == 3 && strcmp(argv[1], "--verify-menu-music-close") == 0;
	bool verifyFlyIn = argc == 3 && strcmp(argv[1], "--verify-race-fly-in") == 0;
	bool verifyCameras = argc == 3 && strcmp(argv[1], "--verify-race-cameras") == 0;
	bool verifySmoker = argc == 4 && strcmp(argv[1], "--verify-smoker") == 0;
	bool verifyDamageSmoke = argc == 4 && strcmp(argv[1], "--verify-damage-smoke") == 0;
	bool verifyTimedRace = argc == 4 && strcmp(argv[1], "--verify-timed-race") == 0;
	bool verifyAnimatedRace = argc == 4 && strcmp(argv[1], "--verify-animated-race") == 0;
	bool verifyReplayLive = argc == 3 && strcmp(argv[1], "--verify-replay-live") == 0;
	bool verifyRecordingLive = verifyReplayLive || (argc == 3 && strcmp(argv[1], "--verify-recording-live") == 0);
	bool verifyBlasterRace = verifyRecordingLive || (argc == 3 && strcmp(argv[1], "--verify-blaster-race") == 0);
	bool verifyWeaponCamera = argc == 3 && strcmp(argv[1], "--verify-weapon-camera") == 0;
	bool verifyMiniMines = argc == 3 && strcmp(argv[1], "--verify-mini-mines") == 0;
	bool verifyDebrisRace = argc == 4 && strcmp(argv[1], "--verify-debris-race") == 0;
	bool verifyDebrisCallers = argc == 3 && strcmp(argv[1], "--verify-debris-callers") == 0;
	bool verifyWreckRace = argc == 3 && strcmp(argv[1], "--verify-wreck-race") == 0;
	bool verifyAiHitAudio = argc == 3 && strcmp(argv[1], "--verify-new-york-ai-hit-audio") == 0;
	bool verifyFatalDamage = argc == 3 && strcmp(argv[1], "--verify-fatal-damage") == 0;
	bool verifySplit = verifySplitLap || verifyDamageSmoke || verifySmoker || verifyTimedRace || verifyAnimatedRace ||
	                   verifyBlasterRace || verifyMiniMines || verifyDebrisRace || verifyDebrisCallers ||
	                   verifyWreckRace || verifyFatalDamage ||
	                   (argc == 4 && strcmp(argv[1], "--verify-split-render") == 0);
	SlipGameSoundState musicGame = {0};
	uint64_t musicFrames = 0;
	SlipRaceControlBinding bindings[2] = {{0}};
	bool inputHeld1[SLIP_INPUT_CODE_COUNT] = {false};
	bool inputPressed[SLIP_INPUT_CODE_COUNT] = {false};
	size_t nonzeroPixelCount = 0;
	size_t i;

	bool capturedFrancePostframe = argc >= 2 && strcmp(argv[1], "--verify-france-postframe-render") == 0;
	bool capturedTunnel = argc >= 2 && strcmp(argv[1], "--verify-arizona-tunnel-render") == 0;
	bool capturedLondon = argc >= 2 && strcmp(argv[1], "--verify-london-captured-render") == 0;
	bool capturedHawaii = argc >= 2 && strcmp(argv[1], "--verify-hawaii-captured-render") == 0;
	bool capturedHawaiiHall = argc >= 2 && strcmp(argv[1], "--verify-hawaii-hall-render") == 0;
	bool hawaiiFirstFrame = argc >= 2 && strcmp(argv[1], "--verify-hawaii-first-frame") == 0;
	bool capturedEgypt = argc >= 2 && strcmp(argv[1], "--verify-egypt-captured-render") == 0;
	static const uint32_t cameraPosition1[3] = {0x003b3de4u, 0x00101b37u, 0x001399bdu};
	static const uint16_t cameraMatrix[9] = {0xeb92u, 0x0003u, 0xc35au, 0x0c08u, 0x3ebau,
	                                         0xfbf5u, 0x3b71u, 0xf34eu, 0xebf9u};
	static const uint32_t playerPosition1[3] = {0x003b39a9u, 0x001019f2u, 0x00139b2au};

	static const uint32_t londonCameraPosition[3] = {0x002ad05du, 0x0018523fu, 0x0076b2b8u};
	static const uint16_t londonCameraMatrix[9] = {0x376fu, 0x0008u, 0x1ffbu, 0x01dau, 0x3fe3u,
	                                               0xfcb9u, 0xe012u, 0x03c4u, 0x3757u};
	static const uint32_t londonPlayerPosition[3] = {0x002ad25du, 0x00184fe0u, 0x0076af43u};
	static const uint16_t londonPlayerMatrix[9] = {0x376fu, 0x0008u, 0x1ffbu, 0x01dau, 0x3fe3u,
	                                               0xfcb9u, 0xe012u, 0x03c4u, 0x3757u};

	static const uint32_t hawaiiCameraPosition[3] = {0x0025d069u, 0x000f3558u, 0x001a9bc7u};
	static const uint16_t hawaiiCameraMatrix[9] = {0x36f8u, 0x0017u, 0x20c7u, 0xfe92u, 0x3ff1u,
	                                               0x0238u, 0xdf41u, 0xfd5cu, 0x36ecu};
	static const uint32_t hawaiiPlayerPosition[3] = {0x0025d292u, 0x000f3363u, 0x001a982au};
	static const uint16_t hawaiiPlayerMatrix[9] = {0x36f8u, 0x0017u, 0x20c7u, 0xfe92u, 0x3ff1u,
	                                               0x0238u, 0xdf41u, 0xfd5cu, 0x36ecu};

	static const uint32_t hawaiiHallCameraPosition[3] = {0x0025f663u, 0x000f1ae3u, 0x001af985u};
	static const uint16_t hawaiiHallCameraMatrix[9] = {0x35d5u, 0xfffeu, 0x229bu, 0xff37u, 0x3ffbu,
	                                                   0x013bu, 0xdd67u, 0xfe8au, 0x35d2u};
	static const uint32_t hawaiiHallPlayerPosition[3] = {0x0025f8a5u, 0x000f18dau, 0x001af603u};
	static const uint16_t hawaiiHallPlayerMatrix[9] = {0x35d5u, 0xfffeu, 0x229bu, 0xff37u, 0x3ffbu,
	                                                   0x013bu, 0xdd67u, 0xfe8au, 0x35d2u};
	static const uint32_t egyptCameraPosition[3] = {0x0028528du, 0x000ffcb3u, 0x0039a5c2u};
	static const uint16_t egyptCameraMatrix[9] = {0x3df2u, 0xfffdu, 0x1017u, 0xfbffu, 0x3dfau,
	                                              0x0f74u, 0xf06bu, 0xf009u, 0x3bfcu};
	static const uint32_t egyptPlayerPosition[3] = {0x002853b1u, 0x000ffbaau, 0x0039a161u};

	if ((argc != 3 && argc != 4 && !verifyLapRun) ||
	    (!capturedFrancePostframe && !capturedTunnel && !capturedLondon && !capturedHawaii && !capturedHawaiiHall &&
	     !hawaiiFirstFrame && !capturedEgypt && !verifyMusic && !verifyMenuMusicClose && !verifyCameras &&
	     !verifyWeaponCamera && !verifyFlyIn && !verifyQuit && !verifyFinish && !verifyLapRun && !verifyFinishDelay &&
	     !verifyRefuel && !verifyDoors && !verifyRefuelRepair && !verifySplit && !verifyAiHitAudio &&
	     strcmp(argv[1], "--verify-race-render") != 0)) {
		return -1;
	}
	if ((verifyRefuel || verifyDoors || verifyLapRun || (verifyQuit && argc == 4)) &&
	    (atoi(argv[3]) < 1 || atoi(argv[3]) > SLIP_RACE_TRACK_COUNT))
		return 4;
	TrackView_RenderSetDiagnostics(capturedFrancePostframe || capturedTunnel || capturedLondon || capturedHawaii ||
	                               capturedHawaiiHall || hawaiiFirstFrame || capturedEgypt);
	SlipResourceHost_Initialize(SLIP_DEBUG_RESOURCE_CAPACITY_BYTES);
	SlipResourceHost_OpenArchives(argv[2], NULL);

	if (!SlipResourceHost_Load(NULL, "SMALL.FNT", &SlipMenu_resources.smallFont) ||
	    !SlipResourceHost_Load(NULL, "SHADE.FNT", &SlipMenu_resources.shadedFont) ||
	    !SlipResourceHost_Load(NULL, "SMALLEST.FNT", &SlipMenu_resources.smallestFont))
		return 4;
	SlipScreenHost_Initialize();
	/* Diagnostic startup bypasses main_sdl: preserve its BIOS palette baseline. */
	SlipVgaDac_InitializeHostBiosDefaults();
	Raster_SetScreenBufferRows(g_framebuffer, SLIPSTREAM_SCREEN_WIDTH);
	Raster_Clear(0, sizeof(g_framebuffer));
	SlipRace_racerCount = (verifySplit || verifyAiHitAudio) ? 2u : 1u;
	SlipRace_racerTable.racerCount = (verifySplit || verifyAiHitAudio) ? 2u : 1u;
	SlipRace_gameMode = verifySplit ? 1 : 0;
	SlipRace_secondPlayerEnabled = 1u;
	SlipRandom_SetState(1u, 1u);
	SlipRace_BuildRacerTable(&SlipRace_racerTable, 1u, verifySplit ? 2u : 0u);
	if (verifyLapRun) {

		SlipConfig_mode = -1;
		SlipConfig_damageOverride = -1;
		SlipRace_type = SLIP_RACE_TYPE_SINGLE;
		SlipRacePlayer_lapCount = 1;

		SlipResourcePayload driverPalettePayload = {0};
		SlipSprite driverSprite;
		if (!SlipResource_LoadByName(&argv[2], 1, "DRIVER0.SPR", &driverPalettePayload) ||
		    !SlipSprite_FromPayload(&driverPalettePayload, &driverSprite))
			return 4;
		SlipSprite_ApplyPalette(&driverSprite);
		SlipVgaDac_Commit();
		SlipResource_ReleaseHandle(&driverPalettePayload);
	}
	HmiDigitalDriver aiHitAudioDriver;
	if (verifyAiHitAudio || verifyWeaponCamera) {

		HmiDigitalDriver_Reset(&aiHitAudioDriver, SLIP_DEBUG_DIGITAL_DRIVER_VERSION);
		SlipGameSound_Reset(&musicGame, &aiHitAudioDriver);
		musicGame.initialized = 1;
		SlipSoundEffects_Install();
		musicGame.digitalCard = SLIP_DEBUG_DIGITAL_DRIVER_VERSION;
		SlipRaceSession_BindSoundHost(&musicGame, 0, NULL, NULL, NULL);
	}
	if (verifyMusic || verifyMenuMusicClose) {
		uint32_t route;
		if (!SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy") || !SDL_Init(SDL_INIT_AUDIO))
			return 4;
		musicGame.initialized = 1;
		for (route = 0; route < HMI_MUSIC_TRACK_COUNT; ++route)
			musicGame.musicRouting[route] = HMI_MUSIC_UNROUTED_DRIVER;
		if (!SlipMenuMusic_Open(argv[2], &musicGame)) {
			SDL_Quit();
			return 4;
		}
		/* Match the default menu setup, rather than the middleware's 127 default. */
		SlipMenuMusic_SetSetting(1);
		if (verifyMenuMusicClose) {
			SlipMenuMusic_Start();
			SlipMenuMusic_Close();
			if (musicGame.initialized != 0)
				return 4;
			SDL_Quit();
			puts("menu_music_close active_menu_without_other_screen_music=passed");
			return 0;
		}
		SlipRaceSession_BindSoundHost(&musicGame, 0, NULL, NULL, NULL);
	}
	if (verifyLapRun) {
		const char *const seed = getenv("SLIP_RACE_RANDOM_STATE");
		if (seed != NULL && seed[0] != 0) {
			unsigned long long first, second;
			char trailing;
			if (sscanf(seed, "%llu,%llu%c", &first, &second, &trailing) != 2 || first > UINT32_MAX ||
			    second > UINT16_MAX) {
				fprintf(stderr, "SLIP_RACE_RANDOM_STATE requires uint32,uint16\n");
				return 2;
			}
			/* Explicit diagnostic input, after table construction and before race setup. */
			SlipRandom_SetState((uint32_t)first, (uint16_t)second);
			printf("race_seed state=%u,%u\n", (uint32_t)first, (uint16_t)second);
		}
	}
	if (verifyRecordingLive) {
		SlipConfig_damageOverride = -1;
		SlipRaceRecording_Install(2 * SLIP_RECORDING_CONTROL_BYTES, SLIP_RECORDING_DEFAULT_CAPACITY_BYTES,
		                          &SlipRaceSession_recordingHost);
	}
	SlipRaceSession_Begin(
	    argv[2],
	    (verifyRefuel || verifyDoors || verifyLapRun || (verifyQuit && argc == 4)) ? (uint16_t)atoi(argv[3])
	    : verifyAiHitAudio                                                         ? SLIP_RACE_TRACK_NEW_YORK
	    : capturedFrancePostframe                                                  ? 5u
	    : capturedLondon
	        ? 8u
	        : (capturedEgypt ? 9u : ((capturedHawaii || capturedHawaiiHall || hawaiiFirstFrame) ? 2u : 6u)),
	    &SlipRace_racerTable, 3u, 2u, 2u, 1u);
	if (verifyAiHitAudio) {
		SlipRaceRacerState *const opponent = &SlipRace_racerTable.records[1];
		if (opponent->racerType != 2 || opponent->objectOffset == 0 || SlipRaceVoice_bank == 0)
			return 4;
		uint16_t lowResource;
		if (!SlipResourceHost_Find(NULL, "LOW.SMP", &lowResource) || !SlipResourceHost_IsResident(NULL, lowResource))
			return 4;

		(void)SlipObject_DispatchEvent(opponent->objectOffset, SLIP_OBJECT_EVENT_APPLY_DAMAGE, 1,
		                               SlipRacePlayer_playerOneObject, 0, 0, 0);
		const uint32_t selection = 14u + opponent->tuningIndex - 1u;
		if (SlipRaceVoice_pendingSelection != selection || SlipRaceVoice_currentRecord == NULL ||
		    SlipRaceVoice_currentRecord->sampleData == NULL || SlipRaceVoice_currentRecord->playbackHandle == 0)
			return 4;
		printf("new_york_ai_hit_audio driver=%u sample=%s playback=%u initialized_and_dispatched=passed\n",
		       opponent->tuningIndex, SlipRaceVoice_currentRecord->sampleName,
		       SlipRaceVoice_currentRecord->playbackHandle);
		SlipRaceVoice_Shutdown(&musicGame);
		return 0;
	}
	if (verifySplitLap) {
		/* AI supplies controls for both local crafts; their local-player IDs and
		 * racer types remain intact. No positions, laps or finish flags are injected. */
		uint16_t players[] = {SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject};
		uint16_t sections[2] = {0}, laps[2] = {0};
		unsigned crossings[2] = {0};
		for (unsigned player = 0; player < 2; ++player) {
			SlipRacePlayer_SetController(players[player], SLIP_RACER_COMPUTER);
			sections[player] = SlipRace_racerTable.records[player].trackComponent;
		}
		const uint32_t frameLimit = argc >= 5 ? (uint32_t)strtoul(argv[4], NULL, 10) : SLIP_DEBUG_RACE_MAXIMUM_FRAMES;
		const uint32_t tickDelta = argc == 6 ? (uint32_t)strtoul(argv[5], NULL, 10) : SLIP_DEBUG_RACE_DEFAULT_TICK_MS;
		if (frameLimit == 0 || frameLimit > SLIP_DEBUG_RACE_MAXIMUM_FRAMES || tickDelta == 0 ||
		    tickDelta > SLIP_DEBUG_RACE_MAXIMUM_TICK_MS)
			return 2;
		for (uint32_t frame = 0; frame < frameLimit; ++frame) {
			const SlipRaceFrameResult result =
			    SlipDebug_RunRaceFixtureFrame(frame * tickDelta, bindings, inputHeld1, inputPressed, false, 0, -1, -1);
			if (result == SLIP_RACE_FRAME_CONTINUE && SlipDebug_HarnessFrame(frame))
				return 0;
			bool winner = false;
			for (unsigned player = 0; player < 2; ++player) {
				SlipRaceRacerState *const racer = &SlipRace_racerTable.records[player];
				if (sections[player] != racer->trackComponent) {
					++crossings[player];
					sections[player] = racer->trackComponent;
				}
				winner |= racer->finished && racer->lapNumber > SlipRacePlayer_lapCount;
				if (traceLapFrames || frame % SLIP_DEBUG_RACE_TRACE_INTERVAL_FRAMES == 0 ||
				    laps[player] != racer->lapNumber || result != SLIP_RACE_FRAME_CONTINUE) {
					printf("split_laps player=%u track=%s frame=%u crossings=%u lap=%u finished=%u result=%u\n",
					       player + 1, argv[3], frame, crossings[player], racer->lapNumber, racer->finished, result);
					if (result == SLIP_RACE_FRAME_CONTINUE) {
						const SlipObject *const object = &SlipObject_table[players[player] / SLIP_OBJECT_DOS_STRIDE];
						const uint16_t bodyOffset = SlipObject_PhysicsOffset(SlipObject_table, players[player]);
						const SlipRaceCollisionBody *const body =
						    (const void *)(SlipRaceCollision_physicsTable + bodyOffset);
						printf("split_motion player=%u section=%04x position=%d,%d,%d direction=%d,%d,%d "
						       "speed=%d contact_time=%u contacts=%d impact=%u step=%u\n",
						       player + 1, racer->trackComponent, object->position.x, object->position.y,
						       object->position.z, object->direction.x, object->direction.y, object->direction.z,
						       object->speed, body->contactTime, body->repeatedContacts, body->impactFlag,
						       SlipFrameTimer_Step());
					}
					fflush(stdout);
				}
				laps[player] = racer->lapNumber;
			}
			if (result != SLIP_RACE_FRAME_CONTINUE)
				return result == SLIP_RACE_FRAME_ENDED && winner && crossings[0] > 1 && crossings[1] > 1 ? 0 : 4;
			if (!SlipRaceSession_lastRenderSucceeded)
				return 4;
		}
		return 4;
	}
	if (verifyLapRun) {
		/* Diagnostic only: the translated AI drives the local craft.
		 * Lap counters, finish flags, and positions are not injected. */
		SlipRaceRacerState *const racer = &SlipRace_racerTable.records[0];
		if (getenv("SLIP_HARNESS_INPUT") == NULL)
			SlipRacePlayer_SetController(SlipRacePlayer_playerOneObject, SLIP_RACER_COMPUTER);
		uint16_t previousSection = racer->trackComponent;
		uint16_t previousLap = racer->lapNumber;
		unsigned crossings = 0;
		const uint32_t frameLimit = argc >= 5 ? (uint32_t)strtoul(argv[4], NULL, 10) : SLIP_DEBUG_RACE_MAXIMUM_FRAMES;
		const uint32_t tickDelta = argc == 6 ? (uint32_t)strtoul(argv[5], NULL, 10) : SLIP_DEBUG_RACE_DEFAULT_TICK_MS;
		if (frameLimit == 0 || frameLimit > SLIP_DEBUG_RACE_MAXIMUM_FRAMES || tickDelta == 0 ||
		    tickDelta > SLIP_DEBUG_RACE_MAXIMUM_TICK_MS)
			return 2;
		for (uint32_t frame = 0; frame < frameLimit; ++frame) {
			const SlipRaceFrameResult result =
			    SlipDebug_RunRaceFixtureFrame(frame * tickDelta, bindings, inputHeld1, inputPressed, false, 0, -1, -1);
			if (result == SLIP_RACE_FRAME_CONTINUE && SlipDebug_HarnessFrame(frame))
				return 0;
			if (racer->trackComponent != previousSection) {
				++crossings;
				previousSection = racer->trackComponent;
			}
			if (traceLapFrames || frame % SLIP_DEBUG_RACE_TRACE_INTERVAL_FRAMES == 0 || frame + 1 == frameLimit ||
			    racer->lapNumber != previousLap || result != SLIP_RACE_FRAME_CONTINUE) {
				printf("race_laps track=%s frame=%u section=%04x crossings=%u lap=%u progress=%u finished=%u "
				       "destroyed=%u result=%u\n",
				       argv[3], frame, racer->trackComponent, crossings, racer->lapNumber, racer->trackProgress,
				       racer->finished, racer->destroyed, result);
				if (result == SLIP_RACE_FRAME_CONTINUE) {
					const uint16_t object = SlipRacePlayer_playerOneObject;
					SlipObjectPosition position;
					SlipObject_Position(SlipObject_table, SLIP_OBJECT_TABLE_DOS_BYTES, object, &position);
					const uint16_t bodyOffset = SlipObject_PhysicsOffset(SlipObject_table, object);
					const SlipRaceCollisionBody *const body =
					    (const void *)(SlipRaceCollision_physicsTable + bodyOffset);
					printf("race_motion position=%d,%d,%d speed=%d contact_time=%u contacts=%d impact=%u "
					       "callback=%s\n",
					       (int32_t)position.positionX, (int32_t)position.positionY, (int32_t)position.positionZ,
					       SlipObject_Speed(SlipObject_table, object), body->contactTime, body->repeatedContacts,
					       body->impactFlag,
					       SlipObject_Callback(object) == SlipRacePlayer_Update        ? "player"
					       : SlipObject_Callback(object) == SlipRacePlayer_RivalUpdate ? "rival"
					                                                                   : "other");
					const SlipObject *const motionObject = &SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE];
					printf("race_direction vector=%d,%d,%d body_flags=%04x step=%u\n", motionObject->direction.x,
					       motionObject->direction.y, motionObject->direction.z, body->flags, SlipFrameTimer_Step());
					printf("race_physics first_time=%u remaining=%u integrated=%u substeps=%u\n",
					       SlipRaceCollision_firstTime, SlipRaceCollision_frameStep, SlipRaceCollision_integratedStep,
					       SlipRacePhysics_completedSubsteps);
				}
				fflush(stdout);
				previousLap = racer->lapNumber;
			}
			if (result != SLIP_RACE_FRAME_CONTINUE)
				return result == SLIP_RACE_FRAME_ENDED && racer->finished && previousLap > SlipRacePlayer_lapCount ? 0
				                                                                                                   : 4;
			if (!SlipRaceSession_lastRenderSucceeded)
				return 4;
		}
		return 4;
	}
	if (verifyMiniMines) {
		/* Fixture input: select player two's Mini Mines, then use the ordinary fire binding. */
		SlipRaceRacerState *const racer = &SlipRace_racerTable.records[1];
		racer->primaryWeaponIndex = SLIP_RACE_WEAPON_BOMBER;
		racer->primaryWeaponAmmo = 2;
		bindings[1].select = SLIP_INPUT_SCAN_W;
		bindings[1].fire = SLIP_INPUT_SCAN_X;
		for (unsigned frame = 0; frame < 400; ++frame) {
			if (SlipDebug_RunRaceFixtureFrame(frame * 14, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
			    SLIP_RACE_FRAME_CONTINUE)
				return 4;
		}
		inputPressed[bindings[1].select] = true;
		if (SlipDebug_RunRaceFixtureFrame(400 * 14, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
		    SLIP_RACE_FRAME_CONTINUE)
			return 4;
		inputHeld1[bindings[1].fire] = true;
		if (SlipDebug_RunRaceFixtureFrame(401 * 14, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
		    SLIP_RACE_FRAME_CONTINUE)
			return 4;
		inputHeld1[bindings[1].fire] = false;
		unsigned mines = 0;
		for (uint16_t object = SlipObject_Next(UINT16_MAX); object != UINT16_MAX; object = SlipObject_Next(object)) {
			if (SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE].actorHandle == 2 &&
			    SlipRacePlayer_ProjectileWeaponIndex(object) == SLIP_RACE_WEAPON_BOMBER &&
			    SlipRacePlayer_ProjectileShooter(object) == SlipRacePlayer_playerTwoObject)
				++mines;
		}
		printf("mini_mines live_input_created=%u ammo=%u\n", mines, racer->primaryWeaponAmmo);
		if (mines != 4 || racer->primaryWeaponAmmo != 1)
			return 4;
		for (unsigned frame = 402; frame < 1200; ++frame) {
			if (SlipDebug_RunRaceFixtureFrame(frame * 14, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
			        SLIP_RACE_FRAME_CONTINUE ||
			    !SlipRaceSession_lastRenderSucceeded)
				return 4;
		}
		for (uint16_t object = SlipObject_Next(UINT16_MAX); object != UINT16_MAX; object = SlipObject_Next(object)) {
			if (SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE].actorHandle == 2 &&
			    SlipRacePlayer_ProjectileWeaponIndex(object) == SLIP_RACE_WEAPON_BOMBER)
				return 4;
		}
		puts("mini_mines live_update_render_and_removal=passed");
		return 0;
	}
	if (verifyWeaponCamera) {
		/* Fixture selects the existing Frag record and then fires using
		 * player one's ordinary control binding. No camera state is injected. */
		SlipRaceRacerState *const racer = &SlipRace_racerTable.records[0];
		racer->primaryWeaponIndex = SLIP_RACE_WEAPON_FRAG;
		racer->primaryWeaponAmmo = 2;
		SlipConfig_weaponsMonitor = 1;
		SlipConfig_rearMonitor = 1;
		for (unsigned frame = 0; frame < 400; ++frame)
			if (SlipDebug_RunRaceFixtureFrame(frame * 14, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
			    SLIP_RACE_FRAME_CONTINUE) {
				fprintf(stderr, "weapon_camera fixture assertion line=%d tracked=%u\n", __LINE__,
				        SlipRaceCamera_projectileObject);
				return 4;
			}
		inputPressed[bindings[0].select] = true;
		if (SlipDebug_RunRaceFixtureFrame(400 * 14, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
		    SLIP_RACE_FRAME_CONTINUE) {
			fprintf(stderr, "weapon_camera fixture assertion line=%d tracked=%u\n", __LINE__,
			        SlipRaceCamera_projectileObject);
			return 4;
		}
		inputHeld1[bindings[0].fire] = true;
		if (SlipDebug_RunRaceFixtureFrame(401 * 14, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
		    SLIP_RACE_FRAME_CONTINUE) {
			fprintf(stderr, "weapon_camera fixture assertion line=%d tracked=%u\n", __LINE__,
			        SlipRaceCamera_projectileObject);
			return 4;
		}
		inputHeld1[bindings[0].fire] = false;
		const uint16_t projectile = SlipRaceCamera_projectileObject;
		if (projectile == 0 || !SlipRaceSession_lastRenderSucceeded) {
			fprintf(stderr, "weapon_camera fixture assertion line=%d tracked=%u\n", __LINE__,
			        SlipRaceCamera_projectileObject);
			return 4;
		}
		const SlipObject *const projectileObject = &SlipObject_table[projectile / SLIP_OBJECT_DOS_STRIDE];

		SlipView3DVec32 expectedPosition = projectileObject->position;
		SlipView3DMatrix expectedMatrix = projectileObject->matrix;
		const uint32_t savedProjectionScale = SlipRendererHost_state.projection.perspectiveScale;
		if (SlipDebug_RunRaceFixtureFrame(402 * 14, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
		    SLIP_RACE_FRAME_CONTINUE)
			return 4;
		const SlipObject *const camera = &SlipObject_table[0];
		if (memcmp(&camera->position, &expectedPosition, sizeof(camera->position)) != 0 ||
		    memcmp(&camera->matrix, &expectedMatrix, sizeof(camera->matrix)) != 0 ||
		    (projectileObject->flags & SLIP_OBJECT_RENDER_HIDDEN) != 0) {
			fprintf(stderr,
			        "weapon_camera assertion line=%d tracked=%u pos=%d matrix=%d flags=%04x camera=(%d,%d,%d) "
			        "projectile=(%d,%d,%d)\n",
			        __LINE__, SlipRaceCamera_projectileObject,
			        memcmp(&camera->position, &expectedPosition, sizeof(camera->position)),
			        memcmp(&camera->matrix, &expectedMatrix, sizeof(camera->matrix)), projectileObject->flags,
			        camera->position.x, camera->position.y, camera->position.z, projectileObject->position.x,
			        projectileObject->position.y, projectileObject->position.z);
			return 4;
		}
		SlipDraw3DProjectState *const projection = &SlipRendererHost_state.projection;
		if (projection->perspectiveScale != savedProjectionScale)
			return 4;
		printf("weapon_camera live_input_projectile=%u weapon=%u active_camera_projection_and_show=passed\n",
		       projectile, SlipRacePlayer_ProjectileWeaponIndex(projectile));
		SlipObject_FreeImmediate(projectile, 0, 0, 0, 0, 0, 0);
		if (SlipRaceCamera_projectileObject != 0) {
			fprintf(stderr, "weapon_camera fixture assertion line=%d tracked=%u\n", __LINE__,
			        SlipRaceCamera_projectileObject);
			return 4;
		}
		SlipView3DVec32 fallbackPosition =
		    SlipObject_table[SlipRacePlayer_playerOneObject / SLIP_OBJECT_DOS_STRIDE].position;
		if (SlipDebug_RunRaceFixtureFrame(403 * 14, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
		        SLIP_RACE_FRAME_CONTINUE ||
		    !SlipRaceSession_lastRenderSucceeded) {
			fprintf(stderr, "weapon_camera fixture assertion line=%d tracked=%u\n", __LINE__,
			        SlipRaceCamera_projectileObject);
			return 4;
		}
		/* The fallback camera uses the player origin, with the reversed matrix. */
		if (memcmp(&camera->position, &fallbackPosition, sizeof(camera->position)) != 0) {
			fprintf(stderr, "weapon_camera fixture assertion line=%d tracked=%u\n", __LINE__,
			        SlipRaceCamera_projectileObject);
			return 4;
		}
		puts("weapon_camera deletion_callback_and_rear_fallback=passed");
		return 0;
	}
	if (verifyBlasterRace) {
		struct ReplayMotion {
			SlipView3DVec32 position;
			SlipView3DVec16 direction;
		};
		static struct ReplayMotion motion[800][2];
		unsigned beamFrames = 0, hitFrames = 0;
		unsigned shooters = 0;
		uint16_t players[2] = {SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject};
		bindings[0].fire = SLIP_INPUT_SCAN_Z;
		bindings[1].fire = SLIP_INPUT_SCAN_X;
		SlipConfig_damageOverride = -1;
		/* Diagnostic warm-up: let the normal update charge both Blasters. */
		for (unsigned frame = 0; frame < 400; ++frame) {
			if (SlipDebug_RunRaceFixtureFrame(frame * 14, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
			    SLIP_RACE_FRAME_CONTINUE)
				return 4;
			for (unsigned player = 0; player < 2; ++player) {
				const SlipObject *const object = &SlipObject_table[players[player] / SLIP_OBJECT_DOS_STRIDE];
				motion[frame][player] = (struct ReplayMotion){object->position, object->direction};
			}
		}
		inputPressed[SLIP_INPUT_SCAN_F1] = true;
		inputPressed[SLIP_INPUT_SCAN_F6] = true;
		for (unsigned frame = 0; frame < 400; ++frame) {
			inputHeld1[bindings[0].fire] = frame < 200;
			inputHeld1[bindings[1].fire] = frame >= 200;
			if (SlipDebug_RunRaceFixtureFrame((frame + 400) * 14, bindings, inputHeld1, inputPressed, false, 0, -1,
			                                  -1) != SLIP_RACE_FRAME_CONTINUE ||
			    !SlipRaceSession_lastRenderSucceeded)
				return 4;
			for (uint16_t object = SlipObject_Next(UINT16_MAX); object != UINT16_MAX;
			     object = SlipObject_Next(object)) {
				if (SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE].actorHandle != 2)
					continue;
				const uint16_t shooter = SlipRacePlayer_ProjectileShooter(object);
				shooters |= shooter == players[0] ? 1u : shooter == players[1] ? 2u : 0u;
			}
			if (frame == 199 && shooters != 1u)
				return 4;
			beamFrames += SlipTrackWorld_beams.queueCount != 0;
			for (unsigned player = 0; player < 2; ++player) {
				const SlipObject *const object = &SlipObject_table[players[player] / SLIP_OBJECT_DOS_STRIDE];
				motion[frame + 400][player] = (struct ReplayMotion){object->position, object->direction};
			}
			for (uint32_t beam = 0; beam < SlipTrackWorld_beams.queueCount; ++beam)
				hitFrames += (SlipTrackWorld_beams.queue[beam].material & SLIP_RACE_BEAM_HIT_FLAG) != 0;
		}
		printf("blaster_race frames=400 beam_frames=%u queued_hits=%u input_shooter_mask=%u\n", beamFrames, hitFrames,
		       shooters);
		if (verifyRecordingLive) {
			SlipRaceRecording *const recording = &SlipRaceRecording_state;
			if (recording->writtenFrames != 800)
				return 4;

			enum { RECORD_DURATION_BYTES = 2, RECORD_CONTROL_BYTES = 6, RECORD_ACTIONS_OFFSET = 4 };

			if (recording->data == NULL || recording->controlBytes != 2 * RECORD_CONTROL_BYTES)
				return 4;
			for (unsigned frame = 0; frame < 800; ++frame) {
				const uint8_t *const saved =
				    recording->data + frame * (RECORD_DURATION_BYTES + recording->controlBytes);
				const uint16_t milliseconds = SlipBytes_ReadLE16(saved);
				const uint16_t firstActions = SlipBytes_ReadLE16(saved + RECORD_DURATION_BYTES + RECORD_ACTIONS_OFFSET);
				const uint16_t secondActions =
				    SlipBytes_ReadLE16(saved + RECORD_DURATION_BYTES + RECORD_CONTROL_BYTES + RECORD_ACTIONS_OFFSET);
				if (milliseconds != (frame < 2 ? 0 : 14) ||
				    ((firstActions & SLIP_ACTION_FIRE) != 0) != (frame >= 400 && frame < 600) ||
				    ((secondActions & SLIP_ACTION_FIRE) != 0) != (frame >= 600))
					return 4;
			}
			inputPressed[SLIP_INPUT_SCAN_ESCAPE] = true;
			for (unsigned frame = 800; frame < 803; ++frame) {
				if (frame == 802)
					inputPressed[SLIP_INPUT_SCAN_ESCAPE] = true;
				if (SlipDebug_RunRaceFixtureFrame(frame * 14, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
				        SLIP_RACE_FRAME_CONTINUE ||
				    recording->writtenFrames != (frame == 802 ? 801u : 800u))
					return 4;
			}
			printf("recording_live frames=%u elapsed=%u both_player_fire_intervals=passed pause_gate=passed\n",
			       recording->writtenFrames, recording->elapsed);
			if (verifyReplayLive) {
				/* Exit through the ordinary pause menu, keeping the recorded stream for replay. */
				inputPressed[SLIP_INPUT_SCAN_ESCAPE] = true;
				if (SlipDebug_RunRaceFixtureFrame(803 * 14, bindings, inputHeld1, inputPressed, false, 0, 160, 89) !=
				    SLIP_RACE_FRAME_CONTINUE)
					return 4;
				inputPressed[SLIP_INPUT_SCAN_ENTER] = true;
				if (SlipDebug_RunRaceFixtureFrame(804 * 14, bindings, inputHeld1, inputPressed, false, 0, 160, 89) !=
				    SLIP_RACE_FRAME_ENDED)
					return 4;
				SlipRaceRacerTable completed = SlipRace_racerTable;
				uint32_t replayTick = 0;
				recording->host.readTick = SlipDebug_ReplayTick;
				recording->host.context = &replayTick;
				memset(inputHeld1, 0, sizeof(inputHeld1));
				memset(inputPressed, 0, sizeof(inputPressed));
				SlipRaceSession_Replay(argv[2], 6, &SlipRace_racerTable, 3, 2, 2, 1);
				players[0] = SlipRacePlayer_playerOneObject;
				players[1] = SlipRacePlayer_playerTwoObject;
				for (unsigned frame = 0; frame <= 800; ++frame) {
					const SlipRaceFrameResult result = SlipDebug_RunRaceFixtureFrame(
					    (1000 + frame) * 14, bindings, inputHeld1, inputPressed, false, 0, -1, -1);
					if (frame == 800) {
						if (result != SLIP_RACE_FRAME_ENDED || SlipRaceSession_exitRequested != 1)
							return 4;
						break;
					}
					if (result != SLIP_RACE_FRAME_CONTINUE)
						return 4;
					for (unsigned player = 0; player < 2; ++player) {
						const SlipObject *const object = &SlipObject_table[players[player] / SLIP_OBJECT_DOS_STRIDE];
						struct ReplayMotion expected = motion[frame][player];
						if (object->position.x != expected.position.x || object->position.y != expected.position.y ||
						    object->position.z != expected.position.z || object->direction.x != expected.direction.x ||
						    object->direction.y != expected.direction.y ||
						    object->direction.z != expected.direction.z) {
							printf("replay_mismatch frame=%u player=%u\n", frame, player + 1);
							return 4;
						}
					}
				}
				if (memcmp(&completed, &SlipRace_racerTable, sizeof(completed)) != 0)
					return 4;
				puts("replay_live matched=800 frames_per_player players=2 completed_table_preserved=passed eof=passed");
			}
			SlipRaceRecording_Release();
		}
		return beamFrames != 0 && hitFrames != 0 && shooters == 3u ? 0 : 4;
	}
	if (verifyDoors) {
		printf("track_doors track=%s count=%u\n", argv[3], SlipTrackWorld_doorCount);
		for (unsigned i = 0; i < SlipTrackWorld_doorCount; ++i) {
			const SlipTrackDoorRecord *const door = &SlipTrackWorld_doors[i];
			if (door->object != 0 || door->shapeHandle != 0 || door->trackSlotAddress != 0 ||
			    door->firstTrackRecord < SLIP_DEBUG_TRACK_RECORD_BASE_TOKEN ||
			    door->secondTrackRecord < SLIP_DEBUG_TRACK_RECORD_BASE_TOKEN)
				return 4;
			printf("door %u links=%x,%x size=%d,%d direction=%d,%d,%d plane=%d,%d,%d endpoints=%d,%d,%d/%d,%d,%d\n", i,
			       door->firstTrackRecord - SLIP_DEBUG_TRACK_RECORD_BASE_TOKEN,
			       door->secondTrackRecord - SLIP_DEBUG_TRACK_RECORD_BASE_TOKEN, door->halfWidth, door->halfHeight,
			       door->directionX, door->directionY, door->directionZ, door->planeOrigin.x, door->planeOrigin.y,
			       door->planeOrigin.z, door->openEndpoint.x, door->openEndpoint.y, door->openEndpoint.z,
			       door->closedEndpoint.x, door->closedEndpoint.y, door->closedEndpoint.z);
			printf("matrix");
			for (unsigned word = 0; word < 9; ++word)
				printf(" %d", door->matrix.m[word]);
			putchar('\n');
			if (verifyDoorShapes) {

				SlipShape3DDoorTemplate shape = SlipShape3D_doorTemplate;
				int32_t width = door->halfWidth;
				int32_t height = door->halfHeight;
				if (door->directionY == 0) {
					width = door->halfHeight;
					height = door->halfWidth;
				}
				const int16_t x = (int16_t)(width >> 6);
				const int16_t y = (int16_t)(height >> 6);
				shape.vertices[0].x = shape.vertices[3].x = (int16_t)(0u - (uint16_t)x);
				shape.vertices[1].x = shape.vertices[2].x = x;
				shape.vertices[0].y = shape.vertices[1].y = y;
				shape.vertices[2].y = shape.vertices[3].y = (int16_t)(0u - (uint16_t)y);
				if (!SlipShape3D_RecalculateBounds(&shape.header, shape.vertices, shape.vertexCount))
					return 4;
				printf("shape %u ", i);
				const unsigned char *const serialized = (const unsigned char *)(const void *)&shape;
				for (size_t byte = 0; byte < sizeof(shape); ++byte)
					printf("%02x", serialized[byte]);
				putchar('\n');
			}
		}
		return 0;
	}
	if (verifyRefuel) {
		const uint32_t record = SlipRacePlayer_refuelSection;
		if (SlipTrackWorld_refuelInitialized != UINT32_MAX || record == 0)
			return 4;
		if (SlipDebug_RunRaceFixtureFrame(28, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
		        SLIP_RACE_FRAME_CONTINUE ||
		    SlipRacePlayer_refuelSection != record)
			return 4;
		printf("track_refuel track=%s record=%08x initialized_and_retained=passed\n", argv[3], record);
		return 0;
	}
	if (capturedFrancePostframe) {

		const uint32_t cameraPosition[3] = {4938620, 1100346, 3804751};
		const uint32_t playerPosition[3] = {4939866, 1094620, 3861794};
		const uint16_t matrix[9] = {16380, 0, (uint16_t)-358, 0, 16384, 30, 358, (uint16_t)-31, 16380};
		if (!SlipRaceSession_DebugRenderCapturedState(cameraPosition, matrix, playerPosition, matrix))
			return 4;
	} else if (capturedTunnel) {
		(void)SlipRaceSession_DebugRenderCapturedState(cameraPosition1, cameraMatrix, playerPosition1, cameraMatrix);
	} else if (capturedLondon) {
		(void)SlipRaceSession_DebugRenderCapturedState(londonCameraPosition, londonCameraMatrix, londonPlayerPosition,
		                                               londonPlayerMatrix);
	} else if (capturedHawaii) {
		(void)SlipRaceSession_DebugRenderCapturedState(hawaiiCameraPosition, hawaiiCameraMatrix, hawaiiPlayerPosition,
		                                               hawaiiPlayerMatrix);
	} else if (capturedHawaiiHall) {
		(void)SlipRaceSession_DebugRenderCapturedState(hawaiiHallCameraPosition, hawaiiHallCameraMatrix,
		                                               hawaiiHallPlayerPosition, hawaiiHallPlayerMatrix);
	} else if (capturedEgypt) {
		(void)SlipRaceSession_DebugRenderCapturedState(egyptCameraPosition, egyptCameraMatrix, egyptPlayerPosition,
		                                               egyptCameraMatrix);
	} else {
		(void)SlipDebug_RunRaceFixtureFrame(28u, bindings, inputHeld1, inputPressed, false, 0u, -1, -1);
	}
	if (verifyRefuelRepair) {

		const uint32_t refuelPosition[3] = {0x004a0c80, 0x000e1680, 0x0009fdc0};
		const uint16_t identity[9] = {0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000};
		if (!SlipRaceSession_DebugRenderCapturedState(refuelPosition, identity, refuelPosition, identity))
			return 4;
		SlipRaceRacerState *const racer = &SlipRace_racerTable.records[0];
		SlipFrameTimer_SetDelta(14);
		const uint32_t repair = (uint32_t)(((uint64_t)0x190000 * SlipFrameTimer_Step()) >> 14);
		if (repair == 0)
			return 4;
		racer->movementDamageQ16 = 0x640000;
		racer->handlingDamageQ16 = repair - 1;
		SlipRacePlayer_Update(0x104, 0, 0, 0, SlipRacePlayer_playerOneObject, 0, 0);
		if (racer->movementDamageQ16 != 0x640000 - repair || racer->handlingDamageQ16 != 0)
			return 4;
		if (!SlipRaceSession_DebugRenderCapturedState(cameraPosition1, cameraMatrix, playerPosition1, cameraMatrix))
			return 4;
		racer->movementDamageQ16 = 0x640000;
		racer->handlingDamageQ16 = 0x640000;
		SlipRacePlayer_Update(0x104, 0, 0, 0, SlipRacePlayer_playerOneObject, 0, 0);
		if (racer->movementDamageQ16 != 0x640000 || racer->handlingDamageQ16 != 0x640000)
			return 4;
		printf("refuel_repair engine_decrement_control_clamp_outside_unchanged=passed decrement=%u\n", repair);
		return 0;
	}
	if (verifyFinishDelay) {

		SlipRace_racerTable.records[0].destroyed = 1;
		for (uint32_t frame = 1; frame <= 1000; ++frame) {
			const SlipRaceFrameResult result =
			    SlipDebug_RunRaceFixtureFrame(28 + frame * 14, bindings, inputHeld1, inputPressed, false, 0, -1, -1);
			if (result == SLIP_RACE_FRAME_ENDED) {
				if (frame != 359 || SlipRaceSession_exitRequested != 0 || SlipRace_racerTable.records[0].finished != 0)
					return 4;
				printf("race_finish_delay expired_and_destroyed_racer_skipped=passed frames=%u\n", frame);
				return 0;
			}
			if (result != SLIP_RACE_FRAME_CONTINUE)
				return 4;
		}
		return 4;
	}
	if (verifyFinish) {

		SlipRace_playerOneFinished = 1;
		SlipRace_playerTwoFinished = 1;
		SlipRace_playerOneFinishDelay = 0;
		SlipRace_playerTwoFinishDelay = 0;
		if (SlipDebug_RunRaceFixtureFrame(42, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
		        SLIP_RACE_FRAME_ENDED ||
		    SlipRaceSession_exitRequested != 0 || SlipRace_racerTable.records[0].finished != 1)
			return 4;
		puts("race_finish finalization_and_teardown=passed");
		return 0;
	}
	if (verifyQuit) {
		/* Quit through the real menu twice, reopening the track in between. */
		const uint16_t track = SlipRaceSession_track;
		for (unsigned pass = 0; pass < 2; ++pass) {
			inputPressed[SLIP_INPUT_SCAN_ESCAPE] = true;
			if (SlipDebug_RunRaceFixtureFrame(42, bindings, inputHeld1, inputPressed, false, 0, 160, 89) !=
			    SLIP_RACE_FRAME_CONTINUE)
				return 4;
			inputPressed[SLIP_INPUT_SCAN_ENTER] = true;
			if (SlipDebug_RunRaceFixtureFrame(56, bindings, inputHeld1, inputPressed, false, 0, 160, 89) !=
			        SLIP_RACE_FRAME_ENDED ||
			    SlipRaceSession_exitRequested != 1 || SlipObject_table != NULL || SlipRaceCollision_enabled != 0 ||
			    SlipArticSlot_initialized != 0 || SlipTrackWorld_doorCount != 0)
				return 4;
			if (pass == 0) {
				memset(inputPressed, 0, sizeof(inputPressed));
				SlipRaceSession_Begin(argv[2], track, &SlipRace_racerTable, 3u, 2u, 2u, 1u);
				if (SlipDebug_RunRaceFixtureFrame(28, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
				    SLIP_RACE_FRAME_CONTINUE)
					return 4;
			}
		}
		puts("race_quit pause_menu_teardown_and_restart=passed");
		return 0;
	}

	if (verifyFlyIn) {
		for (uint32_t frame = 1; frame <= 180; ++frame) {
			/* Reset host diagnostics so a missing draw cannot reuse last frame's success. */
			SlipRaceSession_lastRenderSucceeded = false;
			if (SlipDebug_RunRaceFixtureFrame(28u + frame * 14u, bindings, inputHeld1, inputPressed, false, 0, -1,
			                                  -1) != SLIP_RACE_FRAME_CONTINUE ||
			    !SlipRaceSession_lastRenderSucceeded) {
				printf("race_fly_in missing_render frame=%u\n", frame);
				return 4;
			}
		}
		puts("race_fly_in 180 consecutive frames rendered");
		return 0;
	}
	if (verifySplit) {
		if (SlipRacePlayer_playerOneObject == 0 || SlipRacePlayer_playerTwoObject == 0 ||
		    SlipRacePlayer_playerOneObject == SlipRacePlayer_playerTwoObject)
			return 4;
		inputPressed[SLIP_INPUT_SCAN_F1] = true;
		inputPressed[SLIP_INPUT_SCAN_F6] = true;
		/* Diagnostic damage distinguishes the two racers and exercises compact bars. */
		SlipRace_racerTable.records[0].movementDamageQ16 = 25u << 16;
		SlipRace_racerTable.records[1].movementDamageQ16 = 75u << 16;
		for (uint32_t frame = 0; frame < 3; ++frame) {
			if (SlipDebug_RunRaceFixtureFrame(42u + frame * 14u, bindings, inputHeld1, inputPressed, false, 0, -1,
			                                  -1) != SLIP_RACE_FRAME_CONTINUE)
				return 4;
		}
		for (unsigned view = 0; view < 2; ++view) {
			unsigned nonzero = 0;
			for (unsigned y = view == 0 ? 20 : 121; y < (view == 0 ? 70u : 171u); ++y)
				for (unsigned x = 30; x < 290; ++x)
					nonzero += g_displayFramebuffer[y * 320 + x] != 0;
			printf("split_view player=%u scene_pixels=%u\n", view + 1, nonzero);
			if (nonzero < 100)
				return 4;
		}
		/* Compact consoles begin at rows 88/188; their bar centers are 97/197. */
		if (g_displayFramebuffer[97 * 320 + 25] != 0x39 || g_displayFramebuffer[197 * 320 + 25] != 0x39 ||
		    g_displayFramebuffer[97 * 320 + 50] == 0x39 || g_displayFramebuffer[197 * 320 + 50] != 0x39)
			return 4;
		puts("split_render independent_damage_bars=passed");
		if (strcmp(argv[1], "--verify-split-render") == 0) {
			uint8_t before[2][64 * 8];
			bindings[0].select = SLIP_INPUT_SCAN_Q;
			bindings[1].select = SLIP_INPUT_SCAN_W;
			for (unsigned player = 0; player < 2; ++player) {
				SlipRace_racerTable.records[player].primaryWeaponIndex = player == 0 ? 4 : 7;
				SlipRace_racerTable.records[player].primaryWeaponAmmo = 9;
			}
			for (unsigned pass = 0; pass < 2; ++pass) {
				const unsigned selectedPlayer = 1 - pass;
				for (unsigned player = 0; player < 2; ++player)
					for (unsigned row = 0; row < 8; ++row)
						memcpy(before[player] + row * 64, g_displayFramebuffer + (88 + player * 100 + row) * 320 + 128,
						       64);
				for (unsigned frame = 0; frame < 3; ++frame) {
					memset(inputPressed, 0, sizeof(inputPressed));
					inputPressed[bindings[selectedPlayer].select] = frame == 0;
					if (SlipDebug_RunRaceFixtureFrame(84 + (pass * 3 + frame) * 14, bindings, inputHeld1, inputPressed,
					                                  false, 0, -1, -1) != SLIP_RACE_FRAME_CONTINUE)
						return 4;
				}
				for (unsigned player = 0; player < 2; ++player) {
					bool changed = false;
					for (unsigned row = 0; row < 8; ++row)
						changed |= memcmp(before[player] + row * 64,
						                  g_displayFramebuffer + (88 + player * 100 + row) * 320 + 128, 64) != 0;
					if (changed != (player == selectedPlayer)) {
						printf("split_weapon selected=%u display=%u changed=%u\n", selectedPlayer + 1, player + 1,
						       changed);
						return 4;
					}
				}
			}
			puts("split_render independent_weapon_keys_and_labels=passed");
		}
	}
	if (verifyDamageSmoke || verifySmoker) {
		SlipConfig_damageOverride = -1;
		uint32_t tick = 70;
		if (verifySmoker) {
			bindings[0].select = SLIP_INPUT_SCAN_Q;
			bindings[1].select = SLIP_INPUT_SCAN_W;
			bindings[0].fire = SLIP_INPUT_SCAN_Z;
			bindings[1].fire = SLIP_INPUT_SCAN_X;
			for (unsigned player = 0; player < 2; ++player) {
				SlipRace_racerTable.records[player].primaryWeaponIndex = SLIP_RACE_WEAPON_SMOKER;
				SlipRace_racerTable.records[player].primaryWeaponAmmo = 2;
			}
		}
		for (unsigned frame = 0; frame < 400; ++frame) {
			tick += 14;
			if (SlipDebug_RunRaceFixtureFrame(tick, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
			    SLIP_RACE_FRAME_CONTINUE)
				return 4;
		}
		uint16_t players[] = {SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject};
		for (unsigned player = 0; player < 2; ++player) {
			SlipRace_racerTable.records[player].movementDamageQ16 = 0;
			SlipRace_racerTable.records[player].handlingDamageQ16 = 0;
			if (!verifySmoker)
				(void)SlipObject_DispatchEvent(players[player], SLIP_OBJECT_EVENT_APPLY_DAMAGE, 7, 0, 0, 0, 0);
		}
		if (verifySmoker) {
			for (unsigned player = 0; player < 2; ++player) {
				memset(inputPressed, 0, sizeof(inputPressed));
				inputPressed[bindings[player].select] = true;
				tick += 14;
				if (SlipDebug_RunRaceFixtureFrame(tick, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
				    SLIP_RACE_FRAME_CONTINUE)
					return 4;
				inputHeld1[bindings[player].fire] = true;
				tick += 14;
				if (SlipDebug_RunRaceFixtureFrame(tick, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
				    SLIP_RACE_FRAME_CONTINUE)
					return 4;
				inputHeld1[bindings[player].fire] = false;
				for (unsigned other = 0; other < 2; ++other) {
					const uint32_t expectedAmmo = other <= player ? 1u : 2u;
					if (SlipRace_racerTable.records[other].primaryWeaponAmmo != expectedAmmo) {
						printf("smoker_input player=%u ammo_player=%u ammo=%u expected=%u\n", player + 1, other + 1,
						       SlipRace_racerTable.records[other].primaryWeaponAmmo, expectedAmmo);
						return 4;
					}
				}
			}
			puts("smoker_input independent_selection_fire_ammo=passed");
		}
		unsigned parents = 0;
		for (SlipTimedEffect *entry = SlipTimedEffects_active->next; entry != SlipTimedEffects_active;
		     entry = entry->next) {
			if (entry->descriptor != (verifySmoker ? &SlipRaceEffects_weaponSmoke : &SlipRaceEffects_damageSmoke) ||
			    (verifySmoker ? entry->lifetime < 3944 || entry->lifetime > 4000 : entry->lifetime != 4000))
				return 4;
			parents |= entry->parentObject == players[0] ? 1 : entry->parentObject == players[1] ? 2 : 0;
		}
		if (parents != 3) {
			printf("damage_smoke missing_emitters parents=%u\n", parents);
			return 4;
		}
		unsigned peak = 0, drained = 0;
		uint8_t capture[sizeof(g_displayFramebuffer)];
		for (unsigned frame = 1; frame <= (verifySmoker ? 700u : 500u); ++frame) {
			tick += 14;
			if (SlipDebug_RunRaceFixtureFrame(tick, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
			        SLIP_RACE_FRAME_CONTINUE ||
			    !SlipRaceSession_lastRenderSucceeded)
				return 4;
			if (SlipTimedEffects_objectCount > peak)
				peak = SlipTimedEffects_objectCount;
			if (frame == 30)
				memcpy(capture, g_displayFramebuffer, sizeof(capture));
			if (SlipTimedEffects_active->next == SlipTimedEffects_active && SlipTimedEffects_objectCount == 0) {
				drained = frame;
				break;
			}
		}
		printf("%s parents=%u peak_particles=%u drained_frame=%u\n", verifySmoker ? "weapon_smoke" : "damage_smoke",
		       parents, peak, drained);
		if (peak < 2 || drained < 30)
			return 4;
		memcpy(g_displayFramebuffer, capture, sizeof(capture));
	}
	if (verifyFatalDamage) {

		SlipConfig_damageOverride = -1;
		uint16_t players[] = {SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject};
		uint32_t tick = 70;
		/* Let the normal start protection expire before delivering hits. */
		for (unsigned frame = 0; frame < 400; ++frame) {
			tick += 14;
			if (SlipDebug_RunRaceFixtureFrame(tick, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
			    SLIP_RACE_FRAME_CONTINUE)
				return 4;
		}
		SlipRace_racerTable.records[0].movementDamageQ16 = 0x640000;
		SlipRace_racerTable.records[1].movementDamageQ16 = 0;
		SlipRace_racerTable.records[1].handlingDamageQ16 = 0x640000;
		tick += 14;
		if (SlipDebug_RunRaceFixtureFrame(tick, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
		    SLIP_RACE_FRAME_CONTINUE)
			return 4;
		for (unsigned player = 0; player < 2; ++player) {
			(void)SlipObject_DispatchEvent(players[player], SLIP_OBJECT_EVENT_APPLY_DAMAGE, 0, 0, 0, 0, 0);
			printf("fatal_damage player=%u falling=%u flags=%u engine=%u control=%u\n", player + 1,
			       SlipObject_Callback(players[player]) == SlipRacePlayer_FallingWreckEvent,
			       SlipRaceCollision_BodyFlags(players[player]), SlipRace_racerTable.records[player].movementDamageQ16,
			       SlipRace_racerTable.records[player].handlingDamageQ16);
			if (SlipObject_Callback(players[player]) != SlipRacePlayer_FallingWreckEvent ||
			    SlipRaceCollision_BodyFlags(players[player]) != 1)
				return 4;
		}
		printf("fatal_damage exit_flags=%u,%u timers=%u,%u mode=%u racers=%u\n", SlipRace_playerOneFinished,
		       SlipRace_playerTwoFinished, SlipRace_playerOneFinishDelay, SlipRace_playerTwoFinishDelay,
		       SlipRace_gameMode, SlipRace_racerCount);

		if (SlipRace_playerOneFinished != 1 || SlipRace_playerTwoFinished != 0 ||
		    SlipRace_playerOneFinishDelay != 4000 || SlipRace_playerTwoFinishDelay != 0)
			return 4;
		unsigned frame;
		SlipRaceFrameResult result = SLIP_RACE_FRAME_CONTINUE;
		for (frame = 1; frame <= 1500 && result == SLIP_RACE_FRAME_CONTINUE; ++frame) {
			result =
			    SlipDebug_RunRaceFixtureFrame(tick + frame * 14, bindings, inputHeld1, inputPressed, false, 0, -1, -1);
			if (SlipObject_Callback(players[0]) == NULL && SlipObject_Callback(players[1]) == NULL)
				break;
		}
		printf("fatal_damage engine_player1_control_player2 frames=%u result=%u expired=%u,%u\n", frame, result,
		       SlipObject_Callback(players[0]) == NULL, SlipObject_Callback(players[1]) == NULL);
		return result == SLIP_RACE_FRAME_CONTINUE && SlipObject_Callback(players[0]) == NULL &&
		               SlipObject_Callback(players[1]) == NULL
		           ? 0
		           : 4;
	}
	if (verifyWreckRace) {
		const uint16_t object = SlipRacePlayer_playerOneObject;
		SlipObject *const wreck = &SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE];
		SlipRacePlayer_StartWreck(object, 5000, 15);
		if (wreck->eventCallback != SlipRacePlayer_FallingWreckEvent || SlipRaceCollision_BodyFlags(object) != 1 ||
		    wreck->wreckEffect.remainingBounces < 2 || wreck->wreckEffect.remainingBounces > 3)
			return 4;
		unsigned finalFrames = 0, explosionFrames = 0, debrisFrames = 0, frame;
		for (frame = 1; frame <= 1500; ++frame) {
			if (SlipDebug_RunRaceFixtureFrame(70 + frame * 14, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
			        SLIP_RACE_FRAME_CONTINUE ||
			    !SlipRaceSession_lastRenderSucceeded)
				return 4;
			finalFrames += wreck->eventCallback == SlipRaceEffects_WreckEvent;
			debrisFrames += SlipDebug_CountDebris() != 0;
			for (uint16_t piece = SlipObject_Next(UINT16_MAX); piece != UINT16_MAX; piece = SlipObject_Next(piece)) {
				SlipObject *const effect = &SlipObject_table[piece / SLIP_OBJECT_DOS_STRIDE];
				if (effect->eventCallback == SlipAnimatedEffects_Event &&
				    effect->animatedEffect.descriptor->dosAddress == 0x4f268)
					++explosionFrames;
			}
			if (wreck->eventCallback == NULL)
				break;
		}
		printf("wreck_race frames=%u final_frames=%u explosion_frames=%u debris_frames=%u expired=%u\n", frame,
		       finalFrames, explosionFrames, debrisFrames, wreck->eventCallback == NULL);
		if (finalFrames == 0 || explosionFrames == 0 || debrisFrames == 0 || wreck->eventCallback != NULL ||
		    wreck->slotDrawCallback != NULL || wreck->drawCallback != NULL)
			return 4;
		return 0;
	}
	if (verifyDebrisCallers) {
		uint16_t drones[2];
		unsigned droneCount = 0;
		uint32_t tick = 70;
		for (unsigned frame = 0; frame < 100 && droneCount < 1; ++frame) {
			tick += 14;
			if (SlipDebug_RunRaceFixtureFrame(tick, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
			    SLIP_RACE_FRAME_CONTINUE)
				return 4;
			droneCount = 0;
			for (uint16_t object = SlipObject_Next(UINT16_MAX); object != UINT16_MAX && droneCount < 2;
			     object = SlipObject_Next(object))
				if (SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE].eventCallback == SlipRaceDrone_Event)
					drones[droneCount++] = object;
		}
		if (droneCount < 1) {
			printf("debris_callers spawned_drones=%u expected=1\n", droneCount);
			return 4;
		}

		uint32_t seed = 0;
		for (; seed < 1000; ++seed) {
			SlipRandom_SetState(seed, 0);
			if ((int32_t)SlipRandom_Next() <= 0x7000)
				break;
		}
		if (seed == 1000)
			return 4;
		SlipRandom_SetState(seed, 0);
		unsigned before = SlipDebug_CountDebris();
		SlipRace_debrisBudget = 4;
		(void)SlipObject_DispatchEvent(SlipRacePlayer_playerOneObject, SLIP_OBJECT_EVENT_APPLY_DAMAGE, 1,
		                               SlipRacePlayer_playerTwoObject, 0, 0, 0);
		if (SlipDebug_CountDebris() != before + 3 || SlipRace_debrisBudget != 1)
			return 4;
		puts("debris_caller player_damage=3 quota_remaining=1");
		for (unsigned i = 0; i < 2; ++i) {
			if (i == 1) {

				drones[1] = UINT16_MAX;
				for (unsigned frame = 0; frame < 800 && drones[1] == UINT16_MAX; ++frame) {
					tick += 14;
					if (SlipDebug_RunRaceFixtureFrame(tick, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
					    SLIP_RACE_FRAME_CONTINUE)
						return 4;
					for (uint16_t object = SlipObject_Next(UINT16_MAX); object != UINT16_MAX;
					     object = SlipObject_Next(object))
						if (SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE].eventCallback == SlipRaceDrone_Event)
							drones[1] = object;
				}
				if (drones[1] == UINT16_MAX)
					return 4;
			}
			before = SlipDebug_CountDebris();
			SlipRace_debrisBudget = 4;
			const uint32_t event = i == 0 ? SLIP_OBJECT_EVENT_APPLY_DAMAGE : SLIP_OBJECT_EVENT_COLLISION_STOP;
			(void)SlipObject_DispatchEvent(drones[i], event, 1, SlipRacePlayer_playerOneObject, 0, 0, 0);
			if ((SlipObject_IsLive(drones[i]) &&
			     SlipObject_table[drones[i] / SLIP_OBJECT_DOS_STRIDE].eventCallback == SlipRaceDrone_Event) ||
			    SlipDebug_CountDebris() != before + 4 || SlipRace_debrisBudget != 0) {
				printf("debris_caller drone_case=%u before=%u after=%u quota=%u\n", i, before, SlipDebug_CountDebris(),
				       SlipRace_debrisBudget);
				return 4;
			}
			printf("debris_caller drone_%s=4 removed=1 quota_remaining=0\n", i == 0 ? "damage" : "stop");
		}
		for (unsigned frame = 0; frame < 20; ++frame) {
			tick += 14;
			if (SlipDebug_RunRaceFixtureFrame(tick, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
			        SLIP_RACE_FRAME_CONTINUE ||
			    !SlipRaceSession_lastRenderSucceeded)
				return 4;
		}
		puts("debris_callers subsequent_race_frames=20 passed");
		return 0;
	}
	if (verifyDebrisRace) {
		SlipRace_debrisBudget = 4;
		SlipRandom_SetState(0x12345678, 0x4321);
		SlipDebug_PrintDebrisState("source", SlipRacePlayer_playerOneObject);
		SlipRaceEffects_Debris(6, SlipRacePlayer_playerOneObject, 0xabcd0000);
		printf("debris_random %u %u\n", SlipRandom_stateWords, SlipRandom_stateTail);
		uint16_t debris[4];
		SlipView3DMatrix initialMatrices[4];
		unsigned count = 0;
		for (uint16_t object = SlipObject_Next(UINT16_MAX); object != UINT16_MAX; object = SlipObject_Next(object)) {
			SlipObject *const entry = &SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE];
			if (entry->eventCallback != SlipRaceSession_DebrisEvent)
				continue;
			if (count == 4 || entry->trackSlotOffset == 0 || entry->debrisEffect.elapsed != 0 ||
			    entry->slotDrawCallback != TrackView_DrawShapeEffect || (uint16_t)entry->drawData == 0)
				return 4;
			debris[count] = object;
			initialMatrices[count++] = entry->matrix;
			printf("debris_handle %u %u\n", object, (uint32_t)entry->drawData);
			printf("debris_created object=%u shape=%u speed=%d direction=%d,%d,%d spin=%d,%d,%d\n", object,
			       (uint16_t)entry->drawData, entry->speed, entry->direction.x, entry->direction.y, entry->direction.z,
			       entry->debrisEffect.rotationRateX, entry->debrisEffect.rotationRateY,
			       entry->debrisEffect.rotationRateZ);
		}
		if (count != 4 || SlipRace_debrisBudget != 0)
			return 4;
		SlipFrameTimer_SetDelta(14);
		printf("debris_timer %u %u\n", SlipFrameTimer_delta, SlipFrameTimer_step);
		for (unsigned i = 0; i < count; ++i) {
			SlipDebug_PrintDebrisState("before", debris[i]);
			if (SlipRaceSession_DebrisEvent(SLIP_OBJECT_EVENT_UPDATE, 0, 0, 0, debris[i], 0, 0) != 0 ||
			    !SlipObject_IsLive(debris[i]))
				return 4;
			SlipDebug_PrintDebrisState("after", debris[i]);
		}
		unsigned movedFrames = 0, lastFrame = 0;
		inputPressed[SLIP_INPUT_SCAN_F2] = true;
		inputPressed[SLIP_INPUT_SCAN_F7] = true;
		bool captureSaved = false;
		uint8_t capture[sizeof(g_displayFramebuffer)];
		for (unsigned frame = 1; frame <= 720; ++frame) {
			TrackView_RenderSetDiagnostics(frame == 2);
			if (SlipDebug_RunRaceFixtureFrame(70 + frame * 14, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
			        SLIP_RACE_FRAME_CONTINUE ||
			    !SlipRaceSession_lastRenderSucceeded)
				return 4;
			unsigned live = 0;
			for (unsigned i = 0; i < count; ++i) {
				SlipObject *const entry = &SlipObject_table[debris[i] / SLIP_OBJECT_DOS_STRIDE];
				if (SlipObject_IsLive(debris[i]) && entry->eventCallback == SlipRaceSession_DebrisEvent) {
					++live;
					movedFrames += memcmp(&entry->matrix, &initialMatrices[i], sizeof(entry->matrix)) != 0;
				}
			}
			if (frame == 2) {
				memcpy(capture, g_displayFramebuffer, sizeof(capture));
				captureSaved = true;
			}
			lastFrame = frame;
			if (live == 0)
				break;
		}
		TrackView_RenderSetDiagnostics(false);
		if (movedFrames == 0 || lastFrame == 720 || !captureSaved)
			return 4;
		memcpy(g_displayFramebuffer, capture, sizeof(capture));
		printf("debris_race created=%u rotated_frames=%u last_alive_frame=%u\n", count, movedFrames, lastFrame - 1);
	}
	if (verifyTimedRace) {
		/* Actual damage-smoke descriptor and resource-loaded frames. */
		const SlipTimedEffectDescriptor *const descriptor = &SlipRaceEffects_damageSmoke;
		SlipTimedEffect emitter = {.descriptor = descriptor, .speedLimit = 1000};
		SlipView3DMatrix creationTemplate = {{-31904, -25539, 8244, 0, -31473, 147, 0, 1479, 13468}};
		if (!SlipTimedEffects_Initialize(4, 0, 16, 3, SlipRaceSession_AttachTimedEffect,
		                                 SlipRaceSession_UpdateTimedEffect)) {
			printf("timed_race failed line=%d\n", __LINE__);
			return 4;
		}
		SlipView3DVec32 position = SlipObject_table[SlipRacePlayer_playerOneObject / SLIP_OBJECT_DOS_STRIDE].position;
		uint16_t effect;
		uint32_t displacementScale;
		if (!SlipTimedEffects_CreateObject(&emitter, position, &creationTemplate, 0x723000, &effect,
		                                   &displacementScale)) {
			printf("timed_race failed line=%d\n", __LINE__);
			return 4;
		}
		SlipObject *const object = &SlipObject_table[effect / SLIP_OBJECT_DOS_STRIDE];
		if (object->trackSlotOffset == 0 || displacementScale != 2) {
			printf("timed_race failed line=%d\n", __LINE__);
			return 4;
		}
		unsigned expiredFrame = 0, movedFrames = 0;
		for (unsigned frame = 1; frame <= 240; ++frame) {
			if (SlipDebug_RunRaceFixtureFrame(70 + frame * 14, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
			        SLIP_RACE_FRAME_CONTINUE ||
			    !SlipRaceSession_lastRenderSucceeded) {
				printf("timed_race failed line=%d\n", __LINE__);
				return 4;
			}
			if (!SlipObject_IsLive(effect)) {
				expiredFrame = frame;
				break;
			}
			if (object->position.x != position.x || object->position.y < position.y ||
			    object->position.z != position.z) {
				printf("timed_race failed line=%d\n", __LINE__);
				return 4;
			}
			movedFrames += object->position.y != position.y;
			position = object->position;
		}
		printf("timed_race effect=%u expired_frame=%u remaining_objects=%u moved_frames=%u\n", effect, expiredFrame,
		       SlipTimedEffects_objectCount, movedFrames);
		if (expiredFrame == 0 || movedFrames == 0 || SlipTimedEffects_objectCount != 0) {
			printf("timed_race failed line=%d\n", __LINE__);
			return 4;
		}
		SlipTimedEffects_Cleanup();
	}
	if (verifyAnimatedRace) {
		uint32_t tick = 70;
		uint16_t drone = UINT16_MAX;
		for (unsigned frame = 0; frame < 100 && drone == UINT16_MAX; ++frame) {
			tick += 14;
			if (SlipDebug_RunRaceFixtureFrame(tick, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
			    SLIP_RACE_FRAME_CONTINUE)
				return 4;
			for (uint16_t candidate = SlipObject_Next(UINT16_MAX); candidate != UINT16_MAX;
			     candidate = SlipObject_Next(candidate)) {
				if (SlipObject_table[candidate / SLIP_OBJECT_DOS_STRIDE].eventCallback == SlipRaceDrone_Event)
					drone = candidate;
			}
		}
		if (drone == UINT16_MAX) {
			puts("animated_race no_drone_spawned");
			return 4;
		}

		SlipObject *const player = &SlipObject_table[SlipRacePlayer_playerOneObject / SLIP_OBJECT_DOS_STRIDE];
		SlipView3DVec32 forward =
		    SlipView3D_ScaleVector(player->direction.x, player->direction.y, player->direction.z, 30000);
		SlipView3DVec32 position = {(int32_t)((uint32_t)player->position.x + (uint32_t)forward.x),
		                            (int32_t)((uint32_t)player->position.y + (uint32_t)forward.y),
		                            (int32_t)((uint32_t)player->position.z + (uint32_t)forward.z)};
		/* Controlled bounce message delivered to a normally spawned drone. */
		SlipRaceCollision_bounceEvent.contactPosition = position;
		(void)SlipObject_DispatchEvent(drone, SLIP_OBJECT_EVENT_COLLISION_BOUNCE, 0x1460c, 0, 0, 0, 0);
		if (SlipObject_IsLive(drone))
			return 4;
		uint16_t effect = UINT16_MAX;
		for (uint16_t object = SlipObject_Next(UINT16_MAX); object != UINT16_MAX; object = SlipObject_Next(object)) {
			if (SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE].eventCallback == SlipAnimatedEffects_Event)
				effect = object;
		}
		if (effect == UINT16_MAX) {
			puts("animated_race creation_failed");
			return 4;
		}
		SlipObject *const object = &SlipObject_table[effect / SLIP_OBJECT_DOS_STRIDE];
		if (object->trackSlotOffset == 0 || object->animatedEffect.finalRadius != 0x2620)
			return 4;
		uint8_t capture[SLIPSTREAM_SCREEN_WIDTH * SLIPSTREAM_SCREEN_HEIGHT];
		unsigned frameChanges = 0, expiredFrame = 0;
		bool finalPhaseSeen = false;
		uint16_t previousSprite = (uint16_t)object->drawData;
		TrackView_RenderSetDiagnostics(true);
		for (unsigned frame = 1; frame <= 240; ++frame) {
			if (SlipDebug_RunRaceFixtureFrame(tick + frame * 14, bindings, inputHeld1, inputPressed, false, 0, -1,
			                                  -1) != SLIP_RACE_FRAME_CONTINUE ||
			    !SlipRaceSession_lastRenderSucceeded)
				return 4;
			if (!SlipObject_IsLive(effect)) {
				expiredFrame = frame;
				break;
			}
			if (object->position.x != position.x || object->position.y != position.y ||
			    object->position.z != position.z)
				return 4;
			if (object->animatedEffect.age > object->animatedEffect.initialDuration) {
				bool resolvedFinalFrame = false;
				for (unsigned index = 0; index < 6; ++index)
					resolvedFinalFrame |= (uint16_t)object->drawData == SlipRaceEffects_finalExplosionHandles[index];
				if (!resolvedFinalFrame)
					return 4;
				finalPhaseSeen = true;
			}
			frameChanges += previousSprite != (uint16_t)object->drawData;
			previousSprite = (uint16_t)object->drawData;
			if (frame == 120)
				memcpy(capture, g_displayFramebuffer, sizeof(capture));
		}
		TrackView_RenderSetDiagnostics(false);
		printf("animated_race drone=%u effect=%u frame_changes=%u expired_frame=%u\n", drone, effect, frameChanges,
		       expiredFrame);
		if (frameChanges == 0 || !finalPhaseSeen || expiredFrame < 214 || expiredFrame > 215)
			return 4;

		if (SlipDebug_RunRaceFixtureFrame(tick + (expiredFrame + 1) * 14, bindings, inputHeld1, inputPressed, false, 0,
		                                  -1, -1) != SLIP_RACE_FRAME_CONTINUE ||
		    !SlipRaceSession_lastRenderSucceeded)
			return 4;
		memcpy(g_displayFramebuffer, capture, sizeof(capture));
	}
	if (verifyCameras) {
		const int rear = SlipConfig_RearMonitor();
		const int weapons = SlipConfig_WeaponsMonitor();
		static const uint8_t keys[] = {0x3b, 0x3c, 0x3c, 0x3d, 0x3e, 0x3f, 0x3b};
		uint32_t tick = 42;
		for (size_t key = 0; key < sizeof(keys); ++key) {
			inputPressed[keys[key]] = true;
			for (unsigned frame = 0; frame < 3; ++frame, tick += 14) {
				if (SlipDebug_RunRaceFixtureFrame(tick, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
				    SLIP_RACE_FRAME_CONTINUE)
					return 4;
			}
			if (inputPressed[keys[key]])
				return 4;

			enum { FNV1A_OFFSET_BASIS = 2166136261u, FNV1A_PRIME = 16777619u };

			uint32_t hash = FNV1A_OFFSET_BASIS;
			for (size_t pixel = 0; pixel < sizeof(g_displayFramebuffer); ++pixel)
				hash = (hash ^ g_displayFramebuffer[pixel]) * FNV1A_PRIME;
			printf("race_camera scan=%02x framebuffer=%08x rendered=%u raw_bsp_callbacks=%u rasterized=%u\n", keys[key],
			       hash, SlipRaceSession_lastRenderSucceeded ? 1u : 0u, SlipRaceSession_lastRawBspCallbacks,
			       SlipRaceSession_lastRasterizedPrimitives);
			if (!SlipRaceSession_lastRenderSucceeded || SlipRaceSession_lastRawBspCallbacks == 0u ||
			    SlipRaceSession_lastRasterizedPrimitives == 0u)
				return 4;
		}
		for (unsigned pass = 0; pass < 2; ++pass, tick += 14) {
			inputPressed[SLIP_INPUT_SCAN_F6] = true;
			inputPressed[SLIP_INPUT_SCAN_F7] = true;
			if (SlipDebug_RunRaceFixtureFrame(tick, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
			        SLIP_RACE_FRAME_CONTINUE ||
			    inputPressed[SLIP_INPUT_SCAN_F6] || inputPressed[SLIP_INPUT_SCAN_F7] ||
			    SlipConfig_RearMonitor() != (rear ^ (pass == 0)) ||
			    SlipConfig_WeaponsMonitor() != (weapons ^ (pass == 0)))
				return 4;
			tick += 14;
			if (SlipDebug_RunRaceFixtureFrame(tick, bindings, inputHeld1, inputPressed, false, 0, -1, -1) !=
			    SLIP_RACE_FRAME_CONTINUE)
				return 4;
			if (SlipConfig_RearMonitor() != 0) {
				if (!SlipRaceSession_lastRenderSucceeded)
					return 4;
				for (int x = 210; x <= 301; ++x)
					if (g_displayFramebuffer[99 * SLIPSTREAM_SCREEN_WIDTH + x] != 0 ||
					    g_displayFramebuffer[147 * SLIPSTREAM_SCREEN_WIDTH + x] != 0)
						return 4;
				for (int y = 99; y <= 147; ++y)
					if (g_displayFramebuffer[y * SLIPSTREAM_SCREEN_WIDTH + 210] != 0 ||
					    g_displayFramebuffer[y * SLIPSTREAM_SCREEN_WIDTH + 301] != 0)
						return 4;
			}
		}
		printf("race_camera monitor_toggles=passed\n");
		return 0;
	}
	if (verifyMusic) {
		const uint64_t deadline = SDL_GetTicks() + 5000;
		while (SlipMenuMusic_NonzeroFrames() == 0 && SDL_GetTicks() < deadline)
			SDL_Delay(10);
		musicFrames = SlipMenuMusic_NonzeroFrames();
		printf("race_music_startup nonzero_pcm_frames=%llu\n", (unsigned long long)musicFrames);
		/* Test input follows the actual pause-menu hit-test and frame dispatch. */
		inputPressed[SLIP_INPUT_SCAN_ESCAPE] = true;
		inputPressed[SLIP_INPUT_MOUSE_LEFT] = true;
		if (SlipDebug_RunRaceFixtureFrame(42u, bindings, inputHeld1, inputPressed, false, 0, 160, 70) !=
		    SLIP_RACE_FRAME_OPEN_CONFIGURATION) {
			SlipMenuMusic_Close();
			SDL_Quit();
			return 4;
		}
		SlipMenuMusic_SetSetting(0);
		SlipRaceSession_ConfigurationReturn();
		SlipMenuMusic_SetSetting(1);
		SlipRaceSession_ConfigurationReturn();
		inputPressed[SLIP_INPUT_MOUSE_LEFT] = true;
		if (SlipDebug_RunRaceFixtureFrame(56u, bindings, inputHeld1, inputPressed, false, 0, 160, 53) !=
		        SLIP_RACE_FRAME_CONTINUE ||
		    SlipRace_menuRequest != 1u ||
		    SlipDebug_RunRaceFixtureFrame(70u, bindings, inputHeld1, inputPressed, false, 0, 160, 53) !=
		        SLIP_RACE_FRAME_CONTINUE ||
		    SlipRace_menuRequest != 0u) {
			SlipMenuMusic_Close();
			SDL_Quit();
			return 4;
		}
		/* Escape must open the pause menu again after Continue, not close it. */
		inputPressed[SLIP_INPUT_SCAN_ESCAPE] = true;
		inputPressed[SLIP_INPUT_MOUSE_LEFT] = true;
		if (SlipDebug_RunRaceFixtureFrame(84u, bindings, inputHeld1, inputPressed, false, 0, 160, 90) !=
		    SLIP_RACE_FRAME_ENDED) {
			SlipMenuMusic_Close();
			SDL_Quit();
			return 4;
		}
		puts("race_music_configuration_continue_and_exit passed");
		SlipMenuMusic_Close();
		SlipRaceSession_BindSoundHost(NULL, 0, NULL, NULL, NULL);
		SDL_Quit();
		if (musicFrames == 0)
			return 4;
	}
	/* Captured-state fixtures draw directly; full race iterations have presented. */
	const uint8_t *const capturedPixels = capturedFrancePostframe || capturedTunnel || capturedLondon ||
	                                              capturedHawaii || capturedHawaiiHall || capturedEgypt
	                                          ? g_framebuffer
	                                          : g_displayFramebuffer;
	for (i = 0; i < sizeof(g_framebuffer); ++i) {
		if (capturedPixels[i] != 0) {
			++nonzeroPixelCount;
		}
	}
	printf("race_render_00044906 success=%u raw_bsp_callbacks=%u rasterized=%u nonzero_pixels=%zu\n",
	       SlipRaceSession_lastRenderSucceeded ? 1u : 0u, SlipRaceSession_lastRawBspCallbacks,
	       SlipRaceSession_lastRasterizedPrimitives, nonzeroPixelCount);
	if (argc == 4) {
		if (capturedFrancePostframe) {
			char indexPath[1024];
			const int length = snprintf(indexPath, sizeof(indexPath), "%s.idx", argv[3]);
			if (length < 0 || (size_t)length >= sizeof(indexPath))
				return 3;
			FILE *const indexed = fopen(indexPath, "wb");
			if (indexed == NULL)
				return 3;
			const size_t written = fwrite(capturedPixels, 1, sizeof(g_framebuffer), indexed);
			const int closed = fclose(indexed);
			if (written != sizeof(g_framebuffer) || closed != 0)
				return 3;
		}
		/* Match the host presentation path before converting indexed pixels. */
		SlipVgaDac_Commit();
		SlipVgaDac_RefreshArgbPalette(g_palette);
		FILE *const captureFile = fopen(argv[3], "wb");

		if (captureFile == NULL)
			return 3;
		fprintf(captureFile, "P6\n%d %d\n255\n", SLIPSTREAM_SCREEN_WIDTH, SLIPSTREAM_SCREEN_HEIGHT);
		for (i = 0; i < sizeof(g_framebuffer); ++i) {
			const uint32_t color = g_palette[capturedPixels[i]];
			uint8_t rgb[3] = {(uint8_t)(color >> 16), (uint8_t)(color >> 8), (uint8_t)color};

			if (fwrite(rgb, 1, sizeof(rgb), captureFile) != sizeof(rgb)) {
				fclose(captureFile);
				return 3;
			}
		}
		if (fclose(captureFile) != 0)
			return 3;
	}
	return SlipRaceSession_lastRenderSucceeded && SlipRaceSession_lastRawBspCallbacks != 0u &&
	               SlipRaceSession_lastRasterizedPrimitives != 0u && (nonzeroPixelCount != 0u || SlipRaceDisplay_ready)
	           ? 0
	           : 2;
}

static int SlipDebug_gpuWidth = 1280, SlipDebug_gpuHeight = 720;

static bool SlipDebug_GpuSize(int *width, int *height) {
	*width = SlipDebug_gpuWidth;
	*height = SlipDebug_gpuHeight;
	return true;
}

static int SlipDebug_VerifyRaceGpu(int argc, char **argv) {
	if ((argc != 4 && argc != 5) || strcmp(argv[1], "--verify-race-gpu") != 0)
		return -1;
	if (!SDL_Init(SDL_INIT_VIDEO))
		return 4;
	const char *size = getenv("SLIP_GPU_VERIFY_SIZE");
	if (size && (sscanf(size, "%dx%d", &SlipDebug_gpuWidth, &SlipDebug_gpuHeight) != 2 || SlipDebug_gpuWidth < 2 ||
	             SlipDebug_gpuHeight < 2 || SlipDebug_gpuWidth > SLIP_DEBUG_GPU_MAXIMUM_DIMENSION ||
	             SlipDebug_gpuHeight > SLIP_DEBUG_GPU_MAXIMUM_DIMENSION))
		return 4;
	SDL_Window *window =
	    SDL_CreateWindow("Race GPU verification", SlipDebug_gpuWidth, SlipDebug_gpuHeight, SDL_WINDOW_HIDDEN);
	SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, "gpu") : NULL;
	if (!renderer || !SlipRaceGpu_Initialize(renderer)) {
		fprintf(stderr, "Race GPU initialization: %s\n", SDL_GetError());
		return 4;
	}
	SlipRaceDisplay_Configure(SlipDebug_GpuSize, NULL);
	SlipRaceDisplay_highRes = true;
	char *fixture[] = {argv[0], argc == 5 ? argv[4] : "--verify-race-render", argv[2]};
	int result = SlipDebug_VerifyRaceRender(3, fixture);
	if (result == 0 && strcmp(fixture[1], "--verify-race-cameras") == 0) {
		SlipRaceControlBinding bindings[2] = {{0}};
		bool held[SLIP_INPUT_CODE_COUNT] = {false}, pressed[SLIP_INPUT_CODE_COUNT] = {false};
		SlipConfig_rearMonitor = 1;
		SlipConfig_weaponsMonitor = 0;
		if (SlipDebug_RunRaceFixtureFrame(4000, bindings, held, pressed, false, 0, -1, -1) !=
		        SLIP_RACE_FRAME_CONTINUE ||
		    !SlipRaceSession_lastRenderSucceeded)
			result = 4;
	}
	if (result == 0 && strcmp(fixture[1], "--verify-race-fly-in") == 0) {
		SlipRaceControlBinding bindings[2] = {{0}};
		bool held[SLIP_INPUT_CODE_COUNT] = {false}, pressed[SLIP_INPUT_CODE_COUNT] = {false};
		SlipRaceDisplay_Toggle(NULL);
		if (SlipDebug_RunRaceFixtureFrame(2600, bindings, held, pressed, false, 0, -1, -1) !=
		        SLIP_RACE_FRAME_CONTINUE ||
		    SlipRaceDisplay_ready)
			result = 4;
		SlipRaceDisplay_Toggle(NULL);
		if (SlipDebug_RunRaceFixtureFrame(2614, bindings, held, pressed, false, 0, -1, -1) !=
		        SLIP_RACE_FRAME_CONTINUE ||
		    !SlipRaceDisplay_ready)
			result = 4;
		pressed[SLIP_INPUT_SCAN_ESCAPE] = true;
		if (SlipDebug_RunRaceFixtureFrame(2628, bindings, held, pressed, false, 0, -1, -1) !=
		        SLIP_RACE_FRAME_CONTINUE ||
		    !SlipRaceSession_IsPaused())
			result = 4;
		if (SlipDebug_RunRaceFixtureFrame(2642, bindings, held, pressed, false, 0, -1, -1) !=
		        SLIP_RACE_FRAME_CONTINUE ||
		    !SlipRaceSession_IsPaused())
			result = 4;
		printf("GPU live toggle and Escape pause result=%d\n", result);
	}
	if (!SlipRaceDisplay_ready || !SlipRaceGpu_Present())
		result = 4;
	else {
		SlipRaceDisplay_DrawOverlay();
		SDL_Surface *capture = SDL_RenderReadPixels(renderer, NULL);
		if (!capture || !SDL_SaveBMP(capture, argv[3]))
			result = 4;
		SDL_DestroySurface(capture);
		SDL_RenderPresent(renderer);
	}
	SlipRaceDisplay_EndFrame();
	SlipRaceGpu_Shutdown();
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	printf("race_gpu result=%d capture=%s\n", result, argv[3]);
	return result;
}

static int SlipDebug_VerifySpeedHud(int argc, char **argv) {
	SlipRaceHudAssets assets = {0};
	SlipResourcePayload speedPayload = {0}, timePayload = {0};
	SlipFont speedFont = {0}, timeFont = {0};
	uint8_t actual[320 * 200], expected[320 * 200];

	static const struct {
		uint32_t speed;
		int units;
		const char *text;
	} cases[] = {{0, 0, "0:"},   {714, 0, "0:"}, {715, 0, "1:"},    {71500, 0, "100:"},
	             {443, 1, "0;"}, {444, 1, "1;"}, {71500, 1, "161;"}};

	bool passed = true;
	if (argc != 3 || strcmp(argv[1], "--verify-speed-hud") != 0)
		return -1;
	SlipResourceHost_Initialize(SLIP_DEBUG_RESOURCE_CAPACITY_BYTES);
	SlipResourceHost_OpenArchives(argv[2], NULL);
	if (!SlipResourceHost_Load(NULL, "SPD.FNT", &assets.speedFont))
		return 4;
	if (!SlipResource_LoadByName((const char *const *)&argv[2], 1, "SPD.FNT", &speedPayload) ||
	    !SlipFont_FromPayload(&speedPayload, &speedFont))
		return 4;
	assets.speedFontLoaded = true;
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		memset(actual, 0, sizeof(actual));
		memset(expected, 0, sizeof(expected));
		SlipRaceHud_DrawSpeed(&assets, actual, 320, 4, 8, cases[i].speed, cases[i].units, 0);
		SlipFont_DrawTextClipped(&speedFont, expected, 320, 14, 14, cases[i].text, 0xfc, 0, 0, 319, 199);
		if (memcmp(actual, expected, sizeof(actual)) != 0 || memchr(actual, 0xfc, sizeof(actual)) == NULL)
			passed = false;
	}
	if (!SlipRaceHud_LoadTimeFont(&assets, (const char *const *)&argv[2], 1))
		return 4;
	if (!SlipResource_LoadByName((const char *const *)&argv[2], 1, "TIME.FNT", &timePayload) ||
	    !SlipFont_FromPayload(&timePayload, &timeFont))
		return 4;
	memset(actual, 0, sizeof(actual));
	memset(expected, 0, sizeof(expected));
	SlipRaceHud_DrawFinishPosition(&assets, actual, 320, 8, 7);
	const char *const finishText = "Finished Position 7";
	const int finishX = (320 - SlipFont_MeasureText(&timeFont, finishText)) >> 1;
	SlipFont_DrawTextClipped(&timeFont, expected, 320, finishX, 13, finishText, 0xfe, 0, 0, 319, 199);
	if (memcmp(actual, expected, sizeof(actual)) != 0 || memchr(actual, 0xfe, sizeof(actual)) == NULL)
		passed = false;
	SlipResource_ReleaseHandle(&timePayload);
	SlipResource_ReleaseHandle(&speedPayload);
	SlipResourceHost_Release(NULL, assets.timeFont);
	SlipResourceHost_Release(NULL, assets.speedFont);
	printf("speed_hud_original_font_and_units %s\n", passed ? "passed" : "FAILED");
	return passed ? 0 : 4;
}

static bool SlipDebug_RecordCameraActivation(void *context, uint16_t mode, uint16_t view) {
	uint16_t *const record = context;
	record[0] = mode;
	record[1] = view;
	++record[2];
	return true;
}

static int SlipDebug_VerifyCameraKeys(int argc, char **argv) {
	SlipRaceCameraState state = {0};
	bool pressed[SLIP_INPUT_CODE_COUNT] = {false};
	uint16_t activation[3] = {0};
	if (argc != 2 || strcmp(argv[1], "--verify-camera-keys") != 0)
		return -1;
	state.viewOneMode = 7;
	state.viewTwoMode = 4;
	state.lapNotificationTimer[0] = 12;
	state.lapNotificationTimer[1] = 34;
	state.lapTimeTimer[0] = 56;
	state.lapTimeTimer[1] = 78;
	state.shake[0] = 90;
	state.shake[1] = 12;
	state.lapTime[0] = 123456;
	state.lapTime[1] = 789012;
	SlipRaceCamera_Reset(&state);
	if (state.viewOneMode != 0 || state.viewTwoMode != 0 || state.viewOneModeNameTimer != 6000 ||
	    state.viewTwoModeNameTimer != 6000 || state.viewOneZoomDistance != 0xe000 ||
	    state.viewTwoZoomDistance != 0xe000 || state.lapNotificationTimer[0] != 0 ||
	    state.lapNotificationTimer[1] != 0 || state.lapTimeTimer[0] != 0 || state.lapTimeTimer[1] != 0 ||
	    state.shake[0] != 0 || state.shake[1] != 0 || state.lapTime[0] != 123456 || state.lapTime[1] != 789012)
		return 4;
	{
		SlipObject objects[2] = {0};
		SlipObjectPosition position;
		SlipView3DMatrix matrix;
		SlipObjectMatrixCopy copy;
		SlipObjectMatrixInstall install;
		SlipObjectSetPosition setPosition;
		for (unsigned i = 0; i < 9; ++i) {
			const uint16_t word = i == 0 ? 0x8000u : (uint16_t)(0x111u * i);
			matrix.m[i] = (int16_t)word;
		}
		if (!SlipObject_MatrixInstall(objects, 2 * SLIP_OBJECT_DOS_STRIDE, SLIP_OBJECT_DOS_STRIDE, &matrix, &install) ||
		    !SlipObject_SetPosition(objects, 2 * SLIP_OBJECT_DOS_STRIDE, SLIP_OBJECT_DOS_STRIDE, 0x80000000u, 123,
		                            0xffffffffu, &setPosition) ||
		    !SlipRaceCamera_RearMonitorTransform(objects, 2 * SLIP_OBJECT_DOS_STRIDE, SLIP_OBJECT_DOS_STRIDE, &position,
		                                         &matrix) ||
		    !SlipObject_Position(objects, 2 * SLIP_OBJECT_DOS_STRIDE, 0, &position) ||
		    !SlipObject_MatrixCopy(objects, 2 * SLIP_OBJECT_DOS_STRIDE, 0, &matrix, &copy) ||
		    position.positionX != 0x80000000u || position.positionY != 123 || position.positionZ != 0xffffffffu)
			return 4;
		for (unsigned i = 0; i < 9; ++i) {
			const uint16_t original = i == 0 ? 0x8000u : (uint16_t)(0x111u * i);
			const uint16_t expected = i >= 3 && i < 6 ? original : (uint16_t)((0u - original) & UINT16_MAX);
			if ((uint16_t)matrix.m[i] != expected)
				return 4;
		}
	}
	{
		SlipObject objects[2] = {0};
		SlipObjectSetPosition setPosition;
		SlipObjectPosition position;
		SlipView3DMatrix matrix;
		SlipObjectMatrixCopy copy;
		for (size_t i = 0; i < SLIP_RACE_TV_POSITION_COUNT; ++i)
			state.tvPositions[i] = (SlipView3DVec32){-1, 0, 0};
		state.tvPositions[1] = (SlipView3DVec32){0, 0, -0x4000};
		state.tvPositions[2] = (SlipView3DVec32){0, 0, 0x4000};
		state.tvPositions[3] = (SlipView3DVec32){0, 0, -0x8000};
		if (!SlipObject_SetPosition(objects, sizeof(objects), SLIP_OBJECT_DOS_STRIDE, 0, 0, 0, &setPosition) ||
		    !SlipRaceCamera_Finish(&state, objects, sizeof(objects), SLIP_OBJECT_DOS_STRIDE, 0x8007) ||
		    !SlipObject_Position(objects, sizeof(objects), 0, &position) ||
		    !SlipObject_MatrixCopy(objects, sizeof(objects), 0, &matrix, &copy) || state.finishPosition != 7 ||
		    position.positionX != 0 || position.positionY != 0 || position.positionZ != (uint32_t)-0x4000)
			return 4;
		for (unsigned i = 0; i < 9; ++i)
			if (matrix.m[i] != (i % 4 == 0 ? 0x4000 : 0))
				return 4;
	}
	SlipRaceCamera_ActivateTv(&state);
	if (state.tvSoundDistance != 0x7fffffff || state.tvSoundCamera != 0 || SlipRaceCamera_TvScale(0) != 0x10000u ||
	    SlipRaceCamera_TvScale(0x2620) != 0x10000u || SlipRaceCamera_TvScale(0x2620 + 0x477c0 / 2) != 0x28000u ||
	    SlipRaceCamera_TvScale(0x2620 + 0x477c0) != 0x40000u || SlipRaceCamera_TvScale(0x7fffffff) != 0x40000u ||
	    SlipRaceCamera_TvScale(0x80002620u) != 0x10000u)
		return 4;
	state.viewOneMode = 3;
	state.externalMatrix = (SlipView3DMatrix){{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}};
	state.externalDistance = 0x4c40;
	pressed[SLIP_INPUT_SCAN_KEYPAD_PLUS] = true;
	SlipRaceCamera_ExternalControls(&state, 0x2000, pressed, NULL);
	if (state.externalDistance != 0x2250)
		return 4;
	pressed[SLIP_INPUT_SCAN_KEYPAD_PLUS] = false;
	pressed[SLIP_INPUT_SCAN_KEYPAD_MINUS] = true;
	state.externalDistance = 0xbea0;
	SlipRaceCamera_ExternalControls(&state, 0x2000, pressed, NULL);
	if (state.externalDistance != 0xbea0)
		return 4;
	pressed[SLIP_INPUT_SCAN_KEYPAD_MINUS] = false;
	state.viewTwoMode = 3;
	/* F2 first selects Chase, then the duplicate F2 record selects Camera Dropped. */
	for (unsigned i = 0; i < 3; ++i) {
		const uint16_t expected = i == 1 ? 8 : 4;
		pressed[SLIP_INPUT_SCAN_F2] = true;
		if (!SlipRaceCamera_PollKeys(&state, 0, pressed, SlipDebug_RecordCameraActivation, activation) ||
		    state.viewOneMode != expected || pressed[SLIP_INPUT_SCAN_F2] || activation[0] != 4 || activation[1] != 1 ||
		    activation[2] != (i == 2 ? 2 : 1) || state.viewOneModeNameTimer != 6000)
			return 4;
	}
	/* F7 is not a second-view camera key outside split-screen. */
	pressed[SLIP_INPUT_SCAN_F7] = true;
	if (!SlipRaceCamera_PollKeys(&state, 0, pressed, SlipDebug_RecordCameraActivation, activation) ||
	    !pressed[SLIP_INPUT_SCAN_F7] || state.viewTwoMode != 3)
		return 4;
	if (!SlipRaceCamera_PollKeys(&state, 1, pressed, SlipDebug_RecordCameraActivation, activation) ||
	    pressed[SLIP_INPUT_SCAN_F7] || state.viewTwoMode != 4 || activation[1] != 2)
		return 4;
	/* Finished-racer modes block changes and leave the key unconsumed. */
	for (uint16_t mode = 1; mode <= 2; ++mode) {
		state.viewOneMode = mode;
		pressed[SLIP_INPUT_SCAN_F1] = true;
		if (!SlipRaceCamera_PollKeys(&state, 0, pressed, SlipDebug_RecordCameraActivation, activation) ||
		    state.viewOneMode != mode || !pressed[SLIP_INPUT_SCAN_F1])
			return 4;
	}
	/* Event-driven changes must run the same activation hooks as keyboard changes. */
	state.viewOneMode = 3;
	state.viewTwoMode = 3;
	activation[2] = 0;
	if (!SlipRaceCamera_Event(&state, 0x3e, SlipDebug_RecordCameraActivation, activation) || state.viewOneMode != 6 ||
	    state.viewOneModeNameTimer != 6000 || activation[0] != 6 || activation[1] != 1 || activation[2] != 1)
		return 4;
	if (!SlipRaceCamera_Event(&state, 0x3e, SlipDebug_RecordCameraActivation, activation) || activation[2] != 1)
		return 4;
	if (!SlipRaceCamera_Event(&state, 0x41, SlipDebug_RecordCameraActivation, activation) || state.viewTwoMode != 4 ||
	    state.viewTwoModeNameTimer != 6000 || activation[0] != 4 || activation[1] != 2 || activation[2] != 2)
		return 4;
	puts("camera_key_order_toggle_and_view_gates passed");
	return 0;
}

/* Isolated presenter validation; does not alter the live menu dispatch. */
static unsigned presenterFrames;
static unsigned flybyFrames, flybyFailures;
static bool presenterFast;
static unsigned presenterCaptureFrame = 120;
static const char *presenterCapturePath;
static bool campaignStream;
static unsigned campaignStage;

static void SlipDebug_PresenterFrame(void *context) {
	(void)context;
	++presenterFrames;
	if (SlipRace_flybyChaseEnabled != 0 && ++flybyFrames > 1 && !SlipRaceSession_lastRenderSucceeded)
		++flybyFailures;
	SlipVgaDac_Commit();
	SlipVgaDac_RefreshArgbPalette(g_palette);
	if (campaignStream) {

		if (presenterFrames == 1)
			SlipRandom_SetState(1, 1);
		if (SlipPresenter_state.active)
			campaignStage = campaignStage < 2 ? 1 : 3;
		else
			campaignStage = 2;
		uint32_t state[SLIP_CAPTURE_STATE_WORD_COUNT] = {campaignStage};
		if (SlipPresenter_state.active) {
			for (unsigned layer = 0; layer < SLIP_PRESENTER_LAYER_COUNT; ++layer)
				state[1 + layer] = SlipPresenter_state.layers[SlipPresenter_state.variant != 0][layer].frame;
		}
		SlipRandomState random = SlipRandom_GetState();
		if (SlipHarness_WriteFrame(presenterFrames - 1, SlipFrameTimer_Values().deltaMilliseconds, 0, random.stateWords,
		                           random.stateTail, state, g_displayFramebuffer, &g_vgaDacPalette[0][0]))
			exit(0);
		SlipDebug_clockMilliseconds += SLIP_DEBUG_PRESENTER_TICK_MS;
	}
	if (presenterFrames == presenterCaptureFrame && presenterCapturePath != NULL) {
		FILE *const f = fopen(presenterCapturePath, "wb");
		if (f == NULL)
			SlipRuntime_Fatal("Could not write presenter capture.");
		fprintf(f, "P6\n320 200\n255\n");
		for (size_t i = 0; i < sizeof(g_framebuffer); ++i) {
			const uint32_t pixel = g_palette[g_displayFramebuffer[i]];
			fputc((pixel >> 16) & 255, f);
			fputc((pixel >> 8) & 255, f);
			fputc(pixel & 255, f);
		}
		fclose(f);
	}
	if (!presenterFast)
		SDL_Delay(SLIP_DEBUG_PRESENTER_DELAY_MS);
}

static bool SlipDebug_TestObjectClip(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, void *userData) {
	SlipTrackWorldTraversalContext *const context = userData;
	SlipDraw3DProjectState *const projection = context->projectState;
	if (context->chunkCounter++ == 0) {
		if (!(x0 == 0 && y0 == 0 && x1 == 319 && y1 == 199))
			return false;
		if (!(projection->maxZ == INT32_MAX))
			return false;
		if (!(context->maxZ == INT32_MAX))
			return false;
		if (!(context->frustum->maxZ == INT32_MAX))
			return false;
	} else {
		if (!(x0 == 40 && y0 == 30 && x1 == 80 && y1 == 70))
			return false;
		if (!(projection->maxZ == 123456))
			return false;
		if (!(context->maxZ == 123456))
			return false;
		if (!(context->frustum->maxZ == 123456))
			return false;
	}
	SlipDraw3D_StoreClipBounds(projection, x0, y0, x1, y1);
	return true;
}

static int SlipDebug_VerifyVisibility(void) {
	uint8_t objectList[0x40] = {0};
	uint8_t object[0x20] = {0};
	uint8_t matrix[18] = {0};
	uint8_t trk[0x10] = {0};
	SlipTrackWorldObjectDraw out;
	SlipDraw3DProjectState projection = {.minX = 40, .minY = 30, .maxX = 80, .maxY = 70, .maxZ = 123456};
	SlipTrackWorldProjectFrustum frustum = {.maxZ = 123456};
	SlipTrackWorldTraversalContext context = {
	    .projectState = &projection, .frustum = &frustum, .storeClipBounds = SlipDebug_TestObjectClip};
	context.storeClipBoundsUserData = &context;
	memset(objectList, 0, sizeof(objectList));
	if (!(SlipTrackWorld_ObjectDraw(objectList + 4u, objectList, sizeof(objectList), SLIP_DEBUG_TRACK_RECORD_BASE_TOKEN,
	                                (SLIP_DEBUG_TRACK_RECORD_BASE_TOKEN + 4u), object, sizeof(object), 0x01002000u, 40,
	                                30, 80, 70, 0, 0, 319, 199, 0, 0, 0x20, 123456, 0, 0, 0, matrix, sizeof(matrix), 0,
	                                trk, sizeof(trk), &context, &out)))
		return 4;
	if (!(context.chunkCounter == 2))
		return 4;
	if (!(projection.minX == 40 && projection.minY == 30 && projection.maxX == 80 && projection.maxY == 70))
		return 4;

	SlipObject camera[1] = {0};
	uint8_t cells[0xf00] = {0};
	uint8_t deferred[4] = {0};
	uint32_t gate = 1, renderContext = 0, minX = 40, minY = 30, maxX = 80, maxY = 70;
	uint16_t mask = 0;
	SlipTrackWorldListSetup setup;
	SlipTrackWorldTraversalContext defaults = {.defaultTraversalGate = &gate,
	                                           .renderContextCount = &renderContext,
	                                           .primaryLeft = &minX,
	                                           .primaryTop = &minY,
	                                           .primaryRight = &maxX,
	                                           .primaryBottom = &maxY,
	                                           .mask = &mask};
	if (!SlipTrackWorld_ListSetup(objectList, sizeof(objectList), SLIP_DEBUG_TRACK_RECORD_BASE_TOKEN, deferred,
	                              sizeof(deferred), 0, 1, 0, 0, 0, 319, 199, 40, 30, 80, 70, 0, 0, 123456, 0, 0, 0,
	                              matrix, sizeof(matrix), camera, SLIP_OBJECT_DOS_STRIDE, trk, sizeof(trk), NULL, 0,
	                              NULL, 0, cells, sizeof(cells), 0, 0, &defaults, &setup))
		return 4;
	if (!setup.noSelectedRecord || gate != 0 || renderContext != 1 || mask != 0xffff || minX != 0 || minY != 0 ||
	    maxX != 319 || maxY != 199)
		return 4;
	printf("track_visibility viewport_and_far_plane=passed outside_record_defaults=passed\n");
	return 0;
}

/* Diagnostic resource-file fixture encoding, not mutable game memory. */
static void SlipDebug_BeamFixture16(uint8_t *p, uint16_t value) {
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static void SlipDebug_BeamFixture32(uint8_t *p, uint32_t value) {
	SlipDebug_BeamFixture16(p, (uint16_t)value);
	SlipDebug_BeamFixture16(p + 2, (uint16_t)(value >> 16));
}

/* Regression: the old builder consumed the queue after its first request. */
static int SlipDebug_VerifyBeamBuild(void) {
	uint8_t track[0x200] = {0}, components[0x100] = {0}, cells[0x300] = {0};
	SlipTrackBeamState beams = {0};
	const uint32_t base = 0x01000000u;
	const SlipView3DVec32 origin = {(5 << 20) + 1234, (3 << 20) + 5678, (2 << 20) + 9012};
	/* Resource fixture: a cell containing a single section with no active planes. */
	SlipDebug_BeamFixture32(cells + 548, base + 0x20);
	SlipDebug_BeamFixture16(track + 0x24, 0x40);
	SlipDebug_BeamFixture16(track + 0x40, 1);
	SlipDebug_BeamFixture16(track + 0x44, 0x20);
	SlipDebug_BeamFixture16(track + 0x46, 1);
	SlipDebug_BeamFixture32(track + 0x54, (uint32_t)origin.x);
	SlipDebug_BeamFixture32(track + 0x58, (uint32_t)origin.y);
	SlipDebug_BeamFixture32(track + 0x5c, (uint32_t)origin.z);
	SlipDebug_BeamFixture16(components + 0x24, 0x60);
	for (unsigned axis = 0; axis < 3; ++axis) {
		SlipDebug_BeamFixture16(components + 0x28 + axis * 4, (uint16_t)-1000);
		SlipDebug_BeamFixture16(components + 0x2a + axis * 4, 1000);
	}
	SlipDebug_BeamFixture16(components + 0x60, 1);
	components[0x6a] = 0x40;
	beams.queueCount = 3;
	for (unsigned i = 0; i < 3; ++i) {
		beams.queue[i].start = origin;
		beams.queue[i].end = origin;
		beams.queue[i].end.z += 100 + (int32_t)i;
		beams.queue[i].material = 0x80fd00feu + i;
	}
	if (!(SlipTrackWorld_PreFrameBuild(&beams, track, sizeof(track), components, sizeof(components), cells,
	                                   sizeof(cells), base))) {
		fprintf(stderr, "beam build check failed at %d records=%u section=%x end=%d\n", __LINE__, beams.recordCount,
		        beams.records[0].section, beams.records[0].end.z);
		return 4;
	}
	if (!(beams.queueCount == 0 && beams.recordCount == 3 && beams.built == UINT32_MAX)) {
		fprintf(stderr, "beam build check failed at %d records=%u section=%x end=%d\n", __LINE__, beams.recordCount,
		        beams.records[0].section, beams.records[0].end.z);
		return 4;
	}
	for (unsigned i = 0; i < 3; ++i) {
		if (!(beams.records[i].section == base + 0x42)) {
			fprintf(stderr, "beam build check failed at %d records=%u section=%x end=%d\n", __LINE__, beams.recordCount,
			        beams.records[0].section, beams.records[0].end.z);
			return 4;
		}
		if (!(beams.records[i].end.z == origin.z + 100 + (int32_t)i)) {
			fprintf(stderr, "beam build check failed at %d records=%u section=%x end=%d\n", __LINE__, beams.recordCount,
			        beams.records[0].section, beams.records[0].end.z);
			return 4;
		}
		if (!(beams.records[i].midpoint.z == origin.z + (100 + (int32_t)i) / 2)) {
			fprintf(stderr, "beam build check failed at %d records=%u section=%x end=%d\n", __LINE__, beams.recordCount,
			        beams.records[0].section, beams.records[0].end.z);
			return 4;
		}
		if (!(beams.records[i].material == 0x80fd00feu + i)) {
			fprintf(stderr, "beam build check failed at %d records=%u section=%x end=%d\n", __LINE__, beams.recordCount,
			        beams.records[0].section, beams.records[0].end.z);
			return 4;
		}
		if (!(beams.records[i].continuation == 0 && beams.records[i].type == 0)) {
			fprintf(stderr, "beam build check failed at %d records=%u section=%x end=%d\n", __LINE__, beams.recordCount,
			        beams.records[0].section, beams.records[0].end.z);
			return 4;
		}
	}
	/* A second view retains the already built records, including hit flags. */
	if (!(SlipTrackWorld_PreFrameBuild(&beams, track, sizeof(track), components, sizeof(components), cells,
	                                   sizeof(cells), base))) {
		fprintf(stderr, "beam build check failed at %d records=%u section=%x end=%d\n", __LINE__, beams.recordCount,
		        beams.records[0].section, beams.records[0].end.z);
		return 4;
	}
	if (!(beams.recordCount == 3 && beams.records[2].material == 0x80fd0100u)) {
		fprintf(stderr, "beam build check failed at %d records=%u section=%x end=%d\n", __LINE__, beams.recordCount,
		        beams.records[0].section, beams.records[0].end.z);
		return 4;
	}
	beams.recordCount = 0x180;
	if (!(SlipTrackWorld_AllocateBeam(&beams) == NULL)) {
		fprintf(stderr, "beam build check failed at %d records=%u section=%x end=%d\n", __LINE__, beams.recordCount,
		        beams.records[0].section, beams.records[0].end.z);
		return 4;
	}
	if (!(beams.recordCount == 0x180)) {
		fprintf(stderr, "beam build check failed at %d records=%u section=%x end=%d\n", __LINE__, beams.recordCount,
		        beams.records[0].section, beams.records[0].end.z);
		return 4;
	}
	puts("beam_build requests=3 records=3 retained_for_second_view=1 capacity=passed");
	return 0;
}

static int SlipDebug_VerifyBeamPrimitives(void) {
	SlipView3DMatrix basis;
	SlipView3D_BuildFacingBasis(&basis, 0x4000, 0, 0, 0, 0, -0x4000);
	const SlipView3DMatrix expected = {{0, -0x4000, 0, 0, 0, -0x4000, 0x4000, 0, 0}};
	if (memcmp(&basis, &expected, sizeof(basis)) != 0)
		return 4;
	SlipView3D_BuildFacingBasis(&basis, 0, 0, -0x4000, 0, 0, -0x4000);
	const SlipView3DMatrix identity = {{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}};
	if (memcmp(&basis, &identity, sizeof(basis)) != 0)
		return 4;
	SlipDraw3DRecordPool pool;
	SlipDraw3DRecordPoolInit init;
	SlipDraw3DProjectState projection;
	SlipDraw3D_InitDefaultProjectState(&projection);
	SlipDraw3D_SetViewport(&projection, 0, 0, 319, 199, 0, 0);
	SlipDraw3D_SetProjectionMode(&projection, 1);
	SlipDraw3DVec32 corners[4] = {{10, -20, 100}, {10, -30, 100}, {20, -30, 100}, {20, -20, 100}};
	const SlipDraw3DVec32 *points[4] = {corners, corners + 1, corners + 2, corners + 3};
	const uint16_t shades[4] = {0x12, 0x134, 0x56, 0x78};
	SlipDraw3DClipFlagVisit flags[64];
	SlipDraw3DPostPlaneBoundsVisit bounds[64];
	SlipDraw3DPostPlaneClipRecordVisit records[64];
	SlipDraw3DPostPlaneClipPlaneVisit planes[64];
	for (unsigned variant = 0; variant < 3; ++variant) {
		if (!SlipDraw3D_InitRecordPool(&pool, &init))
			return 4;
		if (variant == 2) {
			corners[0].x = corners[1].x = -10;
		}
		SlipDraw3DPointPolygon polygon;
		if (!SlipDraw3D_PointPolygon(&pool, points, shades, 4, variant == 1 ? 0x8000 : 0xfe, &projection, 0, NULL, 0, 0,
		                             0, 319, 0, 199, 64, flags, 64, bounds, 64, records, 64, planes, 64, &polygon) ||
		    polygon.carryOut)
			return 4;
		uint32_t cursor = pool.inputActiveHeadOffset;
		unsigned count = 0;
		do {
			const SlipDraw3DLinkedDrawRecord *const record =
			    &pool.records[cursor / SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE];
			if (record->drawRecord.screenX < 0 || record->drawRecord.screenX > 319 || count >= 8)
				return 4;
			if (variant == 1 && record->drawRecord.shade != (uint16_t)((shades[count] & 0xffu) << 8))
				return 4;
			cursor = record->links.nextOffset;
			++count;
		} while (cursor != pool.inputActiveHeadOffset);
		if (count != 4 || polygon.drawMode != (variant == 1 ? 1u : 0u))
			return 4;
	}
	puts("beam_primitives basis=passed parallel_fallback=passed flat=passed shaded=passed clipped=passed");
	return 0;
}

static int SlipDebug_VerifyBeamRender(const char *archive, const char *mathArchive) {
	const char *archives[] = {archive, mathArchive};
	SlipView3DMaths maths = {0};
	static SlipDraw3DListNode nodes[16];
	if (!SlipView3D_LoadMathsFromArchives(&maths, archives, 2) || !SlipDraw3D_InitList(nodes, 16))
		return 4;
	SlipObject camera = {0};
	camera.matrix = (SlipView3DMatrix){{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}};
	SlipDraw3DRecordPool pool;
	SlipDraw3DRecordPoolInit init;
	SlipDraw3DProjectState projection;
	TrackViewResourceHandleRegistry registry = {.archives = archives, .archiveCount = 2};
	uint16_t handles[4];
	for (unsigned i = 0; i < 4; ++i) {
		char name[13];
		uint32_t handle;
		snprintf(name, sizeof(name), "Fire%u.SPR", i + 1);
		if (!TrackView_LoadNamedResource(&registry, name, &handle))
			return 4;
		handles[i] = (uint16_t)handle;
	}
	uint8_t sectionRecords[0x100] = {0};
	unsigned sectionOffset = 0x42;
	TrackViewImpactSprites sprites = {4, handles};
	TrackView_SetImpactSprites(&sprites);
	TrackViewRawBspContext context = {.objectTable = &camera,
	                                  .objectTableBytes = SLIP_OBJECT_DOS_STRIDE,
	                                  .projectState = &projection,
	                                  .drawRecordPool = &pool,
	                                  .resourceRegistry = &registry,
	                                  .maths = &maths,
	                                  .chunkBase = sectionRecords,
	                                  .chunkBaseToken = SLIP_DEBUG_TRACK_RECORD_BASE_TOKEN};
	Raster_SetScreenBufferRows(g_framebuffer, SLIPSTREAM_SCREEN_WIDTH);
	SlipTrackWorld_beams = (SlipTrackBeamState){.built = UINT32_MAX, .recordCount = 1};
	SlipTrackWorld_beams.records[0] = (SlipTrackBeamRecord){.section = (SLIP_DEBUG_TRACK_RECORD_BASE_TOKEN + 0x42),
	                                                        .midpoint = {0, 0, 30000},
	                                                        .start = {-6000, 0, 30000},
	                                                        .end = {6000, 0, 30000},
	                                                        .material = 0x00fd00fe};
	uint32_t pixels[3][2] = {{0}};
	for (unsigned variant = 0; variant < 3; ++variant) {
		memset(g_framebuffer, 0, sizeof(g_framebuffer));
		SlipTrackWorld_beams.records[0].material = variant == 0 ? 0x00fd00fe : 0x80fd00fe;
		SlipTrackWorld_beams.records[0].continuation = variant == 2 ? UINT32_MAX : 0;
		for (unsigned view = 0; view < 2; ++view) {
			if (!SlipDraw3D_InitRecordPool(&pool, &init))
				return 4;
			SlipDraw3D_InitDefaultProjectState(&projection);
			SlipDraw3D_SetViewport(&projection, 0, (int16_t)(view * 100), 319, (int16_t)(view * 100 + 99), 159,
			                       (int16_t)(view * 100 + 49));
			SlipDraw3D_SetProjectionMode(&projection, 0);
			Raster_SetClipRect(0, (int16_t)(view * 100), 319, (int16_t)(view * 100 + 99));
			if (!TrackView_DrawComponentActors(0, sectionRecords + sectionOffset, 0, (SlipView3DVec32){0}, &context))
				return 4;
			for (unsigned i = view * 32000; i < (view + 1) * 32000; ++i)
				pixels[variant][view] += g_framebuffer[i] != 0;
			printf("beam_render variant=%u view=%u pixels=%u\n", variant, view + 1, pixels[variant][view]);
			if (pixels[variant][view] == 0 || SlipTrackWorld_beams.recordCount != 1)
				return 4;
		}
	}
	TrackView_SetImpactSprites(NULL);
	for (unsigned view = 0; view < 2; ++view)
		if (pixels[1][view] <= pixels[0][view] || pixels[2][view] != pixels[0][view])
			return 4;
	memset(g_framebuffer, 0, sizeof(g_framebuffer));
	sectionOffset += 0x40;
	if (!TrackView_DrawComponentActors(0, sectionRecords + sectionOffset, 0, (SlipView3DVec32){0}, &context))
		return 4;
	for (size_t pixel = 0; pixel < sizeof(g_framebuffer); ++pixel)
		if (g_framebuffer[pixel] != 0)
			return 4;
	sectionOffset -= 0x40;
	SlipTrackWorld_beams.records[0].type = 1;
	if (!TrackView_DrawComponentActors(0, sectionRecords + sectionOffset, 0, (SlipView3DVec32){0}, &context))
		return 4;
	unsigned randomizedPixels = 0;
	for (size_t pixel = 0; pixel < sizeof(g_framebuffer); ++pixel)
		randomizedPixels += g_framebuffer[pixel] != 0;
	if (randomizedPixels == 0)
		return 4;
	puts("beam_render both_views=passed hit_sprite=passed section_boundary_no_hit=passed section_filter=passed "
	     "randomized_type=passed");
	return 0;
}

static int SlipDebug_VerifySectionSearch(const char *inputPath, const char *outputPath, bool segmentQueries,
                                         bool refuelQueries) {

	FILE *input = fopen(inputPath, "rb"), *output = fopen(outputPath, "wb");
	uint32_t header[5];
	if (!input || !output || fread(header, sizeof(header), 1, input) != 1)
		return 2;
	if (header[0] == 0 || header[0] > 64u * 1024u * 1024u || header[1] >= header[0] || header[2] >= header[0] ||
	    header[3] >= header[0] || header[4] > 100000u)
		return 2;
	uint8_t *const memory = malloc(header[0]);
	if (!memory || fread(memory, header[0], 1, input) != 1)
		return 3;
	uint32_t trd = header[1], trc = header[2], table = header[3];
	for (uint32_t i = 0; i < header[4]; ++i) {
		if (segmentQueries) {
			int32_t coordinates[6];
			if (fread(coordinates, sizeof(coordinates), 1, input) != 1)
				return 4;
			if (refuelQueries) {
				SlipView3DVec32 endpoint;
				bool clipped = SlipTrackWorld_ClipRefuelBeam(
				    memory + trd, header[0] - trd, trd, memory + trc, header[0] - trc, memory + table,
				    header[0] - table, (SlipView3DVec32){coordinates[0], coordinates[1], coordinates[2]},
				    (SlipView3DVec32){coordinates[3], coordinates[4], coordinates[5]}, &endpoint);
				uint32_t result[] = {clipped, (uint32_t)endpoint.x, (uint32_t)endpoint.y, (uint32_t)endpoint.z};
				if (fwrite(result, sizeof(result), 1, output) != 1)
					return 7;
				continue;
			}
			SlipTrackWorldSegmentCollision segment;
			SlipTrackWorld_CheckSegmentTransition(
			    memory + trd, header[0] - trd, trd, memory + trc, header[0] - trc, memory + table, header[0] - table,
			    (SlipView3DVec32){coordinates[0], coordinates[1], coordinates[2]},
			    (SlipView3DVec32){coordinates[3], coordinates[4], coordinates[5]}, &segment);
			uint32_t result[] = {segment.transitionBlocked,    segment.hitPrimitive,
			                     segment.outputPosition.x,     segment.outputPosition.y,
			                     segment.outputPosition.z,     (uint16_t)segment.hitNormalX,
			                     (uint16_t)segment.hitNormalY, (uint16_t)segment.hitNormalZ};
			if (fwrite(result, sizeof(result), 1, output) != 1)
				return 7;
			continue;
		}
		uint32_t query[4], result[6];
		if (fread(query, sizeof(query), 1, input) != 1)
			return 4;
		SlipTrackWorldRecordSearch point;
		SlipTrackWorldOrientedRecordSearch oriented;
		SlipTrackWorldPositiveRecordSearch positive;
		if (!SlipTrackWorld_RecordSearch(memory + trd, header[0] - trd, memory + trc, header[0] - trc, memory + table,
		                                 header[0] - table, trd, query[3], query[0], query[1], query[2], &point))
			return 5;
		if (!SlipTrackWorld_OrientedRecordSearch(memory + trd, header[0] - trd, trd, memory + trc, header[0] - trc, trc,
		                                         memory + table, header[0] - table, query[0], query[1], query[2],
		                                         query[3], &oriented))
			return 6;
		result[0] = point.selectedRecordAddress;
		result[1] = oriented.recordAddress;
		result[2] = oriented.distance;
		if (!SlipTrackWorld_PositiveRecordSearch(memory + trd, header[0] - trd, trd, memory + trc, header[0] - trc, trc,
		                                         memory + table, header[0] - table, query[0], query[1], query[2],
		                                         query[3], &positive))
			return 6;
		result[3] = positive.recordAddress;
		result[4] = positive.distance;
		result[5] = positive.faceAddress;
		if (fwrite(result, sizeof(result), 1, output) != 1)
			return 7;
	}
	fclose(input);
	fclose(output);
	free(memory);
	return 0;
}

int SlipDebug_RunDumpCommand(int argc, char **argv) {
#ifdef SLIP_REPLAY_HARNESS
	if (argc == 3 && strcmp(argv[1], "--verify-saved-games") == 0)
		return SlipMenuTest_SavedGames(argv[2]);
	if (argc == 4 && strcmp(argv[1], "--verify-menu-languages") == 0)
		return SlipMenuTest_Languages(argv[2], argv[3]);
#endif
	if (argc == 3 && strcmp(argv[1], "--verify-shape-material-cache") == 0) {
		SlipResourceHost_Initialize(SLIP_DEBUG_RESOURCE_CAPACITY_BYTES);
		SlipResourceHost_OpenArchives(argv[2], NULL);
		SlipShape3D_Initialize();
		SlipResourcePayload shape, cached;
		const char *archives[] = {argv[2]};
		if (!SlipResource_LoadByName(archives, 1, "RACER0.SHP", &shape))
			return 4;
		SlipShape3DHeader *const header = (void *)shape.data;
		if ((header->flags & SLIP_SHAPE_MATERIALS_PREPARED) != 0)
			return 4;
		const uint16_t flags = header->flags;
		header->flags |= SLIP_SHAPE_MATERIALS_PREPARED;
		if (!SlipResource_LoadByName(archives, 1, "racer0.shp", &cached) || cached.data != shape.data ||
		    header->flags != (flags | SLIP_SHAPE_MATERIALS_PREPARED))
			return 4;
		SlipDraw3D_NotifyMaterials();
		if (header->flags != flags)
			return 4;
		puts("shape_material_cache load_callback=passed cached_reuse=passed material_invalidation=passed");
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "--verify-vehicle-shapes") == 0) {
		SlipResourceHost_Initialize(SLIP_DEBUG_RESOURCE_CAPACITY_BYTES);
		SlipResourceHost_OpenArchives(argv[2], NULL);
		SlipScreenHost_Initialize();
		Raster_SetScreenBufferRows(g_framebuffer, SLIPSTREAM_SCREEN_WIDTH);
		for (int driver = 0; driver < SLIP_RACE_RACER_COUNT; ++driver) {
			SlipView3DMatrix matrix = TrackView_VehicleViewIdentityMatrix();
			for (unsigned frame = 0; frame < 3; ++frame) {
				memset(g_framebuffer, 0, sizeof(g_framebuffer));
				if (!TrackView_DrawVehicleViewModel(argv[2], driver, &matrix, (uint16_t)(frame * 16))) {
					fprintf(stderr, "vehicle_shape driver=%d frame=%u failed\n", driver, frame);
					return 1;
				}
				unsigned pixels = 0;
				for (size_t i = 0; i < sizeof(g_framebuffer); ++i)
					pixels += g_framebuffer[i] != 0;
				if (pixels == 0)
					return 2;
				printf("vehicle_shape driver=%d frame=%u pixels=%u\n", driver, frame, pixels);
			}
		}
		for (uint16_t track = 0; track <= SLIP_RACE_TRACK_COUNT; ++track) {
			memset(g_framebuffer, 0, sizeof(g_framebuffer));
			if (!SlipTrackGlobe_UpdateMatrix(argv[2], track) || !SlipTrackGlobe_Draw(argv[2], track, SLIP_Q14_ONE))
				return 3;
		}
		puts("vehicle_shapes 30 animated ship frames and 11 globe/flag views=passed");
		return 0;
	}

	if (argc == 4 && strcmp(argv[1], "--verify-artic-attachment") == 0) {

		FILE *const output = fopen(argv[3], "wb");
		if (!output)
			return 2;
		SlipView3DMaths maths;
		uint8_t *payload;
		size_t bytes;
		FILE *const input = fopen(argv[2], "rb");
		if (!input)
			return 2;
		fseek(input, 0, SEEK_END);
		bytes = (size_t)ftell(input);
		rewind(input);
		payload = malloc(bytes);
		if (!payload || fread(payload, bytes, 1, input) != 1 ||
		    !SlipView3D_InitMathsFromPayload(&maths, payload, bytes))
			return 3;
		fclose(input);
		free(payload);
		for (unsigned seed = 1; seed <= 32; ++seed) {
			for (unsigned mode = 0; mode < 16; ++mode) {
				const unsigned selection = mode % 8;

				struct {
					SlipArticActorHeader actor;
					SlipView3DMatrix cachedMatrix;
					uint8_t padding[0x100 - sizeof(SlipArticActorHeader) - sizeof(SlipView3DMatrix)];
					SlipArticPartRecord part;
					SlipArticPartRecord child;
				} arena = {0};

				SlipObject objects[2] = {0};
				SlipObject *const object = &objects[1];
				object->drawData = 0x500000;
				object->position =
				    (SlipView3DVec32){(int32_t)(0x7ffffff0u + seed), -234 * (int32_t)seed, 351 * (int32_t)seed};
				SlipView3D_BuildYawMatrix(&maths, (int16_t)(seed * 1777), &object->matrix);
				arena.cachedMatrix = object->matrix;
				if (seed % 3 == 0)
					arena.cachedMatrix.m[0] ^= 1;
				arena.actor.cachedPosition = object->position;
				if (mode >= 8)
					arena.actor.cachedPosition.x = 0;
				arena.actor.owner = (uint16_t)(SLIP_OBJECT_DOS_STRIDE + (selection == 1));
				arena.actor.parts[0] = 0x500100;
				arena.actor.partCount = 1;
				arena.actor.parts[16] = 0x500100;
				arena.actor.cachedPartTag = selection == 6 ? 0x66616e31 : 0;
				arena.part.header.tag = 0x66616e31;
				arena.part.header.firstChild = 0x500260;
				arena.part.header.worldPosition = (SlipView3DVec32){111 * (int32_t)seed, -222, 333};
				arena.child.header.parent = 0x500100;
				arena.child.header.nextSibling = 0x500260;
				arena.child.header.localPosition = (SlipView3DVec32){12345 * (int32_t)seed, -4321, 32768};
				arena.child.matrixValid = seed % 2 ? 0 : UINT16_MAX;
				arena.child.rotationCallbackOffset = (seed % 4) * 4;
				arena.child.angle = (uint16_t)(seed * 997);
				arena.part.namedPointCount = selection == 3 ? 0 : 2;
				arena.part.namedPoints[0] = (SlipArticNamedPoint){0x6c61736c, {123, -456, 789}};
				arena.part.namedPoints[1] = (SlipArticNamedPoint){0x6c617372, {-32769, 65537, -111}};
				const uint32_t partTag = selection == 2 ? 0x12345678 : selection >= 5 ? 0x66616e31 : 0x6d61696e;
				const uint32_t pointTag = selection == 4 ? 0x12345678 : selection == 7 ? 0x6c61736c : 0x6c617372;
				SlipArticSlotPosition position = {0x12345678, 0x87654321, 0x10203040, false};
				if (!SlipArticSlot_WorldPosition(partTag, pointTag, SLIP_OBJECT_DOS_STRIDE, objects,
				                                 2 * SLIP_OBJECT_DOS_STRIDE, (uint8_t *)&arena, sizeof(arena), 0x500000,
				                                 (uint8_t *)&arena, sizeof(arena), 0x500000, &maths, &position))
					return 4;
				uint32_t result[] = {position.lookupFailed,
				                     position.positionX,
				                     position.positionY,
				                     position.positionZ,
				                     arena.actor.cachedPartTag,
				                     arena.actor.parts[16],
				                     (uint32_t)arena.actor.cachedPosition.x,
				                     (uint32_t)arena.actor.cachedPosition.y,
				                     (uint32_t)arena.actor.cachedPosition.z,
				                     arena.child.matrixValid,
				                     (uint32_t)arena.child.header.worldPosition.x,
				                     (uint32_t)arena.child.header.worldPosition.y,
				                     (uint32_t)arena.child.header.worldPosition.z};
				if (fwrite(result, sizeof(result), 1, output) != 1)
					return 5;
				for (unsigned i = 0; i < 9; ++i) {
					uint32_t value = (uint16_t)arena.child.worldMatrix.m[i];
					if (fwrite(&value, sizeof(value), 1, output) != 1)
						return 5;
				}
			}
		}
		SlipView3D_FreeMaths(&maths);
		return fclose(output) == 0 ? 0 : 5;
	}
	/* Isolated fixtures do not run the game's screen installer. */
	if (argc >= 2 && (strncmp(argv[1], "--dump-", 7) == 0 || strncmp(argv[1], "--verify-", 9) == 0 ||
	                  strncmp(argv[1], "--capture-", 10) == 0)) {
		static SlipScreenPage pages[2];
		g_drawPage = &pages[0];
		g_displayPage = &pages[1];
		static RasterPerspectiveEntry perspectiveTable[RASTER_PERSPECTIVE_ENTRY_COUNT];
		Raster_BuildPerspectiveTable(perspectiveTable);
		Raster_perspectiveTable = perspectiveTable;
	}

	if (argc == 3 && strcmp(argv[1], "--verify-playback-timing") == 0) {
		FILE *const file = fopen(argv[2], "w");
		if (file == NULL)
			return 4;
		for (unsigned empty = 0; empty < 2; ++empty) {
			SlipRaceRecordingFrame frames[3] = {
			    {.milliseconds = 65535},
			    {.milliseconds = 3},
			    {.milliseconds = 65535},
			};
			SlipRaceRecording state = {
			    .frames = frames, .writtenFrames = empty ? 0 : 3, .controlBytes = 12, .lateness = 0x4567};
			SlipDebugRecordingClock clock = {.state = &state};
			SlipRaceRecordingHost host = {SlipDebug_RecordingTick, SlipDebug_RecordingReset, SlipDebug_RecordingFrame,
			                              &clock};
			SlipRaceRecording_ResetPlayback(&state, &host);
			fprintf(file, "B %u %u %u %u %u %u\n", empty, state.playbackFrame, state.lastPlaybackTick,
			        state.playbackTime, state.skipPlaybackWait, clock.resets);
			for (unsigned i = 0; i < 4; ++i) {
				SlipRacePlayerControl controls[2] = {0};
				state.seeking = true;
				bool ended = SlipRaceRecording_Read(&state, &host, controls);
				fprintf(file, "P %u %u %u %u %u %u %u %u %u %u\n", ended, state.playbackFrame, state.lastPlaybackTick,
				        state.skipPlaybackWait, state.lateness, state.seeking, clock.tickIndex, clock.frames,
				        clock.callbackCursor, SlipFrameTimer_delta);
			}
		}
		const int failed = ferror(file);
		fclose(file);
		return failed ? 4 : 0;
	}
	if (argc == 3 && strcmp(argv[1], "--verify-race-recording") == 0) {
		const uint32_t capacities[] = {0, 31, 32, 39, 40, 45, 46, 48, 60, 100000};
		const uint32_t deltas[] = {0, 14, 16, 999, 1000, 1001, 65535, 65536};
		FILE *const file = fopen(argv[2], "w");
		if (file == NULL)
			return 4;
		for (uint16_t bytes = 6; bytes <= 12; bytes += 6) {
			for (size_t c = 0; c < sizeof(capacities) / sizeof(capacities[0]); ++c) {
				SlipRaceRecordingFrame frames[8] = {0};
				SlipRaceRecording state = {.frames = frames, .capacityBytes = capacities[c], .controlBytes = bytes};
				SlipRaceRecording_Reset(&state);
				for (uint16_t i = 0; i < 8; ++i) {
					SlipRacePlayerControl controls[2] = {
					    {(int16_t)(-32000 + i), (int16_t)(30000 - i), i},
					    {(int16_t)(1234 + i), (int16_t)(-2345 - i), (uint16_t)(0x8000 + i)},
					};
					SlipFrameTimer_delta = deltas[i];
					SlipRaceRecording_Write(&state, controls);
					fprintf(file, "W %u %u %u %u %u\n", bytes, capacities[c], i, state.writtenFrames, state.elapsed);
				}
				state.recording = false;
				SlipRacePlayerControl ignored[2] = {0};
				SlipRaceRecording_Write(&state, ignored);
				fprintf(file, "S %u %u\n", state.writtenFrames, state.elapsed);
				while (state.playbackFrame < state.writtenFrames) {
					SlipRacePlayerControl controls[2] = {{111, 222, 333}, {444, 555, 666}};
					state.playbackFrame = SlipRaceRecording_ReadFrame(&state, controls);
					fprintf(file, "R %u %u %u %d %d %u %d %d %u\n", SlipFrameTimer_delta,
					        SlipFrameTimer_step & UINT16_MAX, SlipFrameTimer_inverse, controls[0].steering,
					        controls[0].pitch, controls[0].actions, controls[1].steering, controls[1].pitch,
					        controls[1].actions);
				}
			}
		}
		const int failed = ferror(file);
		fclose(file);
		return failed ? 4 : 0;
	}
	if (argc == 3 && strcmp(argv[1], "--verify-results-input") == 0) {
		const int16_t xs[] = {-32768, -1, 39, 40, 127, 128, 189, 190, 277, 278, 32767};
		const int16_t ys[] = {-32768, -1, 174, 175, 191, 192, 32767};
		FILE *const file = fopen(argv[2], "w");
		if (file == NULL)
			return 4;
		for (size_t x = 0; x < sizeof(xs) / sizeof(xs[0]); ++x) {
			for (size_t y = 0; y < sizeof(ys) / sizeof(ys[0]); ++y) {
				for (unsigned mask = 0; mask < 8; ++mask) {
					bool pressed[SLIP_INPUT_CODE_COUNT] = {false};
					pressed[SLIP_INPUT_SCAN_ESCAPE] = (mask & 1) != 0;
					pressed[SLIP_INPUT_SCAN_ENTER] = (mask & 2) != 0;
					pressed[SLIP_INPUT_MOUSE_LEFT] = (mask & 4) != 0;
					const uint32_t hover = SlipRaceResults_HitTest(xs[x], ys[y]);
					const SlipRaceResultsAction action = SlipRaceResults_ReadInput(hover, pressed);
					const unsigned remaining = pressed[SLIP_INPUT_SCAN_ESCAPE] | (pressed[SLIP_INPUT_SCAN_ENTER] << 1) |
					                           (pressed[SLIP_INPUT_MOUSE_LEFT] << 2);
					fprintf(file, "%d\t%d\t%u\t%u\t%d\t%u\n", xs[x], ys[y], mask, hover, action, remaining);
				}
			}
		}
		const int failed = ferror(file);
		fclose(file);
		return failed ? 4 : 0;
	}

	if (argc == 4 &&
	    (strcmp(argv[1], "--verify-results-rows") == 0 || strcmp(argv[1], "--verify-championship-rows") == 0 ||
	     strcmp(argv[1], "--verify-championship-final-rows") == 0)) {
		bool championship = strcmp(argv[1], "--verify-championship-rows") == 0;
		bool final = strcmp(argv[1], "--verify-championship-final-rows") == 0;
		SlipResourcePayload payloads[2] = {0};
		SlipFont fonts[2];
		const char *names[2] = {"RESULTSA.FNT", "RESULTSB.FNT"};
		if (final) {
			names[0] = "RESULTSC.FNT";
			names[1] = "RESULTSD.FNT";
		}
		const uint32_t times[10] = {0, 9, 10, 59999, 60000, 3599999, 3600000, 86399999, 360000000, UINT32_MAX};
		uint8_t pixels[320 * 200];
		SlipRaceRacerTable racers = {.racerCount = 10};
		char path[1024];
		for (unsigned i = 0; i < 2; ++i) {
			if (!SlipResource_LoadByName((const char *const *)&argv[2], 1, names[i], &payloads[i]) ||
			    !SlipFont_FromPayload(&payloads[i], &fonts[i]))
				return 4;
			snprintf(path, sizeof(path), "%s.font%u", argv[3], i);
			FILE *const file = fopen(path, "wbx");
			if (file == NULL)
				return 4;
			const size_t written = fwrite(payloads[i].data, 1, payloads[i].size, file);
			fclose(file);
			if (written != payloads[i].size)
				return 4;
		}
		FILE *file = fopen(argv[3], "wbx");
		if (file == NULL)
			return 4;
		for (unsigned pass = 0; pass < 2; ++pass) {
			for (unsigned i = 0; i < 10; ++i) {
				racers.records[i].tuningIndex = (uint16_t)(i + 1);
				racers.records[i].racerType = (uint16_t)(i % 3);
				racers.records[i].racePosition = (uint16_t)(10 - i);
				racers.records[i].finished = (uint8_t)((i + pass) % 2);
				racers.records[i].totalRaceTime = times[i];
				racers.records[i].championshipPosition = (uint16_t)(10 - i);
				racers.records[i].championshipPoints = (uint16_t)(times[i] + pass);
			}
			memset(pixels, 0, sizeof(pixels));
			if (final)
				SlipChampionshipFinal_DrawRows(&racers, &fonts[0], &fonts[1], pixels, 320);
			else if (championship)
				SlipChampionship_DrawRows(&racers, &fonts[0], &fonts[1], pixels, 320);
			else
				SlipRaceResults_DrawRows(&racers, &fonts[0], &fonts[1], pixels, 320);
			if (fwrite(pixels, 1, sizeof(pixels), file) != sizeof(pixels)) {
				fclose(file);
				return 4;
			}
		}
		fclose(file);
		if (final) {
			snprintf(path, sizeof(path), "%s.inputs", argv[3]);
			file = fopen(path, "wbx");
			if (file == NULL)
				return 4;
			for (unsigned hover = 0; hover < 3; ++hover) {
				for (unsigned edges = 0; edges < 8; ++edges) {
					bool pressed[SLIP_INPUT_CODE_COUNT] = {false};
					pressed[SLIP_INPUT_SCAN_ESCAPE] = (edges & 1) != 0;
					pressed[SLIP_INPUT_SCAN_ENTER] = (edges & 2) != 0;
					pressed[SLIP_INPUT_MOUSE_LEFT] = (edges & 4) != 0;
					uint8_t result[4];
					result[0] = SlipChampionshipFinal_ReadInput(hover, pressed);
					result[1] = pressed[SLIP_INPUT_SCAN_ESCAPE];
					result[2] = pressed[SLIP_INPUT_SCAN_ENTER];
					result[3] = pressed[SLIP_INPUT_MOUSE_LEFT];
					if (fwrite(result, 1, sizeof(result), file) != sizeof(result)) {
						fclose(file);
						return 4;
					}
				}
			}
			fclose(file);
		}
		SlipResourcePayload spritePayloads[2] = {0}, strings = {0};
		SlipSprite sprites[2];
		const char *spriteNames[2] = {"RACERES.SPR", "RACERESD.SPR"};
		if (final) {
			spriteNames[0] = "FINALPOS.SPR";
			spriteNames[1] = "FINPOSD.SPR";
		}
		char labels[2][80], title[80];
		const char *labelPointers[2] = {labels[0], labels[1]};
		if (!SlipResource_LoadByName((const char *const *)&argv[2], 1,
		                             final          ? "FINALPOS.ST0"
		                             : championship ? "CHAMPPOS.ST0"
		                                            : "RACERES.ST0",
		                             &strings) ||
		    !SlipStringTable_FindText(&strings, "BUT1", labels[0], sizeof(labels[0])) ||
		    (!final && !SlipStringTable_FindText(&strings, "BUT2", labels[1], sizeof(labels[1]))) ||
		    !SlipStringTable_FindText(&strings, championship || final ? "TITL" : "TIT4", title, sizeof(title)))
			return 4;
		for (unsigned i = 0; i < 2; ++i) {
			if (!SlipResource_LoadByName((const char *const *)&argv[2], 1, spriteNames[i], &spritePayloads[i]) ||
			    !SlipSprite_FromPayload(&spritePayloads[i], &sprites[i]))
				return 4;
			snprintf(path, sizeof(path), "%s.sprite%u", argv[3], i);
			file = fopen(path, "wbx");
			if (file == NULL)
				return 4;
			const size_t written = fwrite(spritePayloads[i].data, 1, spritePayloads[i].size, file);
			fclose(file);
			if (written != spritePayloads[i].size)
				return 4;
		}
		Raster_SetScreenBufferRows(pixels, 320);
		for (uint32_t hover = 0; hover < 3; ++hover) {
			memset(pixels, 0, sizeof(pixels));
			if (final)
				SlipChampionshipFinal_DrawFrame(&racers, &sprites[0], &sprites[1], &fonts[0], &fonts[1], title,
				                                labels[0], hover, pixels, 320);
			else if (championship)
				SlipChampionship_DrawFrame(&racers, &sprites[0], &sprites[1], &fonts[0], &fonts[1], labelPointers,
				                           title, hover, pixels, 320);
			else
				SlipRaceResults_DrawFrame(&racers, &sprites[0], &sprites[1], &fonts[0], &fonts[1], labelPointers, title,
				                          hover, pixels, 320);
			snprintf(path, sizeof(path), "%s.frame%u", argv[3], hover);
			file = fopen(path, "wbx");
			if (file == NULL)
				return 4;
			const size_t written = fwrite(pixels, 1, sizeof(pixels), file);
			fclose(file);
			if (written != sizeof(pixels))
				return 4;
		}
		for (unsigned i = 0; i < 2; ++i) {
			SlipResource_ReleaseHandle(&payloads[i]);
			SlipResource_ReleaseHandle(&spritePayloads[i]);
		}
		SlipResource_ReleaseHandle(&strings);
		return 0;
	}
	if (argc == 4 && strcmp(argv[1], "--verify-screen-clip") == 0) {
		FILE *input = fopen(argv[2], "rb"), *output = fopen(argv[3], "wb");
		uint32_t header[3];
		if (!input || !output)
			return 2;
		while (fread(header, sizeof(header), 1, input) == 1) {
			SlipDraw3DDrawRecord records[2];
			SlipDraw3DSplitScreenX splitX;
			SlipDraw3DSplitScreenY splitY;
			if (fread(records, sizeof(records), 1, input) != 1)
				return 3;
			const int success =
			    header[0] == 0
			        ? SlipDraw3D_SplitScreenXRecord((uint8_t *)records, sizeof(records), 0, sizeof(records[0]),
			                                        header[1], (int32_t)header[2], 0, 199, &splitX)
			        : SlipDraw3D_SplitScreenYRecord((uint8_t *)records, sizeof(records), 0, sizeof(records[0]),
			                                        header[1], (int32_t)header[2], &splitY);
			if (!success || fwrite(records, sizeof(records), 1, output) != 1)
				return 4;
		}
		fclose(input);
		return fclose(output) == 0 ? 0 : 5;
	}
	if (argc == 3 && strcmp(argv[1], "--verify-weapon-labels") == 0) {
		/* Host diagnostic output must never replace an existing file. */
		FILE *const output = fopen(argv[2], "wbx");
		if (!output)
			return 2;
		const uint32_t counts[] = {UINT32_MAX, 0, 1, 5, 9, 10, 208, 0x80000000u};
		for (unsigned weapon = 0; weapon < 12; ++weapon) {
			for (unsigned count = 0; count < sizeof(counts) / sizeof(counts[0]); ++count) {
				char label[24] = {0};
				SlipRacePlayer_BuildWeaponLabel(SlipRacePlayer_records, weapon, counts[count], label);
				if (fwrite(label, sizeof(label), 1, output) != 1)
					return 3;
			}
		}
		return fclose(output) == 0 ? 0 : 3;
	}
	if (argc == 4 && strcmp(argv[1], "--verify-section-search") == 0)
		return SlipDebug_VerifySectionSearch(argv[2], argv[3], false, false);
	if (argc == 4 && strcmp(argv[1], "--verify-section-segments") == 0)
		return SlipDebug_VerifySectionSearch(argv[2], argv[3], true, false);
	if (argc == 4 && strcmp(argv[1], "--verify-refuel-segments") == 0)
		return SlipDebug_VerifySectionSearch(argv[2], argv[3], true, true);
	if (argc == 4 && strcmp(argv[1], "--verify-beam-render") == 0)
		return SlipDebug_VerifyBeamRender(argv[2], argv[3]);
	if (argc == 2 && strcmp(argv[1], "--verify-beam-primitives") == 0)
		return SlipDebug_VerifyBeamPrimitives();
	if (argc == 2 && strcmp(argv[1], "--verify-beam-build") == 0)
		return SlipDebug_VerifyBeamBuild();
	if (argc == 2 && strcmp(argv[1], "--verify-voice-shutdown") == 0) {
		SlipGameSoundState sound = {0};
		SlipRaceVoiceRecord *const record = &SlipRaceVoice_raceRecords[0];
		uint8_t payload = 1;
		SlipResourceBlock block = {.capacityBytes = 32, .lockCount = 2};
		SlipResourceHandle handles[2] = {{0}, {.block = &block}};
		SlipResourceHandle *const savedHandles = SlipResource_handles;
		const uint32_t savedReclaimable = SlipResource_cachedBytes;
		SlipResource_handles = handles;
		record->resourceHandle = 1;
		record->sampleData = &payload;
		record->playbackHandle = 1;
		SlipRaceVoice_bank = 3;
		SlipRaceVoice_Shutdown(&sound);
		if (SlipRaceVoice_bank != 3 || block.lockCount != 2 || block.flags != 0)
			return 4;
		sound.digitalCard = 1;
		SlipRaceVoice_Shutdown(&sound);
		if (SlipRaceVoice_bank != 0 || block.lockCount != 1 || block.flags != SLIP_RESOURCE_BLOCK_CACHED ||
		    record->sampleData != &payload || record->resourceHandle != 1 || record->playbackHandle != 1 ||
		    SlipResource_cachedBytes != savedReclaimable + 64)
			return 4;
		SlipRaceVoice_Shutdown(&sound);
		SlipResource_handles = savedHandles;
		SlipResource_cachedBytes = savedReclaimable;
		puts("voice_shutdown disabled_gate_release_inactive_bank=passed");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--verify-voice-completion") == 0) {
		SlipGameSoundState sound = {0};
		SlipRaceVoiceRecord record = {.resourceHandle = 1, .speakingDriver = 10};
		uint8_t payload = 1;
		SlipResourceBlock block = {.lockCount = 2};
		SlipResourceHandle handles[2] = {{0}, {.block = &block}};
		SlipResourceHandle *const savedHandles = SlipResource_handles;
		SlipResource_handles = handles;
		SlipRaceVoice_bank = 3;
		SlipRaceVoice_currentRecord = &record;
		SlipRaceVoice_speakingDriver = 10;
		/* The sound gate suppresses the return without clearing current state. */
		if (SlipRaceVoice_SpeakingDriver(&sound) != 0 || SlipRaceVoice_speakingDriver != 10)
			return 4;
		sound.digitalCard = 1;

		if (SlipRaceVoice_SpeakingDriver(&sound) != 10)
			return 4;
		record.playbackHandle = 1;
		record.sampleData = &payload;
		/* The uninitialized hardware adapter reports this handle stopped. */
		if (SlipRaceVoice_SpeakingDriver(&sound) != 0 || record.playbackHandle != 0 || record.sampleData != NULL ||
		    record.resourceHandle != 1 || block.lockCount != 1)
			return 4;
		SlipRaceVoice_bank = 0;
		SlipRaceVoice_currentRecord = NULL;
		SlipResource_handles = savedHandles;
		puts("voice_completion gate_preservation_finished_cleanup=passed");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--verify-collision-dispatch") == 0) {
		SlipObject objects[2] = {0};
		SlipRaceCollisionBody bodies[2] = {0};
		SlipObject_table = objects;
		SlipObject_count = 2;
		SlipRaceCollision_physicsTable = (uint8_t *)(void *)bodies;
		SlipDebug_collisionBody = &bodies[1];
		objects[1].allocated = 1;
		objects[1].eventCallback = SlipDebug_CollisionEvent;
		for (unsigned type = 1; type <= 3; type += 2) {
			bodies[1] = (SlipRaceCollisionBody){.otherObject = 0x12345678,
			                                    .contactPosition = {0x23456789, -2345, 76543},
			                                    .impactFlag = 0x56780001,
			                                    .impactMagnitude = -98765,
			                                    .objectHandle = SLIP_OBJECT_DOS_STRIDE,
			                                    .normalX = -123,
			                                    .normalY = 456,
			                                    .normalZ = -789,
			                                    .material = 0x9abc};
			objects[1].speed = 1234;
			SlipDebug_collisionEventCount = 0;
			if (type == 1)
				SlipRaceCollision_DispatchBodyContact(0x58, 0x24681357);
			else
				SlipRaceCollision_DispatchTrackContact(0x58, 0x24681357);
			if (SlipDebug_collisionEventCount != 2 || objects[1].speed != 0)
				return 4;
			if (type == 1 &&
			    (SlipDebug_collisionEvents[1][1] != 0x12345678 ||
			     SlipRaceCollision_stopEvent.impactMagnitude != -98765 || SlipRaceCollision_stopEvent.normalZ != -789 ||
			     SlipRaceCollision_stopEvent.contactPosition.y != -2345 ||
			     SlipRaceCollision_stopEvent.impactFlag != 0x56780001))
				return 4;
			if (type == 3 && ((uint16_t)SlipRaceCollision_bounceEvent.material != 0x9abc ||
			                  SlipRaceCollision_bounceEvent.contactPosition.z != 76543))
				return 4;
			for (unsigned event = 0; event < 2; ++event) {
				printf("collision_dispatch type=%u", type);
				for (unsigned field = 0; field < 6; ++field)
					printf(" %08x", SlipDebug_collisionEvents[event][field]);
				putchar('\n');
			}
		}
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--verify-slot-collision") == 0) {
		SlipObject objects[1] = {0};
		/* Disk TRD: record +0x20 links to record +0x40 via its first neighbour word. */
		uint8_t trd[0x80] = {[0x24] = 0x40};
		for (unsigned test = 0; test < 7; ++test) {
			SlipTrackSlotRecord slot = {.flags = 1};
			SlipTrackWorldSlotCollisionState registers = {0xaaaa, 0xbbbb, 0xcccc, 0xdddd, 0xeeee, 0x3000, 0xffff};
			for (unsigned corner = 0; corner < 8; ++corner)
				slot.cornerTrackRecords[corner] = 0x10000020;
			if (test == 1)
				slot.cornerTrackRecords[7] = 0;
			if (test >= 2)
				slot.cornerTrackRecords[7] = 0x10000040;
			if (test == 3)
				slot.cornerTrackRecords[6] = 0x10000060;
			if (test == 4)
				slot.cornerTrackRecords[7] = 0x10000060;
			if (test >= 5) {
				slot.flags = 2;
				slot.currentTrackRecordAddress = test == 6 ? 0x10000020 : 0;
			}
			bool collision =
			    SlipTrackWorld_QuerySlotCollision((uint8_t *)(void *)&slot, objects, SLIP_OBJECT_DOS_STRIDE, trd,
			                                      sizeof(trd), 0x10000000, NULL, 0, 0, NULL, 0, &registers);
			if (collision != (test == 1 || test == 3 || test == 4 || test == 5))
				return 4;
			if (test == 0 && slot.currentTrackRecordAddress != 0x10000020)
				return 4;
			if (registers.preservedObjectFreeValue != 0xbbbb || registers.slotAddress != 0x3000)
				return 4;
			if (test >= 5) {
				if (registers.currentRecordOrFlags != 2 || registers.remainingCornerCount != 0xcccc ||
				    registers.secondRecordOrOffset != 0xdddd || registers.cornerCursorAddress != 0xeeee ||
				    registers.firstRecordAddress != 0xffff)
					return 4;
			} else {
				static const uint32_t expectedCurrentRecordsOrFlags[] = {0x10000020, 0, 0x10000040, 0x10000040,
				                                                         0x10000060};
				static const uint32_t expectedSecondRecordsOrOffsets[] = {0, 0, 0x40, 0x10000060, 0x60};
				bool earlyExit = test == 1 || test == 3;
				if (registers.currentRecordOrFlags != expectedCurrentRecordsOrFlags[test] ||
				    registers.secondRecordOrOffset != expectedSecondRecordsOrOffsets[test] ||
				    registers.firstRecordAddress != 0x10000020 ||
				    registers.remainingCornerCount != (earlyExit ? 1u : 0u) ||
				    registers.cornerCursorAddress != (earlyExit ? 0x301cu : 0x3020u))
					return 4;
			}
		}
		puts("slot_collision single_linked_missing_third_unlinked_type_two_registers=passed");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--verify-shape-bounds") == 0) {
		const unsigned char *const serialized = (const unsigned char *)(const void *)&SlipShape3D_doorTemplate;
		printf("door_template ");
		for (size_t byte = 0; byte < sizeof(SlipShape3D_doorTemplate); ++byte)
			printf("%02x", serialized[byte]);
		putchar('\n');
		SlipShape3DHeader header = {.vertexOffset = 0x3a, .scaleShift = 6};
		SlipShape3DVertex vertices[2] = {{-3, 4, 0}, {3, -4, 0}};
		if (!SlipShape3D_RecalculateBounds(&header, vertices, 2) || header.radius != 320 || header.minimumX != -192 ||
		    header.maximumX != 192 || header.minimumY != -256 || header.maximumY != 256 || header.minimumZ != 0 ||
		    header.maximumZ != 0)
			return 4;

		vertices[0] = (SlipShape3DVertex){1000, -1000, 0};
		header.scaleShift = 38; /* x86 masks the shift count to five bits. */
		if (!SlipShape3D_RecalculateBounds(&header, vertices, 1) || header.minimumX != 32767 ||
		    header.maximumX != 64000 || header.minimumY != -64000 || header.maximumY != -32767)
			return 4;
		header.vertexOffset = 0;
		header.radius = 123;
		SlipShape3D_radius = 456;
		if (!SlipShape3D_RecalculateBounds(&header, NULL, 0) || header.radius != 123 || SlipShape3D_radius != 456)
			return 4;
		puts("shape_bounds scaled_vertices_initial_extrema_shift_mask_absent_list=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-matrix-reflection") == 0) {
		SlipView3DMatrix matrix = {{0x4000, 0, 0, 0x2000, 0x2000, 0, 0, 0, 0x4000}};
		const SlipView3DMatrix reflected = {{0x4000, 0, 0, 0x2000, -0x2000, 0, 0, 0, 0x4000}};
		if (!SlipView3D_ReflectMatrixRows(&matrix, 0, 0x4000, 0) || memcmp(&matrix, &reflected, sizeof(matrix)) != 0)
			return 4;
		matrix.m[4] = INT16_MIN;
		if (!SlipView3D_ReflectMatrixRows(&matrix, 0, 0x4000, 0) || matrix.m[4] != INT16_MIN ||
		    SlipView3D_ReflectMatrixRows(NULL, 0, 0x4000, 0))
			return 4;
		puts("matrix_reflection tilted_rows_word_wrap_null=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-matrix-constructor") == 0) {
		const SlipView3DMatrix up = {{0x4000, 0, 0, 0, 0, -0x4000, 0, 0x4000, 0}};
		const SlipView3DMatrix down = {{0x4000, 0, 0, 0, 0, 0x4000, 0, -0x4000, 0}};
		const SlipView3DMatrix forward = {{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}};
		SlipView3DMatrix matrix;
		if (!SlipView3D_BuildMatrixFromVector(&matrix, 123, 0x4000, -456) ||
		    memcmp(&matrix, &up, sizeof(matrix)) != 0 ||
		    !SlipView3D_BuildMatrixFromVector(&matrix, 123, INT16_MAX, -456) ||
		    memcmp(&matrix, &up, sizeof(matrix)) != 0 ||
		    !SlipView3D_BuildMatrixFromVector(&matrix, 123, -0x4000, -456) ||
		    memcmp(&matrix, &down, sizeof(matrix)) != 0 ||
		    !SlipView3D_BuildMatrixFromVector(&matrix, 123, INT16_MIN, -456) ||
		    memcmp(&matrix, &down, sizeof(matrix)) != 0 || !SlipView3D_BuildMatrixFromVector(&matrix, 0, 0, 0x4000) ||
		    memcmp(&matrix, &forward, sizeof(matrix)) != 0 ||
		    !SlipView3D_BuildMatrixFromVector32(&matrix, 0, 0, 0x10000) ||
		    memcmp(&matrix, &forward, sizeof(matrix)) != 0)
			return 4;
		puts("matrix_constructor vertical_thresholds_signed_extremes_forward_normalized32=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-timed-effect-random") == 0) {
		SlipTimedEffectDescriptor descriptor = {0};
		SlipTimedEffect entry = {.descriptor = &descriptor, .displacementX = 55, .displacementY = -55};
		SlipRandom_SetState(1, 2);
		SlipTimedEffects_Displace(&entry, 0x8000);
		if (entry.displacementX != 55 || entry.displacementY != -55 || SlipRandom_stateWords != 1 ||
		    SlipRandom_stateTail != 2)
			return 4;
		descriptor.displacementRange = 0x4000;
		SlipTimedEffects_Displace(&entry, 0x8000);
		if (entry.displacementX != 3 || entry.displacementY != 12 || SlipRandom_stateWords != 0x00030002 ||
		    SlipRandom_stateTail != 9)
			return 4;
		SlipRandom_SetState(0, 2);
		SlipTimedEffects_Displace(&entry, 0x8000);
		if (entry.displacementX != -2 || entry.displacementY != -4)
			return 4;
		SlipTimedEffectFrames *const frames = malloc(sizeof(*frames) + sizeof(uint16_t));
		if (frames == NULL)
			return 4;
		frames->count = 1;
		frames->handles = (uint16_t *)(frames + 1);
		frames->delay = 0;
		frames->handles[0] = 7;
		SlipRandom_SetState(1, 2);
		uint16_t selected = SlipTimedEffects_SelectFrame(frames, 7);
		bool repeated = selected == 7 && SlipRandom_stateWords == 0x00030002 && SlipRandom_stateTail == 9;
		SlipRandom_SetState(1, 2);
		selected = SlipTimedEffects_SelectFrame(frames, 8);
		free(frames);
		if (!repeated || selected != 7 || SlipRandom_stateWords != 0x00010000 || SlipRandom_stateTail != 3)
			return 4;
		puts("timed_effect_random zero_range, register_scale, signs, four_draw_retry=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-material-callback-dispatch") == 0) {
		SlipDraw3D_NotifyMaterials();
		SlipDraw3D_RegisterMaterialCallback(SlipShape3D_Shutdown);
		SlipDraw3D_RegisterMaterialCallback(SlipShape3D_Shutdown);
		SlipShape3D_initialized = UINT32_MAX;
		SlipDraw3D_NotifyMaterials();
		if (SlipShape3D_initialized != 0)
			return 4;
		uint8_t raw[4 + SLIP_DRAW3D_RAW_MATERIAL_RECORD_SIZE] = {1, 0, 1, 0};
		uint8_t expanded[4 + SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE];
		uint8_t appended[4 + 2 * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE];
		SlipDraw3DMaterialInstall install;
		SlipDraw3DMaterialAppend append;
		SlipShape3D_initialized = UINT32_MAX;
		if (!SlipDraw3D_SetMaterialsNoExisting(raw, sizeof(raw), 0, 1, expanded, sizeof(expanded), &install) ||
		    SlipShape3D_initialized != 0 || install.recordsExpanded != 1)
			return 4;
		SlipShape3D_initialized = UINT32_MAX;
		if (!SlipDraw3D_SetMaterialsAppend(expanded, sizeof(expanded), 1, raw, sizeof(raw), 2, appended,
		                                   sizeof(appended), &append) ||
		    SlipShape3D_initialized != 0 || append.appendedTableCount != 2)
			return 4;
		SlipShape3D_initialized = UINT32_MAX;
		raw[2] = 2;
		if (SlipDraw3D_SetMaterialsNoExisting(raw, sizeof(raw), 0, 1, expanded, sizeof(expanded), &install) ||
		    SlipDraw3D_SetMaterialsAppend(expanded, sizeof(expanded), 1, raw, sizeof(raw), 2, appended,
		                                  sizeof(appended), &append) ||
		    SlipShape3D_initialized != UINT32_MAX)
			return 4;
		raw[2] = 1;
		if (SlipDraw3D_SetMaterialsNoExisting(raw, sizeof(raw), 0, 1, expanded, sizeof(expanded) - 1, &install) ||
		    SlipDraw3D_SetMaterialsAppend(expanded, sizeof(expanded), 1, raw, sizeof(raw), 2, appended,
		                                  sizeof(appended) - 1, &append) ||
		    SlipShape3D_initialized != UINT32_MAX)
			return 4;
		puts("material_callback_dispatch empty_registered_install_append_rejected_input=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-resource-resident-extension") == 0) {
		SlipResource_residentExtension = 123;
		if (SlipResource_ResidentExtension("DOTLESS") || SlipResource_residentExtension != 123 ||
		    !SlipResource_ResidentExtension("A.") || SlipResource_residentExtension != 0x20202020 ||
		    !SlipResource_ResidentExtension("A.s") || SlipResource_residentExtension != 0x20202053 ||
		    !SlipResource_ResidentExtension("A.sHpX") || SlipResource_residentExtension != 0x20504853 ||
		    !SlipResource_ResidentExtension("A.b.c") || SlipResource_residentExtension != 0x20432e42)
			return 4;
		puts("resource_resident_extension dotless_preserved_empty_short_truncated_first_dot=passed");
		return 0;
	}
	/* One-pixel disk sprite through the live bonus callback and rasterizer. */
	if (argc == 3 && strcmp(argv[1], "--verify-cross-effect") == 0) {
		SlipObject objects[2] = {0};
		SlipDraw3DProjectState state;
		SlipView3DMaths maths = {0};
		const char *archives[] = {argv[2]};
		static SlipDraw3DListNode nodes[2];
		if (!SlipView3D_LoadMathsFromArchives(&maths, archives, 1) || !SlipDraw3D_InitList(nodes, 2))
			return 4;
		SlipDraw3D_InitDefaultProjectState(&state);
		SlipDraw3D_SetViewport(&state, 0, 0, 319, 199, 0, 0);
		SlipDraw3D_SetProjectionMode(&state, 1);
		SlipDraw3D_SetMinimumDepth(1);
		SlipDraw3D_SetMaximumDepth(1000);
		TrackViewRawBspContext context = {.objectTable = objects,
		                                  .objectTableBytes = 2 * SLIP_OBJECT_DOS_STRIDE,
		                                  .projectState = &state,
		                                  .maths = &maths};
		objects[1].flags = 1;
		objects[1].viewPosition = (SlipView3DVec32){50, -50, 100};
		objects[1].drawData = 0x5a0000;
		Raster_SetScreenBufferRows(g_framebuffer, SLIPSTREAM_SCREEN_WIDTH);
		const uint32_t sizes[] = {0, 2, 21, 101, UINT32_MAX};
		for (unsigned deferred = 0; deferred < 2; ++deferred) {
			for (size_t test = 0; test < sizeof(sizes) / sizeof(sizes[0]); ++test) {
				objects[1].drawExtent = sizes[test];
				memset(g_framebuffer, 0, sizeof(g_framebuffer));
				Raster_SetClipRect(0, 0, test == 2 ? 1 : 319, test == 2 ? 1 : 199);
				if (deferred) {
					if (!SlipDraw3D_ListPushFrame(&SlipDraw3D_listState, nodes, sizeof(nodes)) ||
					    !TrackView_QueueCrossEffect(&context, 0xabcd0000u | SLIP_OBJECT_DOS_STRIDE) ||
					    nodes[0].payload != SLIP_OBJECT_DOS_STRIDE || nodes[0].sortKey != 100 ||
					    nodes[0].callback != TrackView_DrawCrossEffect ||
					    !SlipDraw3D_ListTraverse(&SlipDraw3D_listState, nodes, sizeof(nodes), &context) ||
					    !SlipDraw3D_ListPopFrame(&SlipDraw3D_listState))
						return 4;
				} else if (!TrackView_DrawCrossEffect(&context, SLIP_OBJECT_DOS_STRIDE))
					return 4;
				for (int y = 0; y < 200; ++y)
					for (int x = 0; x < 320; ++x) {
						bool lit = test == 2 ? ((x == 50 && y >= 45 && y <= 55) || (y == 50 && x >= 44 && x <= 56))
						                     : (test != 3 && x == 50 && y == 50);
						if (g_framebuffer[y * 320 + x] != (lit ? 0x5a : 0)) {
							fprintf(
							    stderr,
							    "cross mismatch deferred=%u case=%zu x=%d y=%d actual=%u expected=%u sin=%d cos=%d\n",
							    deferred, test, x, y, g_framebuffer[y * 320 + x], lit ? 0x5a : 0,
							    SlipView3D_SinQ14(&maths, 0), SlipView3D_CosQ14(&maths, 0));
							return 4;
						}
					}
				int16_t minX, minY, maxX, maxY;
				Raster_GetClipRect(&minX, &minY, &maxX, &maxY);
				if (minX || minY || maxX != (test == 2 ? 1 : 319) || maxY != (test == 2 ? 1 : 199))
					return 4;
			}
		}

		objects[1].viewPosition.x = 0x12340032;
		if (!SlipDraw3D_ListPushFrame(&SlipDraw3D_listState, nodes, sizeof(nodes)) ||
		    !TrackView_QueueCrossEffect(&context, 0xabcd0000u | SLIP_OBJECT_DOS_STRIDE) ||
		    nodes[0].payload != (0x12340000u | SLIP_OBJECT_DOS_STRIDE) ||
		    !SlipDraw3D_ListPopFrame(&SlipDraw3D_listState))
			return 4;
		if (SlipDraw3D_DetailValue(0, 1, 0x01000000u, 256) != 0x01000000u ||
		    SlipDraw3D_DetailValue(0, 1, 0x01000000u, 1) != INT32_MAX)
			return 4;
		SlipView3D_FreeMaths(&maths);
		puts("cross_effect direct_deferred_point_cross_cull_clip_restore_word_products_payload_detail_dividend=passed");
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "--verify-shape-effect-cull") == 0) {
		SlipObject objects[2] = {0};
		const char *archives[] = {argv[2]};
		TrackViewResourceHandleRegistry registry = {
		    .archives = archives, .archiveCount = 1, .entries = {{"NYBOAT1.SHP", 1}}, .entryCount = 1};
		TrackViewRawBspContext context = {
		    .objectTable = objects, .objectTableBytes = 2 * SLIP_OBJECT_DOS_STRIDE, .resourceRegistry = &registry};
		objects[0].matrix = objects[1].matrix = TrackView_VehicleViewIdentityMatrix();
		objects[1].flags = 1;
		objects[1].viewPosition = (SlipView3DVec32){0x12345678, 0, -1000000};
		objects[1].drawData = 0xabcd0001;

		if (!TrackView_DrawShapeEffect(&context, 0) || !TrackView_DrawShapeEffect(&context, SLIP_OBJECT_DOS_STRIDE))
			return 4;
		SlipDraw3DListNode nodes[2];
		SlipDraw3DListState *const state = &SlipDraw3D_listState;
		if (!SlipDraw3D_InitList(nodes, 2) || !SlipDraw3D_ListPushFrame(state, nodes, sizeof(nodes)) ||
		    !TrackView_QueueShapeEffect(&context, 0xbeef0000u | SLIP_OBJECT_DOS_STRIDE) ||
		    nodes[0].callback != TrackView_DrawShapeEffect || nodes[0].payload != SLIP_OBJECT_DOS_STRIDE ||
		    nodes[0].sortKey != (uint32_t)objects[1].viewPosition.z ||
		    !SlipDraw3D_ListTraverse(state, nodes, sizeof(nodes), &context) || !SlipDraw3D_ListPopFrame(state))
			return 4;
		puts("shape_effect zero_object_resource_handle_cull_deferred_payload=passed");
		return 0;
	}
	if (argc == 3 && (strcmp(argv[1], "--verify-bonus-sprite") == 0 || strcmp(argv[1], "--verify-timed-sprite") == 0 ||
	                  strcmp(argv[1], "--verify-deferred-timed-sprite") == 0)) {
		SlipObjectDrawCallback drawCallback = strcmp(argv[1], "--verify-timed-sprite") == 0
		                                          ? TrackView_DrawTimedEffect
		                                          : TrackView_ExecuteBonusDrawCallback;
		static SlipDraw3DListNode deferredNodes[2];
		if (strcmp(argv[1], "--verify-deferred-timed-sprite") == 0) {
			if (!SlipDraw3D_InitList(deferredNodes, 2))
				return 4;
			drawCallback = SlipDebug_DrawDeferredSprite;
		}
		SlipObject objects[2] = {0};
		SlipDraw3DRecordPool pool;
		SlipDraw3DRecordPoolInit poolInit;
		SlipDraw3DProjectState projection;
		const char *archives[] = {argv[2]};
		TrackViewResourceHandleRegistry registry = {
		    .archives = archives, .archiveCount = 1, .entries = {{"DOT.SPR", 1}}, .entryCount = 1};
		TrackViewRawBspContext context = {.objectTable = objects,
		                                  .objectTableBytes = 2 * SLIP_OBJECT_DOS_STRIDE,
		                                  .projectState = &projection,
		                                  .drawRecordPool = &pool,
		                                  .resourceRegistry = &registry};
		if (!SlipDraw3D_InitRecordPool(&pool, &poolInit))
			return 4;
		SlipDraw3D_InitDefaultProjectState(&projection);
		SlipDraw3D_SetViewport(&projection, 0, 0, 319, 199, 0, 0);
		SlipDraw3D_SetProjectionMode(&projection, 1);
		if (projection.projectMask != SlipDraw3D_ProjectMaskOrthographic)
			return 4;
		objects[1].flags = 1;
		objects[1].viewPosition = (SlipView3DVec32){10, -20, 100};
		objects[1].drawData = objects[1].drawExtent = 1;
		Raster_SetScreenBufferRows(g_framebuffer, SLIPSTREAM_SCREEN_WIDTH);
		Raster_SetClipRect(13, 14, 15, 16);
		memset(g_framebuffer, 0, sizeof(g_framebuffer));
		if (!drawCallback(&context, SLIP_OBJECT_DOS_STRIDE))
			return 4;
		for (int y = 0; y < SLIPSTREAM_SCREEN_HEIGHT; ++y)
			for (int x = 0; x < SLIPSTREAM_SCREEN_WIDTH; ++x)
				if (g_framebuffer[y * SLIPSTREAM_SCREEN_WIDTH + x] !=
				    (x >= 9 && x <= 11 && y >= 19 && y <= 21 ? 0x5a : 0))
					return 4;
		int16_t minX, minY, maxX, maxY;
		Raster_GetClipRect(&minX, &minY, &maxX, &maxY);
		if (minX != 13 || minY != 14 || maxX != 15 || maxY != 16)
			return 4;

		SlipDraw3D_SetAuxiliaryClipPlane(&projection, (SlipDraw3DVec32){10, -20, 100}, 0x4000, 0, 0);
		projection.renderFlags = 0x1230;
		context.rendererFlags = 0x200;
		memset(g_framebuffer, 0, sizeof(g_framebuffer));
		if (!drawCallback(&context, SLIP_OBJECT_DOS_STRIDE) || projection.renderFlags != 0x1230 ||
		    context.rendererFlags != 0x200)
			return 4;
		for (int y = 0; y < SLIPSTREAM_SCREEN_HEIGHT; ++y)
			for (int x = 0; x < SLIPSTREAM_SCREEN_WIDTH; ++x)
				if (g_framebuffer[y * SLIPSTREAM_SCREEN_WIDTH + x] !=
				    (x >= 10 && x <= 11 && y >= 19 && y <= 21 ? 0x5a : 0))
					return 4;
		Raster_GetClipRect(&minX, &minY, &maxX, &maxY);
		if (minX != 13 || minY != 14 || maxX != 15 || maxY != 16)
			return 4;
		/* The sphere touches the plane, but every quad corner is behind it. */
		SlipDraw3D_SetAuxiliaryClipPlane(&projection, (SlipDraw3DVec32){10, -20, 101}, 0, 0, 0x4000);
		memset(g_framebuffer, 0, sizeof(g_framebuffer));
		if (!drawCallback(&context, SLIP_OBJECT_DOS_STRIDE) || projection.renderFlags != 0x1230 ||
		    context.rendererFlags != 0x200)
			return 4;
		for (size_t i = 0; i < sizeof(g_framebuffer); ++i)
			if (g_framebuffer[i] != 0)
				return 4;
		SlipDraw3D_ClearAuxiliaryClipPlane(&projection);
		objects[1].drawExtent = UINT32_MAX;
		memset(g_framebuffer, 0, sizeof(g_framebuffer));
		if (!drawCallback(&context, SLIP_OBJECT_DOS_STRIDE))
			return 4;
		for (size_t i = 0; i < sizeof(g_framebuffer); ++i)
			if (g_framebuffer[i] != 0)
				return 4;
		for (unsigned pass = 0; pass < 2; ++pass) {
			objects[1].viewPosition.x = pass == 0 ? 319 : 65546;
			objects[1].drawExtent = pass == 0 ? 0 : 1;
			if (!drawCallback(&context, SLIP_OBJECT_DOS_STRIDE))
				return 4;
			for (size_t i = 0; i < sizeof(g_framebuffer); ++i)
				if (g_framebuffer[i] != 0)
					return 4;
		}
		SlipDraw3D_SetProjectionMode(&projection, 0);
		if (projection.projectMask != SlipDraw3D_ProjectMaskPerspective)
			return 4;
		/* A preceding one-record ring must be returned even on depth rejection. */
		const uint32_t first = pool.records[0].links.nextOffset;
		SlipDraw3DLinkedDrawRecord *const active = &pool.records[first / SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE];
		const uint32_t next = active->links.nextOffset;
		pool.records[0].links.nextOffset = next;
		pool.records[next / SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE].links.prevOffset = 0;
		active->links.nextOffset = active->links.prevOffset = first;
		pool.inputActiveHeadOffset = first;
		objects[1].viewPosition.z = 0;
		if (!drawCallback(&context, SLIP_OBJECT_DOS_STRIDE) || pool.inputActiveHeadOffset != 0 ||
		    pool.records[0].links.nextOffset != first || active->links.nextOffset != next ||
		    active->links.prevOffset != 0)
			return 4;
		printf("%s projection_corner_clipping_ring_return=passed\n", argv[1]);
		return 0;
	}
	/* Drive the real blaster-created callback through the object dispatcher. */
	if (argc == 2 && strcmp(argv[1], "--verify-projectile-events") == 0) {
		SlipObject objects[5] = {0};
		SlipObjectInitTable init;
		SlipObjectSlotFill fill;
		SlipView3DMatrix matrix = {{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}};
		SlipView3DMaths maths = {0};
		uint8_t ownerMismatch[0xc2] = {0};
		SlipRacePlayerHostBindings context = {.objectTable = objects,
		                                      .objectTableBytes = 5 * SLIP_OBJECT_DOS_STRIDE,
		                                      .maths = &maths,
		                                      .articSlotPool = ownerMismatch,
		                                      .articSlotPoolBytes = sizeof(ownerMismatch),
		                                      .articSlotPoolOffset = 0x1000};
		if (!SlipObject_InitTableFresh(objects, 5 * SLIP_OBJECT_DOS_STRIDE, 5, &init) ||
		    !SlipObject_SlotFill(&matrix, 123, 456, 789, NULL, 0x1000, NULL, &fill) || fill.carryOut ||
		    fill.objectOffset != SLIP_OBJECT_DOS_STRIDE)
			return 4;
		SlipRacePlayer_BindHostContext(&context);
		SlipRacePlayer_FireBlaster(SLIP_OBJECT_DOS_STRIDE, 0);
		if (objects[2].allocated == 0 || objects[3].allocated == 0 || objects[2].eventCallback == NULL)
			return 4;
		uint16_t first = 2 * SLIP_OBJECT_DOS_STRIDE, second = 3 * SLIP_OBJECT_DOS_STRIDE;
		SlipRacePlayerProjectileState *state = (void *)SlipObject_PrivateState(first);
		if (state->remainingTime != 5000 || state->shooterObject != SLIP_OBJECT_DOS_STRIDE ||
		    objects[2].position.x != 0 || objects[2].position.y != 0 || objects[2].position.z != 0)
			return 4;
		objects[2].position = (SlipView3DVec32){123, -456, 789};
		if (SlipObject_DispatchEvent(first, 0xabcd0101u, 0, 0, 0, 0, 0) != 0 || state->position.x != 123 ||
		    state->position.y != -456 || state->position.z != 789)
			return 4;
		state->remainingTime = INT32_MIN;
		SlipFrameTimer_delta = 1;
		if (SlipObject_DispatchEvent(first, 0xabcd0104u, 0, 0, 0, 0, 0) != 0 || objects[2].allocated == 0 ||
		    state->remainingTime != INT32_MAX)
			return 4;
		state->remainingTime = 0;
		SlipFrameTimer_delta = 0;
		SlipObject_DispatchEvent(first, 0x104, 0, 0, 0, 0, 0);
		if (objects[2].allocated == 0)
			return 4;
		SlipFrameTimer_delta = 1;
		SlipObject_DispatchEvent(first, 0x104, 0, 0, 0, 0, 0);
		state = (void *)SlipObject_PrivateState(second);
		state->remainingTime = INT32_MAX;
		SlipFrameTimer_delta = UINT32_MAX;
		SlipObject_DispatchEvent(second, 0x104, 0, 0, 0, 0, 0);
		if (objects[2].allocated != 0 || objects[3].allocated != 0)
			return 4;
		/* Recreate the pool with a target to exercise initial aim and steering. */
		if (!SlipObject_InitTableFresh(objects, 5 * SLIP_OBJECT_DOS_STRIDE, 5, &init) ||
		    !SlipObject_SlotFill(&matrix, 0, 0, 0, NULL, 0x1000, NULL, &fill) || fill.carryOut ||
		    !SlipObject_SlotFill(&matrix, 100, 0, 0, NULL, 0, NULL, &fill) || fill.carryOut)
			return 4;
		SlipRacePlayer_FireBlaster(SLIP_OBJECT_DOS_STRIDE, 2 * SLIP_OBJECT_DOS_STRIDE);
		first = 3 * SLIP_OBJECT_DOS_STRIDE;
		state = (void *)SlipObject_PrivateState(first);
		if (objects[3].allocated == 0 || objects[4].allocated == 0 ||
		    state->targetObject != 2 * SLIP_OBJECT_DOS_STRIDE || objects[3].direction.x != 0x4000 ||
		    objects[3].direction.y != 0 || objects[3].direction.z != 0)
			return 4;
		SlipFrameTimer_delta = 0;
		objects[2].position = (SlipView3DVec32){0, 0, 100};
		SlipObject_DispatchEvent(first, 0x104, 0, 0, 0, 0, 0);
		if (objects[3].direction.x != 0x4000 || objects[3].direction.z != 0)
			return 4;
		objects[2].position = (SlipView3DVec32){100, 0, 10};
		SlipObject_DispatchEvent(first, 0x104, 0, 0, 0, 0, 0);
		SlipView3DVec16 followed = objects[3].direction;
		if (followed.z <= 0 || followed.x <= followed.z || followed.y != 0)
			return 4;
		objects[2].position = (SlipView3DVec32){-100, 0, 0};
		SlipObject_DispatchEvent(first, 0x104, 0, 0, 0, 0, 0);
		if (memcmp(&objects[3].direction, &followed, sizeof(followed)) != 0)
			return 4;
		puts("projectile_events creation_ax_dispatch_wrapped_timer_initial_aim_steering_gate=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-camera-missing-part") == 0) {
		SlipObject objects[2] = {0};
		uint8_t ownerMismatch[0xc2] = {0};
		SlipView3DMaths maths = {0};
		SlipArticSlotPosition result;
		objects[1].drawData = 0x1000;
		objects[1].matrix = (SlipView3DMatrix){{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}};
		if (!SlipRaceCamera_Cockpit(objects, 2 * SLIP_OBJECT_DOS_STRIDE, SLIP_OBJECT_DOS_STRIDE, 1, ownerMismatch,
		                            sizeof(ownerMismatch), 0x1000, NULL, 0, 0, &maths, &result) ||
		    !result.lookupFailed || objects[0].position.x != 1 || objects[0].position.y != 0x42ce7 ||
		    objects[0].position.z != 0 ||
		    memcmp(&objects[0].matrix, &objects[1].matrix, sizeof(objects[0].matrix)) != 0)
			return 4;
		if (!SlipRaceCamera_Rear(objects, 2 * SLIP_OBJECT_DOS_STRIDE, SLIP_OBJECT_DOS_STRIDE, 2, ownerMismatch,
		                         sizeof(ownerMismatch), 0x1000, NULL, 0, 0, &maths) ||
		    objects[0].position.x != 2 || objects[0].position.y != 0x42d0f || objects[0].position.z != 0 ||
		    objects[0].matrix.m[0] != -0x4000 || objects[0].matrix.m[4] != 0x4000 || objects[0].matrix.m[8] != -0x4000)
			return 4;
		puts("camera_missing_part preserves_dispatch_registers_installs_camera=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-shape-load-callback") == 0) {
		SlipShape3DHeader header = {.version = 12, .flags = 0xffff, .radius = 123};
		SlipShape3D_Loaded(&header);
		if (header.flags != 0xfffe || header.radius != 123)
			return 4;
		header.version = 11;
		header.flags = 0xffff;
		SlipShape3D_InvalidateMaterials(&header);
		if (header.flags != 0xfffe || header.version != 11 || header.radius != 123)
			return 4;
		SlipShape3D_InvalidateMaterials(&header);
		if (header.flags != 0xfffe)
			return 4;
		puts("shape_callbacks valid_load_material_invalidation_unconditional=passed");
		return 0;
	}

	if (argc == 3 && strcmp(argv[1], "--verify-resource-case") == 0) {
		SlipResourceHost_Initialize(SLIP_DEBUG_RESOURCE_CAPACITY_BYTES);
		SlipResourceHost_OpenArchives(argv[2], NULL);
		for (unsigned c = 0; c < 256; ++c) {
			const unsigned expected = c >= 97 && c <= 122 ? c - 32 : c;
			if (SlipResource_Uppercase((uint8_t)c) != expected)
				return 4;
		}
		if (SlipResource_ExtensionKey(0x73687020u) != 0x20504853u ||
		    SlipResource_ExtensionKey(0x617a8040u) != 0x40805a41u)
			return 4;
		const char *archives[] = {argv[2]};
		const char *missing[] = {"does-not-exist-case-check.res"};
		SlipResourcePayload first, second, third;
		if (!SlipResource_LoadByName(archives, 1, "case.bin", &first) ||
		    !SlipResource_LoadByName(missing, 1, "CaSe.BiN", &second) ||
		    !SlipResource_LoadByName(missing, 1, "CASE.BIN", &third) || first.data != second.data ||
		    second.data != third.data || first.size != 4 || memcmp(first.data, "DOS!", 4) != 0 || first.ownsData ||
		    second.ownsData || third.ownsData)
			return 4;
		SlipResource_ReleaseHandle(&first);
		if (memcmp(second.data, "DOS!", 4) != 0)
			return 4;
		TrackViewResourceHandleRegistry registry = {.archives = archives, .archiveCount = 1};
		uint32_t handle = UINT32_MAX, sameHandle = 0;
		if (TrackView_FindNameRecord(&registry, "case.bin", &handle) || registry.entryCount != 0 ||
		    handle != UINT32_MAX)
			return 4;
		if (!TrackView_LoadNamedResource(&registry, "case.bin", &handle) ||
		    !TrackView_FindNameRecord(&registry, "CaSe.BiN", &sameHandle) || sameHandle != handle ||
		    registry.entryCount != 1)
			return 4;
		TrackView_ReleaseResource(&registry, handle);
		if (registry.entries[0].payload.data != NULL || !TrackView_FindNameRecord(&registry, "CASE.BIN", &sameHandle) ||
		    sameHandle != handle || !TrackView_LoadResourceHandlePayload(&registry, handle, &first) ||
		    first.data != second.data)
			return 4;
		puts("resource_case lookup_does_not_load_release_preserves_cached_identity=passed");
		TrackViewResourceHandleRegistry nextRegistry = {.archives = archives, .archiveCount = 1};
		uint32_t nextHandle = 0;
		if (!TrackView_LoadNamedResource(&nextRegistry, "other.bin", &nextHandle) ||
		    !TrackView_LoadNamedResource(&nextRegistry, "case.bin", &nextHandle) || nextHandle != handle)
			return 4;
		puts("resource_case named_handle_survives_renderer_registry_reset=passed");
		puts("resource_case ascii_all_bytes_mixed_case_cache_identity_borrowed_payload=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-resource-unlock") == 0) {
		SlipResourceHandle handles[3] = {0};
		SlipResourceBlock block = {.lockCount = 2}, other = {0};
		SlipResourceBlock *const active = &SlipResource_allocatedBlocks;
		active->next = &block;
		active->previous = &other;
		block.next = &other;
		block.previous = active;
		other.next = active;
		other.previous = &block;
		handles[2].block = &block;
		SlipResource_handles = handles;
		block.flags = SLIP_RESOURCE_BLOCK_ALLOCATED_CACHED_MOVABLE;
		block.capacityBytes = 64;
		SlipResource_cachedBytes = 96;
		if (SlipResource_ActivateResident(&handles[2]) != &block ||
		    block.flags != SLIP_RESOURCE_BLOCK_ALLOCATED_MOVABLE || SlipResource_cachedBytes != 0 ||
		    block.lockCount != 2)
			return 4;
		if (SlipResource_ActivateResident(&handles[2]) != &block || SlipResource_cachedBytes != 0)
			return 4;
		SlipResource_ReleaseRecord(2);
		SlipResource_ReleaseRecord(2);
		if (block.flags != SLIP_RESOURCE_BLOCK_ALLOCATED_CACHED_MOVABLE || SlipResource_cachedBytes != 96 ||
		    handles[2].block != &block)
			return 4;
		SlipResource_ActivateResident(&handles[2]);
		if (SlipResource_cachedBytes != 0)
			return 4;
		SlipResource_Unlock(2);
		if (block.lockCount != 1 || active->next != &block || active->previous != &other)
			return 4;
		SlipResource_Unlock(2);
		if (block.lockCount != 0 || active->next != &other || active->previous != &block || other.next != &block ||
		    block.previous != &other || block.next != active)
			return 4;
		SlipResource_Unlock(2);
		if (block.lockCount != UINT16_MAX || active->next != &other || active->previous != &block)
			return 4;
		SlipResource_freeBlocks.next = &SlipResource_freeBlocks;
		SlipResource_freeBlocks.previous = &SlipResource_freeBlocks;
		SlipResource_freeBytes = 0;
		block.flags = SLIP_RESOURCE_BLOCK_ALLOCATED_IMMEDIATE_RELEASE;
		block.handleByteOffset = 0x20;
		SlipResource_ReleaseRecord(2);
		if (handles[2].block != NULL || SlipResource_freeBytes != 96 || active->next != &other ||
		    active->previous != &other)
			return 4;
		SlipResource_ReleaseRecord(2);
		if (SlipResource_freeBytes != 96)
			return 4;
		SlipResource_handles = NULL;
		puts("resource_unlock nested_zero_tail_transition_word_underflow=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-resource-regions") == 0) {
		SlipResourceBlock regions[3] = {0};
		SlipResourceBlock *const sentinel = &SlipResource_freeBlocks;
		sentinel->next = sentinel->previous = sentinel;
		SlipResource_freeBytes = UINT32_MAX - 31;
		SlipResource_totalBytes = 0;
		for (unsigned i = 0; i < 3; ++i) {
			regions[i].physicalPrevious = regions[i].physicalNext = sentinel;
			regions[i].requestedBytes = 17;
			regions[i].handleByteOffset = 23;
			regions[i].lockCount = 31;
			SlipResource_AddRegion(&regions[i], 128);
			if (regions[i].capacityBytes != 96 || regions[i].flags != 0 || regions[i].physicalPrevious != NULL ||
			    regions[i].physicalNext != NULL || regions[i].requestedBytes != 17 ||
			    regions[i].handleByteOffset != 23 || regions[i].lockCount != 31)
				return 4;
		}
		if (SlipResource_totalBytes != 384 || SlipResource_freeBytes != 352 || sentinel->next != &regions[0] ||
		    regions[0].next != &regions[2] || regions[2].previous != &regions[0] || regions[2].next != &regions[1] ||
		    regions[1].previous != &regions[2] || regions[1].next != sentinel || sentinel->previous != &regions[1])
			return 4;
		SlipResource_Coalesce(&regions[0]);
		if (regions[0].capacityBytes != 96 || regions[0].next != &regions[2])
			return 4;
		puts("resource_regions accounting_wrap_list_order_physical_separation_payload=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-resource-back-allocation") == 0) {
		const uint32_t capacities[] = {320, 316, 64};
		for (unsigned test = 0; test < 3; ++test) {
			SlipResourceBlock original = {.capacityBytes = capacities[test], .handleByteOffset = UINT32_MAX};
			SlipResourceBlock split = {.handleByteOffset = UINT32_MAX};
			SlipResourceBlock following = {.flags = SLIP_RESOURCE_BLOCK_ALLOCATED};
			SlipResourceBlock *const freeList = &SlipResource_freeBlocks;
			SlipResourceBlock *const active = &SlipResource_allocatedBlocks;
			freeList->next = freeList->previous = &original;
			original.next = original.previous = freeList;
			original.physicalNext = &following;
			following.physicalPrevious = &original;
			active->next = active->previous = active;
			SlipResource_freeBytes = capacities[test] + 32;
			SlipResourceBlock *const block = SlipResource_AllocateBack(&original, 61, test == 0 ? &split : NULL);
			if (block != (test == 0 ? &split : &original) || block->flags != SLIP_RESOURCE_BLOCK_ALLOCATED ||
			    block->lockCount != 0 || block->requestedBytes != 61 || active->next != block ||
			    active->previous != block)
				return 4;
			if (test == 0) {
				if (original.capacityBytes != 224 || split.capacityBytes != 64 || original.physicalNext != &split ||
				    split.physicalPrevious != &original || following.physicalPrevious != &split ||
				    freeList->next != &original || SlipResource_freeBytes != 256)
					return 4;
			} else if (test == 1) {
				if (freeList->next != freeList || block->capacityBytes != 316 || SlipResource_freeBytes != 0)
					return 4;
			} else {
				if (freeList->next != block || freeList->previous != block || SlipResource_freeBytes != 0)
					return 4;
				continue;
			}
			SlipResource_ReleaseBlock(block);
			SlipResource_Coalesce(block);
			if (original.capacityBytes != capacities[test] || original.physicalNext != &following ||
			    following.physicalPrevious != &original || SlipResource_freeBytes != capacities[test] + 32)
				return 4;
		}
		puts("resource_back_allocation threshold_physical_links_exact_branch_release_roundtrip=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-resource-front-allocation") == 0) {
		const uint32_t capacities[] = {320, 316, 64};
		for (unsigned test = 0; test < 3; ++test) {
			SlipResourceBlock block = {.capacityBytes = capacities[test], .handleByteOffset = UINT32_MAX};
			SlipResourceBlock remainder = {.requestedBytes = 777, .handleByteOffset = 888, .lockCount = 9};
			SlipResourceBlock following = {.flags = SLIP_RESOURCE_BLOCK_ALLOCATED},
			                  existing = {.flags = SLIP_RESOURCE_BLOCK_ALLOCATED};
			SlipResourceBlock *const freeList = &SlipResource_freeBlocks;
			SlipResourceBlock *const active = &SlipResource_allocatedBlocks;
			freeList->next = freeList->previous = &block;
			block.next = block.previous = freeList;
			block.physicalNext = &following;
			following.physicalPrevious = &block;
			active->next = active->previous = &existing;
			existing.next = existing.previous = active;
			SlipResource_freeBytes = capacities[test] + 32;
			if (SlipResource_AllocateFront(&block, 61, test == 0 ? &remainder : NULL) != &block ||
			    block.requestedBytes != 61 || block.flags != SLIP_RESOURCE_BLOCK_ALLOCATED || block.lockCount != 0 ||
			    existing.next != &block || block.previous != &existing || active->previous != &block)
				return 4;
			if (test == 0) {
				if (block.capacityBytes != 64 || remainder.capacityBytes != 224 ||
				    following.physicalPrevious != &remainder || remainder.physicalPrevious != &block ||
				    freeList->next != &remainder || freeList->previous != &remainder ||
				    remainder.requestedBytes != 777 || remainder.handleByteOffset != 888 || remainder.lockCount != 9 ||
				    SlipResource_freeBytes != 256)
					return 4;
			} else if (block.capacityBytes != capacities[test] || freeList->next != freeList ||
			           block.physicalNext != &following || SlipResource_freeBytes != 0) {
				return 4;
			}
			SlipResource_ReleaseBlock(&block);
			SlipResource_Coalesce(&block);
			if (block.capacityBytes != capacities[test] || block.physicalNext != &following ||
			    following.physicalPrevious != &block || SlipResource_freeBytes != capacities[test] + 32)
				return 4;
		}
		puts("resource_front_allocation rounding_split_boundary_payload_order_release_roundtrip=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-resource-fallbacks") == 0) {
		SlipResourceBlock freeBlock = {.capacityBytes = 64};
		SlipResourceBlock locked = {.flags = SLIP_RESOURCE_BLOCK_ALLOCATED_MOVABLE}, following = {.capacityBytes = 64};
		SlipResourceBlock *const freeList = &SlipResource_freeBlocks;
		freeList->next = freeList->previous = &freeBlock;
		freeBlock.next = freeBlock.previous = freeList;
		freeBlock.physicalNext = &locked;
		locked.physicalNext = &following;
		SlipResource_compactBytes = 999;
		if (SlipResource_Compact(65) || SlipResource_compactBytes != 68 || freeBlock.capacityBytes != 64 ||
		    SlipResource_CompactStep())
			return 4;
		locked.lockCount = 1;
		SlipResource_compactBytes = 999;
		if (SlipResource_Compact(65) || SlipResource_compactBytes != 999)
			return 4;
		locked.lockCount = 0;
		locked.flags = SLIP_RESOURCE_BLOCK_ALLOCATED;
		if (SlipResource_Compact(65) || SlipResource_compactBytes != 999)
			return 4;
		SlipResourceBlock blocks[2] = {0};
		SlipResourceBlock *const active = &SlipResource_allocatedBlocks;
		active->next = &blocks[0];
		active->previous = &blocks[1];
		blocks[0].next = &blocks[1];
		blocks[0].previous = active;
		blocks[1].next = active;
		blocks[1].previous = &blocks[0];
		blocks[0].flags = blocks[1].flags = SLIP_RESOURCE_BLOCK_ALLOCATED_RECLAIMABLE;
		blocks[0].lockCount = 1;
		blocks[1].capacityBytes = 32;
		blocks[1].handleByteOffset = UINT32_MAX;
		SlipResource_reclaimEnabled = 0;
		SlipResource_freeBytes = 0;
		if (SlipResource_ReclaimUnlocked())
			return 4;
		SlipResource_reclaimEnabled = 1;
		if (!SlipResource_ReclaimUnlocked() || blocks[1].flags != SLIP_RESOURCE_BLOCK_RECLAIM_WHEN_UNLOCKED ||
		    active->next != &blocks[0] || active->previous != &blocks[0] || SlipResource_freeBytes != 64 ||
		    SlipResource_ReclaimUnlocked())
			return 4;
		puts("resource_fallbacks compaction_failure_target_barriers_enabled_forward_reclaim=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-resource-reclaim") == 0) {
		SlipResourceBlock blocks[3] = {0};
		SlipResourceBlock *const active = &SlipResource_allocatedBlocks;
		SlipResourceBlock *const freeList = &SlipResource_freeBlocks;
		active->next = &blocks[0];
		active->previous = &blocks[2];
		freeList->next = freeList->previous = freeList;
		for (unsigned i = 0; i < 3; ++i) {
			blocks[i].next = i == 2 ? active : &blocks[i + 1];
			blocks[i].previous = i == 0 ? active : &blocks[i - 1];
			blocks[i].handleByteOffset = UINT32_MAX;
			blocks[i].capacityBytes = 64;
			blocks[i].flags = i == 2 ? SLIP_RESOURCE_BLOCK_ALLOCATED : SLIP_RESOURCE_BLOCK_ALLOCATED_CACHED;
		}
		SlipResource_freeBytes = 0;
		SlipResource_cachedBytes = 192;
		SlipResource_MoveBlockToTail(&blocks[0]);
		if (active->next != &blocks[1] || active->previous != &blocks[0] || blocks[2].next != &blocks[0] ||
		    blocks[0].previous != &blocks[2])
			return 4;
		if (!SlipResource_ReclaimBlock() || blocks[0].flags != SLIP_RESOURCE_BLOCK_CACHED ||
		    active->previous != &blocks[2] || SlipResource_freeBytes != 96 || SlipResource_cachedBytes != 96)
			return 4;
		if (!SlipResource_ReclaimBlock() || blocks[1].flags != SLIP_RESOURCE_BLOCK_CACHED ||
		    active->next != &blocks[2] || active->previous != &blocks[2] || SlipResource_freeBytes != 192 ||
		    SlipResource_cachedBytes != 0)
			return 4;
		if (SlipResource_ReclaimBlock() || blocks[2].flags != SLIP_RESOURCE_BLOCK_ALLOCATED ||
		    SlipResource_freeBytes != 192)
			return 4;
		active->next = active->previous = active;
		if (SlipResource_ReclaimBlock())
			return 4;
		puts("resource_reclaim tail_order_reverse_scan_one_candidate_none_empty=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-resource-handle-growth") == 0) {
		SlipResourceBlock oldBlock = {.flags = SLIP_RESOURCE_BLOCK_ALLOCATED, .capacityBytes = 80};
		SlipResourceBlock newBlock = {.flags = SLIP_RESOURCE_BLOCK_ALLOCATED_CACHED}, allocated = {0};
		SlipResourceHandle oldRecords[5] = {0}, newRecords[8] = {0};
		SlipResource_handleCount = 3;
		SlipResource_InitHandleTable(&oldBlock, oldRecords);
		const unsigned order[4] = {0, 2, 4, 3};
		for (unsigned i = 0; i < 3; ++i) {
			oldRecords[i + 2].block = &newBlock;
			oldRecords[i + 2].nameOffset = 200 + i;
			const uint32_t handle = SlipResource_TakeAvailableHandle(SlipResource_freeHandles->next);
			if (handle != i + 2 || oldRecords[handle].block != NULL || oldRecords[handle].nameOffset != 200 + i)
				return 4;
		}
		if (oldRecords[1].next != &oldRecords[1] || oldRecords[1].previous != &oldRecords[1])
			return 4;
		for (unsigned i = 0; i < 5; ++i) {
			oldRecords[i].block = &newBlock;
			oldRecords[i].nameOffset = 100 + i;
		}
		newRecords[5].nameOffset = 999;
		allocated.next = allocated.previous = &oldBlock;
		oldBlock.next = oldBlock.previous = &allocated;
		SlipResource_freeBlocks.next = &SlipResource_freeBlocks;
		SlipResource_freeBlocks.previous = &SlipResource_freeBlocks;
		SlipResource_freeBytes = 0;
		SlipResource_handleCount = 6;
		SlipResource_RelocateHandles(&newBlock, newRecords, 3, &oldBlock);
		for (unsigned i = 0; i < 5; ++i) {
			if (newRecords[i].block != &newBlock || newRecords[i].nameOffset != 100 + i)
				return 4;
		}
		for (unsigned i = 0; i < 4; ++i) {
			if (newRecords[order[i]].next != &newRecords[order[(i + 1) % 4]] ||
			    newRecords[order[i]].previous != &newRecords[order[(i + 3) % 4]])
				return 4;
		}
		if (SlipResource_handles != newRecords || newRecords[1].next != &newRecords[5] ||
		    newRecords[1].previous != &newRecords[7] || newRecords[5].previous != &newRecords[1] ||
		    newRecords[5].next != &newRecords[6] || newRecords[6].next != &newRecords[7] ||
		    newRecords[7].next != &newRecords[1] || newRecords[5].nameOffset != 999 ||
		    newBlock.flags != SLIP_RESOURCE_BLOCK_ALLOCATED_CACHED_MOVABLE || newBlock.handleByteOffset != UINT32_MAX ||
		    oldBlock.flags != SLIP_RESOURCE_BLOCK_MOVABLE || SlipResource_freeBytes != 112 ||
		    allocated.next != &allocated || SlipResource_freeBlocks.next != &oldBlock)
			return 4;
		const uint32_t handle = SlipResource_TakeAvailableHandle(SlipResource_freeHandles->next);
		if (handle != 5 || newRecords[2].next != &newRecords[5] || newRecords[5].next != &newRecords[4] ||
		    newRecords[5].nameOffset != 999 || newRecords[1].next != &newRecords[6])
			return 4;
		SlipResource_RecycleHandle(&newRecords[5]);
		if (newRecords[1].previous != &newRecords[5] || newRecords[7].next != &newRecords[5])
			return 4;
		puts("resource_handle_growth indices_links_payload_new_free_ring_old_block_release=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-resource-handle-init") == 0) {
		SlipResourceBlock allocation = {.flags = SLIP_RESOURCE_BLOCK_ALLOCATED_CACHED, .handleByteOffset = 123};
		SlipResourceHandle records[5] = {0};
		for (unsigned i = 0; i < 5; ++i) {
			records[i].block = &allocation;
			records[i].nameOffset = 100 + i;
		}
		SlipResource_handleCount = 3;
		SlipResource_InitHandleTable(&allocation, records);
		if (allocation.flags != SLIP_RESOURCE_BLOCK_ALLOCATED_CACHED_MOVABLE ||
		    allocation.handleByteOffset != UINT32_MAX || SlipResource_handles != records ||
		    SlipResource_activeHandles != records || SlipResource_freeHandles != records + 1 ||
		    records[0].next != records || records[0].previous != records || records[1].previous != records + 4 ||
		    records[4].next != records + 1)
			return 4;
		for (unsigned i = 1; i < 4; ++i) {
			if (records[i].next != records + i + 1 || records[i + 1].previous != records + i)
				return 4;
		}
		for (unsigned i = 0; i < 5; ++i) {
			if (records[i].block != &allocation || records[i].nameOffset != 100 + i)
				return 4;
		}
		SlipResource_handleCount = 1;
		SlipResource_InitHandleTable(&allocation, records);
		if (records[1].next != records + 2 || records[1].previous != records + 2 || records[2].next != records + 1 ||
		    records[2].previous != records + 1)
			return 4;
		puts("resource_handle_init sentinels_count_one_many_metadata_payload_preserved=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-resource-recycle-handle") == 0) {
		SlipResourceHandle active = {0}, available = {0}, first = {0}, second = {0};
		SlipResourceBlock block = {0};
		active.next = &first;
		active.previous = &second;
		first.previous = &active;
		first.next = &second;
		second.previous = &first;
		second.next = &active;
		available.next = available.previous = &available;
		first.block = &block;
		first.nameOffset = 0x12345678;
		SlipResource_freeHandles = &available;
		SlipResource_RecycleHandle(&first);
		if (active.next != &second || second.previous != &active || available.next != &first ||
		    available.previous != &first || first.previous != &available || first.next != &available)
			return 4;
		SlipResource_RecycleHandle(&second);
		if (active.next != &active || active.previous != &active || available.next != &first ||
		    available.previous != &second || first.next != &second || second.previous != &first ||
		    second.next != &available || first.block != &block || first.nameOffset != 0x12345678)
			return 4;
		/* Exercise the real anonymous-release chain with an owning table record. */
		SlipResourceHandle handles[1] = {0};
		SlipResourceBlock allocated = {0};
		active.next = active.previous = &handles[0];
		handles[0].next = handles[0].previous = &active;
		handles[0].block = &block;
		handles[0].nameOffset = UINT32_MAX;
		SlipResource_handles = handles;
		allocated.next = allocated.previous = &block;
		block.next = block.previous = &allocated;
		block.capacityBytes = 64;
		block.flags = SLIP_RESOURCE_BLOCK_ALLOCATED;
		block.handleByteOffset = 0;
		SlipResource_freeBlocks.next = &SlipResource_freeBlocks;
		SlipResource_freeBlocks.previous = &SlipResource_freeBlocks;
		SlipResource_freeBytes = 0;
		SlipResource_ReleaseRecord(0);
		if (handles[0].block != NULL || handles[0].nameOffset != UINT32_MAX || available.previous != &handles[0] ||
		    second.next != &handles[0] || active.next != &active || allocated.next != &allocated ||
		    SlipResource_freeBytes != 96 || SlipResource_freeBlocks.next != &block)
			return 4;
		/* A null block still recycles the handle and performs no byte accounting. */
		SlipResource_ReleaseRecord(0);
		if (SlipResource_freeBytes != 96 || available.previous != &handles[0])
			return 4;
		SlipResource_handles = NULL;
		puts("resource_recycle_handle empty_nonempty_tail_order_payload_preserved=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-resource-release-block") == 0) {
		SlipResourceHandle handles[3] = {0};
		SlipResourceBlock active = {0}, firstFree = {0}, released = {0};
		SlipResourceBlock *const freeList = &SlipResource_freeBlocks;
		freeList->next = freeList->previous = &firstFree;
		firstFree.next = firstFree.previous = freeList;
		active.next = active.previous = &released;
		released.next = released.previous = &active;
		released.capacityBytes = 64;
		released.flags = SLIP_RESOURCE_BLOCK_ALLOCATED_CACHED_MOVABLE;
		released.handleByteOffset = 0x20;
		handles[2].block = &released;
		handles[2].nameOffset = UINT32_MAX;
		SlipResource_handles = handles;
		SlipResource_freeBytes = UINT32_MAX - 31;
		SlipResource_cachedBytes = 48;
		SlipResource_ReleaseBlock(&released);
		if (handles[2].block != NULL || handles[2].nameOffset != UINT32_MAX ||
		    released.flags != SLIP_RESOURCE_BLOCK_CACHED_MOVABLE || SlipResource_freeBytes != 64 ||
		    SlipResource_cachedBytes != UINT32_MAX - 47 || active.next != &active || active.previous != &active ||
		    freeList->next != &firstFree || firstFree.next != &released || released.previous != &firstFree ||
		    released.next != freeList || freeList->previous != &released)
			return 4;
		/* Sentinel owner and no bit 4: no table access or secondary counter change. */
		released.handleByteOffset = UINT32_MAX;
		released.flags = SLIP_RESOURCE_BLOCK_ALLOCATED;
		SlipResource_handles = NULL;
		SlipResource_ReleaseBlock(&released);
		if (SlipResource_freeBytes != 160 || SlipResource_cachedBytes != UINT32_MAX - 47 || released.flags != 0 ||
		    firstFree.next != &released || released.next != freeList)
			return 4;
		puts("resource_release_block handle_flags_wrapped_counters_list_order_unowned=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-resource-coalesce") == 0) {
		SlipResourceBlock blocks[5] = {0};
		for (unsigned i = 0; i < 3; ++i) {
			blocks[i].capacityBytes = 10 * (i + 1);
			blocks[i].next = &blocks[i == 2 ? 4 : i + 1];
			blocks[i].previous = &blocks[i == 0 ? 4 : i - 1];
			blocks[i].physicalNext = &blocks[i + 1];
			blocks[i + 1].physicalPrevious = &blocks[i];
		}
		blocks[4].next = &blocks[0];
		blocks[4].previous = &blocks[2];
		SlipResource_Coalesce(&blocks[1]);
		if (blocks[0].capacityBytes != 124 || blocks[0].physicalNext != &blocks[3] ||
		    blocks[3].physicalPrevious != &blocks[0] || blocks[4].next != &blocks[0] ||
		    blocks[4].previous != &blocks[0] || blocks[0].next != &blocks[4] || blocks[0].previous != &blocks[4])
			return 4;
		blocks[3].flags = SLIP_RESOURCE_BLOCK_ALLOCATED;
		SlipResource_Coalesce(&blocks[0]);
		if (blocks[0].capacityBytes != 124 || blocks[0].physicalNext != &blocks[3])
			return 4;
		blocks[0].physicalNext = NULL;
		SlipResource_Coalesce(&blocks[0]);
		if (blocks[0].capacityBytes != 124)
			return 4;
		puts("resource_coalesce next_then_previous_single_merges_links_allocated_null=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-timed-effect-speed") == 0) {
		SlipObject objects[2] = {0};
		SlipTimedEffectObjectState *const state = &objects[1].timedEffect;
		const uint16_t object = SLIP_OBJECT_DOS_STRIDE;
		SlipObject_SetSpeed(objects, object, 100);
		SlipTimedEffects_speedLimit = 0x12345678;
		uint32_t callbackValue = 0x12345678;
		bool ok = SlipTimedEffects_UpdateSpeed(state, 65535, &callbackValue, objects, object);
		ok = ok && callbackValue == 0x12340000;
		ok = ok && SlipObject_Speed(objects, object) == 100 && SlipTimedEffects_speedLimit == 0x12345678;
		state->speedLimit = 1000;
		SlipTimedEffects_speedLimit = 0;
		callbackValue = 0;
		ok = ok && SlipTimedEffects_UpdateSpeed(state, 100, &callbackValue, objects, object);
		ok = ok && SlipObject_Speed(objects, object) == 199 && callbackValue == 99;
		callbackValue = 0;
		ok = ok && SlipTimedEffects_UpdateSpeed(state, 1000, &callbackValue, objects, object);
		ok = ok && SlipObject_Speed(objects, object) == 1000 && callbackValue == 1000;
		SlipObject_SetSpeed(objects, object, 0);
		callbackValue = 0x10000;
		ok = ok && SlipTimedEffects_UpdateSpeed(state, 0, &callbackValue, objects, object);
		ok = ok && SlipObject_Speed(objects, object) == 1000;
		SlipTimedEffects_speedLimit = 0x12340000;
		callbackValue = 0;
		ok = ok && !SlipTimedEffects_UpdateSpeed(state, 4000, &callbackValue, objects, object);
		ok = ok && SlipTimedEffects_speedLimit == 0x123403e8 && SlipObject_Speed(objects, object) == 1000;
		SlipTimedEffects_speedLimit = 0;
		SlipObject_SetSpeed(objects, object, INT32_MAX);
		callbackValue = 0;
		ok = ok && SlipTimedEffects_UpdateSpeed(state, 1000, &callbackValue, objects, object);
		ok = ok && SlipObject_Speed(objects, object) == (int32_t)0x800003e7u;
		SlipTimedEffectDescriptor descriptor = {.growthDuration = 100};
		state->descriptor = &descriptor;
		state->initialExtent = 0x10000;
		state->holdExtent = 0x10000;
		state->frameCountdown = 1;
		SlipTimedEffectPhase phase;
		SlipTimedEffects_Phase(state, 1, &phase);
		callbackValue = phase.callbackValue;
		ok = ok && callbackValue == 0x10000 && phase.writeExtent;
		ok = ok && SlipTimedEffects_UpdateFrame(state, phase.step, objects, 2 * SLIP_OBJECT_DOS_STRIDE, object, 0, 0,
		                                        &callbackValue);
		SlipObject_SetSpeed(objects, object, 0);
		ok = ok && SlipTimedEffects_UpdateSpeed(state, phase.step, &callbackValue, objects, object);
		ok = ok && SlipObject_Speed(objects, object) == 1000;
		if (!ok)
			return 4;
		puts("timed_effect_speed zero_limit_interpolation_clamp_eax_upper_div_fault_signed_wrap_callback_eax=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-timed-effect-frames") == 0) {
		SlipTimedEffectFrames *const frames = malloc(sizeof(*frames) + 4 * sizeof(uint16_t));
		if (frames == NULL)
			return 4;
		frames->count = 4;
		frames->handles = (uint16_t *)(frames + 1);
		frames->delay = 12;
		for (unsigned i = 0; i < 4; ++i)
			frames->handles[i] = (uint16_t)(10 + i);
		SlipTimedEffectDescriptor descriptor = {.initialFrames = frames, .finalFrames = frames};
		SlipObject objects[2] = {0};
		SlipTimedEffectObjectState *const state = &objects[1].timedEffect;
		state->descriptor = &descriptor;
		state->frameCountdown = 5;
		objects[1].drawData = 99;
		const uint16_t object = SLIP_OBJECT_DOS_STRIDE;
		const size_t bytes = 2 * SLIP_OBJECT_DOS_STRIDE;
		uint32_t frameHandleValue = 0x12345678;
		SlipRandom_SetState(1, 2);
		bool ok =
		    SlipTimedEffects_UpdateFrame(state, 5, objects, bytes, object, 0x12345678, 0x56789abc, &frameHandleValue);
		ok = ok && frameHandleValue == 0x12340000 && state->frameCountdown == 0 && objects[1].drawData == 99 &&
		     SlipRandom_stateTail == 2;
		ok = ok &&
		     SlipTimedEffects_UpdateFrame(state, 1, objects, bytes, object, 0x12345678, 0x56789abc, &frameHandleValue);
		ok = ok && frameHandleValue == 10 && state->frameCountdown == 12 && objects[1].drawData == 0x1234000a &&
		     SlipRandom_stateTail == 3;
		state->phase = 2;
		SlipTimedEffects_fraction = 0x8000;
		ok = ok && SlipTimedEffects_UpdateFrame(state, 100, objects, bytes, object, 0x12345678, 0x56789abc,
		                                        &frameHandleValue);
		ok = ok && frameHandleValue == 12 && objects[1].drawData == 0x5678000c && state->frameCountdown == 12 &&
		     SlipRandom_stateTail == 3;
		SlipTimedEffects_fraction = 0xffff;
		ok = ok &&
		     SlipTimedEffects_UpdateFrame(state, 0, objects, bytes, object, 0x12345678, 0x56789abc, &frameHandleValue);
		ok = ok && objects[1].drawData == 0x5678000d;
		state->phase = 1;
		state->frameCountdown = INT16_MIN;
		ok = ok &&
		     SlipTimedEffects_UpdateFrame(state, 1, objects, bytes, object, 0x12345678, 0x56789abc, &frameHandleValue);
		ok = ok && state->frameCountdown == INT16_MAX && objects[1].drawData == 0x5678000d;
		free(frames);
		if (!ok)
			return 4;
		puts("timed_effect_frames timer_zero_negative_wrap_random_final_index=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-timed-effect-position") == 0) {
		SlipObject objects[2] = {0};
		SlipTimedEffect entry = {.x = 1, .y = 2, .z = 3, .displacementX = 4, .displacementY = 6};
		SlipView3DVec32 position;
		const size_t objectBytes = 2 * SLIP_OBJECT_DOS_STRIDE;
		if (!SlipTimedEffects_Position(&entry, NULL, 0, &position) || position.x != 1 || position.y != 2 ||
		    position.z != 3)
			return 4;
		entry.parentObject = SLIP_OBJECT_DOS_STRIDE;
		objects[1].position = (SlipView3DVec32){100, 200, 300};
		objects[1].matrix = (SlipView3DMatrix){{0, 0x4000, 0, -0x4000, 0, 0, 0, 0, 0x4000}};
		if (!SlipTimedEffects_Position(&entry, objects, objectBytes, &position) || position.x != 92 ||
		    position.y != 205 || position.z != 303)
			return 4;
		objects[1].matrix = (SlipView3DMatrix){{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}};
		objects[1].position = (SlipView3DVec32){1, INT32_MAX, 0};
		entry.x = INT32_MAX;
		entry.displacementX = 1;
		entry.y = 1;
		entry.displacementY = 0;
		if (!SlipTimedEffects_Position(&entry, objects, objectBytes, &position) || position.x != INT32_MIN + 1 ||
		    position.y != INT32_MIN || position.z != 3)
			return 4;
		puts("timed_effect_position unparented, parent_rotation, wrapped_additions=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-timed-effect-state") == 0) {
		SlipObject objects[2] = {0};
		SlipObjectInitTable init;
		SlipObjectSlotFill fill;
		SlipTimedEffectDescriptor descriptor = {0};
		const SlipView3DMatrix matrix = {{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}};
		if (!SlipObject_InitTableFresh(objects, 2 * SLIP_OBJECT_DOS_STRIDE, 2, &init) ||
		    !SlipObject_SlotFill(&matrix, 1, 2, 3, NULL, 0, NULL, &fill) || fill.carryOut)
			return 4;
		SlipTimedEffectObjectState *const state = SlipObject_TimedEffectState(SLIP_OBJECT_DOS_STRIDE);
		if (state != &objects[1].timedEffect)
			return 4;
		*state = (SlipTimedEffectObjectState){&descriptor, 100, -20, 30, 2, -1, 0xabcd};
		if (objects[1].timedEffect.descriptor != &descriptor || objects[1].timedEffect.age != 100 ||
		    objects[1].timedEffect.frameCountdown != -1 || objects[1].timedEffect.speedLimit != 0xabcd ||
		    objects[1].position.x != 1 || objects[1].position.y != 2 || objects[1].position.z != 3 ||
		    memcmp(&objects[1].matrix, &matrix, sizeof(matrix)) != 0)
			return 4;
		puts("timed_effect_state native_pointer, typed_fields, adjacent_object_fields=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-timed-reclaim") == 0) {
		SlipObject objects[5] = {0};
		const size_t bytes = 5 * SLIP_OBJECT_DOS_STRIDE;
		SlipObjectInitTable init;
		SlipObjectSlotFill fill;
		const SlipView3DMatrix matrix = {{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}};
		uint16_t selected;
		if (!SlipObject_InitTableFresh(objects, bytes, 5, &init) ||
		    SlipTimedEffects_SelectOldest(objects, bytes, &selected) || selected != UINT16_MAX)
			return 4;
		for (unsigned i = 1; i < 5; ++i) {
			if (!SlipObject_SlotFill(&matrix, 0, 0, 0, TrackView_DrawTimedEffect, 0, NULL, &fill) || fill.carryOut ||
			    fill.objectOffset != i * SLIP_OBJECT_DOS_STRIDE)
				return 4;
		}
		objects[2].slotDrawCallback = TrackView_ExecuteBonusDrawCallback;
		objects[2].timedEffect.age = UINT32_MAX;
		objects[3].slotDrawCallback = TrackView_QueueTimedEffect;
		if (!SlipTimedEffects_SelectOldest(objects, bytes, &selected) || selected != 4 * SLIP_OBJECT_DOS_STRIDE)
			return 4; /* Zero age is eligible; equal ages select the last object. */
		objects[3].timedEffect.age = 0x80000000u;
		objects[4].timedEffect.age = 1;
		if (!SlipTimedEffects_SelectOldest(objects, bytes, &selected) || selected != 3 * SLIP_OBJECT_DOS_STRIDE)
			return 4;
		objects[3].timedEffect.age = UINT32_MAX;
		objects[4].timedEffect.age = UINT32_MAX;
		objects[4].eventCallback = SlipDebug_ReclaimEvent;
		SlipObject_BeginDeferredSection();
		SlipObject_BeginDeferredSection();
		if (!SlipTimedEffects_Reclaim(objects, bytes, 0xabcd0000, 11, 22, 33, 44, 55) || objects[4].allocated)
			return 4;
		if (SlipDebug_reclaimEvent[0] != (0xabcd0000u | SLIP_OBJECT_EVENT_FREE) || SlipDebug_reclaimEvent[1] != 11 ||
		    SlipDebug_reclaimEvent[2] != 22 || SlipDebug_reclaimEvent[3] != 33 ||
		    SlipDebug_reclaimEvent[4] != 4 * SLIP_OBJECT_DOS_STRIDE || SlipDebug_reclaimEvent[5] != 44 ||
		    SlipDebug_reclaimEvent[6] != 55)
			return 4;
		if (!SlipTimedEffects_Reclaim(objects, bytes, 0, 0, 0, 0, 0, 0) || objects[3].allocated)
			return 4;
		SlipObject_Free(SLIP_OBJECT_DOS_STRIDE, 0, 0, 0, 0, 0, 0);
		if (SlipTimedEffects_Reclaim(objects, bytes, 0, 0, 0, 0, 0, 0) || !objects[1].allocated ||
		    !objects[2].allocated)
			return 4;
		SlipObject_EndDeferredSection(0, 0, 0, 0, 0, 0);
		if (!objects[1].allocated)
			return 4;
		SlipObject_EndDeferredSection(0, 0, 0, 0, 0, 0);
		if (objects[1].allocated)
			return 4;
		puts("timed_reclaim identities_unsigned_age_ties_immediate_free_registers_nested_lock=passed");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--verify-cross-events") == 0) {
		SlipObject objects[2] = {0};
		SlipObjectInitTable init;
		SlipObjectSlotFill fill;
		const SlipView3DMatrix matrix = {{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}};
		const uint16_t object = SLIP_OBJECT_DOS_STRIDE;
		if (!SlipObject_InitTableFresh(objects, 2 * SLIP_OBJECT_DOS_STRIDE, 2, &init) ||
		    !SlipObject_SlotFill(&matrix, 0, 0, 0, TrackView_DrawCrossEffect, 0x9999abcd, SlipCrossEffects_Event,
		                         &fill) ||
		    fill.carryOut)
			return 4;
		SlipCrossEffectState *const state = SlipObject_CrossEffectState(object);
		*state = (SlipCrossEffectState){SlipDebug_CrossUpdate, 100, -12345};
		if (SlipObject_DispatchEvent(object, 0x56780101, 1, 2, 3, 4, 5) != UINT32_MAX ||
		    state->remainingLifetime != 100 || objects[1].drawData != 0x9999abcd || SlipDebug_crossUpdateCount != 0)
			return 4;
		SlipFrameTimer_step = 0x89abcdef;
		SlipFrameTimer_inverse = 0x12345678;
		SlipCrossEffects_paletteStart = 0x40;
		SlipCrossEffects_paletteCount = 0x20;
		const uint16_t lifetimes[] = {6, 5, 0x8000, 0};
		const uint16_t deltas[] = {5, 5, 1, 0xffff};
		const uint16_t expected[] = {1, 0, 0x7fff, 1};
		for (unsigned i = 0; i < 4; ++i) {
			state->remainingLifetime = lifetimes[i];
			SlipFrameTimer_delta = 0x76540000u | deltas[i];
			if (SlipObject_DispatchEvent(object, 0xabcd0104, 11, 22, 33, 44, 55) != 0xa1b2c3d4 ||
			    state->remainingLifetime != expected[i] || SlipDebug_crossUpdateLifetime != expected[i] ||
			    SlipDebug_crossUpdateSlot30 != 0x005fabcd || SlipDebug_crossUpdateCount != i + 1 ||
			    !objects[1].allocated)
				return 4;
		}
		/* Observe the expiry free call directly, without replacing its implementation. */
		objects[1].eventCallback = SlipDebug_ReclaimEvent;
		state->remainingLifetime = 1;
		SlipFrameTimer_delta = 0x76540002;
		SlipObject_BeginDeferredSection();
		if (SlipCrossEffects_Event(0xabcd0104, 11, 22, 0x87654321, object, 44, 0x13572468) != 0x76540002 ||
		    objects[1].allocated || state->remainingLifetime != UINT16_MAX || SlipDebug_crossUpdateCount != 4 ||
		    SlipDebug_reclaimEvent[0] != 0x76540102 || SlipDebug_reclaimEvent[1] != 0x89abcdef ||
		    SlipDebug_reclaimEvent[2] != 0x12345678 || SlipDebug_reclaimEvent[3] != 0x87654321 ||
		    SlipDebug_reclaimEvent[4] != object || SlipDebug_reclaimEvent[5] != (uintptr_t)state ||
		    SlipDebug_reclaimEvent[6] != 0x13572468)
			return 4;
		SlipObject_EndDeferredSection(0, 0, 0, 0, 0, 0);
		puts("cross_events ax_dispatch_wrapped_lifetime_zero_survives_color_angle_update_return_native_free=passed");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--verify-cross-lifecycle") == 0) {
		/* Use a null attachment here; actual attachment creation remains pending. */
		const uint16_t cleanupCount = SlipRuntime_cleanupCount;
		SlipCrossEffects_Initialize(0x80000000u, NULL);
		if (SlipCrossEffects_initialized != UINT32_MAX || SlipCrossEffects_drawMode != 0x80000000u ||
		    SlipCrossEffects_attach != NULL || SlipRuntime_cleanupCount != cleanupCount + 1 ||
		    SlipRuntime_cleanupCallbacks[cleanupCount] != SlipCrossEffects_Cleanup)
			return 4;
		SlipCrossEffects_Initialize(7, NULL);
		if (SlipCrossEffects_drawMode != 0x80000000u || SlipRuntime_cleanupCount != cleanupCount + 1)
			return 4;
		SlipObject objects[2] = {0};
		const uint16_t starts[] = {0x40, 0xfff0, 0xffff, 0};
		const uint16_t counts[] = {0x20, 0x30, 1, 0};
		const uint32_t expectedPaletteEndValues[] = {0x1234005f, 0x1234001f, 0x1233ffff, 0x1233ffff};
		const uint32_t expectedDrawDataValues[] = {0x005fabcd, 0x001fabcd, 0xffffabcd, 0xffffabcd};
		for (unsigned i = 0; i < 4; ++i) {
			SlipCrossEffects_paletteStart = starts[i];
			SlipCrossEffects_paletteCount = counts[i];
			objects[1].drawData = 0x7654abcd;
			uint32_t paletteEndValue = 0x12345678;
			if (!SlipCrossEffects_Color(objects, 2 * SLIP_OBJECT_DOS_STRIDE, SLIP_OBJECT_DOS_STRIDE,
			                            &paletteEndValue) ||
			    objects[1].drawData != expectedDrawDataValues[i] || paletteEndValue != expectedPaletteEndValues[i])
				return 4;
		}
		SlipCrossEffects_paletteStart = 123;
		SlipCrossEffects_paletteCount = 45;
		SlipRuntime_cleanupCallbacks[cleanupCount]();
		SlipCrossEffects_Cleanup();
		if (SlipCrossEffects_initialized != 0 || SlipCrossEffects_drawMode != 0x80000000u ||
		    SlipCrossEffects_paletteStart != 123 || SlipCrossEffects_paletteCount != 45)
			return 4;
		SlipCrossEffects_Initialize(7, NULL);
		if (SlipCrossEffects_initialized != UINT32_MAX || SlipCrossEffects_drawMode != 7 ||
		    SlipRuntime_cleanupCount != cleanupCount + 1)
			return 4;
		puts("cross_lifecycle first_init_cleanup_reinit_palette_wrap_slot_angle_edx_borrow=passed");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--verify-cross-reclaim") == 0) {
		SlipObject objects[5] = {0};
		const size_t bytes = 5 * SLIP_OBJECT_DOS_STRIDE;
		SlipObjectInitTable init;
		SlipObjectSlotFill fill;
		const SlipView3DMatrix matrix = {{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}};
		uint16_t selected;
		if (!SlipObject_InitTableFresh(objects, bytes, 5, &init) ||
		    SlipCrossEffects_Select(objects, bytes, &selected) || selected != UINT16_MAX)
			return 4;
		for (unsigned i = 1; i < 5; ++i) {
			if (!SlipObject_SlotFill(&matrix, 0, 0, 0, TrackView_DrawCrossEffect, 0, NULL, &fill) || fill.carryOut ||
			    fill.objectOffset != i * SLIP_OBJECT_DOS_STRIDE)
				return 4;
		}
		objects[2].slotDrawCallback = TrackView_ExecuteBonusDrawCallback;
		objects[2].crossEffect.remainingLifetime = UINT16_MAX;
		objects[3].slotDrawCallback = TrackView_QueueCrossEffect;
		if (!SlipCrossEffects_Select(objects, bytes, &selected) || selected != 4 * SLIP_OBJECT_DOS_STRIDE)
			return 4; /* Zero lifetime is eligible; equal lifetimes select the last object. */
		objects[3].crossEffect.remainingLifetime = 0x8000u;
		objects[4].crossEffect.remainingLifetime = 1;
		if (!SlipCrossEffects_Select(objects, bytes, &selected) || selected != 3 * SLIP_OBJECT_DOS_STRIDE)
			return 4;
		objects[3].crossEffect.remainingLifetime = UINT16_MAX;
		objects[4].crossEffect.remainingLifetime = UINT16_MAX;
		objects[4].eventCallback = SlipDebug_ReclaimEvent;
		SlipObject_BeginDeferredSection();
		SlipObject_BeginDeferredSection();
		if (!SlipCrossEffects_Reclaim(objects, bytes, 0xabcd0000, 11, 22, 33, 44, 55) || objects[4].allocated)
			return 4;
		if (SlipDebug_reclaimEvent[0] != (0xabcd0000u | SLIP_OBJECT_EVENT_FREE) || SlipDebug_reclaimEvent[1] != 11 ||
		    SlipDebug_reclaimEvent[2] != 22 || SlipDebug_reclaimEvent[3] != 33 ||
		    SlipDebug_reclaimEvent[4] != 4 * SLIP_OBJECT_DOS_STRIDE || SlipDebug_reclaimEvent[5] != 44 ||
		    SlipDebug_reclaimEvent[6] != 55)
			return 4;
		if (!SlipCrossEffects_Reclaim(objects, bytes, 0, 0, 0, 0, 0, 0) || objects[3].allocated)
			return 4;
		SlipObject_Free(SLIP_OBJECT_DOS_STRIDE, 0, 0, 0, 0, 0, 0);
		if (SlipCrossEffects_Reclaim(objects, bytes, 0, 0, 0, 0, 0, 0) || !objects[1].allocated ||
		    !objects[2].allocated)
			return 4;
		SlipObject_EndDeferredSection(0, 0, 0, 0, 0, 0);
		if (!objects[1].allocated)
			return 4;
		SlipObject_EndDeferredSection(0, 0, 0, 0, 0, 0);
		if (objects[1].allocated)
			return 4;
		puts("cross_reclaim identities_unsigned_lifetime_ties_immediate_free_registers_nested_lock=passed");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--verify-object-native-payload") == 0) {
		SlipObject objects[3] = {0};
		SlipObjectInitTable init;
		SlipObjectSlotFill fill;
		const SlipView3DMatrix matrix = {{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}};
		if (!SlipObject_InitTableFresh(objects, 3 * SLIP_OBJECT_DOS_STRIDE, 3, &init))
			return 4;
		for (unsigned i = 1; i < 3; ++i) {
			if (!SlipObject_SlotFill(&matrix, 0, 0, 0, NULL, 0, SlipDebug_ReclaimEvent, &fill) || fill.carryOut)
				return 4;
		}
		const uintptr_t nativeState = (uintptr_t)&objects[1].crossEffect;
		if (SlipObject_DispatchEvent(SLIP_OBJECT_DOS_STRIDE, 0xabcd0104, 11, 22, 33, nativeState, 44) != 0xabcd0104 ||
		    SlipDebug_reclaimEvent[5] != nativeState)
			return 4;
		SlipObject_DispatchEvent(UINT16_MAX, 0xabcd0104, 11, 22, 33, nativeState, 44);
		if (SlipDebug_reclaimEvent[5] != nativeState || SlipDebug_reclaimEvent[4] != 2 * SLIP_OBJECT_DOS_STRIDE)
			return 4;
		SlipObject_SetServer(7, SlipDebug_ReclaimEvent);
		SlipObject_FreeImmediate(SLIP_OBJECT_DOS_STRIDE, 0xabcd0000, 11, 22, 33, nativeState, 44);
		if (objects[1].allocated || !objects[2].allocated || SlipDebug_nativeFreeData[0] != nativeState ||
		    SlipDebug_nativeFreeData[1] != nativeState || SlipDebug_nativeFreeData[2] != nativeState)
			return 4;
		puts("object_native_payload direct_broadcast_free_server_freed_pointer_width=passed");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--verify-object-immediate-free") == 0) {
		SlipObject objects[4] = {0};
		SlipObjectInitTable init;
		SlipObjectSlotFill fill;
		const SlipView3DMatrix matrix = {{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}};
		if (!SlipObject_InitTableFresh(objects, 4 * SLIP_OBJECT_DOS_STRIDE, 4, &init))
			return 4;
		for (unsigned i = 1; i < 4; ++i) {
			if (!SlipObject_SlotFill(&matrix, i, 0, 0, NULL, 0, NULL, &fill) || fill.carryOut ||
			    fill.objectOffset != i * SLIP_OBJECT_DOS_STRIDE)
				return 4;
		}
		SlipObject_BeginDeferredSection();
		SlipObject_BeginDeferredSection();
		SlipObject_Free(SLIP_OBJECT_DOS_STRIDE, 0, 1, 2, 3, 4, 5);
		SlipObject_FreeImmediate(2 * SLIP_OBJECT_DOS_STRIDE, 0, 6, 7, 8, 9, 10);
		SlipObject_Free(3 * SLIP_OBJECT_DOS_STRIDE, 0, 11, 12, 13, 14, 15);
		if (objects[1].allocated == 0 || objects[2].allocated != 0 || objects[3].allocated == 0)
			return 4;
		SlipObject_EndDeferredSection(0, 0, 0, 0, 0, 0);
		if (objects[1].allocated == 0 || objects[3].allocated == 0)
			return 4;
		SlipObject_EndDeferredSection(0, 0, 0, 0, 0, 0);
		if (objects[1].allocated != 0 || objects[3].allocated != 0)
			return 4;
		puts("object_immediate_free bypass, nested_lock_restore, deferred_queue=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-object-slot-matrix") == 0) {
		SlipObject objects[2] = {0};
		const size_t objectBytes = 2 * SLIP_OBJECT_DOS_STRIDE;
		const SlipView3DMatrix matrix = {{0x3f9e, 0, 0x06f0, 0x0018, 0x3ffe, -221, -1776, 0x00df, 0x3f9e}};
		SlipObjectInitTable init;
		SlipObjectSlotFill fill;
		if (!SlipObject_InitTableFresh(objects, objectBytes, 2, &init) ||
		    !SlipObject_SlotFill(&matrix, 4465301, 1093323, 4081385, NULL, 0x470ec, NULL, &fill) || fill.carryOut ||
		    fill.objectOffset != SLIP_OBJECT_DOS_STRIDE || memcmp(&objects[1].matrix, &matrix, sizeof(matrix)) != 0 ||
		    objects[1].direction.x != -1776 || objects[1].direction.y != 0xdf || objects[1].direction.z != 0x3f9e ||
		    objects[1].position.x != 4465301 || objects[1].position.y != 1093323 || objects[1].position.z != 4081385 ||
		    objects[1].drawData != 0x470ec)
			return 4;
		if (!SlipObject_SlotFill(&matrix, 0, 0, 0, NULL, 0, NULL, &fill) || !fill.carryOut)
			return 4;
		puts("object_slot_matrix captured_basis_direction_position_exhaustion=passed");
		return 0;
	}
	/* Isolated door event checks; the live door object initializer is still pending. */
	if (argc == 2 && strcmp(argv[1], "--verify-door-events") == 0) {
		SlipObject objects[3] = {0};
		SlipTrackSlotRecord slots[2] = {0};
		SlipTrackDoorRecord *const door = &SlipTrackWorld_doors[0];
		const uint16_t object = SLIP_OBJECT_DOS_STRIDE;
		const size_t objectBytes = 3 * SLIP_OBJECT_DOS_STRIDE;
		SlipRaceCollision_objectTable = objects;
		SlipRaceCollision_objectTableBytes = objectBytes;
		SlipFrameTimer_step = 0x4000;
		*door = (SlipTrackDoorRecord){.speed = 100,
		                              .object = object,
		                              .openEndpoint = {1000, 0, 0},
		                              .closedEndpoint = {0, 0, 0},
		                              .endpointDelay = 71,
		                              .directionX = 0x4000};
		if (SlipTrackWorld_DoorEvent(0x104, object, 0, door, objects, objectBytes, slots, sizeof(slots), 0x1000) != 0 ||
		    objects[1].position.x != 100 || door->direction != 0)
			return 4;
		objects[1].position.x = 900;
		if (SlipTrackWorld_DoorEvent(0x104, object, 0, door, objects, objectBytes, slots, sizeof(slots), 0x1000) != 0 ||
		    objects[1].position.x != 1000 || door->direction != UINT32_MAX || door->speed != 0x37dc ||
		    door->endpointDelayRemaining != 71)
			return 4;
		door->speed = 100;
		objects[1].position.x = 100;
		if (SlipTrackWorld_DoorEvent(0x104, object, 0, door, objects, objectBytes, slots, sizeof(slots), 0x1000) != 0 ||
		    objects[1].position.x != 0 || door->direction != UINT32_MAX)
			return 4;
		if (SlipTrackWorld_DoorEvent(0x104, object, 0, door, objects, objectBytes, slots, sizeof(slots), 0x1000) != 0 ||
		    objects[1].position.x != 0 || door->direction != 0)
			return 4;
		objects[2].trackSlotOffset = sizeof(slots[0]);
		slots[1].flags = 4;
		door->direction = UINT32_MAX;
		if (SlipTrackWorld_DoorEvent(0x106, object, 2 * object, door, objects, objectBytes, slots, sizeof(slots),
		                             0x1000) != 0 ||
		    door->direction != 0 || door->speed != 0x6fb8 || door->endpointDelay != 71)
			return 4;
		door->directionX = INT16_MIN;
		door->direction = UINT32_MAX;
		SlipTrackWorld_DoorDirection(door);
		if (SlipTrackWorld_doorDirection.x != INT16_MIN ||
		    SlipTrackWorld_DoorEvent(0xabcd0107, object, 0, door, objects, objectBytes, slots, sizeof(slots), 0x1000) !=
		        0xabcd0000 ||
		    SlipTrackWorld_DoorEvent(0x1234, object, 0, door, objects, objectBytes, slots, sizeof(slots), 0x1000) !=
		        UINT32_MAX)
			return 4;
		SlipTrackWorld_doorCount = 3;
		SlipTrackWorld_doors[1].object = object;
		SlipTrackWorld_doors[2].object = 2 * object;
		if (SlipTrackWorld_DoorEvent(0x102, object, 0, door, objects, objectBytes, slots, sizeof(slots), 0x1000) != 0 ||
		    door->object != 0 || SlipTrackWorld_doors[1].object != 0 || SlipTrackWorld_doors[2].object != 2 * object)
			return 4;

		uint8_t physics[3 * 0x58] = {0};
		SlipRaceCollision_physicsTable = physics;
		SlipRaceCollision_activeBodyOffset = 0;
		SlipRaceCollision_freeBodyOffset = 0x58;
		SlipRaceCollision_bodyCount = 1;
		SlipRaceCollision_InitializeBodyLists();
		if (SlipRaceCollision_CreateBody(object, 1))
			return 4;
		SlipRaceCollision_trackQuery = SlipDebug_DoorCollision;
		objects[1].position.x = 77;
		SlipTrackWorld_doorPosition = (SlipView3DVec32){99, 0, 0};
		door->direction = UINT32_MAX;
		door->speed = 42;
		door->endpointDelay = door->endpointDelayRemaining = 71;
		if (!SlipTrackWorld_MoveDoor(door, object, objects, objectBytes) || objects[1].position.x != 77 ||
		    door->direction != 0 || door->speed != 0x37dc || door->endpointDelay != 0 ||
		    door->endpointDelayRemaining != 0)
			return 4;
		door->speed = 42;
		if (!SlipTrackWorld_MoveDoor(door, object, objects, objectBytes) || objects[1].position.x != 99 ||
		    door->speed != 42)
			return 4;
		SlipRaceCollision_trackQuery = NULL;
		puts("door_events movement_endpoint_equality_collision_stop_rollback_word_negation_free=passed");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--verify-auxiliary-plane") == 0) {
		SlipDraw3DProjectState state = {0};
		SlipDraw3DVec32 origin = {100, 200, 300};

		SlipDraw3D_SetAuxiliaryClipPlane(&state, origin, (int16_t)32768, (int16_t)65535, 16384);
		if (!state.auxiliaryClipPlaneEnabled || state.auxiliaryClipPlaneNormal.x != -32768 ||
		    state.auxiliaryClipPlaneNormal.y != -1 || state.auxiliaryClipPlaneNormal.z != 16384 ||
		    SlipDraw3D_ClassifyShapeBounds((SlipDraw3DVec32){101, 200, 300}, 0, &state) != 5 ||
		    SlipDraw3D_ClassifyShapeBounds((SlipDraw3DVec32){99, 200, 300}, 0, &state) != 1)
			return 4;
		SlipDraw3D_ClearAuxiliaryClipPlane(&state);
		if (state.auxiliaryClipPlaneEnabled || state.auxiliaryClipPlaneOrigin.x != 100 ||
		    state.auxiliaryClipPlaneOrigin.y != 200 || state.auxiliaryClipPlaneOrigin.z != 300 ||
		    state.auxiliaryClipPlaneNormal.x != -32768 ||
		    SlipDraw3D_ClassifyShapeBounds((SlipDraw3DVec32){101, 200, 300}, 0, &state) != 1)
			return 4;
		puts("auxiliary_plane signed_word_normal_classification_clear=passed");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--verify-door-draw-state") == 0) {
		SlipObject objects[2] = {0};
		TrackViewRawBspContext context = {0};
		SlipDraw3DProjectState state = {0};
		SlipShape3DDoorTemplate shape = SlipShape3D_doorTemplate;
		objects[0].matrix = (SlipView3DMatrix){{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}};
		objects[0].position = (SlipView3DVec32){10, 20, 30};
		objects[1].matrix = (SlipView3DMatrix){{100, 200, 300, 400, 500, 600, 700, 800, 900}};
		objects[1].drawData = 0x339a6;
		objects[1].flags = 1;
		objects[1].viewPosition.z = -1000000;
		context.objectTable = objects;
		context.objectTableBytes = sizeof(objects) / sizeof(objects[0]) * SLIP_OBJECT_DOS_STRIDE;
		context.projectState = &state;
		SlipTrackWorld_doors[0].planeOrigin = (SlipView3DVec32){110, 220, 330};
		SlipTrackWorld_doors[0].directionX = INT16_MIN;
		SlipTrackWorld_doors[0].directionY = 200;
		SlipTrackWorld_doors[0].directionZ = -300;
		SlipTrackWorld_doorShapes[0] = (SlipResourcePayload){(uint8_t *)&shape, sizeof(shape), false};
		SlipView3DMatrix original = objects[1].matrix;
		if (!TrackView_DrawDoor(&context, SLIP_OBJECT_DOS_STRIDE) || state.auxiliaryClipPlaneEnabled ||
		    state.auxiliaryClipPlaneOrigin.x != 100 || state.auxiliaryClipPlaneOrigin.y != 200 ||
		    state.auxiliaryClipPlaneOrigin.z != 300 || state.auxiliaryClipPlaneNormal.x != INT16_MIN ||
		    state.auxiliaryClipPlaneNormal.y != -200 || state.auxiliaryClipPlaneNormal.z != 300 ||
		    !TrackView_DrawDoorReverse(&context, SLIP_OBJECT_DOS_STRIDE) ||
		    memcmp(&objects[1].matrix, &original, sizeof(original)) != 0 || state.auxiliaryClipPlaneEnabled)
			return 4;
		SlipTrackWorld_doorShapes[0] = (SlipResourcePayload){0};
		puts("door_draw culled_path_plane_transform_word_negation_clear_matrix_restore=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-track-registration-release") == 0) {
		for (unsigned test = 0; test < 6; ++test) {
			SlipObject objects[2] = {0};
			SlipTrackSlotRecord slot = {0};
			SlipObjectInitTable init;
			SlipObjectSlotFill fill;
			const SlipView3DMatrix matrix = {{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}};
			const SlipTrackWorldSlotCollisionState entry = {0x12345678, 0x23456789, 0x3456789a, 0x456789ab,
			                                                0,          0x3000,     0x56789abc};
			const uint32_t flags[] = {1, 2, 5};
			bool deferred = test >= 3;
			if (!SlipObject_InitTableFresh(objects, 2 * SLIP_OBJECT_DOS_STRIDE, 2, &init) ||
			    !SlipObject_SlotFill(&matrix, 0, 0, 0, NULL, 0, NULL, &fill) || fill.carryOut)
				return 4;
			slot.flags = flags[test % 3];
			slot.ownerObjectOffset = fill.objectOffset;
			if (deferred)
				SlipObject_BeginDeferredSection();
			if (!SlipTrackWorld_RegisterSlot(0, NULL, 0, 0, 0, (uint8_t *)&slot, sizeof(slot), 0x3000, NULL, 0, 0,
			                                 objects, 2 * SLIP_OBJECT_DOS_STRIDE, NULL, 0, (uint8_t *)&slot, &entry))
				return 4;
			if ((objects[1].allocated != 0) != deferred)
				return 4;
			if (deferred)
				SlipObject_EndDeferredSection(0x87654321, 0x76543210, 0x65432109, 0x54321098, 0x43210987, 0x32109876);
			if (objects[1].allocated != 0 || !SlipObject_SlotFill(&matrix, 0, 0, 0, NULL, 0, NULL, &fill) ||
			    fill.carryOut || fill.objectOffset != SLIP_OBJECT_DOS_STRIDE)
				return 4;
		}
		puts("track_registration_release normal_type_two_flag4_immediate_deferred_reuse=passed");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--verify-track-registration") == 0) {
		for (unsigned test = 0; test < 4; ++test) {
			SlipTrackDrawRecord draws[4] = {0};
			SlipTrackSlotRecord slot = {0};
			SlipObject objects[2] = {0};
			SlipObjectDrawCallback callbacks[4] = {0};
			SlipTrackWorldSlotDrawRing ring;
			uint8_t trd[0x90] = {0};
			const uint32_t drawBase = 0x1000;
			for (unsigned i = 0; i < 8; ++i)
				slot.cornerTrackRecords[i] = 0x2020;
			slot.cornerTrackRecords[7] = 0x2060;
			slot.flags = 1;
			slot.ownerObjectOffset = SLIP_OBJECT_DOS_STRIDE;
			if (test != 0) {
				trd[0x24] = 0x60;
				trd[0x26] = 4;
			}
			if (test == 1) {
				trd[0x64] = 0x20;
				trd[0x66] = 8;
			}
			if (test == 3) {
				slot.doorAddress = 0x339a6;
				SlipTrackWorld_doors[0].firstTrackRecord = 0x2020;
				SlipTrackWorld_doors[0].secondTrackRecord = 0x2060;
			}
			if (!SlipTrackWorld_InitSlotDrawRing((uint8_t *)draws, sizeof(draws), drawBase, 3, NULL, 0, &ring) ||
			    !SlipTrackWorld_RegisterSlot(0, (uint8_t *)draws, sizeof(draws), drawBase, drawBase, (uint8_t *)&slot,
			                                 sizeof(slot), 0x3000, trd, sizeof(trd), 0x2000, objects,
			                                 2 * SLIP_OBJECT_DOS_STRIDE, callbacks, 4, (uint8_t *)&slot, NULL))
				return 4;
			if (test == 0) {
				if (slot.firstDrawAddress != 0x1038 || slot.secondDrawAddress != 0 || draws[1].edgeReference != 0 ||
				    draws[0].nextAddress != 0x1070)
					return 4;
			} else if (test == 1) {
				if (slot.firstDrawAddress != 0x1038 || slot.secondDrawAddress != 0x1070 ||
				    draws[1].pairedDrawAddress != 0x1070 || draws[2].pairedDrawAddress != 0x1038 ||
				    draws[1].edgeReference != 4 || draws[2].edgeReference != 8)
					return 4;
			} else if (test == 2) {
				if (slot.firstDrawAddress != 0x1070 || slot.secondDrawAddress != 0 || draws[1].edgeReference != 4 ||
				    draws[2].edgeReference != 0 || draws[1].ownerTrackRecordAddress != 0x2020 ||
				    draws[2].ownerTrackRecordAddress != 0x2020 || draws[0].nextAddress != 0x10a8)
					return 4;
			}
			if (test == 3 &&
			    (slot.firstDrawAddress != 0x1038 || slot.secondDrawAddress != 0x1070 ||
			     draws[1].pairedDrawAddress != 0x1070 || draws[2].pairedDrawAddress != 0x1038 ||
			     draws[1].ownerTrackRecordAddress != 0x2020 || draws[2].ownerTrackRecordAddress != 0x2060 ||
			     draws[1].edgeReference != 0 || draws[2].edgeReference != 0 ||
			     draws[1].objectOffset != SLIP_OBJECT_DOS_STRIDE || draws[2].objectOffset != SLIP_OBJECT_DOS_STRIDE ||
			     callbacks[1] != TrackView_DrawDoorReverse || callbacks[2] != TrackView_DrawDoor))
				return 4;
		}
		puts("track_registration_single_paired_missing_reverse_DOS_fallback=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-track-draw-exhaustion") == 0) {
		SlipTrackDrawRecord sentinel = {.nextAddress = 0x1000, .previousAddress = 0x1000};
		uint8_t owner[0x22] = {0};
		SlipTrackWorldSlotDrawAlloc result;
		(void)SlipTrackWorld_AllocSlotDrawRecord((uint8_t *)&sentinel, sizeof(sentinel), 0x1000, 0x1000, owner,
		                                         sizeof(owner), 0x2000, &result);
		return 4;
	}
	/* TRD owner remains serialized; draw links are resident records. */
	if (argc == 2 && strcmp(argv[1], "--verify-track-draw-lifecycle") == 0) {
		SlipTrackDrawRecord records[3] = {0};
		SlipTrackSlotRecord slot = {0};
		uint8_t owner[0x22] = {0};
		SlipTrackWorldSlotDrawRing ring;
		SlipTrackWorldSlotDrawAlloc allocated;
		SlipTrackWorldOwnerDrawLinksClear cleared;
		const uint32_t base = 0x1000;
		if (!SlipTrackWorld_InitSlotDrawRing((uint8_t *)records, sizeof(records), base, 2, NULL, 0, &ring) ||
		    !SlipTrackWorld_AllocSlotDrawRecord((uint8_t *)records, sizeof(records), base, base, owner, sizeof(owner),
		                                        0x2000, &allocated))
			return 4;
		slot.firstDrawAddress = allocated.allocatedAddress;
		if (slot.firstDrawAddress != base + 0x38 ||
		    !SlipTrackWorld_AllocSlotDrawRecord((uint8_t *)records, sizeof(records), base, base, owner, sizeof(owner),
		                                        0x2000, &allocated))
			return 4;
		slot.secondDrawAddress = allocated.allocatedAddress;
		if (slot.secondDrawAddress != base + 0x70 || records[1].nextAddress != base + 0x70 ||
		    records[2].nextAddress != base + 0x38 ||
		    !SlipTrackWorld_ClearOwnerDrawLinks(1, (uint8_t *)records, sizeof(records), base, base, owner,
		                                        sizeof(owner), 0x2000, (uint8_t *)&slot, sizeof(slot), &cleared) ||
		    !cleared.callFreeFirstDraw || !cleared.callFreeSecondDraw || slot.firstDrawAddress ||
		    slot.secondDrawAddress || owner[0x10] || owner[0x11] || records[0].nextAddress != base + 0x70 ||
		    records[0].previousAddress != base + 0x38 || records[2].nextAddress != base + 0x38 ||
		    records[2].previousAddress != base || records[1].nextAddress != base ||
		    records[1].previousAddress != base + 0x70)
			return 4;
		{
			SlipTrackWorldSlotDrawClearVisit visits[2];
			SlipTrackWorldSlotDrawClear clear;
			records[0].attachmentTransformReady = 99;
			records[1].attachmentTransformReady = 88;
			records[2].attachmentTransformReady = 77;
			records[1].edgeReference = 111;
			records[1].pairedDrawAddress = 222;
			if (!SlipTrackWorld_ClearSlotDrawLinks((uint8_t *)records, sizeof(records), 2, visits, 2, &clear) ||
			    clear.clearedCount != 2 || records[0].attachmentTransformReady != 99 ||
			    records[1].attachmentTransformReady || records[2].attachmentTransformReady ||
			    records[1].edgeReference != 111 || records[1].pairedDrawAddress != 222)
				return 4;
		}
		puts("track_draw_lifecycle singleton_pair_owner_cleanup_free_ring_frame_clear=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-track-slot-free") == 0) {
		SlipTrackSlotRecord slots[4] = {0};
		const uint32_t base = 0x1000;
		const uint32_t stride = sizeof(slots[0]);
		slots[0].nextSlotAddress = slots[0].previousSlotAddress = base + 2 * stride;
		slots[2].nextSlotAddress = slots[2].previousSlotAddress = base;
		slots[1].nextSlotAddress = slots[1].previousSlotAddress = base + 3 * stride;
		slots[3].nextSlotAddress = slots[3].previousSlotAddress = base + stride;
		slots[2].flags = 123;
		if (!SlipTrackWorld_FreeSlotListEntry(0, NULL, 0, 0, 0, NULL, 0, 0, (uint8_t *)slots, sizeof(slots), base,
		                                      base + stride, (uint8_t *)&slots[2]) ||
		    slots[0].nextSlotAddress != base || slots[0].previousSlotAddress != base ||
		    slots[1].nextSlotAddress != base + 2 * stride || slots[1].previousSlotAddress != base + 3 * stride ||
		    slots[2].nextSlotAddress != base + 3 * stride || slots[2].previousSlotAddress != base + stride ||
		    slots[3].nextSlotAddress != base + stride || slots[3].previousSlotAddress != base + 2 * stride ||
		    slots[2].flags != 123)
			return 4;
		{
			SlipTrackWorldSlotListAlloc allocated;
			slots[2].firstDrawAddress = 123;
			slots[2].secondDrawAddress = 456;
			slots[2].currentTrackRecordAddress = 789;
			for (unsigned i = 0; i < 8; ++i)
				slots[2].cornerTrackRecords[i] = i + 1;
			if (!SlipTrackWorld_AllocSlotListEntry((uint8_t *)slots, sizeof(slots), base, base, base + stride,
			                                       &allocated) ||
			    allocated.carry || allocated.allocatedAddress != base + 2 * stride ||
			    slots[0].nextSlotAddress != base + 2 * stride || slots[0].previousSlotAddress != base + 2 * stride ||
			    slots[2].nextSlotAddress != base || slots[2].previousSlotAddress != base ||
			    slots[1].nextSlotAddress != base + 3 * stride || slots[3].previousSlotAddress != base + stride ||
			    slots[2].flags != 123 || slots[2].firstDrawAddress || slots[2].secondDrawAddress ||
			    slots[2].currentTrackRecordAddress)
				return 4;
			for (unsigned i = 0; i < 8; ++i)
				if (slots[2].cornerTrackRecords[i] != 0)
					return 4;
			if (!SlipTrackWorld_AllocSlotListEntry((uint8_t *)slots, sizeof(slots), base, base, base + stride,
			                                       &allocated) ||
			    allocated.carry ||
			    !SlipTrackWorld_AllocSlotListEntry((uint8_t *)slots, sizeof(slots), base, base, base + stride,
			                                       &allocated) ||
			    !allocated.carry)
				return 4;
		}
		puts("track_slot_lifecycle free_allocate_ring_links_clear_fields_exhaustion=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-orthographic-bounds") == 0) {
		const uint8_t list[16] = {1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0xff, 0xff};
		const SlipView3DMatrix matrix = {{0, 0, 0x4000, 0, 0, 0, 0, 0, 0}};
		SlipTrackWorldPrimitiveBoundsCall calls[1];
		SlipTrackWorldPrimitiveBoundsVisit visits[1];
		size_t visitCount;
		{
			SlipTrackWorldPrimitiveBoundsEvaluatedVisit evaluated[1];
			SlipTrackWorldPrimitiveBoundsEvaluated result;
			if (!SlipTrackWorld_PrimitiveBoundsEvaluated(
			        list, sizeof(list), 0, 0, 0, 0, 0, 1, 0, 0, (SlipView3DVec32){0}, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			        calls, 1, visits, 1, &visitCount, evaluated, 1, 0, 0, 0, 0, 0, 0, &matrix, &result) ||
			    !evaluated[0].calledPlaneClassify || !evaluated[0].plane.carry || evaluated[0].callDraw3DPrimitivePath)
				return 4;
		}
		{
			SlipTrackWorldPrimitiveBoundsEvaluatedExecuteVisit evaluated[1];
			SlipTrackWorldPrimitiveBoundsEvaluatedExecute result;
			if (!SlipTrackWorld_PrimitiveBoundsEvaluatedExecute(
			        list, sizeof(list), 0, 0, 0, 0, 0, 1, 0, 0, (SlipView3DVec32){0}, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			        0, 0, 0, 0, 0, 0, 0, calls, 1, visits, 1, &visitCount, evaluated, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			        0, 0, 0, 0, &matrix, &result) ||
			    !evaluated[0].calledPlaneClassify || !evaluated[0].plane.carry || evaluated[0].callDraw3DPrimitivePath)
				return 4;
		}
		puts("orthographic_bounds both_adapters_cull_without_vertex_lookup=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-sprite-affine") == 0) {
		uint8_t payload[20] = {2, 0, 2, 0};
		RasterAffineScanlineLoopVisit visits[16];
		Raster_SetScreenBufferRows(g_framebuffer, SLIPSTREAM_SCREEN_WIDTH);
		for (unsigned pass = 0; pass < 3; ++pass) {
			RasterTexturedPoint points[4] = {{.x = 10, .y = 14, .u = 0, .v = 0x4000},
			                                 {.x = 10, .y = 10},
			                                 {.x = 14, .y = 10, .u = 0x4000},
			                                 {.x = 14, .y = 14, .u = 0x4000, .v = 0x4000}};
			if (pass == 1)
				for (unsigned i = 0; i < 4; ++i)
					points[i].y = 10;
			memset(payload + 16, pass == 2 ? 0 : 0x5a, 4);
			memset(g_framebuffer, 0x11, sizeof(g_framebuffer));
			size_t written;
			if (!Raster_DrawAffineTexturedPolygon(payload, sizeof(payload), points, 4, 0, visits, 16, &written))
				return 4;
			size_t observed = 0;
			for (int y = 0; y < SLIPSTREAM_SCREEN_HEIGHT; ++y)
				for (int x = 0; x < SLIPSTREAM_SCREEN_WIDTH; ++x) {
					const uint8_t value = g_framebuffer[y * SLIPSTREAM_SCREEN_WIDTH + x];
					if (value == 0x11)
						continue;
					if (value != 0x5a || x < 10 || x > 14 || y < 10 || y > (pass == 1 ? 10 : 14) || pass == 2)
						return 4;
					++observed;
				}
			if (observed != written || observed != (pass == 2 ? 0u : pass == 1 ? 5u : 25u))
				return 4;
		}
		puts("sprite_affine quad_horizontal_transparency=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-sprite-polygon") == 0) {
		SlipDraw3DRecordPool pool;
		SlipDraw3DRecordPoolInit init;
		SlipDraw3DProjectState state;
		SlipDraw3DClipFlagVisit flagVisits[64];
		SlipDraw3DPostPlaneBoundsVisit boundsVisits[64];
		SlipDraw3DPostPlaneClipRecordVisit recordVisits[64];
		SlipDraw3DPostPlaneClipPlaneVisit planeVisits[64];
		SlipDraw3DSpritePolygon result;
		const SlipDraw3DTextureCoordinates uv[4] = {{0, 0}, {0, 65535}, {65535, 65535}, {65535, 0}};
		for (unsigned pass = 0; pass < 3; ++pass) {
			SlipDraw3DVec32 vertices[4] = {{-10, -10, 100}, {-10, 10, 100}, {10, 10, 100}, {10, -10, 100}};
			const SlipDraw3DVec32 *points[4] = {vertices, vertices + 1, vertices + 2, vertices + 3};
			if (!SlipDraw3D_InitRecordPool(&pool, &init))
				return 4;
			SlipDraw3D_InitDefaultProjectState(&state);
			SlipDraw3D_SetViewport(&state, 0, 0, 100, 100, 50, 50);
			SlipDraw3D_SetProjectionMode(&state, 1);
			state.minZ = 1;
			state.maxZ = 1000;
			state.renderFlags = pass == 2 ? 4 : 0;
			state.auxiliaryClipPlaneOrigin = (SlipDraw3DVec32){0};
			state.auxiliaryClipPlaneNormal = (SlipDraw3DVec32){0x4000, 0, 0};
			if (pass == 1)
				for (unsigned i = 0; i < 4; ++i)
					vertices[i].x += 200;
			if (!SlipDraw3D_SpritePolygon(&pool, points, uv, 4, 17, &state, 0, NULL, 0, 0, 0, 100, 0, 100, 128,
			                              flagVisits, 64, boundsVisits, 64, recordVisits, 64, planeVisits, 64,
			                              &result) ||
			    result.carryOut != (pass == 1) || result.calledClipDepth != (pass == 2) ||
			    result.calledClipScreen != (pass != 1) || result.mode != 2 || result.drawMode != 3 ||
			    result.textureHandle != 17)
				return 4;
			uint32_t offset = pool.inputActiveHeadOffset;
			unsigned count = 0;
			do {
				if (offset % SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE != 0 ||
				    offset / SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE >= SLIP_DRAW3D_RECORD_POOL_TOTAL_COUNT || count >= 8)
					return 4;
				SlipDraw3DLinkedDrawRecord *const record = &pool.records[offset / SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE];
				const uint32_t next = record->links.nextOffset;
				if (next / SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE >= SLIP_DRAW3D_RECORD_POOL_TOTAL_COUNT ||
				    pool.records[next / SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE].links.prevOffset != offset ||
				    (pass == 0 &&
				     (record->drawRecord.textureU != uv[count].u || record->drawRecord.textureV != uv[count].v)) ||
				    (pass == 2 && record->drawRecord.world.x < 0))
					return 4;
				offset = next;
				++count;
			} while (offset != pool.inputActiveHeadOffset);
			if (count != 4)
				return 4;
		}
		puts("sprite_polygon ring_uv_common_reject_auxiliary_clip=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-point-draw") == 0) {
		SlipDraw3DProjectState state;
		SlipDraw3D_InitDefaultProjectState(&state);
		SlipDraw3D_SetViewport(&state, 5, 5, 30, 30, 0, 0);
		SlipDraw3D_SetMinimumDepth(1);
		SlipDraw3D_SetMaximumDepth(1000);
		state.projectMask = SlipDebug_PointMask;
		state.projectPrimary = SlipDebug_PointProject;
		Raster_SetScreenBufferRows(g_framebuffer, SLIPSTREAM_SCREEN_WIDTH);
		Raster_SetClipRect(0, 0, 319, 199);
		memset(g_framebuffer, 0, sizeof(g_framebuffer));
		SlipDraw3D_DrawPoint((SlipDraw3DVec32){10, 20, 0}, 0x5a, &state);
		SlipDraw3D_DrawPoint((SlipDraw3DVec32){10, 20, 1001}, 0x5a, &state);
		if (SlipDebug_pointClassifications || SlipDebug_pointProjections)
			return 4;
		SlipDebug_pointMask = 1;
		SlipDraw3D_DrawPoint((SlipDraw3DVec32){10, 20, 1}, 0x5a, &state);
		if (SlipDebug_pointClassifications != 1 || SlipDebug_pointProjections)
			return 4;
		SlipDebug_pointMask = 0x10000;
		SlipDraw3D_DrawPoint((SlipDraw3DVec32){10, 20, 1000}, 0xab5a, &state);
		if (SlipDebug_pointClassifications != 2 || SlipDebug_pointProjections != 1 ||
		    g_framebuffer[20 * SLIPSTREAM_SCREEN_WIDTH + 10] != 0x5a)
			return 4;
		memset(g_framebuffer, 0, sizeof(g_framebuffer));
		const SlipDraw3DVec32 rejected[] = {{65546, 20, 10}, {10, 65556, 10}, {4, 10, 10},
		                                    {31, 10, 10},    {10, 4, 10},     {10, 31, 10}};
		for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i)
			SlipDraw3D_DrawPoint(rejected[i], 0x5a, &state);
		for (size_t i = 0; i < sizeof(g_framebuffer); ++i)
			if (g_framebuffer[i])
				return 4;
		for (unsigned corner = 0; corner < 4; ++corner)
			SlipDraw3D_DrawPoint((SlipDraw3DVec32){corner & 1 ? 30 : 5, corner & 2 ? 30 : 5, 10}, 0x5a, &state);
		if (g_framebuffer[5 * 320 + 5] != 0x5a || g_framebuffer[5 * 320 + 30] != 0x5a ||
		    g_framebuffer[30 * 320 + 5] != 0x5a || g_framebuffer[30 * 320 + 30] != 0x5a)
			return 4;
		Raster_SetClipRect(11, 0, 319, 199);
		SlipDraw3D_DrawPoint((SlipDraw3DVec32){10, 20, 10}, 0x5a, &state);
		if (g_framebuffer[20 * 320 + 10])
			return 4;
		puts("point_draw depth_dispatch_mask_width_coordinate_width_inclusive_edges_raster_clip=passed");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--verify-sprite-point") == 0) {
		SlipDraw3DProjectState state;
		SlipDraw3DDrawRecord record = {0};
		SlipDraw3D_InitDefaultProjectState(&state);
		SlipDraw3D_SetViewport(&state, 0, 0, 100, 100, 0, 0);
		state.minZ = 1;
		state.maxZ = 1000;
		/* Distinct real projectors make the indirect-call selection observable. */
		state.projectPrimary = SlipDraw3D_ProjectOrthographicCallback;
		state.projectSecondary = SlipDraw3D_ProjectPerspective32Callback;
		state.modeOneReciprocal = 0x40000000;

		SlipDraw3D_SetMinimumDepth(1);
		SlipDraw3D_SetMaximumDepth(1000);
		int32_t projectedX = 123, projectedY = 456;
		if (!SlipDraw3D_ProjectVisiblePoint((SlipDraw3DVec32){10, -20, 512}, &state, &projectedX, &projectedY) ||
		    projectedX != 10 || projectedY != 20)
			return 4;
		projectedX = 123;
		projectedY = 456;
		if (SlipDraw3D_ProjectVisiblePoint((SlipDraw3DVec32){10, -20, 0}, &state, &projectedX, &projectedY) ||
		    projectedX != 123 || projectedY != 456)
			return 4;
		for (unsigned alternate = 0; alternate < 2; ++alternate) {
			state.renderFlags = alternate ? 8 : 0;
			record.depth = 12345;
			if (SlipDraw3D_ProjectDrawRecordPoint(&record, (SlipDraw3DVec32){10, -20, 512}, &state) != 2 ||
			    record.screenX != (alternate ? 5 : 10) || record.screenY != (alternate ? 10 : 20) ||
			    record.depth != 12345)
				return 4;
		}
		state.renderFlags = 0;
		for (unsigned far = 0; far < 2; ++far) {
			record.screenX = 123;
			record.screenY = 456;
			if (SlipDraw3D_ProjectDrawRecordPoint(&record, (SlipDraw3DVec32){0, 0, far ? 1001 : 0}, &state) !=
			        (far ? 0x101u : 0x81u) ||
			    record.screenX != 123 || record.screenY != 456)
				return 4;
		}
		for (unsigned edge = 0; edge < 2; ++edge) {
			if (SlipDraw3D_ProjectDrawRecordPoint(&record, (SlipDraw3DVec32){0x3ffd + (int32_t)edge, 0, 512}, &state) !=
			    (edge ? 0x12u : 0x212u))
				return 4;
		}
		state.renderFlags = 4;
		state.auxiliaryClipPlaneOrigin = (SlipDraw3DVec32){0, 0, 0};
		state.auxiliaryClipPlaneNormal = (SlipDraw3DVec32){1, 0, 0};
		for (unsigned negative = 0; negative < 2; ++negative) {
			const int32_t x = negative ? -8193 : -8192;
			record.screenX = 123;
			const uint32_t flags = SlipDraw3D_ProjectDrawRecordPoint(&record, (SlipDraw3DVec32){x, 0, 512}, &state);
			if (record.depth != (negative ? -1 : 0) || flags != (negative ? 0x2001u : 0x20au) ||
			    (negative && record.screenX != 123))
				return 4;
		}
		state.auxiliaryClipPlaneOrigin.x = -1;
		state.auxiliaryClipPlaneNormal.x = 0x4000;
		if (SlipDraw3D_ProjectDrawRecordPoint(&record, (SlipDraw3DVec32){INT32_MAX, 0, 512}, &state) != 0x2001 ||
		    record.depth != INT32_MIN || record.world.x != INT32_MAX)
			return 4;
		puts("sprite_point projection_dispatch_depth_auxiliary_rounding_wrap_screen_flags=passed");
		return 0;
	}

	if (argc == 2 && strcmp(argv[1], "--verify-bsp-plane") == 0) {
		SlipDraw3DVertexRecord vertices[2] = {0};
		TrackViewChunkCallbackContext context = {0};
		SlipTrackWorldPlaneClassify result;
		vertices[1].sourceX = 1;
		vertices[1].sourceY = -1;
		vertices[1].sourceZ = 3;
		context.currentChunkOrigin = (SlipView3DVec32){INT32_MAX - 64, 0, 0};
		for (unsigned test = 0; test < 3; ++test) {
			SlipView3DVec32 origin = {test == 0 ? 0 : -1, 0, 0};
			const uint16_t normal = test == 2 ? (uint16_t)-1 : 1;
			if (!SlipTrackWorld_ClassifyPlaneFromSource(0, 1, normal, 0, 0, (const uint8_t *)(const void *)vertices,
			                                            sizeof(vertices), origin, TrackView_ChunkSourcePoint, &context,
			                                            NULL, &result) ||
			    result.sourceX != 1 || result.sourceY != -1 || result.sourceZ != 3 ||
			    result.pointMinusOrigin.x != (test == 0 ? INT32_MAX : INT32_MIN) ||
			    result.dotProduct != (test == 0   ? INT64_C(2147483647)
			                          : test == 1 ? -INT64_C(2147483648)
			                                      : INT64_C(2147483648)) ||
			    result.carry != (test != 1))
				return 4;
		}
		{
			SlipView3DMatrix matrix = {{0}};
			const int16_t coefficients[] = {0x4000, 0x4000, 0x4000, 1, INT16_MIN, INT16_MAX};
			const int16_t normals[] = {1, -1, 0, 1, INT16_MIN, INT16_MIN};
			const bool carries[] = {true, false, false, false, false, true};
			for (size_t i = 0; i < sizeof(normals) / sizeof(normals[0]); ++i) {
				matrix.m[2] = coefficients[i];
				if (!SlipTrackWorld_ClassifyPlaneFromSource(1, UINT16_MAX, (uint16_t)normals[i], 0, 0, NULL, 0,
				                                            (SlipView3DVec32){0}, NULL, NULL, &matrix, &result) ||
				    result.callTransformSourcePoint || result.carry != carries[i])
					return 4;
			}
			matrix.m[2] = 0;
			matrix.m[5] = 0x4000;
			matrix.m[8] = -0x4000;
			if (!SlipTrackWorld_ClassifyPlaneFromSource(1, UINT16_MAX, 0, 2, 1, NULL, 0, (SlipView3DVec32){0}, NULL,
			                                            NULL, &matrix, &result) ||
			    !result.carry)
				return 4;
		}
		puts("bsp_plane typed_source_dword_wrap_signed_dot_orthographic_low_word=passed");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--verify-projection-dispatch") == 0) {
		SlipDraw3DProjectState state;
		const SlipDraw3DVec32 point = {64, 32, 1024};
		int32_t x, y;
		SlipDraw3D_InitDefaultProjectState(&state);
		for (unsigned step = 0; step < 3; ++step) {
			const uint32_t scale = step == 1 ? 0x40000 : 0x10000;
			SlipDraw3D_SetProjectionScale(&state, scale);
			if (state.projectPrimary != (step == 1 ? SlipDraw3D_ProjectCheckedPerspectiveCallback
			                                       : SlipDraw3D_ProjectPerspective32Callback) ||
			    state.projectSecondary != (step == 1 ? SlipDraw3D_ProjectCheckedPerspectiveCallback
			                                         : SlipDraw3D_ProjectPerspective16Callback))
				return 4;
			state.projectPrimary(point, &x, &y, &state);
			if (x != (step == 1 ? 224 : 176) || y != (step == 1 ? 68 : 92))
				return 4;
			state.projectSecondary(point, &x, &y, &state);
			if (x != (step == 1 ? 224 : 176) || y != (step == 1 ? 68 : 92))
				return 4;
		}
		SlipDraw3D_SetProjectionMode(&state, 1);
		if (state.projectPrimary != SlipDraw3D_ProjectOrthographicCallback ||
		    state.projectSecondary != SlipDraw3D_ProjectOrthographicCallback)
			return 4;
		state.projectPrimary(point, &x, &y, &state);
		if (x != 224 || y != 68)
			return 4;
		printf("projection_dispatch normal_zoom_restore_orthographic=passed\n");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--verify-track-visibility") == 0)
		return SlipDebug_VerifyVisibility();
	if (argc == 2 && strcmp(argv[1], "--verify-split-selection") == 0) {
		bool passed = SlipMenu_DebugAcceptSplitDrivers();
		printf("split_selection local_racer_assignment=%s\n", passed ? "passed" : "FAILED");
		return passed ? 0 : 4;
	}
	if (argc == 3 && strcmp(argv[1], "--verify-split-garage") == 0) {
		SlipResourceHost_Initialize(SLIP_DEBUG_RESOURCE_CAPACITY_BYTES);
		SlipResourceHost_OpenArchives(argv[2], NULL);

		if (!SlipResourceHost_Load(NULL, "SMALL.FNT", &SlipMenu_resources.smallFont) ||
		    !SlipResourceHost_Load(NULL, "SHADE.FNT", &SlipMenu_resources.shadedFont) ||
		    !SlipResourceHost_Load(NULL, "SMALLEST.FNT", &SlipMenu_resources.smallestFont))
			return 4;
		SlipScreenHost_Initialize();
		Raster_SetScreenBufferRows(g_framebuffer, 320);
		bool passed = SlipMenu_DebugSplitGarage(argv[2]);
		printf("split_garage independent_purchases_survive_race_start=%s\n", passed ? "passed" : "FAILED");
		return passed ? 0 : 4;
	}
	if (argc == 3 && strcmp(argv[1], "--capture-campaign") == 0) {
		if (!SDL_Init(SDL_INIT_EVENTS))
			return 4;
		SlipVgaDac_InitializeHostBiosDefaults();
		SlipFrameTimer_InitializeHostRate(SLIP_FRAME_TIMER_MAXIMUM_RATE_HZ);
		presenterFast = true;
		SlipConfigHost_Initialize(argv[2]);
		SlipMenu_Init(argv[2], NULL, NULL, SlipDebug_PresenterFrame, NULL);
		/* Fixture starts at driver acceptance; retain the palette installed
		 * by the skipped driver screen,
		 * including reserved entry 248. */
		SlipResourcePayload driverPalette = {0};
		SlipSprite driverSprite;
		if (!SlipResource_LoadByName(&argv[2], 1, "DRIVER0.SPR", &driverPalette) ||
		    !SlipSprite_FromPayload(&driverPalette, &driverSprite))
			return 4;
		SlipSprite_ApplyPalette(&driverSprite);
		SlipVgaDac_Commit();
		SlipResource_ReleaseHandle(&driverPalette);
		SlipDebug_fixedClock = true;
		campaignStream = true;
		SlipRandom_SetState(1, 1);
		bool garage = SlipMenu_DebugAcceptChampionship();
		printf("campaign_capture garage=%u frames=%u stage=%u\n", garage, presenterFrames, campaignStage);
		SDL_Quit();
		return garage && campaignStage == 3 ? 0 : 4;
	}
	if ((argc == 4 || argc == 5) && strcmp(argv[1], "--verify-campaign-accept") == 0) {
		if (!SDL_Init(SDL_INIT_EVENTS))
			return 4;
		SlipVgaDac_InitializeHostBiosDefaults();
		SlipFrameTimer_InitializeHostRate(SLIP_FRAME_TIMER_MAXIMUM_RATE_HZ);
		presenterFast = true;
		presenterCaptureFrame =
		    argc == 5 ? (unsigned)strtoul(argv[4], NULL, 10) : SLIP_DEBUG_PRESENTER_DEFAULT_CAPTURE_FRAME;
		presenterCapturePath = argv[3];
		SlipConfigHost_Initialize(argv[2]);
		SlipMenu_Init(argv[2], NULL, NULL, SlipDebug_PresenterFrame, NULL);
		bool garage = SlipMenu_DebugAcceptChampionship();
		printf("campaign_accept garage=%u frames=%u renderer=%u primitives=%u racers=%u flyby_frames=%u "
		       "failed_frames=%u\n",
		       garage, presenterFrames, SlipRaceSession_lastRenderSucceeded, SlipRaceSession_lastRasterizedPrimitives,
		       SlipRace_racerTable.racerCount, flybyFrames, flybyFailures);
		SDL_Quit();
		return garage && presenterFrames > SLIP_DEBUG_PRESENTER_DEFAULT_CAPTURE_FRAME && flybyFrames > 1 &&
		               flybyFailures == 0 && SlipRaceSession_lastRenderSucceeded
		           ? 0
		           : 4;
	}
	if (argc == 4 && strcmp(argv[1], "--verify-campaign-presenter") == 0) {
		if (!SDL_Init(SDL_INIT_EVENTS))
			return 4;
		SlipVgaDac_InitializeHostBiosDefaults();
		SlipFrameTimer_InitializeHostRate(SLIP_FRAME_TIMER_MAXIMUM_RATE_HZ);
		presenterCapturePath = argv[3];
		SlipConfigHost_Initialize(argv[2]);
		SlipMenu_Init(argv[2], NULL, NULL, SlipDebug_PresenterFrame, NULL);
		for (unsigned after = 0; after < 2; ++after) {
			presenterFrames = 0;
			SlipMenu_CampaignPresenter(6, after);
			printf("presenter after=%u frames=%u\n", after, presenterFrames);
			presenterCapturePath = NULL;
		}
		SDL_Quit();
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--verify-monitor-border") == 0) {
		Raster_SetScreenBufferRows(g_framebuffer, SLIPSTREAM_SCREEN_WIDTH);
		for (unsigned outline = 0; outline < 2; ++outline) {
			memset(g_framebuffer, 0x5a, sizeof(g_framebuffer));
			Raster_FillRectUnchecked(outline ? RASTER_RECTANGLE_OUTLINE_FLAG : 0u, 210, 99, 301, 147);
			for (int y = 0; y < SLIPSTREAM_SCREEN_HEIGHT; ++y) {
				for (int x = 0; x < SLIPSTREAM_SCREEN_WIDTH; ++x) {
					bool inside = x >= 210 && x <= 301 && y >= 99 && y <= 147;
					bool edge = x == 210 || x == 301 || y == 99 || y == 147;
					const uint8_t expected = inside && (!outline || edge) ? 0 : 0x5a;
					if (g_framebuffer[y * SLIPSTREAM_SCREEN_WIDTH + x] != expected)
						return 4;
				}
			}
		}
		printf("monitor rectangle fill_and_outline=passed\n");
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "--verify-camera-positions") == 0) {
		SlipRaceCameraState state = {0};
		const char *archive = argv[2];
		for (uint16_t track = 1; track <= SLIP_RACE_TRACK_COUNT; ++track) {
			unsigned active = 0;
			if (!SlipRaceCamera_LoadPositions(&state, &archive, 1, track))
				return 4;
			for (size_t i = 0; i < SLIP_RACE_TV_POSITION_COUNT; ++i)
				if (state.tvPositions[i].x != -1)
					++active;
			printf("camera_positions track=%u active=%u\n", (unsigned)track, active);
			if (active == 0)
				return 4;
		}
		return 0;
	}
	const int gpuResult = SlipDebug_VerifyRaceGpu(argc, argv);
	if (gpuResult >= 0)
		return gpuResult;
	const int cameraResult = SlipDebug_VerifyCameraKeys(argc, argv);
	if (cameraResult >= 0)
		return cameraResult;
	const int result = SlipDebug_VerifySpeedHud(argc, argv);
	return result >= 0 ? result : SlipDebug_VerifyRaceRender(argc, argv);
}
