#include "race_session.h"
#include "actor_resources.h"
#include "byte_order.h"
#include "draw_list_host.h"
#include "game_errors.h"
#include "guided_projectile_creation.h"
#include "material_host.h"
#include "menu_resources.h"
#include "race_voice.h"
#include "race_voice_host.h"
#include "renderer_allocation.h"
#include "renderer_host.h"
#include "renderer_lifecycle.h"
#include "renderer_state.h"
#include "resource_host.h"
#include "resource_storage.h"
#include "vga_dac.h"

#include "artic_slot.h"
#include "config_menu_host.h"
#include "config_settings.h"
#include "cross_effects.h"
#include "draw3d.h"
#include "font.h"
#include "frame_timer.h"
#include "input.h"
#include "menu.h"
#include "menu_music.h"
#include "port_app_bridge.h"
#include "race_bonus.h"
#include "race_camera.h"
#include "race_collision.h"
#include "race_collision_host.h"
#include "race_drone.h"
#include "race_effects.h"
#include "race_hud.h"
#include "race_intro.h"
#include "race_map.h"
#include "race_physics.h"
#include "race_recording.h"
#include "race_recording_host.h"
#include "resource.h"
#include "runtime.h"
#include "shape_effects.h"
#include "string_table.h"
#include "text_layout.h"
#include "track_assets.h"
#include "track_view_render.h"
#include "track_world.h"
#include "view3d.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const SlipTrackAssetCalls SlipRaceSession_trackAssetCalls = {.load = SlipResourceHost_Load,
                                                                    .find = SlipResourceHost_Find,
                                                                    .lock = SlipResourceHost_Lock,
                                                                    .payload = SlipResourceHost_Payload,
                                                                    .unlock = SlipResourceHost_Unlock,
                                                                    .release = SlipResourceHost_Release};

static void SlipRaceSession_RegisterArticExit(void *context, void (*cleanup)(void)) {
	(void)context;
	SlipRuntime_RegisterExit(cleanup);
}

static const SlipArticSlotResourceCalls SlipRaceSession_articResourceCalls = {.allocate = SlipResourceHost_Allocate,
                                                                              .lock = SlipResourceHost_Lock,
                                                                              .payload = SlipResourceHost_Payload,
                                                                              .unlock = SlipResourceHost_Unlock,
                                                                              .release = SlipResourceHost_Release,
                                                                              .registerExit =
                                                                                  SlipRaceSession_RegisterArticExit};

enum {
	SLIP_RACE_TEXTURE_MEMORY_RESERVE = 0x8000u,
	SLIP_INTRO_TEXTURE_MEMORY_RESERVE = 0x10000u,
	SLIP_RACE_MAXIMUM_TEXTURE_BYTES = 0x30d40u,
	SLIP_RACE_MAXIMUM_TEXTURE_FRAME = 3u
};

uint16_t SlipRaceSession_track;
uint16_t SlipRaceSession_exitRequested;
static bool SlipRaceSession_replay;
static SlipRaceRecordingStart SlipRaceSession_recordingStart;

static uint32_t SlipRaceSession_RecordingClockHost(void *context) {
	(void)context;
	return (uint32_t)SlipSdl_TicksMs();
}

static void SlipRaceSession_RecordingCallback(void *context) { (void)context; }

const SlipRaceRecordingHost SlipRaceSession_recordingHost = {
    SlipRaceSession_RecordingClockHost,
    SlipRaceSession_RecordingCallback,
    SlipRaceSession_RecordingCallback,
    NULL,
    .resources = &SlipRaceRecordingHost_resources,
};
bool SlipRaceSession_lastRenderSucceeded;
uint32_t SlipRaceSession_lastRawBspCallbacks;
uint32_t SlipRaceSession_lastRasterizedPrimitives;
/* Post-frame diagnostic copy for non-invasive debugger inspection.  Only
 * scalar state and counters are inspected after the frame; copied pointers
 * to frame-local storage are deliberately never dereferenced. */
TrackViewRawBspContext SlipRaceSession_lastTrackViewContext;
static uint32_t SlipRaceSession_currentTrackRecordAddress;
static uint32_t SlipRaceSession_slotDrawBaseAddress;
static uint32_t SlipRaceSession_slotDrawFreeHeadAddress;
static uint16_t SlipRaceSession_trackWorldInitialized;
static uint16_t SlipRaceSession_trackDrawInitialized;
static uint16_t SlipRaceSession_slotDrawResource, SlipRaceSession_slotListResource;
static uint16_t SlipRaceSession_cellResource, SlipRaceSession_beamResource;
static uint16_t SlipRaceSession_axisRampResource, SlipRaceSession_axisTestResource;
static uint16_t SlipRaceSession_objectListResource, SlipRaceSession_deferredListResource,
    SlipRaceSession_deferredScanResource;
static uint32_t SlipRaceSession_slotListBaseAddress;
static uint32_t SlipRaceSession_slotListFreeHeadAddress;

static SlipObject SlipRaceSession_objectTableHost[SLIP_OBJECT_COUNT];
static SlipRacePlayerPrivateRecord SlipRaceSession_playerStates[100];
static SlipRaceBonusHostBindings SlipRaceSession_bonusBindings;
static SlipRaceDroneHostBindings SlipRaceSession_droneBindings;
static SlipRaceRacerState *SlipRaceSession_racerStates;

static uint16_t SlipRaceSession_slotDrawCount;
static uint8_t *SlipRaceSession_slotDrawHost;
static SlipObjectDrawCallback SlipRaceSession_slotDrawCallbacks[SLIP_TRACK_SLOT_DRAW_RECORD_COUNT];
static SlipTrackSlotRecord *SlipRaceSession_trackSlots;
static uint8_t *SlipRaceSession_cellTableHost;

static SlipTrackWorldCellTableVisit SlipRaceSession_cellVisitsHost[512];
static SlipArticSlotPool SlipRaceSession_articPool;
static SlipTrackAssetBundle SlipRaceSession_trackBundle;
SlipView3DMaths SlipRaceSession_maths;
static SlipResourcePayload SlipRaceSession_racerArtPayload[10];
static SlipResourcePayload SlipRaceSession_droneArtPayload;
static uint16_t SlipRaceSession_droneArtHandle;
static uint16_t SlipRaceSession_racerArtHandles[10];
static uint32_t SlipRaceSession_effectsInitialized;
static uint16_t SlipRaceSession_weaponShapeHandles[7];
static const TrackViewImpactSprites SlipRaceSession_impactSprites = {4, SlipRaceEffects_fireHandles};
static TrackViewResourceHandleRegistry SlipRaceSession_resourceRegistry;
static char SlipRaceSession_archivePathHost[2][512];
static const char *SlipRaceSession_archivesHost[2];
static size_t SlipRaceSession_archiveCountHost;
static SlipSoundEffectsState SlipRaceSession_soundEffects;
static SlipGameSoundState *SlipRaceSession_gameSound;

enum {
	SLIP_RACE_SPEECH_BANK = 3,
	SLIP_RACE_SPEECH_PRELOAD = 0,
	SLIP_RACE_SPEECH_SUPPRESS_RECENT = 1,
	SLIP_RACE_SPEECH_RESERVED_BYTES = 0x108000,
	SLIP_RACE_PORTRAIT_FIRST_INDEX = 0,
	SLIP_RACE_PORTRAIT_COUNT = 10,
	SLIP_RACE_PORTRAIT_LEFT = 260,
	SLIP_RACE_PORTRAIT_RIGHT = 311,
	SLIP_RACE_PORTRAIT_TOP = 40,
	SLIP_RACE_PORTRAIT_WINDOW_OFFSET = 24,
	SLIP_RACE_PORTRAIT_LABEL_OFFSET = 39,
	SLIP_RACE_PORTRAIT_LABEL_COLOR = 254,
	SLIP_RACE_PORTRAIT_LABEL_CENTERED = 2,
	SLIP_RACE_PORTRAIT_LABEL_FONT_SPACING = UINT16_MAX
};

static uint16_t SlipRaceSession_portraits[SLIP_RACE_PORTRAIT_COUNT];
static uint16_t SlipRaceSession_soundSet;
static SlipSoundEffectLock SlipRaceSession_lockSound;
static SlipSoundEffectUnlock SlipRaceSession_unlockSound;
static void *SlipRaceSession_soundContext;
static uint8_t *SlipRaceSession_materialTable;
static size_t SlipRaceSession_materialTableBytes;
static uint16_t SlipRaceSession_materialGlobal;
static SlipRaceTrackMaterialGlobals SlipRaceSession_materialGlobals;
static TrackViewMaterialInit SlipRaceSession_materialInit;
static SlipRaceTrackFrameCallback SlipRaceSession_frameCallback;
static TrackViewTrackLifecycleCallback SlipRaceSession_cleanupCallback;
static TrackViewCloudState SlipRaceSession_cloudState;
static uint32_t SlipRaceSession_renderMode;
static uint32_t SlipRaceSession_farTextureDepth;
static int32_t SlipRaceSession_componentDistance;
static int32_t SlipRaceSession_componentRadius;
static int32_t SlipRaceSession_detailThreshold;
static uint32_t SlipRaceSession_detailDistance;
static uint32_t SlipRaceSession_primitiveFlagsReady;
static uint32_t SlipRaceSession_underSeaColor;
static uint32_t SlipRaceSession_shading;
static uint32_t SlipRaceSession_shadingSecondary;
static uint32_t SlipRaceSession_textureMode;
static uint32_t SlipRaceSession_shadows;

static const uint32_t SlipRaceSession_mapCenters[11] = {
    0x00000000u, 0x00320046u, 0x00380046u, 0x00380046u, 0x003a0046u, 0x003c0046u,
    0x00380046u, 0x00380046u, 0x00380046u, 0x00380046u, 0x0038005au,
};

static const int32_t SlipRaceSession_mapScales[11] = {
    0x00000000, 0x01600000, 0x01600000, 0x01600000, 0x01600000, 0x01800000,
    0x01600000, 0x01600000, 0x01c00000, 0x01700000, 0x01800000,
};

typedef struct SlipRacePauseRect {
	int16_t minX;
	int16_t minY;
	int16_t maxX;
	int16_t maxY;
} SlipRacePauseRect;

typedef struct SlipRacePauseNavigation {
	uint16_t itemCount;
	uint16_t currentItem;
	int16_t up[4];
	int16_t down[4];
	int16_t left[4];
	int16_t right[4];
	int16_t centers[4][2];
} SlipRacePauseNavigation;

typedef enum SlipRacePauseState {
	SLIP_RACE_PAUSE_RUNNING,
	SLIP_RACE_PAUSE_LOCAL,
	SLIP_RACE_PAUSE_REMOTE
} SlipRacePauseState;

typedef enum SlipRacePauseAction {
	SLIP_RACE_PAUSE_ACTION_NONE,
	SLIP_RACE_PAUSE_ACTION_CONTINUE,
	SLIP_RACE_PAUSE_ACTION_CONFIGURATION,
	SLIP_RACE_PAUSE_ACTION_QUIT_RACE,
	SLIP_RACE_PAUSE_ACTION_EXIT_TO_DOS,
	SLIP_RACE_PAUSE_ACTION_REPLAY_END
} SlipRacePauseAction;

static const SlipRacePauseRect SlipRaceSession_remotePauseRect = {101, 64, 220, 96};
static const SlipRacePauseRect SlipRaceSession_pauseRects[4] = {
    {101, 46, 220, 60}, {101, 64, 220, 78}, {101, 83, 220, 96}, {101, 100, 220, 114}};

static SlipRacePauseNavigation SlipRaceSession_pauseNavigation = {4,
                                                                  0,
                                                                  {-1, 0, 1, 2},
                                                                  {1, 2, 3, -1},
                                                                  {-1, -1, -1, -1},
                                                                  {-1, -1, -1, -1},
                                                                  {{160, 53}, {160, 71}, {160, 89}, {160, 107}}};

enum {

	SLIP_RACE_STATE_RECORD_COUNT = 0x20,

	SLIP_RACE_VERTEX_CAPACITY = 0x190,
	SLIP_RACE_TRAVERSAL_CAPACITY = 4096
};

static SlipView3DMatrix SlipRaceSession_cameraWorldMatrix;
static SlipView3DMatrix SlipRaceSession_cameraViewMatrix;
static SlipDraw3DStateRecord *SlipRaceSession_drawStates;
static uint8_t SlipRaceSession_materialBackgroundStrips[0x400];
static uint8_t SlipRaceSession_builtBackgroundStrips[0x400];
static uint8_t *SlipRaceSession_axisRamps;
static uint8_t *SlipRaceSession_axisTests;
static uint8_t *SlipRaceSession_objectList;
static uint8_t *SlipRaceSession_deferredList;
static uint8_t *SlipRaceSession_deferredScan;
static SlipDraw3DVertexRecord *SlipRaceSession_vertices;
static SlipTrackWorldTraversalVisit SlipRaceSession_traversalVisits[SLIP_RACE_TRAVERSAL_CAPACITY];
static uint32_t SlipRaceSession_traversalOffsets[SLIP_RACE_TRAVERSAL_CAPACITY];
static uint16_t SlipRaceSession_traversalDepths[SLIP_RACE_TRAVERSAL_CAPACITY];
static SlipTrackWorldDosAddressMap SlipRaceSession_entryMap[0x300];
static SlipTrackWorldDosAddressMap SlipRaceSession_recordMap[0x10000];
static SlipTrackWorldDeferredListDirectExecuteVisit SlipRaceSession_deferredVisits[4096];
static SlipTrackWorldAxisTestVisit SlipRaceSession_axisVisits[33];
static SlipTrackWorldSlotDrawClearVisit SlipRaceSession_slotClearVisits[SLIP_TRACK_SLOT_DRAW_RECORD_COUNT - 1];
static SlipDraw3DReturnActiveVisit SlipRaceSession_backgroundReturns[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];

static SlipDraw3DPointPointerRingVisit SlipRaceSession_backgroundPoints[2u * 64u * 4u];
static SlipDraw3DStripDispatchVisit SlipRaceSession_backgroundStrips[2u * 64u];
static SlipDraw3DBackgroundStripTableVisitFixed SlipRaceSession_backgroundTableA[64];
static SlipDraw3DBackgroundStripTableVisitMaterial SlipRaceSession_backgroundTableB[64];
static SlipRacePlayerHostBindings SlipRaceSession_playerContext;
static SlipRaceCameraState SlipRaceSession_cameraState = {
    .externalMatrix = {{0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000}}, .externalDistance = 0x4c40};
static SlipRaceHudAssets SlipRaceSession_hudAssets;
SlipRaceHudState SlipRaceSession_hudState;
static SlipStringTableSlot *SlipRaceSession_pauseStrings;

enum { SLIP_RACE_PAUSE_FIRST_OPTION_TAG = 0x4f505431, SLIP_RACE_PAUSE_REMOTE_TAG = 0x4f505231 };

static const SlipStringTableResources SlipRaceSession_pauseStringResources = {.load = SlipResourceHost_Load,
                                                                              .lock = SlipResourceHost_Lock,
                                                                              .unlock = SlipResourceHost_Unlock,
                                                                              .release = SlipResourceHost_Release};
static bool SlipRaceSession_pauseAssetsReady;
static uint16_t SlipRaceSession_pauseState;
static uint32_t SlipRaceSession_pauseSelection;
static int32_t SlipRaceSession_menuMouseX;
static int32_t SlipRaceSession_menuMouseY;
static uint16_t SlipRaceSession_countdownTimer;
static uint16_t SlipRaceSession_finishDelay;
static bool SlipRaceSession_playerReady;

typedef struct GuidedProjectileHost {
	SlipView3DMatrix shooterMatrix, exhaustedMatrix;
	SlipView3DVec32 position;
	uint16_t shape;
	uint32_t eventCallbackGuestAddress;
	uint32_t drawCallbackGuestAddress;
} GuidedProjectileHost;

static void SlipRaceSession_GuidedProjectileVoice(void *context, uint32_t selection) {
	(void)context;
	SlipRaceVoiceCalls calls = SlipRaceVoiceHost_Calls(SlipRaceSession_gameSound);
	SlipRaceVoice_Play(SlipRaceSession_gameSound->digitalCard, selection, &calls);
}

static int32_t SlipRaceSession_GuidedProjectileSpeed(void *context, uint16_t object) {
	(void)context;
	return SlipObject_Speed(SlipObject_table, object);
}

static SlipView3DVec32 SlipRaceSession_GuidedProjectilePosition(void *context, uint16_t object, uint32_t side) {
	(void)context;
	return SlipRacePlayer_WeaponPosition(object, side);
}

static SlipView3DMatrix *SlipRaceSession_GuidedProjectileMatrix(void *context, uint16_t object) {
	GuidedProjectileHost *const host = context;
	SlipObjectMatrixCopy copied;
	(void)SlipObject_MatrixCopy(SlipObject_table, SLIP_OBJECT_TABLE_DOS_BYTES, object, &host->shooterMatrix, &copied);
	return &host->shooterMatrix;
}

static bool SlipRaceSession_GuidedProjectileFill(void *context, const SlipView3DMatrix **source,
                                                 SlipView3DVec32 position, SlipObjectDrawCallback draw, uint16_t shape,
                                                 SlipObjectEventCallback event, uint16_t *object) {
	GuidedProjectileHost *const host = context;
	host->position = position;
	host->shape = shape;

	if (*source == &host->exhaustedMatrix)
		SlipObject_ExhaustedMatrix(&host->exhaustedMatrix);
	SlipObjectSlotFill filled;
	if (!SlipObject_SlotFill(*source, (uint32_t)position.x, (uint32_t)position.y, (uint32_t)position.z, draw, shape,
	                         event, &filled))
		SlipRuntime_Fatal("Guided projectile object binding failed");
	if (filled.carryOut) {
		*source = &host->exhaustedMatrix;
		return false;
	}
	*object = (uint16_t)filled.objectOffset;
	return true;
}

static bool SlipRaceSession_GuidedProjectileReclaim(void *context) {
	const GuidedProjectileHost *const host = context;
	return SlipRaceEffects_Reclaim(SlipObject_table, SLIP_OBJECT_TABLE_DOS_BYTES, (uint32_t)host->position.x,
	                               (uint32_t)host->position.y, (uint32_t)host->position.z,
	                               host->drawCallbackGuestAddress, host->eventCallbackGuestAddress, host->shape);
}

static void SlipRaceSession_GuidedProjectileFlags(void *context, uint16_t object, uint32_t flags) {
	(void)context;
	SlipObjectActorHandleWriteResult set;
	(void)SlipObject_SetActorHandle(object, flags, &set);
}

static SlipRacePlayerProjectileState *SlipRaceSession_GuidedProjectilePrivate(void *context, uint16_t object) {
	(void)context;
	return (SlipRacePlayerProjectileState *)(void *)SlipObject_PrivateState(object);
}

static bool SlipRaceSession_GuidedProjectileBody(void *context, uint16_t object, uint16_t flags) {
	(void)context;
	/* The collision API returns carry; the creator's binding returns success. */
	return !SlipRaceCollision_CreateBody(object, flags);
}

static void SlipRaceSession_GuidedProjectileShapeBounds(void *context, uint16_t shape, SlipView3DVec32 *minimum,
                                                        SlipView3DVec32 *maximum) {
	(void)context;
	SlipResourcePayload payload;
	if (SlipRaceSession_resourceRegistry.hostResources) {

		const uint8_t *const data = SlipResourceHost_Lock(NULL, shape);
		payload = SlipResourceHost_Payload(shape);
		payload.data = (uint8_t *)data;
	} else if (!TrackView_LoadResourceHandlePayload(&SlipRaceSession_resourceRegistry, shape, &payload))
		SlipRuntime_Fatal("Guided projectile shape binding failed");
	if (payload.data == NULL || payload.size < sizeof(SlipShape3DHeader))
		SlipRuntime_Fatal("Guided projectile shape binding failed");

	const SlipShape3DHeader *const header = (const SlipShape3DHeader *)(const void *)payload.data;
	*minimum = (SlipView3DVec32){header->minimumX, header->minimumY, header->minimumZ};
	*maximum = (SlipView3DVec32){header->maximumX, header->maximumY, header->maximumZ};
	if (SlipRaceSession_resourceRegistry.hostResources)
		SlipResourceHost_Unlock(NULL, shape);
	else
		SlipResource_ReleaseHandle(&payload);
}

static void SlipRaceSession_GuidedProjectileBodyBounds(void *context, uint16_t object, SlipView3DVec32 minimum,
                                                       SlipView3DVec32 maximum) {
	(void)context;
	SlipRaceCollision_SetBodyBounds(object, minimum.x, minimum.y, minimum.z, maximum.x, maximum.y, maximum.z);
}

static bool SlipRaceSession_GuidedProjectileTrack(void *context, uint16_t object, uint16_t shape, uint32_t flags) {
	(void)context;
	(void)shape;
	SlipRacePlayerHostBindings *const player = &SlipRaceSession_playerContext;
	SlipTrackWorldAddSlot added;
	if (!SlipTrackWorld_AddSlot(
	        object, flags, 1, SlipRaceSession_slotDrawHost, (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u),
	        SlipRaceSession_slotDrawCallbacks, SLIP_TRACK_SLOT_DRAW_RECORD_COUNT, SlipRaceSession_slotDrawBaseAddress,
	        SlipRaceSession_slotDrawFreeHeadAddress, player->slotListBase, player->slotListBytes,
	        player->slotListBaseOffset, player->slotListSentinelOffset, SlipRaceSession_slotListFreeHeadAddress,
	        player->objectTable, player->objectTableBytes, player->articSlotPool, player->articSlotPoolBytes,
	        player->articSlotPoolOffset, player->trdBase, player->trackDataSize, player->trackDataOffset,
	        player->componentBase, player->componentBaseBytes, player->componentBaseOffset, player->trackTable,
	        player->trackTableBytes, &added))
		SlipRuntime_Fatal("Guided projectile track binding failed");
	return !added.carryOut;
}

static void SlipRaceSession_GuidedProjectileSetSpeed(void *context, uint16_t object, int32_t speed) {
	(void)context;
	SlipObject_SetSpeed(SlipObject_table, object, speed);
}

static void SlipRaceSession_GuidedProjectileExclude(void *context, uint16_t object, uint16_t shooter) {
	(void)context;
	(void)SlipRaceCollision_ExcludePair(object, shooter);
}

static void SlipRaceSession_GuidedProjectileSmoke(void *context, uint16_t object, SlipView3DVec32 position,
                                                  int32_t lifetime, uint32_t type) {
	(void)context;
	(void)type;
	SlipRaceEffects_EmitSmoke(object, position, lifetime, &SlipRaceEffects_projectileTrail);
}

static void SlipRaceSession_GuidedProjectileCamera(void *context, uint16_t object, uint16_t shooter) {
	(void)context;
	SlipRaceCamera_TrackProjectile(object, shooter, SlipRacePlayer_playerOneObject);
}

static void SlipRaceSession_GuidedProjectileSound(void *context, uint32_t positionX, uint32_t positionY,
                                                  uint32_t positionZ, uint32_t sound, uint16_t object, uint32_t flags) {
	(void)context;
	SlipSoundEffects_Queue(&SlipRaceSession_soundEffects, positionX, positionY, positionZ, sound, object, flags);
}

static void SlipRaceSession_GuidedProjectileFree(void *context, uint16_t object) {
	(void)context;
	SlipObject_Free(object, 0, 0, 0, 0, 0, 0);
}

void SlipRaceSession_FireSuperSeeker(uint16_t shooter, uint16_t target) {
	GuidedProjectileHost host = {.eventCallbackGuestAddress = 0x5d24b, .drawCallbackGuestAddress = 0x5c316};
	const SlipGuidedProjectileCalls calls = {.context = &host,
	                                         .playerObject = &SlipRacePlayer_playerOneObject,
	                                         .shapeHandle = &SlipRaceSession_weaponShapeHandles[6],
	                                         .voice = SlipRaceSession_GuidedProjectileVoice,
	                                         .getSpeed = SlipRaceSession_GuidedProjectileSpeed,
	                                         .weaponMountPosition = SlipRaceSession_GuidedProjectilePosition,
	                                         .copyObjectMatrix = SlipRaceSession_GuidedProjectileMatrix,
	                                         .fill = SlipRaceSession_GuidedProjectileFill,
	                                         .reclaim = SlipRaceSession_GuidedProjectileReclaim,
	                                         .setObjectFlags = SlipRaceSession_GuidedProjectileFlags,
	                                         .private = SlipRaceSession_GuidedProjectilePrivate,
	                                         .body = SlipRaceSession_GuidedProjectileBody,
	                                         .shapeBounds = SlipRaceSession_GuidedProjectileShapeBounds,
	                                         .setBodyBounds = SlipRaceSession_GuidedProjectileBodyBounds,
	                                         .track = SlipRaceSession_GuidedProjectileTrack,
	                                         .setSpeed = SlipRaceSession_GuidedProjectileSetSpeed,
	                                         .exclude = SlipRaceSession_GuidedProjectileExclude,
	                                         .smoke = SlipRaceSession_GuidedProjectileSmoke,
	                                         .camera = SlipRaceSession_GuidedProjectileCamera,
	                                         .queueSound = SlipRaceSession_GuidedProjectileSound,
	                                         .free = SlipRaceSession_GuidedProjectileFree,
	                                         .drawCallback = TrackView_DrawWeaponProjectile,
	                                         .eventCallback = SlipRacePlayer_GuidedProjectileEvent};
	SlipGuidedProjectile_FireSuperSeeker(shooter, target, &calls);
}

void SlipRaceSession_FireSuperFrag(uint16_t shooter, uint16_t target) {
	GuidedProjectileHost host = {.eventCallbackGuestAddress = 0x5d24b, .drawCallbackGuestAddress = 0x5c316};
	const SlipGuidedProjectileCalls calls = {.context = &host,
	                                         .playerObject = &SlipRacePlayer_playerOneObject,
	                                         .shapeHandle = &SlipRaceSession_weaponShapeHandles[3],
	                                         .voice = SlipRaceSession_GuidedProjectileVoice,
	                                         .getSpeed = SlipRaceSession_GuidedProjectileSpeed,
	                                         .weaponMountPosition = SlipRaceSession_GuidedProjectilePosition,
	                                         .copyObjectMatrix = SlipRaceSession_GuidedProjectileMatrix,
	                                         .fill = SlipRaceSession_GuidedProjectileFill,
	                                         .reclaim = SlipRaceSession_GuidedProjectileReclaim,
	                                         .setObjectFlags = SlipRaceSession_GuidedProjectileFlags,
	                                         .private = SlipRaceSession_GuidedProjectilePrivate,
	                                         .body = SlipRaceSession_GuidedProjectileBody,
	                                         .shapeBounds = SlipRaceSession_GuidedProjectileShapeBounds,
	                                         .setBodyBounds = SlipRaceSession_GuidedProjectileBodyBounds,
	                                         .track = SlipRaceSession_GuidedProjectileTrack,
	                                         .setSpeed = SlipRaceSession_GuidedProjectileSetSpeed,
	                                         .exclude = SlipRaceSession_GuidedProjectileExclude,
	                                         .smoke = SlipRaceSession_GuidedProjectileSmoke,
	                                         .camera = SlipRaceSession_GuidedProjectileCamera,
	                                         .queueSound = SlipRaceSession_GuidedProjectileSound,
	                                         .free = SlipRaceSession_GuidedProjectileFree,
	                                         .drawCallback = TrackView_DrawWeaponProjectile,
	                                         .eventCallback = SlipRacePlayer_GuidedProjectileEvent};
	SlipGuidedProjectile_FireSuperFrag(shooter, target, &calls);
}

void SlipRaceSession_FireFrag(uint16_t shooter, uint16_t target) {
	GuidedProjectileHost host = {.eventCallbackGuestAddress = 0x5d24b, .drawCallbackGuestAddress = 0x5c316};
	const SlipGuidedProjectileCalls calls = {.context = &host,
	                                         .playerObject = &SlipRacePlayer_playerOneObject,
	                                         .shapeHandle = &SlipRaceSession_weaponShapeHandles[3],
	                                         .voice = SlipRaceSession_GuidedProjectileVoice,
	                                         .getSpeed = SlipRaceSession_GuidedProjectileSpeed,
	                                         .weaponMountPosition = SlipRaceSession_GuidedProjectilePosition,
	                                         .copyObjectMatrix = SlipRaceSession_GuidedProjectileMatrix,
	                                         .fill = SlipRaceSession_GuidedProjectileFill,
	                                         .reclaim = SlipRaceSession_GuidedProjectileReclaim,
	                                         .setObjectFlags = SlipRaceSession_GuidedProjectileFlags,
	                                         .private = SlipRaceSession_GuidedProjectilePrivate,
	                                         .body = SlipRaceSession_GuidedProjectileBody,
	                                         .shapeBounds = SlipRaceSession_GuidedProjectileShapeBounds,
	                                         .setBodyBounds = SlipRaceSession_GuidedProjectileBodyBounds,
	                                         .track = SlipRaceSession_GuidedProjectileTrack,
	                                         .setSpeed = SlipRaceSession_GuidedProjectileSetSpeed,
	                                         .exclude = SlipRaceSession_GuidedProjectileExclude,
	                                         .smoke = SlipRaceSession_GuidedProjectileSmoke,
	                                         .camera = SlipRaceSession_GuidedProjectileCamera,
	                                         .queueSound = SlipRaceSession_GuidedProjectileSound,
	                                         .free = SlipRaceSession_GuidedProjectileFree,
	                                         .drawCallback = TrackView_DrawWeaponProjectile,
	                                         .eventCallback = SlipRacePlayer_GuidedProjectileEvent};
	SlipGuidedProjectile_FireFrag(shooter, target, &calls);
}

void SlipRaceSession_FireSeeker(uint16_t shooter, uint16_t target) {
	GuidedProjectileHost host = {.eventCallbackGuestAddress = 0x5d24b, .drawCallbackGuestAddress = 0x5c316};
	const SlipGuidedProjectileCalls calls = {.context = &host,
	                                         .playerObject = &SlipRacePlayer_playerOneObject,
	                                         .shapeHandle = &SlipRaceSession_weaponShapeHandles[6],
	                                         .voice = SlipRaceSession_GuidedProjectileVoice,
	                                         .getSpeed = SlipRaceSession_GuidedProjectileSpeed,
	                                         .weaponMountPosition = SlipRaceSession_GuidedProjectilePosition,
	                                         .copyObjectMatrix = SlipRaceSession_GuidedProjectileMatrix,
	                                         .fill = SlipRaceSession_GuidedProjectileFill,
	                                         .reclaim = SlipRaceSession_GuidedProjectileReclaim,
	                                         .setObjectFlags = SlipRaceSession_GuidedProjectileFlags,
	                                         .private = SlipRaceSession_GuidedProjectilePrivate,
	                                         .body = SlipRaceSession_GuidedProjectileBody,
	                                         .shapeBounds = SlipRaceSession_GuidedProjectileShapeBounds,
	                                         .setBodyBounds = SlipRaceSession_GuidedProjectileBodyBounds,
	                                         .track = SlipRaceSession_GuidedProjectileTrack,
	                                         .setSpeed = SlipRaceSession_GuidedProjectileSetSpeed,
	                                         .exclude = SlipRaceSession_GuidedProjectileExclude,
	                                         .smoke = SlipRaceSession_GuidedProjectileSmoke,
	                                         .camera = SlipRaceSession_GuidedProjectileCamera,
	                                         .queueSound = SlipRaceSession_GuidedProjectileSound,
	                                         .free = SlipRaceSession_GuidedProjectileFree,
	                                         .drawCallback = TrackView_DrawWeaponProjectile,
	                                         .eventCallback = SlipRacePlayer_GuidedProjectileEvent};
	SlipGuidedProjectile_FireSeeker(shooter, target, &calls);
}

void SlipRaceSession_FireBomber(uint16_t shooter, uint16_t target) {
	GuidedProjectileHost host = {.eventCallbackGuestAddress = 0x5d24b, .drawCallbackGuestAddress = 0x5c316};
	const SlipGuidedProjectileCalls calls = {.context = &host,
	                                         .playerObject = &SlipRacePlayer_playerOneObject,
	                                         .shapeHandle = &SlipRaceSession_weaponShapeHandles[2],
	                                         .voice = SlipRaceSession_GuidedProjectileVoice,
	                                         .getSpeed = SlipRaceSession_GuidedProjectileSpeed,
	                                         .weaponMountPosition = SlipRaceSession_GuidedProjectilePosition,
	                                         .copyObjectMatrix = SlipRaceSession_GuidedProjectileMatrix,
	                                         .fill = SlipRaceSession_GuidedProjectileFill,
	                                         .reclaim = SlipRaceSession_GuidedProjectileReclaim,
	                                         .setObjectFlags = SlipRaceSession_GuidedProjectileFlags,
	                                         .private = SlipRaceSession_GuidedProjectilePrivate,
	                                         .body = SlipRaceSession_GuidedProjectileBody,
	                                         .shapeBounds = SlipRaceSession_GuidedProjectileShapeBounds,
	                                         .setBodyBounds = SlipRaceSession_GuidedProjectileBodyBounds,
	                                         .track = SlipRaceSession_GuidedProjectileTrack,
	                                         .setSpeed = SlipRaceSession_GuidedProjectileSetSpeed,
	                                         .exclude = SlipRaceSession_GuidedProjectileExclude,
	                                         .smoke = SlipRaceSession_GuidedProjectileSmoke,
	                                         .camera = SlipRaceSession_GuidedProjectileCamera,
	                                         .queueSound = SlipRaceSession_GuidedProjectileSound,
	                                         .free = SlipRaceSession_GuidedProjectileFree,
	                                         .drawCallback = TrackView_DrawWeaponProjectile,
	                                         .eventCallback = SlipRacePlayer_GuidedProjectileEvent};
	SlipGuidedProjectile_FireBomber(shooter, target, &calls);
}

static void SlipRaceSession_GuidedProjectileOrthonormalize(void *context, SlipView3DMatrix *matrix) {
	(void)context;
	SlipView3D_OrthonormalizeForwardBasis(matrix);
}

void SlipRaceSession_FireScrambler(uint16_t shooter, uint16_t target) {
	GuidedProjectileHost host = {.eventCallbackGuestAddress = 0x5d33a, .drawCallbackGuestAddress = 0x5c316};
	const SlipGuidedProjectileCalls calls = {.context = &host,
	                                         .playerObject = &SlipRacePlayer_playerOneObject,
	                                         .shapeHandle = &SlipRaceSession_weaponShapeHandles[5],
	                                         .voice = SlipRaceSession_GuidedProjectileVoice,
	                                         .getSpeed = SlipRaceSession_GuidedProjectileSpeed,
	                                         .weaponMountPosition = SlipRaceSession_GuidedProjectilePosition,
	                                         .copyObjectMatrix = SlipRaceSession_GuidedProjectileMatrix,
	                                         .orthonormalize = SlipRaceSession_GuidedProjectileOrthonormalize,
	                                         .fill = SlipRaceSession_GuidedProjectileFill,
	                                         .reclaim = SlipRaceSession_GuidedProjectileReclaim,
	                                         .setObjectFlags = SlipRaceSession_GuidedProjectileFlags,
	                                         .private = SlipRaceSession_GuidedProjectilePrivate,
	                                         .body = SlipRaceSession_GuidedProjectileBody,
	                                         .shapeBounds = SlipRaceSession_GuidedProjectileShapeBounds,
	                                         .setBodyBounds = SlipRaceSession_GuidedProjectileBodyBounds,
	                                         .track = SlipRaceSession_GuidedProjectileTrack,
	                                         .setSpeed = SlipRaceSession_GuidedProjectileSetSpeed,
	                                         .exclude = SlipRaceSession_GuidedProjectileExclude,
	                                         .smoke = SlipRaceSession_GuidedProjectileSmoke,
	                                         .camera = SlipRaceSession_GuidedProjectileCamera,
	                                         .queueSound = SlipRaceSession_GuidedProjectileSound,
	                                         .free = SlipRaceSession_GuidedProjectileFree,
	                                         .drawCallback = TrackView_DrawWeaponProjectile,
	                                         .eventCallback = SlipRacePlayer_ScramblerEvent};
	SlipGuidedProjectile_FireScrambler(shooter, target, &calls);
}

static SlipView3DVec32 SlipRaceSession_DisrupterPosition(void *context, uint16_t object) {
	(void)context;
	SlipObjectPosition position;
	(void)SlipObject_Position(SlipObject_table, SLIP_OBJECT_TABLE_DOS_BYTES, object, &position);
	return (SlipView3DVec32){(int32_t)position.positionX, (int32_t)position.positionY, (int32_t)position.positionZ};
}

static SlipView3DVec16 SlipRaceSession_DisrupterNormalize(void *context, SlipView3DVec32 vector) {
	(void)context;
	SlipView3DNormalizeVector3D normalized;
	(void)SlipView3D_NormalizeVector3D((uint32_t)vector.x, (uint32_t)vector.y, (uint32_t)vector.z, &normalized);
	return (SlipView3DVec16){(int16_t)normalized.unitXQ14, (int16_t)normalized.unitYQ14, (int16_t)normalized.unitZQ14};
}

static void SlipRaceSession_DisrupterDirection(void *context, uint16_t object, SlipView3DVec16 direction) {
	(void)context;
	SlipObject_SetDirectionQ14(SlipObject_table, object, (uint16_t)direction.x, (uint16_t)direction.y,
	                           (uint16_t)direction.z);
}

static void SlipRaceSession_GuidedProjectileObjectSound(void *context, uint32_t effect, uint16_t object,
                                                        uint16_t mode) {
	(void)context;

	SlipSoundEffects_Queue(&SlipRaceSession_soundEffects, 2, 0, 0, effect, object, mode);
}

void SlipRaceSession_FireDisrupter(uint16_t shooter, uint16_t target) {
	GuidedProjectileHost host = {.eventCallbackGuestAddress = 0x5c741, .drawCallbackGuestAddress = 0};
	const SlipGuidedProjectileCalls calls = {.context = &host,
	                                         .playerObject = &SlipRacePlayer_playerOneObject,
	                                         .voice = SlipRaceSession_GuidedProjectileVoice,
	                                         .weaponMountPosition = SlipRaceSession_GuidedProjectilePosition,
	                                         .copyObjectMatrix = SlipRaceSession_GuidedProjectileMatrix,
	                                         .fill = SlipRaceSession_GuidedProjectileFill,
	                                         .reclaim = SlipRaceSession_GuidedProjectileReclaim,
	                                         .setObjectFlags = SlipRaceSession_GuidedProjectileFlags,
	                                         .private = SlipRaceSession_GuidedProjectilePrivate,
	                                         .eventCallback = SlipRacePlayer_DisrupterEvent,
	                                         .objectPosition = SlipRaceSession_DisrupterPosition,
	                                         .normalize = SlipRaceSession_DisrupterNormalize,
	                                         .setDirection = SlipRaceSession_DisrupterDirection,
	                                         .objectSound = SlipRaceSession_GuidedProjectileObjectSound};
	SlipGuidedProjectile_FireDisrupter(shooter, target, &calls);
}

void SlipRaceSession_FireAmbler(uint16_t shooter, uint16_t target) {
	GuidedProjectileHost host = {.eventCallbackGuestAddress = 0x5cb03, .drawCallbackGuestAddress = 0x5c316};
	const SlipGuidedProjectileCalls calls = {.context = &host,
	                                         .playerObject = &SlipRacePlayer_playerOneObject,
	                                         .shapeHandle = &SlipRaceSession_weaponShapeHandles[1],
	                                         .voice = SlipRaceSession_GuidedProjectileVoice,
	                                         .getSpeed = SlipRaceSession_GuidedProjectileSpeed,
	                                         .weaponMountPosition = SlipRaceSession_GuidedProjectilePosition,
	                                         .copyObjectMatrix = SlipRaceSession_GuidedProjectileMatrix,
	                                         .fill = SlipRaceSession_GuidedProjectileFill,
	                                         .reclaim = SlipRaceSession_GuidedProjectileReclaim,
	                                         .setObjectFlags = SlipRaceSession_GuidedProjectileFlags,
	                                         .private = SlipRaceSession_GuidedProjectilePrivate,
	                                         .body = SlipRaceSession_GuidedProjectileBody,
	                                         .shapeBounds = SlipRaceSession_GuidedProjectileShapeBounds,
	                                         .setBodyBounds = SlipRaceSession_GuidedProjectileBodyBounds,
	                                         .track = SlipRaceSession_GuidedProjectileTrack,
	                                         .setSpeed = SlipRaceSession_GuidedProjectileSetSpeed,
	                                         .camera = SlipRaceSession_GuidedProjectileCamera,
	                                         .objectSound = SlipRaceSession_GuidedProjectileObjectSound,
	                                         .free = SlipRaceSession_GuidedProjectileFree,
	                                         .drawCallback = TrackView_DrawWeaponProjectile,
	                                         .eventCallback = SlipRacePlayer_AmblerHyperNeuroEvent};
	SlipGuidedProjectile_FireAmbler(shooter, target, &calls);
}

void SlipRaceSession_FireHyperNeuro(uint16_t shooter, uint16_t target) {
	GuidedProjectileHost host = {.eventCallbackGuestAddress = 0x5cb03, .drawCallbackGuestAddress = 0x5c316};
	const SlipGuidedProjectileCalls calls = {.context = &host,
	                                         .playerObject = &SlipRacePlayer_playerOneObject,
	                                         .shapeHandle = &SlipRaceSession_weaponShapeHandles[4],
	                                         .getSpeed = SlipRaceSession_GuidedProjectileSpeed,
	                                         .weaponMountPosition = SlipRaceSession_GuidedProjectilePosition,
	                                         .copyObjectMatrix = SlipRaceSession_GuidedProjectileMatrix,
	                                         .fill = SlipRaceSession_GuidedProjectileFill,
	                                         .reclaim = SlipRaceSession_GuidedProjectileReclaim,
	                                         .setObjectFlags = SlipRaceSession_GuidedProjectileFlags,
	                                         .private = SlipRaceSession_GuidedProjectilePrivate,
	                                         .body = SlipRaceSession_GuidedProjectileBody,
	                                         .shapeBounds = SlipRaceSession_GuidedProjectileShapeBounds,
	                                         .setBodyBounds = SlipRaceSession_GuidedProjectileBodyBounds,
	                                         .track = SlipRaceSession_GuidedProjectileTrack,
	                                         .setSpeed = SlipRaceSession_GuidedProjectileSetSpeed,
	                                         .camera = SlipRaceSession_GuidedProjectileCamera,
	                                         .objectSound = SlipRaceSession_GuidedProjectileObjectSound,
	                                         .free = SlipRaceSession_GuidedProjectileFree,
	                                         .drawCallback = TrackView_DrawWeaponProjectile,
	                                         .eventCallback = SlipRacePlayer_AmblerHyperNeuroEvent};
	SlipGuidedProjectile_FireHyperNeuro(shooter, target, &calls);
}

static SlipView3DVec32 SlipRaceSession_MiniMinesTransform(void *context, const SlipView3DMatrix *matrix,
                                                          SlipView3DVec32 position) {
	(void)context;
	return SlipView3D_TransformPositionByColumns(matrix, position);
}

void SlipRaceSession_FireMiniMines(uint16_t shooter, uint16_t target) {
	(void)target;
	GuidedProjectileHost host = {.eventCallbackGuestAddress = 0x5d453, .drawCallbackGuestAddress = 0x5c316};
	const SlipGuidedProjectileMiniMinesCalls calls = {
	    .projectile = {.context = &host,
	                   .playerObject = &SlipRacePlayer_playerOneObject,
	                   .shapeHandle = &SlipRaceSession_weaponShapeHandles[0],
	                   .voice = SlipRaceSession_GuidedProjectileVoice,
	                   .copyObjectMatrix = SlipRaceSession_GuidedProjectileMatrix,
	                   .orthonormalize = SlipRaceSession_GuidedProjectileOrthonormalize,
	                   .fill = SlipRaceSession_GuidedProjectileFill,
	                   .reclaim = SlipRaceSession_GuidedProjectileReclaim,
	                   .setObjectFlags = SlipRaceSession_GuidedProjectileFlags,
	                   .private = SlipRaceSession_GuidedProjectilePrivate,
	                   .body = SlipRaceSession_GuidedProjectileBody,
	                   .shapeBounds = SlipRaceSession_GuidedProjectileShapeBounds,
	                   .setBodyBounds = SlipRaceSession_GuidedProjectileBodyBounds,
	                   .track = SlipRaceSession_GuidedProjectileTrack,
	                   .exclude = SlipRaceSession_GuidedProjectileExclude,
	                   .free = SlipRaceSession_GuidedProjectileFree,
	                   .drawCallback = TrackView_DrawWeaponProjectile,
	                   .eventCallback = SlipRacePlayer_MiniMinesEvent,
	                   .objectPosition = SlipRaceSession_DisrupterPosition,
	                   .queueSound = SlipRaceSession_GuidedProjectileSound},
	    .transform = SlipRaceSession_MiniMinesTransform};
	SlipGuidedProjectileMiniMinesContinuation continuation = SlipGuidedProjectile_FireMiniMines(shooter, &calls);
	if (continuation.displacedStack) {

		fprintf(stderr,
		        "Unimplemented DOS execution at 5d59f: unmatched POP EDX before POPAD; "
		        "remaining iterations %u, offset-table byte offset %u. "
		        "POPAD shifts EDI<-saved ESI, ESI<-saved EBP, EBP<-saved ESP, "
		        "EBX<-saved EDX, EDX<-saved ECX, ECX<-saved EAX, EAX<-outer saved EDI; "
		        "ESP delta +4. The shifted loop and caller continuation are not executed.\n",
		        continuation.remainingIterations, continuation.offsetTableByteOffset);
		abort();
	}
}

static bool SlipRaceSession_SoundObjectPosition(void *context, uint16_t object, int32_t *x, int32_t *y, int32_t *z) {
	SlipObjectPosition objectPosition;
	(void)context;
	if (!SlipObject_Position(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, object, &objectPosition))
		return false;
	*x = (int32_t)objectPosition.positionX;
	*y = (int32_t)objectPosition.positionY;
	*z = (int32_t)objectPosition.positionZ;
	return true;
}

static bool SlipRaceSession_SoundTrackLight(void *context, uint16_t object, uint16_t *light) {
	(void)context;
	return SlipRacePlayer_TrackLight(object, light);
}

void SlipRaceSession_BindSoundHost(SlipGameSoundState *gameSound, uint16_t soundSet, SlipSoundEffectLock lockSound,
                                   SlipSoundEffectUnlock unlockSound, void *context) {
	SlipRaceSession_gameSound = gameSound;
	SlipRaceSession_soundSet = soundSet;
	SlipRaceSession_lockSound = lockSound;
	SlipRaceSession_unlockSound = unlockSound;
	SlipRaceSession_soundContext = context;
}

static void SlipRaceSession_PreCollisionStep(uint32_t frameStep) {
	SlipTrackWorld_PreCollisionStep(
	    (uint16_t)frameStep, (uint8_t *)(void *)SlipRaceSession_trackSlots,
	    SlipRaceSession_playerContext.slotListBaseOffset, SlipRaceSession_playerContext.slotListSentinelOffset,
	    SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, SlipRaceSession_trackBundle.trdPayload.data,
	    SlipRaceSession_trackBundle.trdPayload.size, SlipRaceSession_trackBundle.trdBaseToken,
	    SlipRaceSession_trackBundle.trcPayload.data, SlipRaceSession_trackBundle.trcPayload.size,
	    SlipRaceSession_trackBundle.trcBaseToken, SlipRaceSession_cellTableHost, (SLIP_TRACK_WORLD_CELL_TABLE_BYTES));
}

static uint32_t SlipRaceSession_DoorEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                          uint32_t eventFlags, uint16_t object, uintptr_t dispatchData,
                                          uint32_t dispatchFrame) {
	(void)eventValue;
	(void)eventFlags;
	(void)dispatchData;
	(void)dispatchFrame;
	const uint32_t offset =
	    (uint32_t)SlipRaceSession_objectTableHost[object / SLIP_OBJECT_DOS_STRIDE].drawData - 0x339a6u;
	SlipTrackDoorRecord *const door =
	    offset % 0x64u == 0 && offset / 0x64u < 8u ? &SlipTrackWorld_doors[offset / 0x64u] : NULL;
	return SlipTrackWorld_DoorEvent(eventCode, object, (uint16_t)eventPayload, door, SlipRaceSession_objectTableHost,
	                                SLIP_OBJECT_TABLE_DOS_BYTES, SlipRaceSession_trackSlots,
	                                ((0x40u + 8u + 2u) * sizeof(SlipTrackSlotRecord)),
	                                SlipRaceSession_playerContext.slotListBaseOffset);
}

static void SlipRaceSession_CreateDoorSlot(uint16_t object) {
	SlipRacePlayerHostBindings *const player = &SlipRaceSession_playerContext;
	const uint32_t doorOffset = (uint32_t)player->objectTable[object / SLIP_OBJECT_DOS_STRIDE].drawData - 0x339a6u;
	SlipTrackDoorRecord *const door = &SlipTrackWorld_doors[doorOffset / 0x64u];
	SlipResourcePayload *const payload = &SlipTrackWorld_doorShapes[doorOffset / 0x64u];
	if (door->shapeHandle != 0) {
		SlipResourceHost_Release(NULL, door->shapeHandle);
		memset(payload, 0, sizeof(*payload));
		door->shapeHandle = 0;
	}
	door->speed = 0x37dc;
	door->direction = UINT32_MAX;
	SlipTrackWorldAddSlot added;
	bool translated = SlipTrackWorld_AddSlot(
	    object, 2, 1, SlipRaceSession_slotDrawHost, (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u),
	    SlipRaceSession_slotDrawCallbacks, SLIP_TRACK_SLOT_DRAW_RECORD_COUNT, SlipRaceSession_slotDrawBaseAddress,
	    SlipRaceSession_slotDrawFreeHeadAddress, player->slotListBase, player->slotListBytes,
	    player->slotListBaseOffset, player->slotListSentinelOffset, SlipRaceSession_slotListFreeHeadAddress,
	    player->objectTable, player->objectTableBytes, player->articSlotPool, player->articSlotPoolBytes,
	    player->articSlotPoolOffset, player->trdBase, player->trackDataSize, player->trackDataOffset,
	    player->componentBase, player->componentBaseBytes, player->componentBaseOffset, player->trackTable,
	    player->trackTableBytes, &added);
	if (!translated || added.carryOut)
		SlipRuntime_Fatal("CreateDoorSlot - TrackSlotAdd failed");
	SlipTrackWorldSlotListSelect selected;
	if (!SlipTrackWorld_SelectSlotListEntry(0, player->slotListBaseOffset, player->objectTable,
	                                        player->objectTableBytes, object, &selected) ||
	    selected.carry)
		SlipRuntime_Fatal("CreateDoorSlot - TrackSlotAdd failed");
	SlipTrackSlotRecord *const slot = &SlipRaceSession_trackSlots[selected.slotOffset / sizeof(*slot)];
	door->object = object;
	door->trackSlotAddress = selected.slotAddress;
	slot->doorAddress = 0x339a6u + doorOffset;
	int32_t width = door->halfWidth, height = door->halfHeight;
	if (door->directionY == 0) {
		width = door->halfHeight;
		height = door->halfWidth;
	}
	SlipShape3DDoorTemplate *const shape = &SlipShape3D_doorTemplate;
	int16_t x = (int16_t)(width >> 6), y = (int16_t)(height >> 6);
	shape->vertices[0].x = shape->vertices[3].x = (int16_t)(0u - (uint16_t)x);
	shape->vertices[1].x = shape->vertices[2].x = x;
	shape->vertices[0].y = shape->vertices[1].y = y;
	shape->vertices[2].y = shape->vertices[3].y = (int16_t)(0u - (uint16_t)y);
	SlipDraw3DMaterialNumber material;
	if (!SlipDraw3D_GetMaterialNumber(SlipRaceSession_materialTable, SlipRaceSession_materialTableBytes,
	                                  SlipRaceSession_materialGlobal, (const uint8_t *)"DOORS", 6, &material) ||
	    material.carryOut)
		SlipRuntime_Fatal("CreateDoorSlot - door material missing");
	shape->material = material.materialIndex;

	if (!SlipResourceHost_Allocate(NULL, sizeof(*shape), 0, &door->shapeHandle))
		SlipRuntime_Fatal("CreateDoorSlot - shape allocation failed");
	payload->data = SlipResourceHost_LockWritable(NULL, door->shapeHandle);
	*payload = SlipResourceHost_Payload(door->shapeHandle);
	if (!SlipShape3D_RecalculateBounds(&shape->header, shape->vertices, shape->vertexCount))
		SlipRuntime_Fatal("CreateDoorSlot - shape bounds failed");
	memcpy(payload->data, shape, sizeof(*shape));
	SlipResourceHost_Unlock(NULL, door->shapeHandle);
	(void)SlipRaceCollision_CreateBody(object, 2);

	const uint8_t *const bounds = SlipResourceHost_Lock(NULL, door->shapeHandle);
	const int32_t minX = (int32_t)SlipBytes_ReadLE32(bounds + 0x20);
	const int32_t maxX = (int32_t)SlipBytes_ReadLE32(bounds + 0x24);
	const int32_t minY = (int32_t)SlipBytes_ReadLE32(bounds + 0x28);
	const int32_t maxY = (int32_t)SlipBytes_ReadLE32(bounds + 0x2c);
	SlipResourceHost_Unlock(NULL, door->shapeHandle);
	SlipRaceCollision_SetBodyBounds(object, minX, minY, -0x7a0, maxX, maxY, 0x7a0);
}

static void SlipRaceSession_InitializeDoors(void) {
	if (SlipRacePlayer_trackStateEnabled == 0 || SlipTrackWorld_doorsInitialized != 0)
		return;
	SlipTrackWorld_doorsInitialized = UINT32_MAX;
	for (uint16_t i = 0; i < SlipTrackWorld_doorCount; ++i) {
		SlipTrackDoorRecord *const door = &SlipTrackWorld_doors[i];
		door->endpointDelay = 0;
		door->endpointDelayRemaining = 0;
		SlipObjectSlotFill created;
		if (!SlipObject_SlotFill(&door->matrix, (uint32_t)door->closedEndpoint.x, (uint32_t)door->closedEndpoint.y,
		                         (uint32_t)door->closedEndpoint.z, NULL, 0, SlipRaceSession_DoorEvent, &created) ||
		    created.carryOut)
			SlipRuntime_Fatal("InitDoors - object allocation failed");
		SlipObjectSlotDataWriteResult assigned;
		if (!SlipObject_SetDrawData(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, created.objectOffset,
		                            0x339a6u + (uint32_t)i * 0x64u, &assigned))
			SlipRuntime_Fatal("InitDoors - object binding failed");
		SlipRaceSession_CreateDoorSlot((uint16_t)created.objectOffset);
	}
}

static void SlipRaceSession_ReleaseDoors(void) {
	if (SlipRacePlayer_trackStateEnabled == 0)
		return;
	const uint16_t count = SlipTrackWorld_doorCount;
	if (count == 0)
		return;
	for (uint16_t i = 0; i < count; ++i) {
		SlipTrackDoorRecord *const door = &SlipTrackWorld_doors[i];
		if (door->shapeHandle != 0)
			SlipResourceHost_Release(NULL, door->shapeHandle);
		memset(&SlipTrackWorld_doorShapes[i], 0, sizeof(SlipTrackWorld_doorShapes[i]));
		door->shapeHandle = 0;

		SlipObject_Free(door->object, 0, 0, count - i, 0, 0, 0);
	}
	SlipTrackWorld_doorCount = 0;
}

static SlipTrackWorldSlotDrawInstall SlipRaceSession_slotDrawInstall;
static SlipTrackWorldSlotListInstall SlipRaceSession_slotListInstall;
static uint32_t SlipRaceSession_RemoveObjectTrackSlot(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                                      uint32_t eventFlags, uint16_t objectOffset,
                                                      uintptr_t dispatchData, uint32_t dispatchFrame);
static void SlipRaceSession_SetEnvironmentDetail(uint32_t detail);
static void SlipRaceSession_PostCollisionStep(void);
static bool SlipRaceSession_QueryTrackCollision(uint32_t step, uint16_t object);
static bool SlipRaceSession_CheckLineOfSight(uint16_t first, uint16_t second);
static void SlipRaceSession_ShutdownTrack(void);
static void SlipRaceSession_ShutdownTrackDraw(void);

/* Portable payload binding for an original anonymous Allocate/Lock pair. */
static bool SlipRaceSession_LockAllocatedWorkspace(uint32_t bytes, uint16_t *resource, uint8_t **base) {
	if (!SlipResourceHost_Allocate(NULL, bytes, 0, resource))
		return false;
	*base = SlipResourceHost_LockWritable(NULL, *resource);
	return true;
}

static bool SlipRaceSession_InitializeTrack(SlipTrackWorldSlotDrawInstall *draw, SlipTrackWorldSlotListInstall *list) {
	if (SlipRaceSession_trackWorldInitialized == UINT16_MAX) {
		*draw = SlipRaceSession_slotDrawInstall;
		*list = SlipRaceSession_slotListInstall;
		return true;
	}
	SlipRaceSession_trackWorldInitialized = UINT16_MAX;
	SlipRacePlayer_trackStateEnabled = 1;
	SlipTrackWorld_doorCount = 1;
	uint8_t *slots;
	if (!SlipRaceSession_LockAllocatedWorkspace((0x40u + 8u + 2u) * 0x118u, &SlipRaceSession_slotListResource, &slots))
		SlipRuntime_Fatal("TrackInstall - InstallSlot Out of Memory");
	SlipRaceSession_trackSlots = (SlipTrackSlotRecord *)(void *)slots;
	if (!SlipTrackWorld_BindSlotList(0x40u, slots, (0x40u + 8u + 2u) * 0x118u, SlipRaceSession_slotListResource,
	                                 SlipResourceHost_Payload(SlipRaceSession_slotListResource).address, NULL, 0, list))
		return false;
	if (!SlipRaceSession_LockAllocatedWorkspace(SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u,
	                                            &SlipRaceSession_slotDrawResource, &SlipRaceSession_slotDrawHost))
		SlipRuntime_Fatal("TrackInstall - InstallSlotDraw Out of Memory");
	if (!SlipTrackWorld_BindSlotDraw(0x40u, SlipRaceSession_slotDrawHost, SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u,
	                                 SlipRaceSession_slotDrawResource,
	                                 SlipResourceHost_Payload(SlipRaceSession_slotDrawResource).address, NULL, 0, draw))
		return false;
	uint8_t *beam;
	if (!SlipRaceSession_LockAllocatedWorkspace(0x4e00u, &SlipRaceSession_beamResource, &beam))
		SlipRuntime_Fatal("TrackInstall - Out of Memory");
	SlipTrackWorld_beams.resourceRecords = (SlipTrackBeamRecord *)(void *)beam;
	SlipTrackWorld_beams.recordCount = 0;
	SlipTrackWorld_beams.queueCount = 0;
	(void)SlipResource_GetUsage();
	SlipObject_SetServer(1u, SlipRaceSession_RemoveObjectTrackSlot);
	if (!SlipRaceSession_LockAllocatedWorkspace(SLIP_TRACK_WORLD_CELL_TABLE_BYTES, &SlipRaceSession_cellResource,
	                                            &SlipRaceSession_cellTableHost))
		return false;
	SlipRaceSession_slotDrawCount = draw->slotDrawCount;
	SlipRaceSession_slotDrawBaseAddress = draw->baseAddress;
	SlipRaceSession_slotDrawFreeHeadAddress = draw->ring.baseAddress;
	SlipRaceSession_slotListBaseAddress = list->baseAddress;
	SlipRaceSession_slotListFreeHeadAddress = list->freeListAddress;
	memset(SlipRaceSession_slotDrawCallbacks, 0, sizeof(SlipRaceSession_slotDrawCallbacks));
	TrackView_SetReplayCallback(TrackView_ExecuteReplay);
	SlipRaceSession_SetEnvironmentDetail(2);
	SlipRaceCollision_SetCallbacks(SlipTrackWorld_CheckSegmentTransition, SlipRaceSession_PreCollisionStep,
	                               SlipRaceSession_CheckLineOfSight, SlipRaceSession_QueryTrackCollision,
	                               SlipRaceSession_PostCollisionStep);
	SlipRaceSession_slotDrawInstall = *draw;
	SlipRaceSession_slotListInstall = *list;
	SlipRuntime_RegisterExit(SlipRaceSession_ShutdownTrack);
	return true;
}

static bool SlipRaceSession_InitializeTrackDraw(void) {
	if (SlipRaceSession_trackDrawInitialized == UINT16_MAX)
		return true;
	SlipRaceSession_trackDrawInitialized = UINT16_MAX;
	if (!SlipRaceSession_LockAllocatedWorkspace(SLIP_TRACK_WORLD_AXIS_RAMP_BYTES, &SlipRaceSession_axisRampResource,
	                                            &SlipRaceSession_axisRamps))
		goto failed;
	if (!SlipRaceSession_LockAllocatedWorkspace(SLIP_TRACK_WORLD_AXIS_TEST_BYTES, &SlipRaceSession_axisTestResource,
	                                            &SlipRaceSession_axisTests))
		goto failed;
	if (!SlipRaceSession_LockAllocatedWorkspace(SLIP_TRACK_WORLD_OBJECT_LIST_BYTES, &SlipRaceSession_objectListResource,
	                                            &SlipRaceSession_objectList))
		goto failed;
	if (!SlipRaceSession_LockAllocatedWorkspace(SLIP_TRACK_WORLD_DEFERRED_LIST_BYTES,
	                                            &SlipRaceSession_deferredListResource, &SlipRaceSession_deferredList))
		goto failed;
	if (!SlipRaceSession_LockAllocatedWorkspace(SLIP_TRACK_WORLD_DEFERRED_SCAN_BYTES,
	                                            &SlipRaceSession_deferredScanResource, &SlipRaceSession_deferredScan))
		goto failed;
	SlipRuntime_RegisterExit(SlipRaceSession_ShutdownTrackDraw);
	return true;
failed:
	SlipRuntime_error = 6;
	return false;
}

static void SlipRaceSession_ShutdownTrack(void) {
	if (SlipRaceSession_trackWorldInitialized != 0) {
		SlipRaceSession_trackWorldInitialized = 0;
		SlipResourceHost_Unlock(NULL, SlipRaceSession_slotDrawResource);
		SlipResourceHost_Release(NULL, SlipRaceSession_slotDrawResource);
		SlipResourceHost_Unlock(NULL, SlipRaceSession_slotListResource);
		SlipResourceHost_Release(NULL, SlipRaceSession_slotListResource);
		SlipResourceHost_Unlock(NULL, SlipRaceSession_cellResource);
		SlipResourceHost_Release(NULL, SlipRaceSession_cellResource);
		SlipRaceSession_ReleaseDoors();
		SlipResourceHost_Unlock(NULL, SlipRaceSession_beamResource);
		SlipResourceHost_Release(NULL, SlipRaceSession_beamResource);
		SlipTrackWorld_beams.resourceRecords = NULL;
	}
}

static void SlipRaceSession_ShutdownRenderer(void) {
	SlipRenderer_Shutdown(&SlipRendererHost_state, &SlipRendererHost_lifecycleCalls);
	SlipRaceSession_materialGlobal = SlipMaterialHost_residency.resource;
}

static void SlipRaceSession_ShutdownTrackDraw(void) {
	if (SlipRaceSession_trackDrawInitialized == 0)
		return;
	SlipRaceSession_trackDrawInitialized = 0;
	if (SlipRaceSession_axisRampResource != 0) {
		SlipResourceHost_Unlock(NULL, SlipRaceSession_axisRampResource);
		SlipResourceHost_Release(NULL, SlipRaceSession_axisRampResource);
		SlipRaceSession_axisRampResource = 0;
	}
	if (SlipRaceSession_axisTestResource != 0) {
		SlipResourceHost_Unlock(NULL, SlipRaceSession_axisTestResource);
		SlipResourceHost_Release(NULL, SlipRaceSession_axisTestResource);
		SlipRaceSession_axisTestResource = 0;
	}
	if (SlipRaceSession_objectListResource != 0) {
		SlipResourceHost_Unlock(NULL, SlipRaceSession_objectListResource);
		SlipResourceHost_Release(NULL, SlipRaceSession_objectListResource);
	}
	if (SlipRaceSession_deferredListResource != 0) {
		SlipResourceHost_Unlock(NULL, SlipRaceSession_deferredListResource);
		SlipResourceHost_Release(NULL, SlipRaceSession_deferredListResource);
	}
	if (SlipRaceSession_deferredScanResource != 0) {
		SlipResourceHost_Unlock(NULL, SlipRaceSession_deferredScanResource);
		SlipResourceHost_Release(NULL, SlipRaceSession_deferredScanResource);
	}
}

static void SlipRaceSession_ShutdownWorld(void) {

	SlipTrackAssets_FreeBundle(&SlipRaceSession_trackBundle, TrackView_ReleaseResource,
	                           &SlipRaceSession_resourceRegistry);
	SlipRaceSession_ShutdownTrackDraw();
	SlipRaceSession_ShutdownTrack();
	SlipRaceCollisionHost_Shutdown();
	SlipShape3D_Shutdown();
	SlipRaceSession_ShutdownRenderer();
	TrackView_ShutdownClouds(&SlipRaceSession_cloudState);
	SlipArticSlot_Shutdown(&SlipRaceSession_articPool);
	SlipDrawListHost_Shutdown();
	SlipObject_Shutdown();
}

static void SlipRaceSession_PostCollisionStep(void) {
	SlipTrackWorld_PostCollisionStep(
	    (uint8_t *)(void *)SlipRaceSession_trackSlots, SlipRaceSession_playerContext.slotListBaseOffset,
	    SlipRaceSession_playerContext.slotListSentinelOffset, SlipRaceSession_objectTableHost,
	    SLIP_OBJECT_TABLE_DOS_BYTES, SlipRaceSession_trackBundle.trdPayload.data,
	    SlipRaceSession_trackBundle.trdPayload.size, SlipRaceSession_trackBundle.trdBaseToken,
	    SlipRaceSession_trackBundle.trcPayload.data, SlipRaceSession_trackBundle.trcPayload.size,
	    SlipRaceSession_trackBundle.trcBaseToken, SlipRaceSession_cellTableHost, (SLIP_TRACK_WORLD_CELL_TABLE_BYTES));
}

static bool SlipRaceSession_QueryTrackCollision(uint32_t preservedCallerValue, uint16_t object) {
	return SlipTrackWorld_QueryObjectCollision(
	    preservedCallerValue, object, (uint8_t *)(void *)SlipRaceSession_trackSlots,
	    SlipRaceSession_playerContext.slotListBaseOffset, SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES,
	    SlipRaceSession_trackBundle.trdPayload.data, SlipRaceSession_trackBundle.trdPayload.size,
	    SlipRaceSession_trackBundle.trdBaseToken, SlipRaceSession_trackBundle.trcPayload.data,
	    SlipRaceSession_trackBundle.trcPayload.size, SlipRaceSession_trackBundle.trcBaseToken,
	    SlipRaceSession_cellTableHost, (SLIP_TRACK_WORLD_CELL_TABLE_BYTES));
}

uint32_t SlipRaceSession_AttachTimedEffect(uint16_t object, int32_t radius, const SlipTimedEffect *emitter,
                                           uint32_t callerValue) {
	SlipRacePlayerHostBindings *const player = &SlipRaceSession_playerContext;
	SlipTrackWorldAddSlot added;
	bool translated = SlipTrackWorld_AddSlot(
	    object, 2, 1, SlipRaceSession_slotDrawHost, (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u),
	    SlipRaceSession_slotDrawCallbacks, SLIP_TRACK_SLOT_DRAW_RECORD_COUNT, SlipRaceSession_slotDrawBaseAddress,
	    SlipRaceSession_slotDrawFreeHeadAddress, player->slotListBase, player->slotListBytes,
	    player->slotListBaseOffset, player->slotListSentinelOffset, SlipRaceSession_slotListFreeHeadAddress,
	    player->objectTable, player->objectTableBytes, player->articSlotPool, player->articSlotPoolBytes,
	    player->articSlotPoolOffset, player->trdBase, player->trackDataSize, player->trackDataOffset,
	    player->componentBase, player->componentBaseBytes, player->componentBaseOffset, player->trackTable,
	    player->trackTableBytes, &added);
	if (!translated)
		SlipRuntime_Fatal("Timed effect track attachment translation failed (0004fcd2)");
	if (added.carryOut)
		SlipObject_FreeImmediate(object, (uint32_t)radius, 2, callerValue, 0, (uintptr_t)emitter,
		                         emitter->descriptor->dosAddress);
	return 2;
}

static void SlipRaceSession_AttachAnimatedEffect(uint16_t object, int32_t radius) {
	SlipRacePlayerHostBindings *const player = &SlipRaceSession_playerContext;
	SlipTrackWorldAddSlot added;
	bool translated = SlipTrackWorld_AddSlot(
	    object, 2, 1, SlipRaceSession_slotDrawHost, (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u),
	    SlipRaceSession_slotDrawCallbacks, SLIP_TRACK_SLOT_DRAW_RECORD_COUNT, SlipRaceSession_slotDrawBaseAddress,
	    SlipRaceSession_slotDrawFreeHeadAddress, player->slotListBase, player->slotListBytes,
	    player->slotListBaseOffset, player->slotListSentinelOffset, SlipRaceSession_slotListFreeHeadAddress,
	    player->objectTable, player->objectTableBytes, player->articSlotPool, player->articSlotPoolBytes,
	    player->articSlotPoolOffset, player->trdBase, player->trackDataSize, player->trackDataOffset,
	    player->componentBase, player->componentBaseBytes, player->componentBaseOffset, player->trackTable,
	    player->trackTableBytes, &added);
	if (!translated)
		SlipRuntime_Fatal("Animated effect track attachment translation failed (0004fcee)");
	if (added.carryOut)
		SlipObject_FreeImmediate(object, (uint32_t)radius, 2, 0, 0, 0, 0);
}

static void SlipRaceSession_AttachCrossEffect(uint16_t object) {
	SlipRacePlayerHostBindings *const player = &SlipRaceSession_playerContext;
	SlipTrackWorldAddSlot added;
	bool translated = SlipTrackWorld_AddSlot(
	    object, 2, 1, SlipRaceSession_slotDrawHost, (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u),
	    SlipRaceSession_slotDrawCallbacks, SLIP_TRACK_SLOT_DRAW_RECORD_COUNT, SlipRaceSession_slotDrawBaseAddress,
	    SlipRaceSession_slotDrawFreeHeadAddress, player->slotListBase, player->slotListBytes,
	    player->slotListBaseOffset, player->slotListSentinelOffset, SlipRaceSession_slotListFreeHeadAddress,
	    player->objectTable, player->objectTableBytes, player->articSlotPool, player->articSlotPoolBytes,
	    player->articSlotPoolOffset, player->trdBase, player->trackDataSize, player->trackDataOffset,
	    player->componentBase, player->componentBaseBytes, player->componentBaseOffset, player->trackTable,
	    player->trackTableBytes, &added);
	if (!translated)
		SlipRuntime_Fatal("Cross effect track attachment translation failed (0004fd00)");
	if (added.carryOut)
		SlipObject_FreeImmediate(object, 0, 2, 0, 0, 0, 0);
}

void SlipRaceSession_UpdateTimedEffect(uint16_t object, uint32_t speedResult) {
	if (SlipRaceSession_QueryTrackCollision(speedResult, object))
		return;
	SlipObjectPosition original;
	(void)SlipObject_Position(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, object, &original);
	const uint16_t step = (uint16_t)SlipFrameTimer_Step();
	const uint32_t speed = (uint32_t)SlipObject_Speed(SlipRaceSession_objectTableHost, object);
	const uint64_t product = (uint64_t)step * speed;
	const uint32_t distance = (uint32_t)(product >> 14);
	SlipObjectDirection direction = SlipObject_Direction(SlipRaceSession_objectTableHost, object);
	SlipView3DVec32 displacement = SlipView3D_ScaleVector(direction.directionXQ14, direction.directionYQ14,
	                                                      direction.directionZQ14, (int32_t)distance);
	const uint32_t x = (uint32_t)displacement.x + original.positionX;
	const uint32_t y = (uint32_t)displacement.y + original.positionY;
	const uint32_t z = (uint32_t)displacement.z + original.positionZ;
	SlipObjectSetPosition positioned;
	(void)SlipObject_SetPosition(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, object, x, y, z,
	                             &positioned);
	if (SlipRaceSession_QueryTrackCollision(x, object)) {
		const SlipTimedEffectObjectState *const state = SlipObject_TimedEffectState(object);
		const SlipTimedEffectDescriptor *const descriptor = state->descriptor;

		SlipObject_FreeImmediate(object, x, y, z, (uint32_t)(product >> 32), (uintptr_t)descriptor, distance);
		return;
	}
	(void)SlipObject_SetPosition(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, object,
	                             original.positionX, original.positionY, original.positionZ, &positioned);
}

static uint32_t SlipRaceSession_UpdateAnimatedEffect(uint16_t object) {
	SlipObjectPosition original;
	(void)SlipObject_Position(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, object, &original);
	const uint16_t step = (uint16_t)SlipFrameTimer_Step();
	const uint32_t speed = (uint32_t)SlipObject_Speed(SlipRaceSession_objectTableHost, object);
	const uint32_t distance = (uint32_t)(((uint64_t)step * speed) >> 14);
	SlipObjectDirection direction = SlipObject_Direction(SlipRaceSession_objectTableHost, object);
	SlipView3DVec32 displacement = SlipView3D_ScaleVector(direction.directionXQ14, direction.directionYQ14,
	                                                      direction.directionZQ14, (int32_t)distance);
	const uint32_t x = (uint32_t)displacement.x + original.positionX;
	const uint32_t y = (uint32_t)displacement.y + original.positionY;
	const uint32_t z = (uint32_t)displacement.z + original.positionZ;
	SlipObjectSetPosition positioned;
	(void)SlipObject_SetPosition(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, object, x, y, z,
	                             &positioned);
	if (SlipRaceSession_QueryTrackCollision(x, object)) {
		SlipObject_FreeImmediate(object, x, y, z, 0, 0, distance);
		return x;
	}
	(void)SlipObject_SetPosition(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, object,
	                             original.positionX, original.positionY, original.positionZ, &positioned);
	return original.positionX;
}

uint32_t SlipRaceSession_UpdateCrossEffect(uint16_t object) {
	SlipRaceEffects_ReadVelocity(object);
	const uint16_t gravityStep = (uint16_t)SlipFrameTimer_Step();
	const uint16_t gravityWord = (uint16_t)(((uint32_t)0x3d50u * gravityStep) >> 14);
	int32_t vertical = (int32_t)((uint32_t)SlipRaceEffects_velocity.y - (uint32_t)(int32_t)(int16_t)gravityWord);
	if (vertical < -28600)
		vertical = -28600;
	SlipRaceEffects_velocity.y = vertical;
	SlipRaceEffects_WriteVelocity(object);

	SlipObjectPosition original;
	(void)SlipObject_Position(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, object, &original);
	const uint16_t step = (uint16_t)SlipFrameTimer_Step();
	const uint32_t speed = (uint32_t)SlipObject_Speed(SlipRaceSession_objectTableHost, object);
	const uint32_t distance = (uint32_t)(((uint64_t)step * speed) >> 14);
	SlipObjectDirection direction = SlipObject_Direction(SlipRaceSession_objectTableHost, object);
	SlipView3DVec32 displacement = SlipView3D_ScaleVector(direction.directionXQ14, direction.directionYQ14,
	                                                      direction.directionZQ14, (int32_t)distance);
	const uint32_t x = (uint32_t)displacement.x + original.positionX;
	const uint32_t y = (uint32_t)displacement.y + original.positionY;
	const uint32_t z = (uint32_t)displacement.z + original.positionZ;
	SlipObjectSetPosition positioned;
	(void)SlipObject_SetPosition(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, object, x, y, z,
	                             &positioned);
	if (SlipRaceSession_QueryTrackCollision(x, object)) {
		SlipObject_FreeImmediate(object, x, y, z, 0, 0, distance);
		return 0;
	}
	(void)SlipObject_SetPosition(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, object,
	                             original.positionX, original.positionY, original.positionZ, &positioned);
	SlipObject_SetSpeed(SlipRaceSession_objectTableHost, object, speed);
	return 0;
}

static uint32_t SlipRaceSession_debrisMode;
static SlipView3DVec32 SlipRaceSession_debrisPosition;
static SlipView3DVec32 SlipRaceSession_debrisDirection;
static uint16_t SlipRaceSession_debrisSource;
static uint16_t SlipRaceSession_debrisObject;

static bool SlipRaceSession_CreateOneDebris(uint32_t *drawPrefixOrImpulseScale) {
	SlipRacePlayerHostBindings *const player = &SlipRaceSession_playerContext;
	SlipView3DMatrix matrix;
	SlipObjectMatrixCopy copied;
	(void)SlipObject_MatrixCopy(player->objectTable, player->objectTableBytes, SlipRaceSession_debrisSource, &matrix,
	                            &copied);
	SlipArticDebrisEntry selected;
	if (!SlipArticSlot_SelectDebris(SlipRaceSession_debrisSource, SlipRaceSession_debrisMode, player->objectTable,
	                                player->objectTableBytes, player->articSlotPool, player->articSlotPoolBytes,
	                                player->articSlotPoolOffset, &selected))
		return false;

	if (!SlipShapeEffects_Create(SlipRaceSession_debrisPosition, &matrix, selected.shape, *drawPrefixOrImpulseScale,
	                             &SlipRaceSession_debrisObject))
		return false;
	const uint16_t object = SlipRaceSession_debrisObject;
	const int32_t speed = SlipObject_Speed(player->objectTable, SlipRaceSession_debrisSource);
	SlipObjectDirection direction = SlipObject_Direction(player->objectTable, SlipRaceSession_debrisSource);
	SlipObject_SetSpeed(player->objectTable, object, (uint32_t)speed);
	SlipObject_SetDirectionQ14(player->objectTable, object, direction.directionXQ14, direction.directionYQ14,
	                           direction.directionZQ14);
	SlipRaceEffects_ReadVelocity(object);
	SlipObjectPosition position;
	(void)SlipObject_Position(player->objectTable, player->objectTableBytes, object, &position);
	SlipView3DScaleVector3D scaled;
	SlipView3DNormalizeLength3D normalized;
	(void)SlipView3D_ScaleVector3D(position.positionX - (uint32_t)SlipRaceSession_debrisPosition.x,
	                               position.positionY - (uint32_t)SlipRaceSession_debrisPosition.y,
	                               position.positionZ - (uint32_t)SlipRaceSession_debrisPosition.z, &scaled);
	(void)SlipView3D_NormalizeLength3D(scaled.scaledX, scaled.scaledY, scaled.scaledZ, &normalized);
	SlipRaceSession_debrisDirection =
	    (SlipView3DVec32){(int16_t)normalized.unitXQ14, (int16_t)normalized.unitYQ14, (int16_t)normalized.unitZQ14};
	/* X/Z add the signed low product word, not the high multiplication word. */
	const int16_t randomX = (int16_t)((uint32_t)SlipRandom_Next() * 0x200u);
	SlipRaceSession_debrisDirection.x =
	    (int32_t)((uint32_t)SlipRaceSession_debrisDirection.x + (uint32_t)(int32_t)randomX);
	const uint16_t randomY = (uint16_t)SlipRandom_Next();
	SlipRaceSession_debrisDirection.y = (int32_t)((uint32_t)SlipRaceSession_debrisDirection.y + randomY);
	const int16_t randomZ = (int16_t)((uint32_t)SlipRandom_Next() * 0x200u);
	SlipRaceSession_debrisDirection.z =
	    (int32_t)((uint32_t)SlipRaceSession_debrisDirection.z + (uint32_t)(int32_t)randomZ);
	(void)SlipView3D_ScaleVector3D((uint32_t)SlipRaceSession_debrisDirection.x,
	                               (uint32_t)SlipRaceSession_debrisDirection.y,
	                               (uint32_t)SlipRaceSession_debrisDirection.z, &scaled);
	(void)SlipView3D_NormalizeLength3D(scaled.scaledX, scaled.scaledY, scaled.scaledZ, &normalized);
	*drawPrefixOrImpulseScale = (((uint32_t)(uint16_t)SlipRandom_Next() * 0x45d3u) >> 16) + 0x29e5;
	SlipView3DVec32 impulse = SlipView3D_ScaleVector((int16_t)normalized.unitXQ14, (int16_t)normalized.unitYQ14,
	                                                 (int16_t)normalized.unitZQ14, (int32_t)*drawPrefixOrImpulseScale);
	SlipRaceEffects_velocity.x = (int32_t)((uint32_t)SlipRaceEffects_velocity.x + (uint32_t)impulse.x);
	SlipRaceEffects_velocity.y = (int32_t)((uint32_t)SlipRaceEffects_velocity.y + (uint32_t)impulse.y);
	SlipRaceEffects_velocity.z = (int32_t)((uint32_t)SlipRaceEffects_velocity.z + (uint32_t)impulse.z);
	SlipRaceEffects_WriteVelocity(object);
	SlipRaceDebrisState *const state = &player->objectTable[object / SLIP_OBJECT_DOS_STRIDE].debrisEffect;
	state->elapsed = 0;
	state->rotationRateX = (int16_t)((SlipRandom_Next() & 0xfffu) + 0x3000);
	state->rotationRateY = (int16_t)((SlipRandom_Next() & 0xfffu) + 0x3000);
	state->rotationRateZ = (int16_t)((SlipRandom_Next() & 0xfffu) + 0x3000);
	SlipObjectEventCallbackWriteResult callback;
	(void)SlipObject_SetEventCallback(player->objectTable, player->objectTableBytes, object,
	                                  SlipRaceSession_DebrisEvent, &callback);
	SlipTrackWorldAddSlot added;
	bool translated = SlipTrackWorld_AddSlot(
	    object, 2, 1, SlipRaceSession_slotDrawHost, (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u),
	    SlipRaceSession_slotDrawCallbacks, SLIP_TRACK_SLOT_DRAW_RECORD_COUNT, SlipRaceSession_slotDrawBaseAddress,
	    SlipRaceSession_slotDrawFreeHeadAddress, player->slotListBase, player->slotListBytes,
	    player->slotListBaseOffset, player->slotListSentinelOffset, SlipRaceSession_slotListFreeHeadAddress,
	    player->objectTable, player->objectTableBytes, player->articSlotPool, player->articSlotPoolBytes,
	    player->articSlotPoolOffset, player->trdBase, player->trackDataSize, player->trackDataOffset,
	    player->componentBase, player->componentBaseBytes, player->componentBaseOffset, player->trackTable,
	    player->trackTableBytes, &added);
	if (!translated)
		SlipRuntime_Fatal("Debris track attachment translation failed (0004f9af)");
	if (added.carryOut) {
		SlipObject_FreeImmediate(object, (uint16_t)state->rotationRateZ, 2, normalized.unitZQ14, 0, (uintptr_t)state,
		                         *drawPrefixOrImpulseScale);
		return false;
	}
	return true;
}

void SlipRaceSession_CreateDebris(uint32_t count, uint32_t destruction, uint16_t source,
                                  uint32_t drawPrefixOrImpulseScale) {
	SlipRaceSession_debrisMode = destruction;
	SlipRaceSession_debrisSource = source;
	SlipObjectPosition position;
	(void)SlipObject_Position(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, source, &position);
	SlipRaceSession_debrisPosition =
	    (SlipView3DVec32){(int32_t)position.positionX, (int32_t)position.positionY, (int32_t)position.positionZ};
	if ((int16_t)count > 4)
		count = (count & 0xffff0000u) | 4u;
	uint32_t remaining = (uint16_t)count;
	do {
		if (!SlipRaceSession_CreateOneDebris(&drawPrefixOrImpulseScale))
			break;
		--remaining;
	} while (remaining != 0);
}

uint32_t SlipRaceSession_DebrisEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                     uint32_t eventFlags, uint16_t object, uintptr_t dispatchData,
                                     uint32_t dispatchFrame) {
	if ((uint16_t)eventCode == SLIP_OBJECT_EVENT_FREE) {
		SlipObjectSlotDataReadResult shape;
		(void)SlipObject_GetDrawData(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, object, &shape);
		if ((uint16_t)shape.drawData != 0) {
			TrackView_ReleaseResource(&SlipRaceSession_resourceRegistry, (uint16_t)shape.drawData);
			SlipObjectSlotDataWriteResult cleared;
			(void)SlipObject_SetDrawData(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, object, 0,
			                             &cleared);
		}
		return eventCode & 0xffff0000u;
	}
	if ((uint16_t)eventCode != SLIP_OBJECT_EVENT_UPDATE)
		return UINT32_MAX;
	SlipRaceDebrisState *const state = &SlipRaceSession_objectTableHost[object / SLIP_OBJECT_DOS_STRIDE].debrisEffect;
	const uint32_t previousElapsed = (eventCode & 0xffff0000u) | state->elapsed;
	SlipFrameTimerValues timer = SlipFrameTimer_Values();
	const uint32_t elapsed = timer.deltaMilliseconds + previousElapsed;
	if ((int16_t)elapsed > 10000) {
		SlipObject_FreeImmediate(object, elapsed, timer.stepQ14, timer.frameRateHz, eventFlags, (uintptr_t)state,
		                         previousElapsed);
		return 0;
	}
	state->elapsed = (uint16_t)elapsed;
	SlipRaceEffects_ReadVelocity(object);
	uint16_t step = (uint16_t)SlipFrameTimer_Step();
	const int16_t gravity = (int16_t)(uint16_t)(((uint32_t)0x3d50 * step) >> 14);
	int32_t vertical = (int32_t)((uint32_t)SlipRaceEffects_velocity.y - (uint32_t)(int32_t)gravity);
	if (vertical < -28600)
		vertical = -28600;
	SlipRaceEffects_velocity.y = vertical;
	SlipRaceEffects_WriteVelocity(object);
	step = (uint16_t)SlipFrameTimer_Step();
	const int16_t rotateX = (int16_t)((uint32_t)((int32_t)state->rotationRateX * (int16_t)step) >> 14);
	const int16_t rotateY = (int16_t)((uint32_t)((int32_t)state->rotationRateY * (int16_t)step) >> 14);
	const int16_t rotateZ = (int16_t)((uint32_t)((int32_t)state->rotationRateZ * (int16_t)step) >> 14);
	SlipObjectRotate rotated;
	(void)SlipObject_Rotate(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, object, rotateX, rotateY,
	                        rotateZ, 0, &SlipRaceSession_maths, &rotated);
	SlipObjectPosition original;
	(void)SlipObject_Position(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, object, &original);
	step = (uint16_t)SlipFrameTimer_Step();
	const uint32_t speed = (uint32_t)SlipObject_Speed(SlipRaceSession_objectTableHost, object);
	const uint32_t distance = (uint32_t)(((uint64_t)step * speed) >> 14);
	SlipObjectDirection direction = SlipObject_Direction(SlipRaceSession_objectTableHost, object);
	SlipView3DVec32 displacement = SlipView3D_ScaleVector(direction.directionXQ14, direction.directionYQ14,
	                                                      direction.directionZQ14, (int32_t)distance);
	const uint32_t x = original.positionX + (uint32_t)displacement.x;
	const uint32_t y = original.positionY + (uint32_t)displacement.y;
	const uint32_t z = original.positionZ + (uint32_t)displacement.z;
	SlipObjectSetPosition positioned;
	(void)SlipObject_SetPosition(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, object, x, y, z,
	                             &positioned);
	if (SlipRaceSession_QueryTrackCollision(x, object)) {
		SlipObject_FreeImmediate(object, x, y, z, 0, dispatchData, distance);
		return 0;
	}
	(void)SlipObject_SetPosition(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, object,
	                             original.positionX, original.positionY, original.positionZ, &positioned);
	SlipObject_SetSpeed(SlipRaceSession_objectTableHost, object, speed);
	(void)dispatchFrame;
	(void)eventPayload;
	(void)eventValue;
	return 0;
}

static bool SlipRaceSession_CheckLineOfSight(uint16_t firstObject, uint16_t secondObject) {
	return SlipTrackWorld_CheckLineOfSight(
	    firstObject, secondObject, (uint8_t *)(void *)SlipRaceSession_trackSlots,
	    SlipRaceSession_playerContext.slotListBaseOffset, SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES,
	    SlipRaceSession_trackBundle.trdPayload.data, SlipRaceSession_trackBundle.trdPayload.size,
	    SlipRaceSession_trackBundle.trdBaseToken, SlipRaceSession_trackBundle.trcPayload.data,
	    SlipRaceSession_trackBundle.trcPayload.size, SlipRaceSession_trackBundle.trcBaseToken,
	    SlipRaceSession_cellTableHost, (SLIP_TRACK_WORLD_CELL_TABLE_BYTES));
}

static uint32_t SlipRaceSession_RemoveObjectTrackSlot(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                                      uint32_t eventFlags, uint16_t objectOffset,
                                                      uintptr_t dispatchData, uint32_t dispatchFrame) {
	(void)eventPayload;
	(void)eventValue;
	(void)eventFlags;
	(void)dispatchData;
	(void)dispatchFrame;
	if (((uint16_t)eventCode & 0x0001u) != 0 && SlipRaceSession_trackWorldInitialized != 0) {
		SlipTrackWorld_RemoveObjectSlot(
		    eventCode, objectOffset, 1u, SlipRaceSession_slotDrawHost, (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u),
		    SlipRaceSession_slotDrawBaseAddress, SlipRaceSession_slotDrawFreeHeadAddress,
		    SlipRaceSession_trackBundle.trdPayload.data, SlipRaceSession_trackBundle.trdPayload.size,
		    SlipRaceSession_trackBundle.trdBaseToken, (uint8_t *)(void *)SlipRaceSession_trackSlots,
		    ((0x40u + 8u + 2u) * sizeof(SlipTrackSlotRecord)), SlipRaceSession_slotListBaseAddress,
		    SlipRaceSession_slotListFreeHeadAddress, SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES);
	}
	return eventCode;
}

static void SlipRaceSession_UpdateProgress(void) {

	static const uint32_t lapPositionSpeech[] = {0, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74};
	static const uint32_t finishingDriverSpeech[] = {74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84};
	SlipRaceRacerState *const racerTable = SlipRaceSession_racerStates;
	uint32_t activeLocalRacers = 0;
	uint32_t destroyedLocalRacers = 0;
	const uint16_t activeRacerCount = SlipRace_activeRacerTable->racerCount;
	uint16_t racerIndex;
	uint16_t position;

	for (racerIndex = 0; racerIndex < activeRacerCount; ++racerIndex) {
		SlipRaceRacerState *const lapRacer = &racerTable[racerIndex];
		uint16_t oldComponent;
		uint16_t currentComponent;

		if (lapRacer->racerType == 0 || lapRacer->racerType == 3 || lapRacer->racerType == 1) {
			if (lapRacer->destroyed != 0) {
				++destroyedLocalRacers;
			} else if (lapRacer->finished == 0) {
				++activeLocalRacers;
			}
		}
		if (SlipRacePlayer_startCountdown == 0) {
			const uint32_t elapsedMilliseconds = SlipFrameTimer_Values().deltaMilliseconds;

			lapRacer->currentLapTime += elapsedMilliseconds;
			if (lapRacer->finished == 0) {
				lapRacer->totalRaceTime += elapsedMilliseconds;
			}
		}
		currentComponent = (uint16_t)SlipTrackWorld_CurrentComponent(
		    0, lapRacer->objectOffset, SlipRaceSession_playerContext.slotListBase,
		    SlipRaceSession_playerContext.slotListBytes, SlipRaceSession_playerContext.slotListBaseOffset,
		    SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, SlipRaceSession_trackBundle.trdPayload.data,
		    SlipRaceSession_trackBundle.trdPayload.size, SlipRaceSession_trackBundle.trdBaseToken,
		    SlipRaceSession_trackBundle.trcPayload.data, SlipRaceSession_trackBundle.trcPayload.size,
		    SlipRaceSession_trackBundle.trcBaseToken, SlipRaceSession_cellTableHost,
		    (SLIP_TRACK_WORLD_CELL_TABLE_BYTES));
		oldComponent = lapRacer->trackComponent;
		if (currentComponent != oldComponent) {
			const uint16_t startComponent = SlipTrackWorld_StartComponent(SlipRaceSession_trackBundle.trdPayload.data);
			const uint16_t previousComponent =
			    SlipTrackWorld_PreviousComponent(SlipRaceSession_trackBundle.trdPayload.data);

			lapRacer->trackComponent = currentComponent;
			if (currentComponent != startComponent) {
				if (currentComponent == previousComponent && oldComponent == startComponent) {
					lapRacer->wrongWay = 1;
				}
			} else if (oldComponent == previousComponent) {
				if (lapRacer->wrongWay != 0) {
					lapRacer->wrongWay = 0;
				} else if (lapRacer->finished == 0) {
					if (lapRacer->lapNumber == 0) {
						++lapRacer->lapNumber;
					} else {
						const uint32_t lapTime = lapRacer->currentLapTime;
						uint16_t completedLap;

						lapRacer->currentLapTime = 0;
						if (lapRacer->bestLapTime == 0 || (int32_t)lapTime < (int32_t)lapRacer->bestLapTime) {
							lapRacer->bestLapTime = lapTime;
						}
						if (lapRacer->objectOffset == SlipRacePlayer_playerOneObject) {
							SlipRaceCamera_StoreLapTime(&SlipRaceSession_cameraState, 1, lapTime);

							SlipRaceVoiceCalls calls = SlipRaceVoiceHost_Calls(SlipRaceSession_gameSound);
							SlipRaceVoice_Play(SlipRaceSession_gameSound->digitalCard,
							                   lapPositionSpeech[lapRacer->racePosition], &calls);
						}
						if (lapRacer->objectOffset == SlipRacePlayer_playerTwoObject) {
							SlipRaceCamera_StoreLapTime(&SlipRaceSession_cameraState, 2, lapTime);
						}
						completedLap = lapRacer->lapNumber++;
						if (lapRacer->objectOffset == SlipRacePlayer_playerOneObject) {
							SlipRaceCamera_StoreLapNumber(&SlipRaceSession_cameraState, 1, completedLap);
						}
						if (lapRacer->objectOffset == SlipRacePlayer_playerTwoObject) {
							SlipRaceCamera_StoreLapNumber(&SlipRaceSession_cameraState, 2, completedLap);
						}
						if (SlipRace_type != SLIP_RACE_TYPE_PRACTICE &&
						    (int16_t)completedLap >= (int16_t)SlipRacePlayer_lapCount) {
							uint16_t finishedRacers = 0;
							uint16_t finishIndex;

							lapRacer->finished = 1;
							for (finishIndex = 0; finishIndex < activeRacerCount; ++finishIndex) {
								if (racerTable[finishIndex].finished != 0) {
									++finishedRacers;
								}
							}
							lapRacer->racePosition = finishedRacers;
							SlipRaceCamera_FinishRacer(&SlipRaceSession_cameraState, lapRacer->objectOffset,
							                           SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject);

							if (lapRacer->racerType == 2) {
								SlipRaceVoiceCalls calls = SlipRaceVoiceHost_Calls(SlipRaceSession_gameSound);
								SlipRaceVoice_Play(SlipRaceSession_gameSound->digitalCard,
								                   finishingDriverSpeech[lapRacer->tuningIndex], &calls);
							}
						}
					}
				}
			}
		}
	}

	for (racerIndex = 0; racerIndex < activeRacerCount; ++racerIndex) {
		SlipRaceRacerState *const progressRacer = &racerTable[racerIndex];

		progressRacer->trackProgress = SlipTrackWorld_RaceProgress(
		    progressRacer->objectOffset, SlipRaceSession_playerContext.slotListBase,
		    SlipRaceSession_playerContext.slotListBytes, SlipRaceSession_playerContext.slotListBaseOffset,
		    SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, SlipRaceSession_trackBundle.trdPayload.data,
		    SlipRaceSession_trackBundle.trdPayload.size, SlipRaceSession_trackBundle.trdBaseToken,
		    SlipRaceSession_trackBundle.trcPayload.data, SlipRaceSession_trackBundle.trcPayload.size,
		    SlipRaceSession_trackBundle.trcBaseToken, SlipRaceSession_cellTableHost,
		    (SLIP_TRACK_WORLD_CELL_TABLE_BYTES));
		if (progressRacer->finished == 0) {
			progressRacer->previousRacePosition = progressRacer->racePosition;
			progressRacer->racePosition = UINT16_MAX;
		}
	}

	for (position = 1; (int16_t)position <= (int16_t)activeRacerCount; ++position) {
		SlipRaceRacerState *candidate = NULL;
		int16_t bestLap = 0;
		int32_t bestProgress = INT32_MAX;

		for (racerIndex = 0; racerIndex < activeRacerCount; ++racerIndex) {
			SlipRaceRacerState *const positionRacer = &racerTable[racerIndex];
			int16_t adjustedLap;

			if (positionRacer->racePosition == position) {
				candidate = positionRacer;
				break;
			}
			if (positionRacer->racePosition != UINT16_MAX) {
				continue;
			}
			adjustedLap = (int16_t)(positionRacer->lapNumber - positionRacer->wrongWay);
			if (adjustedLap < 0) {
				adjustedLap = 0;
			}
			if (adjustedLap > bestLap ||
			    (adjustedLap == bestLap && (int32_t)positionRacer->trackProgress <= bestProgress)) {
				candidate = positionRacer;
				bestProgress = (int32_t)positionRacer->trackProgress;
				bestLap = adjustedLap;
			}
		}
		if (candidate == NULL) {

			SlipRuntime_Fatal("ERROR: An unforseen problem has been encountered - please restart Slipstream.");
		}
		candidate->racePosition = position;
	}

	if (destroyedLocalRacers == 2 || (SlipRace_gameMode != 0 && activeRacerCount == 2 && activeLocalRacers < 2) ||
	    ((SlipRace_gameMode == 0 || activeRacerCount != 2) && activeLocalRacers == 0)) {
		if (SlipRaceSession_finishDelay == UINT16_MAX) {
			SlipRaceSession_finishDelay = 5000;
		}
	}
}

static void SlipRaceSession_FinalizeRacers(uint16_t trackIndex) {
	static const uint32_t trackTimeLimit[11] = {
	    0, 90000, 90000, 90000, 90000, 90000, 90000, 90000, 90000, 90000, 90000,
	};
	uint32_t totalTrackLength;
	uint16_t racerIndex;

	totalTrackLength = SlipTrackWorld_TotalLength(SlipRaceSession_trackBundle.trdPayload.data);
	for (racerIndex = 0; racerIndex < SlipRace_activeRacerTable->racerCount; ++racerIndex) {
		SlipRaceRacerState *const racer = &SlipRaceSession_racerStates[racerIndex];
		int16_t lapNumber;

		if (racer->destroyed != 0 || racer->finished != 0) {
			continue;
		}
		racer->totalRaceTime +=
		    (uint32_t)(((uint64_t)racer->trackProgress * trackTimeLimit[trackIndex]) / totalTrackLength);
		lapNumber = (int16_t)racer->lapNumber;
		while (lapNumber < (int16_t)SlipRacePlayer_lapCount) {
			racer->totalRaceTime += trackTimeLimit[trackIndex];
			++lapNumber;
		}
		racer->finished = 1;
	}
}

static void SlipRaceSession_DrawCockpitSight(uint16_t craftObject) {
	const SlipRacePlayerPrivateRecord *playerState;
	const SlipRaceRacerState *racerState;
	uint32_t selectedWeapon;
	uint16_t targetObject;

	Raster_SetClipRect((int16_t)SlipRendererHost_state.projection.minX, (int16_t)SlipRendererHost_state.projection.minY,
	                   (int16_t)SlipRendererHost_state.projection.maxX,
	                   (int16_t)SlipRendererHost_state.projection.maxY);
	playerState = &SlipRaceSession_playerStates[craftObject / 0xaeu];
	racerState = SlipRacePlayer_RacerState(craftObject);
	selectedWeapon = playerState->weaponSelection;
	if (selectedWeapon == 0) {
		selectedWeapon = 0;
	} else if (selectedWeapon == 1) {
		selectedWeapon = racerState->primaryWeaponIndex;
	} else if (selectedWeapon == 2) {
		selectedWeapon = racerState->secondaryWeaponIndex;
	} else {
		return;
	}
	targetObject = playerState->targetObject;
	if (selectedWeapon < 12u && SlipRacePlayer_WeaponTargetRange(SlipRacePlayer_records, selectedWeapon) != 0) {

		SlipRaceHud_DrawNormalSight(&SlipRaceSession_hudAssets, g_framebuffer, SLIPSTREAM_SCREEN_WIDTH,
		                            SlipRendererHost_state.projection.centerX,
		                            SlipRendererHost_state.projection.centerY);
		if (targetObject != 0) {
			SlipView3DVec32 targetView;
			SlipDraw3DVec32 targetPoint;
			uint32_t targetClipMask;
			int32_t targetScreenX;
			int32_t targetScreenY;

			if (SlipObject_ViewPosition(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, targetObject,
			                            &targetView)) {
				targetPoint = (SlipDraw3DVec32){targetView.x, targetView.y, targetView.z};
				if (targetView.z >= (int32_t)SlipDraw3D_minimumDepth &&
				    targetView.z <= (int32_t)SlipDraw3D_maximumDepth) {
					targetClipMask = TrackView_ProjectMask(targetView, &SlipRaceSession_lastTrackViewContext.frustum);
					if (targetClipMask == 0 && SlipDraw3D_ProjectScreen(targetPoint, &SlipRendererHost_state.projection,
					                                                    &targetScreenX, &targetScreenY)) {
						SlipRaceHud_DrawTargetSight(&SlipRaceSession_hudAssets, g_framebuffer, SLIPSTREAM_SCREEN_WIDTH,
						                            targetScreenX, targetScreenY, SlipRace_drawPageToggle);
					}
				}
			}
		}
	}
}

static void SlipRaceSession_DrawHudBarSegment(int x0, int y0, int x1, int y1, uint8_t border, uint8_t fill) {
	if (y1 != y0) {
		Raster_DrawLineSolid(border, (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y0);
		Raster_DrawLineSolid(border, (int16_t)x0, (int16_t)(y0 + 2), (int16_t)x1, (int16_t)(y0 + 2));
		++y0;
		y1 = y0;
	}
	Raster_DrawLineSolid(fill, (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1);
}

static void SlipRaceSession_DrawDamageBar(int x0, int y0, int x1, int y1, uint32_t percent) {
	if (percent == 0) {
		return;
	}
	if (percent != 100u) {
		x1 = x0 + (int)(((uint32_t)(x1 - x0 + 1) * percent) / 100u);
	}
	SlipRaceSession_DrawHudBarSegment(x0, y0, x1, y1, 0x33u, 0x39u);
}

static void SlipRaceSession_DrawChargeBar(int x0, int y0, int x1, int y1, uint32_t charge) {
	if (charge == 0x4000u) {
		SlipRaceSession_DrawHudBarSegment(x0, y0, x1, y1, 0x78u, 0xfcu);
		return;
	}
	if (charge != 0) {
		const int chargeFillEndX = x0 + (int)(((uint32_t)(x1 - x0 + 1) * charge) / 0x4000u);

		SlipRaceSession_DrawHudBarSegment(x0, y0, chargeFillEndX, y1, 0x85u, 0xfcu);
		x0 = chargeFillEndX;
	}
	SlipRaceSession_DrawHudBarSegment(x0, y0, x1, y1, 0x33u, 0x39u);
}

static void SlipRaceSession_DrawTurboIndicator(uint16_t turboActive, uint16_t charge, int consoleMode,
                                               bool splitScreen) {
	int y = 0xa9;

	(void)turboActive;
	SlipText_SelectResourceFont(&SlipText_state, SlipMenu_resources.smallestFont, &SlipRaceHud_fontResources);
	SlipText_SetColor(&SlipText_state, UINT16_MAX);
	SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, 0x81, 0xbf);
	if (splitScreen) {

		y = SlipRendererHost_state.projection.maxY + 1 + (consoleMode == 0 ? 4 : 0);
		SlipRaceSession_DrawChargeBar(0x89, y + 1, 0xb6, y + 3, charge);
		SlipTextPosition compactTurboLabelPosition = {0, (int16_t)(y)};
		SlipText_Draw(&SlipText_state, "Turbo", NULL, &compactTurboLabelPosition);
		return;
	}
	if (consoleMode == 0) {
		y += 7;
	}
	SlipTextPosition turboLabelPosition = {0, (int16_t)(y)};
	SlipText_Draw(&SlipText_state, "Turbo", NULL, &turboLabelPosition);
	SlipRaceSession_DrawChargeBar(0x89, consoleMode == 0 ? 0xb6 : 0xaf, 0xb6, consoleMode == 0 ? 0xb8 : 0xb1, charge);
}

static void SlipRaceSession_DrawWeaponIndicator(uint32_t selectedWeapon, uint32_t ammo, uint16_t charge,
                                                int consoleMode, bool splitScreen) {
	int y = 0xa8;
	char weaponLabel[24];

	if (selectedWeapon >= 12u) {
		return;
	}
	SlipText_SelectResourceFont(&SlipText_state, SlipMenu_resources.smallestFont, &SlipRaceHud_fontResources);
	SlipText_SetColor(&SlipText_state, UINT16_MAX);
	SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, 0x80, 0xbf);
	SlipRacePlayer_BuildWeaponLabel(SlipRacePlayer_records, selectedWeapon, ammo, weaponLabel);
	if (splitScreen) {

		y = SlipRendererHost_state.projection.maxY + 1 + (consoleMode == 0 ? 4 : 0);
		if (ammo == UINT32_MAX)
			SlipRaceSession_DrawChargeBar(0x89, y + 1, 0xb6, y + 3, charge);
		SlipTextPosition compactWeaponLabelPosition = {0, (int16_t)(y)};
		SlipText_Draw(&SlipText_state, weaponLabel, NULL, &compactWeaponLabelPosition);
		return;
	}
	if (ammo == UINT32_MAX) {
		++y;
	}
	if (consoleMode == 0) {
		y += 7;
	}
	SlipTextPosition weaponLabelPosition = {0, (int16_t)(y)};
	SlipText_Draw(&SlipText_state, weaponLabel, NULL, &weaponLabelPosition);
	if (ammo != UINT32_MAX) {
		SlipTextPosition loadingPosition = {0, (int16_t)(y + 6)};
		SlipText_Draw(&SlipText_state, charge == 0x4000u ? "READY" : "LOADING", NULL, &loadingPosition);
	} else {
		SlipRaceSession_DrawChargeBar(0x89, consoleMode == 0 ? 0xb6 : 0xaf, 0xb6, consoleMode == 0 ? 0xb8 : 0xb1,
		                              charge);
	}
}

static void SlipRaceSession_DrawPlayerIndicators(int consoleMode, uint16_t craftObject, uint32_t consoleY) {
	const SlipRacePlayerPrivateRecord *const privateState = &SlipRaceSession_playerStates[craftObject / 0xaeu];
	const SlipRaceRacerState *const racerState = SlipRacePlayer_RacerState(craftObject);
	uint32_t movementDamage;
	uint32_t handlingDamage;
	uint32_t selectedWeapon;
	uint32_t ammo;
	uint16_t charge;
	int damageBarTop;
	int damageBarBottom;
	int16_t savedClipMinX;
	int16_t savedClipMinY;
	int16_t savedClipMaxX;
	int16_t savedClipMaxY;

	if (racerState == NULL || (SlipRaceSession_hudAssets.flags & 1u) != 0) {
		return;
	}
	Raster_GetClipRect(&savedClipMinX, &savedClipMinY, &savedClipMaxX, &savedClipMaxY);
	Raster_SetClipRect(0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1);
	movementDamage = racerState->movementDamageQ16;
	handlingDamage = racerState->handlingDamageQ16;
	if (privateState->amplifiedControlsTimer != 0 || privateState->invertedControlsTimer != 0) {
		handlingDamage = 0x00640000u;
	}
	damageBarTop = consoleMode == 0 ? 0xb9 : 0xb8;
	damageBarBottom = consoleMode == 0 ? 0xbb : 0xba;
	if (SlipRace_gameMode == 1) {

		damageBarTop = (int)consoleY + 8;
		damageBarBottom = damageBarTop + 2;
	}
	SlipRaceSession_DrawDamageBar(0x18, damageBarTop, 0x43, damageBarBottom, movementDamage >> 16);
	SlipRaceSession_DrawDamageBar(0xfc, damageBarTop, 0x127, damageBarBottom, handlingDamage >> 16);

	if ((privateState->powerupSpeedTimer != 0 || privateState->powerupActive != 0) &&
	    SlipRaceSession_hudAssets.turboLoaded) {

		SlipRaceHud_DrawSpriteResource(SlipRaceSession_hudAssets.turboSprite, g_framebuffer, SLIPSTREAM_SCREEN_WIDTH,
		                               SlipRendererHost_state.projection.minX + 0xbe,
		                               SlipRendererHost_state.projection.maxY + (SlipRace_gameMode == 1 ? 1 : 2) +
		                                   (consoleMode == 0 ? 5 : 0));
	}

	selectedWeapon = privateState->weaponSelection;

	if (selectedWeapon <= 2u) {
		if (selectedWeapon == 0) {
			selectedWeapon = 0;
			ammo = UINT32_MAX;
			charge = privateState->weaponCharge[0];
		} else if (selectedWeapon == 1u) {
			selectedWeapon = racerState->primaryWeaponIndex;
			ammo = racerState->primaryWeaponAmmo;
			charge = privateState->weaponCharge[1];
		} else {
			selectedWeapon = racerState->secondaryWeaponIndex;
			ammo = racerState->secondaryWeaponAmmo;
			charge = privateState->weaponCharge[2];
		}
		SlipRaceSession_DrawWeaponIndicator(selectedWeapon, ammo, charge, consoleMode, SlipRace_gameMode == 1);
	} else if (selectedWeapon == 3u) {
		SlipRaceSession_DrawTurboIndicator(privateState->powerupActive, privateState->powerupCharge, consoleMode,
		                                   SlipRace_gameMode == 1);
	}
	Raster_SetClipRect(savedClipMinX, savedClipMinY, savedClipMaxX, savedClipMaxY);
}

static void SlipRaceSession_DrawCameraName(uint16_t view, uint16_t dispatchedMode) {
	uint16_t *const timer = view == 1u ? &SlipRaceSession_cameraState.viewOneModeNameTimer
	                                   : &SlipRaceSession_cameraState.viewTwoModeNameTimer;
	uint16_t delta;
	const char *name;
	if (*timer == 0)
		return;
	delta = (uint16_t)SlipFrameTimer_Values().deltaMilliseconds;

	*timer = *timer < delta ? 0 : (uint16_t)(*timer - delta);
	name = SlipRaceCamera_ModeName(dispatchedMode);
	SlipText_SelectResourceFont(&SlipText_state, SlipRaceSession_hudAssets.timeFont, &SlipRaceHud_fontResources);
	SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, 0, 319);
	SlipText_SetColor(&SlipText_state, 0xff);
	SlipRaceHud_DrawLayoutHost(g_framebuffer, SLIPSTREAM_SCREEN_WIDTH, 0, SlipRendererHost_state.projection.minY + 14,
	                           name, NULL);
}

static void SlipRaceSession_DrawFinalLapNotification(uint16_t playerNumber) {
	const uint16_t playerIndex = playerNumber == 1u ? 0u : 1u;
	uint16_t *const timer = &SlipRaceSession_cameraState.lapNotificationTimer[playerIndex];
	uint16_t displayedLap;
	uint16_t elapsedTime;

	if (*timer == 0) {
		return;
	}
	elapsedTime = (uint16_t)SlipFrameTimer_Values().deltaMilliseconds;
	*timer = (uint16_t)(*timer - elapsedTime);
	if ((int16_t)*timer < 0) {
		*timer = 0;
	}
	displayedLap = (uint16_t)(SlipRaceSession_cameraState.lapNotificationValue[playerIndex] + 1u);
	SlipText_SelectResourceFont(&SlipText_state, SlipRaceSession_hudAssets.timeFont, &SlipRaceHud_fontResources);
	SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, 0, 319);
	SlipText_SetColor(&SlipText_state, 0xff);
	if (SlipRace_type == SLIP_RACE_TYPE_PRACTICE || displayedLap != SlipRacePlayer_lapCount) {
		return;
	}
	SlipRaceHud_DrawLayoutHost(g_framebuffer, SLIPSTREAM_SCREEN_WIDTH, 0, SlipRendererHost_state.projection.minY + 5,
	                           "Final Lap!!", NULL);
}

static bool SlipRaceSession_ActivateCamera(void *context, uint16_t mode, uint16_t view) {
	(void)context;
	if (mode == 4u)
		return SlipRaceCamera_ActivateChase(
		    &SlipRaceSession_cameraState, SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES,
		    view == 1u ? SlipRacePlayer_playerOneObject : SlipRacePlayer_playerTwoObject, view);
	if (mode == 6u) {
		SlipRaceCamera_ActivateTv(&SlipRaceSession_cameraState);
		return true;
	}
	return false;
}

static void SlipRaceSession_TickTimer(uint16_t *remainingTime, uint16_t elapsedTime) {
	if (*remainingTime != 0) {
		*remainingTime = (uint16_t)(*remainingTime - elapsedTime);
		if ((int16_t)*remainingTime < 0) {
			*remainingTime = 0;
		}
	}
}

static void SlipRaceSession_TickCameraTimers(void) {
	const uint16_t elapsedTime = (uint16_t)SlipFrameTimer_Values().deltaMilliseconds;

	SlipRaceSession_TickTimer(&SlipRaceSession_cameraState.lapTimeTimer[0], elapsedTime);
	SlipRaceSession_TickTimer(&SlipRaceSession_cameraState.lapTimeTimer[1], elapsedTime);
	SlipRaceSession_TickTimer(&SlipRaceSession_cameraState.shake[0], elapsedTime);
	SlipRaceSession_TickTimer(&SlipRaceSession_cameraState.shake[1], elapsedTime);
	if (SlipRace_playerOneFinished != 0) {
		SlipRaceSession_TickTimer(&SlipRace_playerOneFinishDelay, elapsedTime);
	}
	if (SlipRace_playerTwoFinished != 0) {
		SlipRaceSession_TickTimer(&SlipRace_playerTwoFinishDelay, elapsedTime);
	}
}

static const char *const SlipRaceSession_racerArtNames[10] = {"RACER0.ART", "RACER1.ART", "RACER2.ART", "RACER3.ART",
                                                              "RACER4.ART", "RACER5.ART", "RACER6.ART", "RACER7.ART",
                                                              "RACER8.ART", "RACER9.ART"};

static void SlipRaceSession_LoadPauseAssets(void) {
	SlipText_SelectResourceFont(&SlipText_state, SlipMenu_resources.smallFont, &SlipRaceHud_fontResources);
	SlipConfigHost_calls.language(NULL);
	if (!SlipStringTable_Load(&SlipStringTable_state, "PAUSED  ", &SlipRaceSession_pauseStringResources,
	                          &SlipRaceSession_pauseStrings))
		SlipGame_ResourceFailure();
	SlipRaceSession_pauseAssetsReady = true;
}

static void SlipRaceSession_DrawPauseOption(const SlipRacePauseRect *rect, const char *text, bool selected) {
	Raster_DrawLineClipped(0x1c, rect->minX, rect->minY, rect->maxX, rect->minY);
	Raster_DrawLineClipped(0x1c, rect->minX, rect->minY, rect->minX, rect->maxY);
	Raster_DrawLineClipped(0x0c, rect->maxX, rect->minY, rect->maxX, rect->maxY);
	Raster_DrawLineClipped(0x0c, rect->minX, rect->maxY, rect->maxX, rect->maxY);
	Raster_FillRectClipped(selected ? 0xfd : 0x14, (int16_t)(rect->minX + 1), (int16_t)(rect->minY + 1),
	                       (int16_t)(rect->maxX - 1), (int16_t)(rect->maxY - 1));
	SlipText_SelectResourceFont(&SlipText_state, SlipMenu_resources.smallFont, &SlipRaceHud_fontResources);
	SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, rect->minX, rect->maxX);
	SlipText_SetColor(&SlipText_state, 0xff);
	SlipTextFontMetrics metrics = {.lineSpacing = 0xff,
	                               .glyphHeight = UINT16_MAX,
	                               .glyphWidth = (uint16_t)rect->minX,
	                               .firstChar = (uint8_t)(rect->maxX >> 8),
	                               .lastChar = (uint8_t)rect->maxX};
	SlipText_FontMetrics(&SlipText_state, &metrics);
	SlipText_DrawCentered(&SlipText_state, text, NULL, (int16_t)metrics.glyphWidth, (uint32_t)rect->minY,
	                      (uint32_t)rect->maxY);
}

static void SlipRaceSession_DrawPauseMenu(void) {
	size_t optionIndex;

	if (SlipRaceSession_pauseState == SLIP_RACE_PAUSE_RUNNING || !SlipRaceSession_pauseAssetsReady) {
		return;
	}
	if (SlipRaceSession_pauseState == SLIP_RACE_PAUSE_REMOTE) {
		const char *const text = SlipStringTable_Get(SlipRaceSession_pauseStrings, SLIP_RACE_PAUSE_REMOTE_TAG,
		                                             &SlipRaceSession_pauseStringResources);
		SlipRaceSession_DrawPauseOption(&SlipRaceSession_remotePauseRect, text, false);
		SlipStringTable_Unlock(SlipRaceSession_pauseStrings, &SlipRaceSession_pauseStringResources);
		return;
	}

	Raster_SetClipRect(0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1);
	for (optionIndex = 0; optionIndex < 4u; ++optionIndex) {
		const char *const text =
		    SlipStringTable_Get(SlipRaceSession_pauseStrings, SLIP_RACE_PAUSE_FIRST_OPTION_TAG + (uint32_t)optionIndex,
		                        &SlipRaceSession_pauseStringResources);
		SlipRaceSession_DrawPauseOption(&SlipRaceSession_pauseRects[optionIndex], text,
		                                SlipRaceSession_pauseSelection == optionIndex + 1u);
		SlipStringTable_Unlock(SlipRaceSession_pauseStrings, &SlipRaceSession_pauseStringResources);
	}
}

static void SlipRaceSession_MenuNavPoll(bool inputPressed[256]) {
	int16_t nextItem;

	if (inputPressed[SLIP_INPUT_SCAN_UP]) {
		inputPressed[SLIP_INPUT_SCAN_UP] = false;
		nextItem = SlipRaceSession_pauseNavigation.up[SlipRaceSession_pauseNavigation.currentItem];
		if (nextItem >= 0) {
			SlipRaceSession_pauseNavigation.currentItem = (uint16_t)nextItem;
		}
	}
	if (inputPressed[SLIP_INPUT_SCAN_DOWN]) {
		inputPressed[SLIP_INPUT_SCAN_DOWN] = false;
		nextItem = SlipRaceSession_pauseNavigation.down[SlipRaceSession_pauseNavigation.currentItem];
		if (nextItem >= 0) {
			SlipRaceSession_pauseNavigation.currentItem = (uint16_t)nextItem;
		}
	}
	if (inputPressed[SLIP_INPUT_SCAN_LEFT]) {
		inputPressed[SLIP_INPUT_SCAN_LEFT] = false;
		nextItem = SlipRaceSession_pauseNavigation.left[SlipRaceSession_pauseNavigation.currentItem];
		if (nextItem >= 0) {
			SlipRaceSession_pauseNavigation.currentItem = (uint16_t)nextItem;
		}
	}
	if (inputPressed[SLIP_INPUT_SCAN_RIGHT]) {
		inputPressed[SLIP_INPUT_SCAN_RIGHT] = false;
		nextItem = SlipRaceSession_pauseNavigation.right[SlipRaceSession_pauseNavigation.currentItem];
		if (nextItem >= 0) {
			SlipRaceSession_pauseNavigation.currentItem = (uint16_t)nextItem;
		}
	}
	SlipRaceSession_menuMouseX +=
	    (((int32_t)SlipRaceSession_pauseNavigation.centers[SlipRaceSession_pauseNavigation.currentItem][0] << 16) -
	     SlipRaceSession_menuMouseX) >>
	    1;
	SlipRaceSession_menuMouseY +=
	    (((int32_t)SlipRaceSession_pauseNavigation.centers[SlipRaceSession_pauseNavigation.currentItem][1] << 16) -
	     SlipRaceSession_menuMouseY) >>
	    1;
}

static uint32_t SlipRaceSession_HitTestPauseMenu(int mouseX, int mouseY) {
	size_t optionIndex;

	for (optionIndex = 0; optionIndex < 4u; ++optionIndex) {
		const SlipRacePauseRect *const rect = &SlipRaceSession_pauseRects[optionIndex];

		if (mouseX >= rect->minX && mouseX <= rect->maxX && mouseY >= rect->minY && mouseY <= rect->maxY) {
			return (uint32_t)optionIndex + 1u;
		}
	}
	return 0;
}

static SlipRacePauseAction SlipRaceSession_UpdatePauseMenu(bool inputPressed[256], int mouseX, int mouseY) {
	if ((SlipRace_controls.actions & SLIP_ACTION_PAUSE) != 0) {
		if (SlipRaceSession_pauseState != SLIP_RACE_PAUSE_REMOTE) {
			if (SlipRaceSession_pauseState == SLIP_RACE_PAUSE_RUNNING) {
				if ((SlipRacePlayer_thirdControls.actions & SLIP_ACTION_PAUSE) != 0 &&
				    SlipRace_secondPlayerEnabled == 0) {
					SlipRaceSession_pauseState = SLIP_RACE_PAUSE_REMOTE;
				} else {
					SlipRaceSession_pauseState = SLIP_RACE_PAUSE_LOCAL;
				}
			} else {
				SlipRaceSession_pauseState = SLIP_RACE_PAUSE_RUNNING;
			}
		}
	} else if ((SlipRacePlayer_thirdControls.actions & SLIP_ACTION_PAUSE) != 0) {
		if (SlipRaceSession_pauseState == SLIP_RACE_PAUSE_REMOTE) {
			SlipRaceSession_pauseState = SLIP_RACE_PAUSE_RUNNING;
		} else if (SlipRaceSession_pauseState != SLIP_RACE_PAUSE_LOCAL) {
			SlipRaceSession_pauseState = SLIP_RACE_PAUSE_REMOTE;
		}
	}

	if (SlipRaceSession_pauseState == SLIP_RACE_PAUSE_RUNNING && SlipRaceRecording_state.installed) {
		SlipRacePlayerControl controls[2] = {SlipRace_controls, {0}};
		if (SlipRace_gameMode == 1)
			controls[1] = SlipRacePlayer_playerTwoControls;
		else if (SlipRace_gameMode != 0)
			controls[1] = SlipRacePlayer_thirdControls;
		if (!SlipRaceSession_replay) {
			SlipRaceRecording_Write(&SlipRaceRecording_state, controls);
		} else {

			bool ended = SlipRaceRecording_Read(&SlipRaceRecording_state, &SlipRaceRecording_state.host, controls);
			SlipRace_controls = controls[0];
			if (ended)
				return SLIP_RACE_PAUSE_ACTION_REPLAY_END;
			if (SlipRace_gameMode == 1)
				SlipRacePlayer_playerTwoControls = controls[1];
			else if (SlipRace_gameMode != 0)
				SlipRacePlayer_thirdControls = controls[1];
		}
	}

	bool cameraKeysReady = SlipRaceCamera_PollKeys(&SlipRaceSession_cameraState, SlipRace_gameMode, inputPressed,
	                                               SlipRaceSession_ActivateCamera, NULL);
	assert(cameraKeysReady);

	if (SlipRace_gameMode != 1) {
		if (inputPressed[0x40]) {
			inputPressed[0x40] = false;
			SlipConfig_ToggleRearMonitor();
		}
		if (inputPressed[0x41]) {
			inputPressed[0x41] = false;
			SlipConfig_ToggleWeaponsMonitor();
		}
	}
	SlipRaceSession_TickCameraTimers();
	if (SlipRaceSession_pauseState != SLIP_RACE_PAUSE_LOCAL) {
		return SLIP_RACE_PAUSE_ACTION_NONE;
	}

	if (mouseX >= 0 && mouseY >= 0) {
		SlipRaceSession_menuMouseX = mouseX * 0x10000;
		SlipRaceSession_menuMouseY = mouseY * 0x10000;
	} else {
		SlipRaceSession_MenuNavPoll(inputPressed);
	}
	SlipRaceSession_pauseSelection =
	    SlipRaceSession_HitTestPauseMenu(SlipRaceSession_menuMouseX >> 16, SlipRaceSession_menuMouseY >> 16);
	if (SlipRaceSession_pauseSelection == 0) {
		return SLIP_RACE_PAUSE_ACTION_NONE;
	}
	if (inputPressed[SLIP_INPUT_MOUSE_LEFT]) {
		inputPressed[SLIP_INPUT_MOUSE_LEFT] = false;
	} else if (inputPressed[SLIP_INPUT_SCAN_ENTER]) {
		inputPressed[SLIP_INPUT_SCAN_ENTER] = false;
	} else {
		return SLIP_RACE_PAUSE_ACTION_NONE;
	}
	return (SlipRacePauseAction)SlipRaceSession_pauseSelection;
}

void SlipRaceSession_ConfigurationReturn(void) {

	SlipMenuMusic_RaceConfigurationReturn();

	SlipRaceHud_ResetConsole(&SlipRaceSession_hudState);
	SlipRaceSession_pauseSelection = 0;
}

static void SlipRaceSession_LoadPalette(const char *name) {
	uint16_t resource;
	if (!SlipResourceHost_Load(NULL, name, &resource))
		SlipGame_ResourceFailure();
	const uint8_t *const palette = SlipResourceHost_Lock(NULL, resource);
	SlipVgaDac_WriteRange(SlipBytes_ReadLE16(palette), SlipBytes_ReadLE16(palette + 2), palette + 4);
	SlipResourceHost_Unlock(NULL, resource);
	SlipResourceHost_Release(NULL, resource);
}

static bool SlipRaceSession_LoadWorld(uint16_t axTrack, const char *const *archives, size_t archiveCount,
                                      SlipTrackWorldSlotDrawInstall *drawOut, SlipTrackWorldSlotListInstall *listOut) {
	SlipObjectInitTable objectInit;
	SlipTrackWorldSlotDrawInstall slotDraw;
	SlipTrackWorldSlotListInstall slotList;
	SlipTrackWorldCellTableBuild cellTable;
	SlipRace_viewIndex = axTrack;

	if (!SlipObject_InitTableFresh(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, 100u, &objectInit))
		return false;
	SlipDrawListHost_Initialize(50u);
	if (!SlipArticSlot_InitializeResourcePool(20u, &SlipRaceSession_articResourceCalls, &SlipRaceSession_articPool))
		return false;

	TrackView_InitializeClouds(&SlipRaceSession_cloudState);

	bool installRenderer = SlipRendererHost_state.initialized == 0;
	SlipRenderer_Initialize(&SlipRendererHost_state, SLIP_RACE_VERTEX_CAPACITY, &SlipRendererHost_lifecycleCalls);
	SlipRaceSession_vertices = SlipRendererHost_state.vertexBase;
	SlipRaceSession_drawStates = SlipResourceStorage_DrawStateRecords(
	    SlipResource_handles[SlipRendererHost_state.stateResource].block, SlipRendererHost_state.stateCount);
	if (installRenderer) {
		SlipDraw3DRecordPoolInit poolInit;
		SlipDraw3DRecordPool *const pool =
		    SlipResourceStorage_RecordPool(SlipResource_handles[SlipRendererHost_state.polygonResource].block);
		if (!SlipDraw3D_InitRecordPool(pool, &poolInit))
			return false;
	}
	SlipShape3D_Initialize();
	SlipRaceSession_LoadPalette(SlipRace_paletteNames[axTrack - 1]);
	SlipView3D_FreeMaths(&SlipRaceSession_maths);
	if (!SlipView3D_LoadMathsFromArchives(&SlipRaceSession_maths, archives, archiveCount)) {
		return false;
	}

	SlipRaceSession_materialTable = NULL;
	SlipRaceSession_materialTableBytes = 0;
	memset(&SlipRaceSession_resourceRegistry, 0, sizeof(SlipRaceSession_resourceRegistry));
	if (!TrackView_BuildTrackMaterialTable(archives, archiveCount, (uint8_t)(axTrack - 1u),
	                                       &SlipRaceSession_materialTable, &SlipRaceSession_materialTableBytes,
	                                       &SlipRaceSession_materialGlobal, &SlipRaceSession_resourceRegistry)) {
		return false;
	}

	SlipRaceCollision_objectTable = SlipRaceSession_objectTableHost;
	SlipRaceCollision_objectTableBytes = SLIP_OBJECT_TABLE_DOS_BYTES;
	if (!SlipRaceCollisionHost_Initialize(0x40u))
		return false;

	if (!SlipRaceSession_InitializeTrack(&slotDraw, &slotList) || !SlipRaceSession_InitializeTrackDraw())
		return false;

	SlipRaceSession_currentTrackRecordAddress = 0;
	SlipRaceSession_primitiveFlagsReady = 0;
	SlipRaceSession_underSeaColor = 0;
	SlipTrackAssets_FreeBundle(&SlipRaceSession_trackBundle, TrackView_ReleaseResource,
	                           &SlipRaceSession_resourceRegistry);
	if (!SlipTrackAssets_LoadBundle(SlipRace_trackNames[axTrack - 1u], &SlipRaceSession_trackBundle,
	                                &SlipRaceSession_trackAssetCalls) ||
	    !SlipTrackWorld_CellTableBuild(
	        SlipRaceSession_cellTableHost, (SLIP_TRACK_WORLD_CELL_TABLE_BYTES),
	        SlipRaceSession_trackBundle.trkPayload.data, SlipRaceSession_trackBundle.trkPayload.size, 0,
	        SlipRaceSession_trackBundle.trdBaseToken, SlipRaceSession_cellVisitsHost,
	        sizeof(SlipRaceSession_cellVisitsHost) / sizeof(SlipRaceSession_cellVisitsHost[0]), &cellTable)) {
		return false;
	}

	SlipTrackWorld_ClearSlotRecordLinks(SlipRaceSession_trackBundle.trdHandle,
	                                    SlipRaceSession_trackBundle.trdPayload.data);
	SlipTrackAssets_LoadScenery(&SlipRaceSession_trackBundle);

	SlipTrackWorld_SetLapDistance(SlipRaceSession_trackBundle.trdPayload.data);
	SlipTrackWorld_BindActorRingHostMappings(SlipRaceSession_trackBundle.trdPayload.data,
	                                         SlipRaceSession_trackBundle.trdPayload.size, SlipRaceSession_slotDrawHost,
	                                         (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u), slotDraw.baseAddress,
	                                         SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES);

	if (!SlipTrackWorld_FindDoors(1, SlipRaceSession_trackBundle.trdPayload.data,
	                              SlipRaceSession_trackBundle.trdPayload.size, SlipRaceSession_trackBundle.trdBaseToken,
	                              SlipRaceSession_trackBundle.trcPayload.data,
	                              SlipRaceSession_trackBundle.trcPayload.size)) {
		return false;
	}

	if (!TrackView_ResolveComponentMaterials((uint8_t *)SlipRaceSession_trackBundle.trcPayload.data,
	                                         SlipRaceSession_trackBundle.trcPayload.size, SlipRaceSession_materialTable,
	                                         SlipRaceSession_materialTableBytes, SlipRaceSession_materialGlobal,
	                                         &SlipRaceSession_materialInit)) {
		return false;
	}

	if (!SlipTrackWorld_InitRefuel(
	        &SlipTrackWorld_refuelInitialized, &SlipRacePlayer_refuelSection, 1u,
	        SlipRaceSession_trackBundle.trdPayload.data, SlipRaceSession_trackBundle.trdPayload.size,
	        SlipRaceSession_trackBundle.trdBaseToken, SlipRaceSession_trackBundle.trcPayload.data,
	        SlipRaceSession_trackBundle.trcPayload.size, SlipRaceSession_materialTable,
	        SlipRaceSession_materialTableBytes)) {
		return false;
	}

	SlipTrackWorld_ResetSlots(1u, SlipRaceSession_trackBundle.trdPayload.data,
	                          SlipRaceSession_trackBundle.trdPayload.size, SlipRaceSession_trackBundle.trdBaseToken,
	                          SlipRaceSession_slotDrawHost, (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u),
	                          slotDraw.baseAddress, slotDraw.ring.baseAddress, SlipRaceSession_slotDrawCount,
	                          (uint8_t *)(void *)SlipRaceSession_trackSlots,
	                          ((0x40u + 8u + 2u) * sizeof(SlipTrackSlotRecord)), slotList.baseAddress,
	                          slotList.activeListAddress, slotList.freeListAddress);
	SlipRaceCollision_ResetBodyLists();
	SlipDraw3D_ResetLighting();
	SlipRace_viewIndex = axTrack;
	SlipRace_ApplyViewDepth((SlipRaceTrackId)axTrack);
	SlipDraw3D_SetAmbientLight(0x1000u);
	SlipDraw3D_SetLightVector(0, (int16_t)0xc000, 0, 0x4000u);
	SlipDraw3D_InitDefaultProjectState(&SlipRendererHost_state.projection);

	*drawOut = slotDraw;
	*listOut = slotList;
	return true;
}

static void SlipRaceSession_LoadEffectSprites(void) {

	for (unsigned frame = 0; frame < 6; ++frame) {
		char name[13];
		uint32_t handle;
		snprintf(name, sizeof(name), "Expl%u.SPR", frame + 1);
		if (!TrackView_LoadNamedResource(&SlipRaceSession_resourceRegistry, name, &handle))
			SlipRuntime_Fatal("Missing explosion sprite (0004fc2e)");
		SlipRaceEffects_explosionHandles[frame] = (uint16_t)handle;
	}

	for (unsigned frame = 0; frame < 6; ++frame) {
		char name[13];
		uint32_t handle;
		snprintf(name, sizeof(name), "ExplF%u.SPR", frame + 1);
		if (!TrackView_LoadNamedResource(&SlipRaceSession_resourceRegistry, name, &handle))
			SlipRuntime_Fatal("Missing final explosion sprite (0004fc2e)");
		SlipRaceEffects_finalExplosionHandles[frame] = (uint16_t)handle;
	}

	for (unsigned frame = 0; frame < 6; ++frame) {
		char name[13];
		uint32_t handle;
		snprintf(name, sizeof(name), "SmkBlk%u.SPR", frame + 1);
		if (!TrackView_LoadNamedResource(&SlipRaceSession_resourceRegistry, name, &handle))
			SlipRuntime_Fatal("Missing smoke sprite (0004fc2e)");
		SlipRaceEffects_blackSmokeHandles[frame] = (uint16_t)handle;
	}

	for (unsigned frame = 0; frame < 9; ++frame) {
		char name[13];
		uint32_t handle;
		snprintf(name, sizeof(name), "SmkBlkF%u.SPR", frame + 1);
		if (!TrackView_LoadNamedResource(&SlipRaceSession_resourceRegistry, name, &handle))
			SlipRuntime_Fatal("Missing smoke sprite (0004fc2e)");
		SlipRaceEffects_finalBlackSmokeHandles[frame] = (uint16_t)handle;
	}

	for (unsigned frame = 0; frame < 6; ++frame) {
		char name[13];
		uint32_t handle;
		snprintf(name, sizeof(name), "SmkGry%u.SPR", frame + 1);
		if (!TrackView_LoadNamedResource(&SlipRaceSession_resourceRegistry, name, &handle))
			SlipRuntime_Fatal("Missing smoke sprite (0004fc2e)");
		SlipRaceEffects_graySmokeHandles[frame] = (uint16_t)handle;
	}

	for (unsigned frame = 0; frame < 9; ++frame) {
		char name[13];
		uint32_t handle;
		snprintf(name, sizeof(name), "SmkGryF%u.SPR", frame + 1);
		if (!TrackView_LoadNamedResource(&SlipRaceSession_resourceRegistry, name, &handle))
			SlipRuntime_Fatal("Missing smoke sprite (0004fc2e)");
		SlipRaceEffects_finalGraySmokeHandles[frame] = (uint16_t)handle;
	}

	for (unsigned impact = 0; impact < 4; ++impact) {
		char name[13];
		uint32_t handle;
		snprintf(name, sizeof(name), "Fire%u.SPR", impact + 1);
		if (!TrackView_LoadNamedResource(&SlipRaceSession_resourceRegistry, name, &handle))
			SlipRuntime_Fatal("Missing Blaster impact sprite (0004fc2e)");
		SlipRaceEffects_fireHandles[impact] = (uint16_t)handle;
	}
}

static void SlipRaceSession_SetEnvironmentDetail(uint32_t environmentDetail) {

	SlipRaceSession_renderMode = environmentDetail;
	SlipMaterialHost_residency.frame = environmentDetail;

	SlipRaceSession_primitiveFlagsReady = 0;
	uint32_t farTextureDepth = 0x000a6cc0u;
	uint32_t componentDistance = 0x00009880;
	uint32_t componentRadius = 0x00005f50;
	uint32_t detailThreshold = 0x20;
	uint32_t detailDistance = 0x001dc900u;
	if (environmentDetail != 0) {
		farTextureDepth = 0x003b9200u;
		componentDistance = 0x00036ce0;
		componentRadius = 0x00017d40;
		detailThreshold = 0x14;
		detailDistance = 0x004a7680u;
		if (environmentDetail != 1u) {
			farTextureDepth = 0x009c5f40u;
			componentDistance = 0x0004d710;
			componentRadius = 0x00029b30;
			detailThreshold = 0x0a;
			detailDistance = 0x0094ed00u;
			if (environmentDetail != 2u) {
				farTextureDepth = 0x01745080u;
				componentDistance = 0x00077240;
				componentRadius = 0x00041870;
				detailThreshold = 5;
				detailDistance = 0x00df6380u;
			}
		}
	}

	SlipRaceSession_farTextureDepth = farTextureDepth;
	SlipRaceSession_componentDistance = componentDistance;
	SlipRaceSession_componentRadius = componentRadius;
	SlipRaceSession_detailThreshold = detailThreshold;
	SlipRaceSession_detailDistance = detailDistance;
}

static void SlipRaceSession_SetEnvironmentDetailResident(uint32_t detail) {
	SlipRaceSession_SetEnvironmentDetail(detail);
	SlipMaterial_MakeResident(&SlipMaterialHost_residency, &SlipMaterialHost_residencyCalls);
	if (SlipMaterialHost_residency.resource != 0) {
		SlipResourcePayload materialPayload = SlipResourceHost_Payload(SlipMaterialHost_residency.resource);
		TrackView_MaterialBytes(materialPayload.data, SlipMaterialHost_residency.table);
	}
}

void SlipRaceSession_ApplyConfigurationValues(void) {
	SlipRaceSession_textureMode = SlipConfig_Textures() == 2 ? 0u : 1u;
	const uint32_t shading = (uint32_t)SlipConfig_Shading();
	SlipRaceSession_shading = shading;
	SlipRaceSession_shadingSecondary = shading != 0 ? shading - 1u : 0u;
	SlipRaceSession_shadows = (uint32_t)SlipConfig_Shadows();
	SlipRaceSession_SetEnvironmentDetailResident((uint32_t)SlipConfig_EnvironmentDetail());
}

static void SlipRaceSession_LoadWeaponShapes(void) {
	static const char *const names[7] = {"AIRMINE.SHP", "AMBLER.SHP",   "BOMBER.SHP", "FRAG.SHP",
	                                     "HYPER.SHP",   "SCRAMBLE.SHP", "SEEKER.SHP"};
	for (unsigned index = 0; index < 7; ++index) {
		uint32_t handle;
		if (!TrackView_LoadNamedResource(&SlipRaceSession_resourceRegistry, names[index], &handle))
			SlipRuntime_Fatal("ERROR: An internal error has occurred. Please restart Slipstream.");
		SlipRaceSession_weaponShapeHandles[index] = (uint16_t)handle;
	}
}

static void SlipRaceSession_ReleaseWeaponShapes(void) {
	for (unsigned index = 0; index < 7; ++index)
		TrackView_ReleaseResource(&SlipRaceSession_resourceRegistry, SlipRaceSession_weaponShapeHandles[index]);
}

static void SlipRaceSession_ReleaseEffectSprites(void) {
	TrackView_ReleaseSequence(&SlipRaceSession_resourceRegistry, SlipRaceEffects_explosionHandles, 6);
	TrackView_ReleaseSequence(&SlipRaceSession_resourceRegistry, SlipRaceEffects_finalExplosionHandles, 6);
	TrackView_ReleaseSequence(&SlipRaceSession_resourceRegistry, SlipRaceEffects_fireHandles, 4);
	TrackView_ReleaseSequence(&SlipRaceSession_resourceRegistry, SlipRaceEffects_blackSmokeHandles, 6);
	TrackView_ReleaseSequence(&SlipRaceSession_resourceRegistry, SlipRaceEffects_finalBlackSmokeHandles, 9);
	TrackView_ReleaseSequence(&SlipRaceSession_resourceRegistry, SlipRaceEffects_graySmokeHandles, 6);
	TrackView_ReleaseSequence(&SlipRaceSession_resourceRegistry, SlipRaceEffects_finalGraySmokeHandles, 9);
}

static void SlipRaceSession_ShutdownBonuses(void) {
	TrackView_ReleaseSequence(&SlipRaceSession_resourceRegistry, SlipRaceSession_bonusBindings.resourceHandles, 6);
}

static void SlipRaceSession_ShutdownEffects(void) {
	if (SlipRaceSession_effectsInitialized != 0) {
		SlipRaceSession_effectsInitialized = 0;
		SlipTimedEffects_Cleanup();
		SlipAnimatedEffects_Cleanup();
		SlipCrossEffects_Cleanup();
		SlipRaceSession_ReleaseEffectSprites();
	}
}

static void SlipRaceSession_InitializeEffects(void) {
	if (SlipRaceSession_effectsInitialized == 0) {
		SlipRaceSession_effectsInitialized = UINT32_MAX;

		(void)SlipTimedEffects_Initialize(10, 0, 40, 3, SlipRaceSession_AttachTimedEffect,
		                                  SlipRaceSession_UpdateTimedEffect);

		SlipAnimatedEffects_Initialize(0, SlipRaceSession_AttachAnimatedEffect, SlipRaceSession_UpdateAnimatedEffect);

		SlipCrossEffects_Initialize(0, SlipRaceSession_AttachCrossEffect);

		SlipShapeEffects_Initialize();

		SlipRaceSession_LoadEffectSprites();

		SlipRaceEffects_InitializeMaterials(SlipRaceSession_materialTable, SlipRaceSession_materialTableBytes,
		                                    SlipRaceSession_materialGlobal);

		TrackView_SetImpactSprites(&SlipRaceSession_impactSprites);
		SlipRuntime_RegisterExit(SlipRaceSession_ShutdownEffects);
		SlipRace_debrisBudgetClock = 0;
		SlipRace_debrisBudget = 0;
	}
}

void SlipRaceSession_StartNew(const char *resPath, uint16_t track, SlipRaceRacerTable *racers,
                              uint32_t environmentDetail, uint32_t shading, uint32_t textures, uint32_t shadows) {
	SlipRaceSession_replay = false;
	SlipRaceSession_Begin(resPath, track, racers, environmentDetail, shading, textures, shadows);
}

void SlipRaceSession_Replay(const char *resPath, uint16_t track, SlipRaceRacerTable *racers, uint32_t environmentDetail,
                            uint32_t shading, uint32_t textures, uint32_t shadows) {
	SlipRaceSession_replay = true;
	SlipRaceSession_Begin(resPath, track, racers, environmentDetail, shading, textures, shadows);
}

void SlipRaceSession_Begin(const char *resPath, uint16_t axTrack, SlipRaceRacerTable *racerTable,
                           uint32_t environmentDetail, uint32_t shading, uint32_t textures, uint32_t shadows) {
	char secondaryPath[512];
	const char *archives[2];
	size_t archiveCount;
	SlipObjectInitTable objectInit;
	SlipTrackWorldSlotDrawInstall slotDraw;
	SlipTrackWorldSlotListInstall slotList;
	SlipTrackWorldCellTableBuild cellTable;
	SlipRaceCreatePlayer createPlayer;
	TrackViewTrackLifecycleArgs trackLifecycle;
	SlipRaceRacerState *playerRacer;
	uint16_t tuningIndex;
	uint32_t racerResourceHandleOrRecordAddress;
	uint16_t localPlayerCount;
	size_t i;
	static const char *const bonusNames[SLIP_RACE_BONUS_COUNT] = {
	    "BONUS0.SPR", "BONUS1.SPR", "BONUS2.SPR", "BONUS3.SPR", "BONUS4.SPR", "BONUS5.SPR",
	};

	SlipRaceSession_track = axTrack;
	SlipRaceSession_exitRequested = 0;
	SlipRacePlayer_track = axTrack;
	SlipRace_activeRacerTable = racerTable;

	if (racerTable != NULL)
		SlipRace_activeRacerTable = SlipRaceRecording_PrepareRace(
		    &SlipRaceSession_recordingStart, SlipRaceSession_replay, racerTable,
		    SlipRaceRecording_state.installed ? &SlipRaceRecording_state : NULL, &SlipRaceRecording_state.host);
	racerTable = SlipRace_activeRacerTable;
	SlipRaceSession_finishDelay = UINT16_MAX;
	SlipRaceSession_playerReady = false;
	SlipRaceSession_pauseState = SLIP_RACE_PAUSE_RUNNING;
	SlipRaceSession_pauseSelection = 0;
	SlipRaceSession_menuMouseX = 0;
	SlipRaceSession_menuMouseY = 0;
	memset(&SlipRaceSession_hudState, 0, sizeof(SlipRaceSession_hudState));

	SlipRaceSession_textureMode = textures == 2u ? 0u : 1u;
	SlipRaceSession_shading = shading;
	SlipRaceSession_shadingSecondary = shading != 0 ? shading - 1u : 0u;
	SlipRaceSession_shadows = shadows;
	SlipRaceSession_SetEnvironmentDetail(environmentDetail);
	SlipRace_playerOneFinished = 0;
	SlipRace_playerTwoFinished = 0;
	archiveCount = SlipMenu_BuildArchiveList(resPath, secondaryPath, archives);
	if (archiveCount == 0 || axTrack == 0 || axTrack > 10u || racerTable == NULL) {
		return;
	}
	SlipRaceSession_archiveCountHost = archiveCount;
	for (i = 0; i < archiveCount; ++i) {
		const size_t length = strlen(archives[i]);

		if (length >= sizeof(SlipRaceSession_archivePathHost[i])) {
			return;
		}
		memcpy(SlipRaceSession_archivePathHost[i], archives[i], length + 1u);
		SlipRaceSession_archivesHost[i] = SlipRaceSession_archivePathHost[i];
	}
	archives[0] = SlipRaceSession_archivesHost[0];
	if (archiveCount > 1u) {
		archives[1] = SlipRaceSession_archivesHost[1];
	}

	SlipRaceSession_LoadPauseAssets();

	if (!SlipObject_InitTableFresh(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, 100u, &objectInit))
		return;
	SlipDrawListHost_Initialize(50u);
	if (!SlipArticSlot_InitializeResourcePool(20u, &SlipRaceSession_articResourceCalls, &SlipRaceSession_articPool))
		return;

	TrackView_InitializeClouds(&SlipRaceSession_cloudState);

	bool installRenderer = SlipRendererHost_state.initialized == 0;
	SlipRenderer_Initialize(&SlipRendererHost_state, SLIP_RACE_VERTEX_CAPACITY, &SlipRendererHost_lifecycleCalls);
	SlipRaceSession_vertices = SlipRendererHost_state.vertexBase;
	SlipRaceSession_drawStates = SlipResourceStorage_DrawStateRecords(
	    SlipResource_handles[SlipRendererHost_state.stateResource].block, SlipRendererHost_state.stateCount);
	if (installRenderer) {
		SlipDraw3DRecordPoolInit poolInit;
		SlipDraw3DRecordPool *const pool =
		    SlipResourceStorage_RecordPool(SlipResource_handles[SlipRendererHost_state.polygonResource].block);
		if (!SlipDraw3D_InitRecordPool(pool, &poolInit))
			return;
	}
	SlipShape3D_Initialize();

	SlipRaceSession_LoadPalette(SlipRace_paletteNames[axTrack - 1u]);

	SlipRaceSession_materialTable = NULL;
	SlipRaceSession_materialTableBytes = 0;
	memset(&SlipRaceSession_resourceRegistry, 0, sizeof(SlipRaceSession_resourceRegistry));
	if (!TrackView_BuildTrackMaterialTable(archives, archiveCount, (uint8_t)(axTrack - 1u),
	                                       &SlipRaceSession_materialTable, &SlipRaceSession_materialTableBytes,
	                                       &SlipRaceSession_materialGlobal, &SlipRaceSession_resourceRegistry)) {
		return;
	}

	SlipRaceCollision_objectTable = SlipRaceSession_objectTableHost;
	SlipRaceCollision_objectTableBytes = SLIP_OBJECT_TABLE_DOS_BYTES;
	if (!SlipRaceCollisionHost_Initialize(0x40u))
		return;

	if (!SlipRaceSession_InitializeTrack(&slotDraw, &slotList) || !SlipRaceSession_InitializeTrackDraw())
		return;

	SlipRaceSession_currentTrackRecordAddress = 0;
	SlipRaceSession_primitiveFlagsReady = 0;
	SlipRaceSession_underSeaColor = 0;
	SlipTrackAssets_FreeBundle(&SlipRaceSession_trackBundle, TrackView_ReleaseResource,
	                           &SlipRaceSession_resourceRegistry);
	if (!SlipTrackAssets_LoadBundle(SlipRace_trackNames[axTrack - 1u], &SlipRaceSession_trackBundle,
	                                &SlipRaceSession_trackAssetCalls) ||
	    !SlipTrackWorld_CellTableBuild(
	        SlipRaceSession_cellTableHost, (SLIP_TRACK_WORLD_CELL_TABLE_BYTES),
	        SlipRaceSession_trackBundle.trkPayload.data, SlipRaceSession_trackBundle.trkPayload.size, 0,
	        SlipRaceSession_trackBundle.trdBaseToken, SlipRaceSession_cellVisitsHost,
	        sizeof(SlipRaceSession_cellVisitsHost) / sizeof(SlipRaceSession_cellVisitsHost[0]), &cellTable)) {
		return;
	}

	SlipTrackWorld_ClearSlotRecordLinks(SlipRaceSession_trackBundle.trdHandle,
	                                    SlipRaceSession_trackBundle.trdPayload.data);
	SlipTrackAssets_LoadScenery(&SlipRaceSession_trackBundle);

	SlipTrackWorld_SetLapDistance(SlipRaceSession_trackBundle.trdPayload.data);
	SlipTrackWorld_BindActorRingHostMappings(SlipRaceSession_trackBundle.trdPayload.data,
	                                         SlipRaceSession_trackBundle.trdPayload.size, SlipRaceSession_slotDrawHost,
	                                         (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u), slotDraw.baseAddress,
	                                         SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES);

	SlipView3D_FreeMaths(&SlipRaceSession_maths);
	if (!SlipView3D_LoadMathsFromArchives(&SlipRaceSession_maths, archives, archiveCount)) {
		return;
	}

	if (!SlipTrackWorld_FindDoors(1, SlipRaceSession_trackBundle.trdPayload.data,
	                              SlipRaceSession_trackBundle.trdPayload.size, SlipRaceSession_trackBundle.trdBaseToken,
	                              SlipRaceSession_trackBundle.trcPayload.data,
	                              SlipRaceSession_trackBundle.trcPayload.size)) {
		return;
	}

	if (!TrackView_ResolveComponentMaterials((uint8_t *)SlipRaceSession_trackBundle.trcPayload.data,
	                                         SlipRaceSession_trackBundle.trcPayload.size, SlipRaceSession_materialTable,
	                                         SlipRaceSession_materialTableBytes, SlipRaceSession_materialGlobal,
	                                         &SlipRaceSession_materialInit)) {
		return;
	}

	if (!SlipTrackWorld_InitRefuel(
	        &SlipTrackWorld_refuelInitialized, &SlipRacePlayer_refuelSection, 1u,
	        SlipRaceSession_trackBundle.trdPayload.data, SlipRaceSession_trackBundle.trdPayload.size,
	        SlipRaceSession_trackBundle.trdBaseToken, SlipRaceSession_trackBundle.trcPayload.data,
	        SlipRaceSession_trackBundle.trcPayload.size, SlipRaceSession_materialTable,
	        SlipRaceSession_materialTableBytes)) {
		return;
	}
	(void)SlipMenu_ApplyPaletteResource(archives, archiveCount, SlipRace_paletteNames[axTrack - 1u]);

	Raster_SetClipRect(0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1);
	Raster_FillRectClipped(0, 0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1);
	SlipMenu_PresentFrame();
	Raster_FillRectClipped(0, 0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1);

	SlipTrackWorld_ResetSlots(1u, SlipRaceSession_trackBundle.trdPayload.data,
	                          SlipRaceSession_trackBundle.trdPayload.size, SlipRaceSession_trackBundle.trdBaseToken,
	                          SlipRaceSession_slotDrawHost, (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u),
	                          slotDraw.baseAddress, slotDraw.ring.baseAddress, SlipRaceSession_slotDrawCount,
	                          (uint8_t *)(void *)SlipRaceSession_trackSlots,
	                          ((0x40u + 8u + 2u) * sizeof(SlipTrackSlotRecord)), slotList.baseAddress,
	                          slotList.activeListAddress, slotList.freeListAddress);
	SlipRaceCollision_ResetBodyLists();
	SlipDraw3D_ResetLighting();
	SlipRace_viewIndex = axTrack;
	SlipRace_ApplyViewDepth((SlipRaceTrackId)axTrack);
	SlipDraw3D_SetAmbientLight(0x1000u);
	SlipDraw3D_SetLightVector(0, (int16_t)0xc000, 0, 0x4000u);
	SlipDraw3D_InitDefaultProjectState(&SlipRendererHost_state.projection);

	SlipRaceSession_resourceRegistry.archives = SlipRaceSession_archivesHost;
	SlipRaceSession_resourceRegistry.archiveCount = SlipRaceSession_archiveCountHost;

	SlipRaceSession_LoadWeaponShapes();
	if (!SlipRaceDrone_Initialize(archives, archiveCount, &SlipRaceSession_resourceRegistry,
	                              &SlipRaceSession_droneArtPayload, &SlipRaceSession_droneArtHandle)) {

		SlipRuntime_Fatal("ERROR: An internal error has occurred. Please restart Slipstream.");
	}

	for (i = 0; i < SLIP_RACE_BONUS_COUNT; ++i) {
		uint32_t bonusHandle;

		if (!TrackView_LoadNamedResource(&SlipRaceSession_resourceRegistry, bonusNames[i], &bonusHandle)) {
			return;
		}
		SlipRaceSession_bonusBindings.resourceHandles[i] = (uint16_t)bonusHandle;
	}

	SlipRaceSession_resourceRegistry.archives = SlipRaceSession_archivesHost;
	SlipRaceSession_resourceRegistry.archiveCount = SlipRaceSession_archiveCountHost;
	for (i = 0; i < 10u; ++i) {
		SlipResource_ReleaseHandle(&SlipRaceSession_racerArtPayload[i]);
		SlipRaceSession_racerArtHandles[i] = 0;
		if (SlipRace_FindRacer(racerTable, (uint16_t)(i + 1u)).racerNotFound) {
			continue;
		}
		if (!TrackView_LoadNamedResource(&SlipRaceSession_resourceRegistry, SlipRaceSession_racerArtNames[i],
		                                 (uint32_t *)&racerResourceHandleOrRecordAddress)) {
			return;
		}
		SlipRaceSession_racerArtHandles[i] = (uint16_t)racerResourceHandleOrRecordAddress;
		SlipActor_PreloadResources((uint16_t)racerResourceHandleOrRecordAddress, &SlipActorHost_resourceCalls);
		SlipRaceSession_racerArtPayload[i] = SlipResourceHost_Payload((uint16_t)racerResourceHandleOrRecordAddress);
	}

	playerRacer = &racerTable->records[0];
	tuningIndex = playerRacer->tuningIndex;
	if (tuningIndex == 0 || tuningIndex > 10u || playerRacer->racerType != 0 || racerTable->racerCount == 0 ||
	    racerTable->racerCount > SLIP_RACE_RACER_COUNT) {
		return;
	}
	memset(&SlipRaceSession_playerContext, 0, sizeof(SlipRaceSession_playerContext));
	SlipRaceSession_playerContext.objectTable = SlipRaceSession_objectTableHost;
	SlipRaceSession_playerContext.objectTableBytes = SLIP_OBJECT_TABLE_DOS_BYTES;
	SlipRaceSession_playerContext.playerStates = SlipRaceSession_playerStates;
	SlipRaceSession_playerContext.playerStateCount =
	    sizeof(SlipRaceSession_playerStates) / sizeof(SlipRaceSession_playerStates[0]);
	SlipRaceSession_racerStates = racerTable->records;
	SlipRaceSession_playerContext.racerStates = SlipRaceSession_racerStates;
	SlipRaceSession_playerContext.racerStateCount = SLIP_RACE_RACER_COUNT;
	SlipRaceSession_playerContext.racerRecordsOffset = 0x00054446u;
	SlipRaceSession_playerContext.weaponRecords = SlipRacePlayer_records;
	SlipRaceSession_playerContext.aiBaseSpeed = SlipRacePlayer_aiBaseSpeed;
	SlipRaceSession_playerContext.aiSpeedScale = SlipRacePlayer_aiSpeedScale;
	SlipRaceSession_playerContext.aiSpeedTableCount =
	    sizeof(SlipRacePlayer_aiBaseSpeed) / sizeof(SlipRacePlayer_aiBaseSpeed[0]);
	SlipRaceSession_playerContext.maths = &SlipRaceSession_maths;
	SlipRaceSession_playerContext.slotListBase = (uint8_t *)(void *)SlipRaceSession_trackSlots;
	SlipRaceSession_playerContext.slotListBytes = ((0x40u + 8u + 2u) * sizeof(SlipTrackSlotRecord));
	SlipRaceSession_playerContext.slotListBaseOffset = slotList.baseAddress;
	SlipRaceSession_playerContext.slotListSentinelOffset = slotList.activeListAddress;
	SlipRaceSession_playerContext.slotDrawCallbacks = SlipRaceSession_slotDrawCallbacks;
	SlipRaceSession_playerContext.slotDrawCallbackCount =
	    sizeof(SlipRaceSession_slotDrawCallbacks) / sizeof(SlipRaceSession_slotDrawCallbacks[0]);
	SlipRaceSession_playerContext.trdBase = SlipRaceSession_trackBundle.trdPayload.data;
	SlipRaceSession_playerContext.trackDataSize = SlipRaceSession_trackBundle.trdPayload.size;
	SlipRaceSession_playerContext.trackDataOffset = SlipRaceSession_trackBundle.trdBaseToken;
	SlipRaceSession_playerContext.trkBase = SlipRaceSession_trackBundle.trkPayload.data;
	SlipRaceSession_playerContext.trkBytes = SlipRaceSession_trackBundle.trkPayload.size;
	SlipRaceSession_playerContext.componentBase = SlipRaceSession_trackBundle.trcPayload.data;
	SlipRaceSession_playerContext.componentBaseBytes = SlipRaceSession_trackBundle.trcPayload.size;
	SlipRaceSession_playerContext.componentBaseOffset = SlipRaceSession_trackBundle.trcBaseToken;
	SlipRaceSession_playerContext.trackTable = SlipRaceSession_cellTableHost;
	SlipRaceSession_playerContext.trackTableBytes = (SLIP_TRACK_WORLD_CELL_TABLE_BYTES);
	SlipRaceSession_playerContext.articSlotPool = SlipRaceSession_articPool.allocation;
	SlipRaceSession_playerContext.articSlotPoolBytes = SlipRaceSession_articPool.allocationBytes;
	SlipRaceSession_playerContext.articSlotPoolOffset = SlipRaceSession_articPool.allocationAddress;
	SlipRaceSession_playerContext.articData = SlipRaceSession_racerArtPayload[tuningIndex - 1u].data;
	SlipRaceSession_playerContext.articDataBytes = SlipRaceSession_racerArtPayload[tuningIndex - 1u].size;
	SlipRaceSession_playerContext.articDataOffset = SlipRaceSession_racerArtPayload[tuningIndex - 1u].address;
	SlipRaceSession_playerContext.soundEffects = &SlipRaceSession_soundEffects;
	SlipRaceSession_playerContext.cameraState = &SlipRaceSession_cameraState;
	SlipRaceSession_playerContext.materialTable = SlipRaceSession_materialTable;
	SlipRaceSession_playerContext.trackStateRecords = SlipTrackWorld_doors;
	SlipRaceSession_playerContext.trackStateRecordCount = SlipTrackWorld_doorCount;
	SlipRaceSession_playerContext.materialTableBytes = SlipRaceSession_materialTableBytes;

	SlipRaceSession_playerContext.transitionFrames = &SlipRaceSession_hudState.playerOneConsoleRedrawFrames;
	SlipRaceSession_playerContext.transitionDuration = &SlipRaceSession_hudState.playerTwoConsoleRedrawFrames;
	SlipRaceSession_playerContext.primaryViewShake = &SlipRaceSession_cameraState.shake[0];
	SlipRaceSession_playerContext.secondaryViewShake = &SlipRaceSession_cameraState.shake[1];
	SlipRacePlayer_BindHostContext(&SlipRaceSession_playerContext);
	SlipRaceSession_bonusBindings.playerBindings = &SlipRaceSession_playerContext;
	SlipRaceSession_bonusBindings.slotDrawBase = SlipRaceSession_slotDrawHost;
	SlipRaceSession_bonusBindings.slotDrawBytes = (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u);
	SlipRaceSession_bonusBindings.slotDrawCallbacks = SlipRaceSession_slotDrawCallbacks;
	SlipRaceSession_bonusBindings.slotDrawCallbackCount =
	    sizeof(SlipRaceSession_slotDrawCallbacks) / sizeof(SlipRaceSession_slotDrawCallbacks[0]);
	SlipRaceSession_bonusBindings.slotDrawBaseAddress = slotDraw.baseAddress;
	SlipRaceSession_bonusBindings.slotDrawFreeListAddress = slotDraw.ring.baseAddress;
	SlipRaceSession_bonusBindings.slotListFreeListAddress = slotList.freeListAddress;

	SlipRaceBonus_BindHostContext(&SlipRaceSession_bonusBindings);
	memset(&SlipRaceSession_droneBindings, 0, sizeof(SlipRaceSession_droneBindings));
	SlipRaceSession_droneBindings.playerBindings = &SlipRaceSession_playerContext;
	SlipRaceSession_droneBindings.articPool = &SlipRaceSession_articPool;
	SlipRaceSession_droneBindings.findResource = TrackView_FindNamedResource;
	SlipRaceSession_droneBindings.findResourceUser = &SlipRaceSession_resourceRegistry;
	SlipRaceSession_droneBindings.artPayload = SlipRaceSession_droneArtPayload.data;
	SlipRaceSession_droneBindings.artPayloadBytes = SlipRaceSession_droneArtPayload.size;
	SlipRaceSession_droneBindings.artResourceHandle = SlipRaceSession_droneArtHandle;
	SlipRaceSession_droneBindings.slotDrawBase = SlipRaceSession_slotDrawHost;
	SlipRaceSession_droneBindings.slotDrawBytes = (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u);
	SlipRaceSession_droneBindings.slotDrawBaseAddress = slotDraw.baseAddress;
	SlipRaceSession_droneBindings.slotDrawFreeListAddress = slotDraw.ring.baseAddress;
	SlipRaceSession_droneBindings.slotListFreeListAddress = slotList.freeListAddress;
	SlipRaceDrone_BindHostContext(&SlipRaceSession_droneBindings);

	localPlayerCount = 0;
	SlipRacePlayer_playerOneObject = 0;
	SlipRacePlayer_playerTwoObject = 0;
	SlipRacePlayer_thirdObject = 0;
	for (i = racerTable->racerCount; i-- != 0;) {
		SlipRaceRacerState *const racer = &racerTable->records[i];
		const uint16_t controllerType = racer->racerType;

		tuningIndex = racer->tuningIndex;
		if (tuningIndex == 0 || tuningIndex > 10u) {
			return;
		}
		SlipRaceSession_playerContext.articData = SlipRaceSession_racerArtPayload[tuningIndex - 1u].data;
		SlipRaceSession_playerContext.articDataBytes = SlipRaceSession_racerArtPayload[tuningIndex - 1u].size;
		SlipRaceSession_playerContext.articDataOffset = SlipRaceSession_racerArtPayload[tuningIndex - 1u].address;
		if (!SlipRace_CreatePlayer(
		        SlipRace_gameMode != 0 && racerTable->racerCount == 2u ? 1u : 0u, racer,
		        SlipRaceSession_racerArtHandles[tuningIndex - 1u], &SlipRaceSession_playerContext,
		        SlipRaceSession_playerContext.articData, SlipRaceSession_playerContext.articDataBytes,
		        &SlipRaceSession_articPool, TrackView_FindNamedResource, &SlipRaceSession_resourceRegistry, 1u,
		        SlipRaceSession_slotDrawHost, (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u), slotDraw.baseAddress,
		        slotDraw.ring.baseAddress, slotList.freeListAddress, &createPlayer)) {
			return;
		}
		if (createPlayer.creationFailed) {
			return;
		}

		SlipRaceSession_playerContext.articData = SlipRaceSession_articPool.allocation;
		SlipRaceSession_playerContext.articDataBytes = SlipRaceSession_articPool.allocationBytes;
		SlipRaceSession_playerContext.articDataOffset = SlipRaceSession_articPool.allocationAddress;

		if (controllerType == 0) {
			SlipRacePlayer_playerOneObject = createPlayer.objectOffset;
			++localPlayerCount;
		} else if (controllerType == 1) {
			SlipRacePlayer_playerTwoObject = createPlayer.objectOffset;
			++localPlayerCount;
		} else if (controllerType == 3) {
			SlipRacePlayer_thirdObject = createPlayer.objectOffset;
		}
		racerResourceHandleOrRecordAddress = 0x00054446u + (uint32_t)i * SLIP_RACE_RACER_RECORD_BYTES;
		(void)SlipObject_DispatchEvent(createPlayer.objectOffset, SLIP_OBJECT_EVENT_SET_CONTROLLER, controllerType, 0,
		                               0, 0, 0);
		(void)SlipObject_DispatchEvent(createPlayer.objectOffset, SLIP_OBJECT_EVENT_BIND_RACER,
		                               racerResourceHandleOrRecordAddress, 0, 0, 0, 0);
	}
	SlipRaceSession_playerReady = localPlayerCount != 0;
	for (i = 0; i < SLIP_RACE_RACER_COUNT; ++i) {
		if (SlipRaceSession_racerArtHandles[i] != 0)
			SlipResourceHost_Release(NULL, SlipRaceSession_racerArtHandles[i]);
	}
	SlipRaceSession_InitializeEffects();

	trackLifecycle =
	    (TrackViewTrackLifecycleArgs){&SlipRaceSession_cloudState, archives, archiveCount, &SlipRaceSession_maths};
	SlipRaceHud_Shutdown(&SlipRaceSession_hudAssets, SlipRaceSession_cleanupCallback, &trackLifecycle);
	SlipRaceSession_frameCallback = g_trackViewFrameCallbacks[axTrack - 1u];
	if (!SlipRaceCamera_LoadPositions(&SlipRaceSession_cameraState, archives, archiveCount, axTrack))
		return;
	SlipRaceSession_cleanupCallback = g_trackViewCleanupCallbacks[axTrack - 1u];
	if (!g_trackViewInitCallbacks[axTrack - 1u](&trackLifecycle)) {
		return;
	}
	if (!SlipRaceTrack_MaterialGlobals(SlipRaceSession_materialTable, SlipRaceSession_materialTableBytes,
	                                   SlipRaceSession_materialGlobal, &SlipRaceSession_materialGlobals)) {
		return;
	}

	SlipRaceSession_hudAssets.flags = 0;
	SlipRaceSession_hudAssets.active = true;

	SlipRaceCamera_Reset(&SlipRaceSession_cameraState);

	if (!SlipRaceHud_LoadTimeFont(&SlipRaceSession_hudAssets, archives, archiveCount) ||
	    !SlipRaceHud_LoadPositionSprites(&SlipRaceSession_hudAssets, archives, archiveCount) ||
	    !SlipRaceHud_LoadNormalSight(&SlipRaceSession_hudAssets, archives, archiveCount) ||
	    !SlipRaceHud_LoadConsoleSprites(&SlipRaceSession_hudAssets, archives, archiveCount, (int)tuningIndex - 1,
	                                    SlipRace_gameMode)) {
		SlipGame_ResourceFailure();
	}
	SlipRaceHud_ResetConsole(&SlipRaceSession_hudState);
	SlipRace_ResetControlHistory();

	SlipTrackWorld_UpdateSlots(
	    1u, SlipRaceSession_slotDrawHost, (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u),
	    SlipRaceSession_slotDrawCallbacks,
	    sizeof(SlipRaceSession_slotDrawCallbacks) / sizeof(SlipRaceSession_slotDrawCallbacks[0]),
	    SlipRaceSession_slotDrawBaseAddress, SlipRaceSession_slotDrawFreeHeadAddress,
	    (uint8_t *)(void *)SlipRaceSession_trackSlots, ((0x40u + 8u + 2u) * sizeof(SlipTrackSlotRecord)),
	    SlipRaceSession_playerContext.slotListBaseOffset, SlipRaceSession_playerContext.slotListSentinelOffset,
	    SlipRaceSession_trackBundle.trdPayload.data, SlipRaceSession_trackBundle.trdPayload.size,
	    SlipRaceSession_trackBundle.trdBaseToken, SlipRaceSession_trackBundle.trcPayload.data,
	    SlipRaceSession_trackBundle.trcPayload.size, SlipRaceSession_trackBundle.trcBaseToken,
	    SlipRaceSession_cellTableHost, (SLIP_TRACK_WORLD_CELL_TABLE_BYTES), SlipRaceSession_objectTableHost,
	    SLIP_OBJECT_TABLE_DOS_BYTES);
	SlipRaceSession_InitializeDoors();

	if (SlipRaceSession_gameSound != NULL &&
	    !SlipSoundEffects_Initialize(&SlipRaceSession_soundEffects, SlipRaceSession_archivesHost,
	                                 SlipRaceSession_archiveCountHost, axTrack, SlipRaceSession_soundSet,
	                                 SlipRaceSession_gameSound, SlipRaceSession_SoundObjectPosition,
	                                 SlipRaceSession_SoundTrackLight, SlipRaceSession_lockSound,
	                                 SlipRaceSession_unlockSound, SlipRaceSession_soundContext)) {
		return;
	}

	SlipRaceSession_portraits[SLIP_RACE_PORTRAIT_FIRST_INDEX] = 0;
	{
		SlipRaceVoiceCalls voiceCalls = SlipRaceVoiceHost_Calls(SlipRaceSession_gameSound);
		const uint32_t digitalCard = SlipRaceSession_gameSound != NULL ? SlipRaceSession_gameSound->digitalCard : 0;
		if (SlipRaceVoice_Setup(digitalCard, SLIP_RACE_SPEECH_BANK, SLIP_RACE_SPEECH_PRELOAD,
		                        SLIP_RACE_SPEECH_SUPPRESS_RECENT, SLIP_RACE_SPEECH_RESERVED_BYTES, &voiceCalls)) {
			if (!SlipResourceHost_LoadSequence(NULL, "GAMEF*.SPR", SLIP_RACE_PORTRAIT_FIRST_INDEX,
			                                   SLIP_RACE_PORTRAIT_COUNT, SlipRaceSession_portraits))
				SlipGame_ResourceFailure();
		}
	}

	{
		enum {
			SLIP_RACE_MUSIC_RANDOM_LIMIT = 3,
			SLIP_RACE_MUSIC_LARGE_SONG = 1,
			SLIP_RACE_MUSIC_REQUIRED_BYTES = 0x80000
		};

		uint32_t song = SlipRandom_Range(SLIP_RACE_MUSIC_RANDOM_LIMIT);
		if (song == SLIP_RACE_MUSIC_LARGE_SONG) {
			const uint32_t availableBytes = SlipResource_freeBytes + SlipResource_cachedBytes;
			if ((int32_t)availableBytes < SLIP_RACE_MUSIC_REQUIRED_BYTES)
				++song;
		}
		if (SlipRaceSession_gameSound != NULL && SlipRaceSession_gameSound->musicCard != 0 &&
		    !SlipMenuMusic_RaceStart(song))
			SlipRuntime_Fatal("Could not load race music.");
	}

	enum { SLIP_RACE_TRACK_SAMPLES_REQUIRED_BYTES = 0x108000 };

	if ((int32_t)SlipResource_GetUsage().availableBytes < SLIP_RACE_TRACK_SAMPLES_REQUIRED_BYTES)
		SlipSoundEffects_ReleaseTrackSamples(&SlipRaceSession_soundEffects);

	SlipMaterial_SetLimits(&SlipMaterialHost_residency,
	                       SlipResource_GetUsage().availableBytes - SLIP_RACE_TEXTURE_MEMORY_RESERVE,
	                       SLIP_RACE_MAXIMUM_TEXTURE_BYTES, SLIP_RACE_MAXIMUM_TEXTURE_FRAME);

	SlipRaceSession_SetEnvironmentDetailResident(environmentDetail);

	SlipRacePlayer_ResetMinimumPosition();

	{
		const SlipRaceBonusSpawnTable *const spawnTable = &SlipRaceBonus_trackTables[axTrack - 1u];
		const SlipRaceBonusSpawn *spawn = spawnTable->spawns;
		int32_t remaining = (int32_t)spawnTable->count;

		while (--remaining >= 0) {
			SlipRaceBonus_Create((SlipView3DVec32){spawn->x, spawn->y, spawn->z}, -1, spawn->type);
			++spawn;
		}
	}

	if (SlipRace_type == SLIP_RACE_TYPE_PRACTICE) {
		SlipRacePlayer_startCountdown = 0;
		SlipRacePlayer_positionBoostTimer = 0;

		SlipSoundEffects_EnableEngineLoops(&SlipRaceSession_soundEffects);
	} else {
		SlipRacePlayer_startCountdown = 5u;
		SlipRaceSession_countdownTimer = 1000u;
		SlipRacePlayer_positionBoostTimer = 15000u;

		SlipSoundEffects_PlayLow(&SlipRaceSession_soundEffects);
	}

	SlipFrameTimer_Reset();
}

static bool SlipRaceSession_DrawTrackFrame(uint16_t overlayEnable) {
	SlipDraw3DRefreshMode0Projection refresh;
	SlipTrackWorldAxisRampWorkspace ramps;
	SlipTrackWorldAxisTestWorkspace tests;
	SlipDraw3DRecordPool *const drawRecordPool =
	    SlipResourceStorage_RecordPool(SlipResource_handles[SlipRendererHost_state.polygonResource].block);
	SlipTrackWorldCameraSetupExecute camera;
	SlipTrackWorldFrameDrawState frameDrawState;
	SlipDraw3DOriginSetup origin;
	SlipTrackWorldProjectFrustum frustum;
	TrackViewRawBspContext context;
	SlipTrackWorldChunkProcessExecution chunk;
	SlipTrackWorldTraversalContext traversal;
	SlipRaceTrackFrameCallbackExecuteArgs background;
	TrackViewCloudDrawContext cloudDraw;
	SlipView3DMatrix cloudViewMatrix;
	SlipTrackWorldFrameEntry frame;
	SlipObjectPosition cameraPosition;
	size_t entryMapCount = 0;
	size_t recordMapCount = 0;
	size_t i;
	const uint32_t minX = (uint32_t)SlipRendererHost_state.projection.minX;
	const uint32_t minY = (uint32_t)SlipRendererHost_state.projection.minY;
	const uint32_t maxX = (uint32_t)SlipRendererHost_state.projection.maxX;
	const uint32_t maxY = (uint32_t)SlipRendererHost_state.projection.maxY;
	int32_t savedTrackMinZ;
	bool rendered;
	int16_t savedClipMinX, savedClipMinY, savedClipMaxX, savedClipMaxY;
	Raster_GetClipRect(&savedClipMinX, &savedClipMinY, &savedClipMaxX, &savedClipMaxY);
	Raster_SetClipRect((int16_t)minX, (int16_t)minY, (int16_t)maxX, (int16_t)maxY);

	if (!SlipDraw3D_RefreshMode0Projection(0, (uint32_t)SlipRendererHost_state.projection.projectionScale, minX, maxX,
	                                       minY, maxY, (uint32_t)SlipRendererHost_state.projection.centerX,
	                                       (uint32_t)SlipRendererHost_state.projection.centerY, &refresh) ||
	    !SlipTrackWorld_BindAxisRampWorkspace(SlipRaceSession_axisRamps, (SLIP_TRACK_WORLD_AXIS_RAMP_BYTES), 0,
	                                          &ramps) ||
	    !SlipTrackWorld_BindAxisTestWorkspace(SlipRaceSession_axisTests, (SLIP_TRACK_WORLD_AXIS_TEST_BYTES), 0,
	                                          &tests) ||
	    !SlipTrackWorld_CameraSetupExecute(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES,
	                                       &SlipRaceSession_cameraWorldMatrix, &SlipRaceSession_cameraViewMatrix,
	                                       &camera) ||
	    !SlipObject_Position(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, 0, &cameraPosition) ||
	    !SlipTrackWorld_FrameDrawState(SlipRendererHost_state.projection.renderFlags, SlipRaceSession_shading,
	                                   SlipRaceSession_shadingSecondary, &frameDrawState)) {
		return false;
	}

	for (i = 0; i < 3u; ++i) {
		if (!SlipDraw3D_SetOrigin(&SlipRaceSession_drawStates[i], &SlipRaceSession_cameraViewMatrix, NULL,
		                          (SlipDraw3DVec32){(int32_t)camera.cameraPositionX, (int32_t)camera.cameraPositionY,
		                                            (int32_t)camera.cameraPositionZ},
		                          (SlipDraw3DVec32){0, 0, 0}, SlipDraw3D_directLight != 0,
		                          (SlipDraw3DVec32){SlipDraw3D_lightX, SlipDraw3D_lightY, SlipDraw3D_lightZ},
		                          &origin)) {
			return false;
		}
	}
	frustum = (SlipTrackWorldProjectFrustum){refresh.maxXStep,
	                                         refresh.minXStep,
	                                         refresh.minYStep,
	                                         refresh.maxYStep,
	                                         refresh.minXPlaneDepthQ,
	                                         refresh.minXPlaneNegXQ,
	                                         refresh.maxXPlaneNegDepthQ,
	                                         refresh.maxXPlaneXQ,
	                                         refresh.maxYPlaneDepthQ,
	                                         refresh.maxYPlaneYQ,
	                                         refresh.minYPlaneNegDepthQ,
	                                         refresh.minYPlaneNegYQ,
	                                         SlipRendererHost_state.projection.minZ,
	                                         SlipRendererHost_state.projection.maxZ};
	memset(&context, 0, sizeof(context));
	context.chunkBase = SlipRaceSession_trackBundle.trdPayload.data;
	context.chunkBaseBytes = SlipRaceSession_trackBundle.trdPayload.size;
	context.chunkBaseToken = SlipRaceSession_trackBundle.trdBaseToken;
	context.componentBase = SlipRaceSession_trackBundle.trcPayload.data;
	context.componentBaseBytes = SlipRaceSession_trackBundle.trcPayload.size;
	context.componentBaseToken = SlipRaceSession_trackBundle.trcBaseToken;
	context.materialTable = SlipRaceSession_materialTable;
	context.materialTableBytes = SlipRaceSession_materialTableBytes;
	context.materialGlobal = SlipRaceSession_materialGlobal;
	context.materialFrameIndex = SlipRaceSession_renderMode;
	context.drawRecordPool = drawRecordPool;
	context.projectState = &SlipRendererHost_state.projection;
	context.hostRenderer = &SlipRendererHost_state;
	context.drawStateRecord = SlipRaceSession_drawStates;
	context.resourceRegistry = &SlipRaceSession_resourceRegistry;
	context.deferredList = SlipRaceSession_deferredList;
	context.deferredListBytes = (SLIP_TRACK_WORLD_DEFERRED_LIST_BYTES);
	context.deferredScan = SlipRaceSession_deferredScan;
	context.deferredScanBytes = (SLIP_TRACK_WORLD_DEFERRED_SCAN_BYTES);
	context.deferredScanBaseToken = 0x08000000u;
	context.objectList = SlipRaceSession_objectList;
	context.objectListBytes = (SLIP_TRACK_WORLD_OBJECT_LIST_BYTES);
	context.objectListBaseToken = SlipResourceHost_Payload(SlipRaceSession_objectListResource).address;
	SlipRaceSession_vertices = SlipRendererHost_state.vertexBase;
	context.vertexRecords = SlipRaceSession_vertices;
	context.vertexRecordCount = SlipRendererHost_state.vertexCapacity;
	context.vertexBufferBase = SlipRaceSession_vertices;
	context.vertexBufferRecordCapacity = SlipRendererHost_state.vertexCapacity;
	context.drawStateRecords = SlipRaceSession_drawStates;
	context.drawStateRecordCount = SLIP_RACE_STATE_RECORD_COUNT;
	context.chunkCallbacks.viewMatrix = &SlipRaceSession_cameraViewMatrix;
	context.origin = (SlipView3DVec32){origin.origin.x, origin.origin.y, origin.origin.z};
	context.frustum = frustum;

	context.viewportMinX = minX;
	context.viewportMinY = minY;
	context.viewportMaxX = maxX;
	context.viewportMaxY = maxY;
	context.cameraWorldX = camera.cameraPositionX;
	context.cameraWorldY = camera.cameraPositionY;
	context.cameraWorldZ = camera.cameraPositionZ;
	context.affineDepthThreshold = SlipRaceSession_trackBundle.affineDepthThreshold;
	context.textureMode = SlipRaceSession_textureMode;
	context.textureScrollPhase = SlipRace_trackAnimationClock;
	context.shading = SlipRaceSession_shading;
	context.shadingSecondary = SlipRaceSession_shadingSecondary;
	context.shadows = SlipRaceSession_shadows;

	context.ambientLightScaleQ14 = 0x1000u;
	context.directLightScaleQ14 = 0x4000u;
	context.scaledLightX = 0;
	context.scaledLightY = 0x0000c000u;
	context.scaledLightZ = 0;
	context.farTextureDepth = SlipRaceSession_farTextureDepth;
	context.componentDistance = SlipRaceSession_componentDistance;
	context.componentRadius = SlipRaceSession_componentRadius;
	context.detailThreshold = SlipRaceSession_detailThreshold;
	context.defaultTraversalGate = (uint32_t)SlipRaceSession_trackBundle.defaultTraversalGate;

	context.useFullObjectViewport = SlipRaceSession_trackBundle.useFullObjectViewport;

	context.rendererFlags = frameDrawState.drawFlagsTo;
	context.shapeProjectionFlags = SlipRendererHost_state.projection.renderFlags;
	context.lightInput = (SlipView3DVec32){SlipDraw3D_lightX, SlipDraw3D_lightY, SlipDraw3D_lightZ};

	SlipView3DVec32 cameraLight = SlipView3D_TransformVector(&SlipRaceSession_cameraWorldMatrix, context.lightInput);
	context.cameraLightX = (uint32_t)cameraLight.x;
	context.cameraLightY = (uint32_t)cameraLight.y;
	context.cameraLightZ = (uint32_t)cameraLight.z;
	context.directLight = SlipDraw3D_directLight;
	context.ambientLight = SlipDraw3D_ambientLight;
	context.materialDepthBase = SlipRaceSession_materialInit.yellowValue;
	context.materialDepthIndex = SlipRaceSession_materialInit.yellow;
	context.detailLevel = SlipRaceSession_renderMode;
	context.traversalCallback = SLIP_TRACK_WORLD_TRAVERSAL_CALLBACK_COMPONENT;
	context.primitiveCallback = SLIP_TRACK_WORLD_PRIMITIVE_CALLBACK_DRAW;
	context.recordCallback = SLIP_TRACK_WORLD_RECORD_CALLBACK_SCENERY;
	context.replayCallback = TrackView_replayCallback;
	context.objectTable = SlipRaceSession_objectTableHost;
	context.objectTableBytes = SLIP_OBJECT_TABLE_DOS_BYTES;
	context.slotDrawBase = SlipRaceSession_slotDrawHost;
	context.slotDrawBytes = (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u);
	context.slotDrawBaseAddress = SlipRaceSession_slotDrawBaseAddress;
	context.slotDrawCallbacks = SlipRaceSession_slotDrawCallbacks;
	context.slotDrawCallbackCount =
	    sizeof(SlipRaceSession_slotDrawCallbacks) / sizeof(SlipRaceSession_slotDrawCallbacks[0]);
	context.slotListBase = (uint8_t *)(void *)SlipRaceSession_trackSlots;
	context.slotListBytes = ((0x40u + 8u + 2u) * sizeof(SlipTrackSlotRecord));
	context.slotListBaseAddress = SlipRaceSession_playerContext.slotListBaseOffset;
	context.articSlotPool = SlipRaceSession_articPool.allocation;
	context.articSlotPoolBytes = SlipRaceSession_articPool.allocationBytes;
	context.articSlotPoolAddress = SlipRaceSession_articPool.allocationAddress;
	context.maths = &SlipRaceSession_maths;
	context.trackCellTable = SlipRaceSession_cellTableHost;
	context.trackCellTableBytes = (SLIP_TRACK_WORLD_CELL_TABLE_BYTES);
	if (SlipRacePlayer_refuelSection != 0)
		context.specialRecord = context.chunkBase + (SlipRacePlayer_refuelSection - context.chunkBaseToken);
	context.cameraMatrix = &SlipRaceSession_cameraViewMatrix;
	context.minDepth = (uint32_t)SlipRendererHost_state.projection.minZ;
	context.frameRenderFlags = frameDrawState.storedFrameFlags;

	if (!TrackView_LoadDrawState(0, &context)) {
		return false;
	}

	if (SlipRaceSession_primitiveFlagsReady == 0) {
		static const uint8_t underSeaName[] = {'U', 'n', 'd', 'e', 'r', 'S', 'e', 'a', 0};
		SlipDraw3DMaterialNumber underSeaLookup;
		uint32_t underSeaMaterialControl;

		SlipRaceSession_primitiveFlagsReady = UINT32_MAX;

		if (SlipDraw3D_GetMaterialNumber(SlipRaceSession_materialTable, SlipRaceSession_materialTableBytes,
		                                 SlipRaceSession_materialGlobal, underSeaName, sizeof(underSeaName),
		                                 &underSeaLookup) &&
		    !underSeaLookup.carryOut) {
			(void)SlipDraw3D_GetMaterialValues(SlipRaceSession_materialTable, SlipRaceSession_materialTableBytes,
			                                   underSeaLookup.materialIndex, &SlipRaceSession_underSeaColor,
			                                   &underSeaMaterialControl);
		}
		TrackViewNormalizePrimitiveFlags(
		    (uint8_t *)context.componentBase, context.componentBaseBytes, SlipRaceSession_materialTable,
		    SlipRaceSession_materialTableBytes, SlipRaceSession_materialGlobal, SlipRaceSession_renderMode,
		    &SlipRaceSession_resourceRegistry, &SlipRaceSession_maths, (SlipView3DVec32){0, (int32_t)0xc000, 0});
	}

	chunk = (SlipTrackWorldChunkProcessExecution){.vertexRecords = context.vertexBufferBase,
	                                              .vertexRecordCapacity = context.vertexBufferRecordCapacity,
	                                              .buildVertexRecords = TrackView_BuildChunkVertexRecords,
	                                              .buildVertexRecordsUserData = &context,
	                                              .restoreVertexBuffer = TrackView_RestoreChunkVertexBuffer,
	                                              .restoreVertexBufferUserData = &context,
	                                              .offset = &context.chunkCallbacks.offset,
	                                              .currentChunkOrigin = &context.chunkCallbacks.currentChunkOrigin,
	                                              .classify = TrackView_RawBspClassify,
	                                              .callback = TrackView_RawBspCallback,
	                                              .rawBspUserData = &context,
	                                              .rawBspDepthCapacity = 256u,
	                                              .chunkFallback = TrackView_ChunkFallback,
	                                              .chunkFallbackUserData = &context,
	                                              .objectContinuation = TrackView_ObjectContinuation,
	                                              .objectContinuationUserData = &context,
	                                              .drawStateLoad = TrackView_LoadDrawState,
	                                              .drawStateLoadUserData = &context};
	traversal = (SlipTrackWorldTraversalContext){0,
	                                             tests.tableBase,
	                                             (SLIP_TRACK_WORLD_AXIS_TEST_BYTES),
	                                             ramps.base,
	                                             (SLIP_TRACK_WORLD_AXIS_RAMP_BYTES),
	                                             {0, 0, 0},
	                                             SlipRendererHost_state.projection.minZ,
	                                             SlipRendererHost_state.projection.maxZ,
	                                             TrackView_ProjectMask,
	                                             &context.frustum,
	                                             context.chunkBase,
	                                             context.chunkBaseBytes,
	                                             NULL,
	                                             0,
	                                             &chunk,
	                                             &context.defaultTraversalGate,
	                                             &context.mask,
	                                             &context.renderContextCount,
	                                             &context.primaryLeft,
	                                             &context.primaryTop,
	                                             &context.primaryRight,
	                                             &context.primaryBottom,
	                                             &context.traversalCallback,
	                                             &context.recordCallback,
	                                             &context.deferredEntryActive,
	                                             0,
	                                             SlipRaceSession_traversalVisits,
	                                             SLIP_RACE_TRAVERSAL_CAPACITY,
	                                             SlipRaceSession_traversalOffsets,
	                                             SlipRaceSession_traversalDepths,
	                                             SLIP_RACE_TRAVERSAL_CAPACITY};

	traversal.projectState = &SlipRendererHost_state.projection;
	traversal.frustum = &context.frustum;
	traversal.storeClipBounds = TrackView_StoreClipBoundsCallback;
	traversal.storeClipBoundsUserData = &context;
	traversal.useFullObjectViewport = context.useFullObjectViewport;
	const SlipTrackWorldComponentRefuelCalls refuelCalls = {&context, TrackView_BuildRefuelBeams,
	                                                        TrackView_StepEffectRandom};
	traversal.refuelCalls = &refuelCalls;

	for (i = 0; i < 0x300u; ++i) {
		const size_t offset = 4u + i * 0x2cu;
		if ((SLIP_TRACK_WORLD_OBJECT_LIST_BYTES)-offset < 0x2cu)
			break;
		SlipRaceSession_entryMap[entryMapCount++] = (SlipTrackWorldDosAddressMap){
		    SlipResourceHost_Payload(SlipRaceSession_objectListResource).address + (uint32_t)offset,
		    SlipRaceSession_objectList + offset, (SLIP_TRACK_WORLD_OBJECT_LIST_BYTES)-offset};
	}
	for (i = 0; i + 0x22u <= context.chunkBaseBytes && i < 0x10000u; ++i) {
		SlipRaceSession_recordMap[recordMapCount++] = (SlipTrackWorldDosAddressMap){
		    SlipRaceSession_trackBundle.trdBaseToken + (uint32_t)i, context.chunkBase + i, context.chunkBaseBytes - i};
	}
	memset(&background, 0, sizeof(background));
	cloudViewMatrix = SlipRaceSession_cameraWorldMatrix;
	cloudDraw = (TrackViewCloudDrawContext){
	    &context,
	    &SlipRaceSession_maths,
	    &cloudViewMatrix,
	    SlipRaceSession_renderMode,
	    (uint16_t)(SlipRace_cloudScrollPhase >> 16),
	    (uint16_t)(SlipRace_cloudScrollFinePhase >> 16),
	    &SlipRaceSession_cloudState,
	    NULL,
	    NULL,
	};

	background.cloudSetting = (uint32_t)SlipConfig_CloudsEnabled();
	background.cloudHook = TrackView_DrawClouds;
	background.cloudHookUserData = &cloudDraw;
	background.maths = &SlipRaceSession_maths;
	background.projectState = &SlipRendererHost_state.projection;
	background.pool = drawRecordPool;
	background.cameraHeight = cameraPosition.positionY;
	background.skyMaterial = SlipRaceSession_materialGlobals.skyMaterial;
	background.groundMaterial = SlipRaceSession_materialGlobals.groundMaterial;
	background.detailLevel = SlipRaceSession_renderMode;
	background.materialStripTable = SlipRaceSession_materialBackgroundStrips;
	background.materialStripTableBytes = sizeof(SlipRaceSession_materialBackgroundStrips);
	background.fixedStripTable = SlipRaceSession_builtBackgroundStrips;
	background.fixedStripTableBytes = sizeof(SlipRaceSession_builtBackgroundStrips);
	background.materialTable = SlipRaceSession_materialTable;
	background.materialTableBytes = SlipRaceSession_materialTableBytes;
	background.viewMatrix = &SlipRaceSession_cameraWorldMatrix;
	background.projectionScale = (uint32_t)SlipRendererHost_state.projection.projectionScale;
	background.cachedProjectionScale = 0xffffffffu;
	background.cachedFixedStripCount = 0xffu;
	background.viewportX = (uint32_t)SlipRendererHost_state.projection.centerX;
	background.viewportY = (uint32_t)SlipRendererHost_state.projection.centerY;
	background.detailScale = 0x4000u;
	background.stateRecord = SlipRaceSession_drawStates;
	background.ambientLight = 0x1000u;
	background.renderFlags = context.rendererFlags;
	background.minX = minX;
	background.maxX = maxX;
	background.minY = minY;
	background.maxY = maxY;
	background.returnVisits = SlipRaceSession_backgroundReturns;
	background.returnVisitCapacity = SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT;
	background.pointRingVisits = SlipRaceSession_backgroundPoints;
	background.pointRingVisitCapacity =
	    sizeof(SlipRaceSession_backgroundPoints) / sizeof(SlipRaceSession_backgroundPoints[0]);
	background.stripVisits = SlipRaceSession_backgroundStrips;
	background.stripVisitCapacity =
	    sizeof(SlipRaceSession_backgroundStrips) / sizeof(SlipRaceSession_backgroundStrips[0]);
	background.fixedStripVisits = SlipRaceSession_backgroundTableA;
	background.fixedStripVisitCapacity = 64;
	background.materialStripVisits = SlipRaceSession_backgroundTableB;
	background.materialStripVisitCapacity = 64;

	if (!SlipTrackWorld_PreFrameBuild(
	        &SlipTrackWorld_beams, SlipRaceSession_playerContext.trdBase, SlipRaceSession_playerContext.trackDataSize,
	        SlipRaceSession_playerContext.componentBase, SlipRaceSession_playerContext.componentBaseBytes,
	        SlipRaceSession_playerContext.trackTable, SlipRaceSession_playerContext.trackTableBytes,
	        SlipRaceSession_playerContext.trackDataOffset))
		SlipRuntime_Fatal("Invalid beam section data (0003e26c)");

	enum { SLIP_TRACK_MINIMUM_RENDER_DEPTH = 0x3d0 };

	savedTrackMinZ = SlipRendererHost_state.projection.minZ;
	SlipDraw3D_SetMinimumDepth(SLIP_TRACK_MINIMUM_RENDER_DEPTH);
	SlipRendererHost_state.projection.minZ = SLIP_TRACK_MINIMUM_RENDER_DEPTH;
	frustum.minZ = SLIP_TRACK_MINIMUM_RENDER_DEPTH;
	context.frustum.minZ = SLIP_TRACK_MINIMUM_RENDER_DEPTH;
	context.minDepth = SLIP_TRACK_MINIMUM_RENDER_DEPTH;
	traversal.minZ = SLIP_TRACK_MINIMUM_RENDER_DEPTH;
	memset(&frame, 0, sizeof(frame));
	rendered = SlipTrackWorld_FrameEntry(
	    SlipRaceSession_frameCallback, 0, SlipRendererHost_state.projection.renderFlags, context.textureMode,
	    context.shading, context.componentDistance, context.shadingSecondary, context.componentRadius, minX, minY, maxX,
	    maxY, (uint32_t)SlipRendererHost_state.projection.maxZ, 0, 0, 0, 0xffffu, (uint32_t)savedTrackMinZ, 1, 0, 0, 0,
	    0, 0x10u, SlipRaceSession_underSeaColor, context.traversalCallback, context.recordCallback,
	    context.defaultTraversalGate, minX, minY, maxX, maxY, SlipRaceSession_trackBundle.trkPayload.data,
	    SlipRaceSession_trackBundle.trkPayload.size, context.chunkBase, context.chunkBaseBytes,
	    SlipRaceSession_cellTableHost, (SLIP_TRACK_WORLD_CELL_TABLE_BYTES), SlipRaceSession_trackBundle.trdBaseToken,
	    SlipRaceSession_currentTrackRecordAddress, &traversal, &background, SlipRaceSession_objectTableHost,
	    SLIP_OBJECT_TABLE_DOS_BYTES, &SlipRaceSession_cameraWorldMatrix, &SlipRaceSession_cameraViewMatrix, 0,
	    context.detailThreshold, ramps.rampX, (SLIP_TRACK_WORLD_AXIS_RAMP_BYTES), ramps.rampY,
	    (SLIP_TRACK_WORLD_AXIS_RAMP_BYTES)-0x9cu, ramps.rampZ, (SLIP_TRACK_WORLD_AXIS_RAMP_BYTES)-0xd8u,
	    tests.testTableX, (SLIP_TRACK_WORLD_AXIS_TEST_BYTES), tests.testTableY,
	    (SLIP_TRACK_WORLD_AXIS_TEST_BYTES)-0x16u, tests.testTableZ, (SLIP_TRACK_WORLD_AXIS_TEST_BYTES)-0x1cu,
	    SlipRaceSession_axisVisits, 33, SlipRaceSession_objectList, (SLIP_TRACK_WORLD_OBJECT_LIST_BYTES),
	    SlipResourceHost_Payload(SlipRaceSession_objectListResource).address, SlipRaceSession_deferredList,
	    (SLIP_TRACK_WORLD_DEFERRED_LIST_BYTES), SlipRaceSession_entryMap, entryMapCount, SlipRaceSession_recordMap,
	    recordMapCount, SlipRaceSession_trackBundle.trcBaseToken, context.componentBase, context.componentBaseBytes,
	    context.vertexRecords, context.vertexRecordCount, TrackView_LoadDrawState, &context,
	    TrackView_BuildComponentVertexRecords, &context, TrackView_RestoreComponentVertexBuffer, &context,
	    SlipRaceSession_deferredVisits, 4096, context.specialRecord, 0, 0, context.shadows, 0, 0,
	    context.ambientLightScaleQ14, context.scaledLightX, context.scaledLightY, context.scaledLightZ,
	    context.directLightScaleQ14, 0, context.primitiveCallback, TrackView_StoreClipBoundsCallback, &context,
	    TrackView_DirectCallback, &context, TrackView_DrawComponentActors, &context, 0, SlipRaceSession_slotDrawHost,
	    (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u), SlipRaceSession_slotDrawCount, SlipRaceSession_slotClearVisits,
	    SLIP_TRACK_SLOT_DRAW_RECORD_COUNT - 1, overlayEnable, 0, 0, 0, 0, TrackView_SphereCull, &frustum,
	    TrackView_SceneryCallback, &context, TrackView_ApplyComponentLight, TrackView_RestoreComponentLight, &context,
	    &frame);
	SlipDraw3D_SetMinimumDepth((uint32_t)savedTrackMinZ);
	SlipRendererHost_state.projection.minZ = savedTrackMinZ;
	frustum.minZ = savedTrackMinZ;
	context.frustum.minZ = savedTrackMinZ;
	context.minDepth = (uint32_t)savedTrackMinZ;
	traversal.minZ = savedTrackMinZ;
	if (TrackView_RenderDiagnosticsEnabled()) {
		size_t diagnosticIndex;

		fprintf(stderr,
		        "track_view_deferred_0003a491 count=%u visits=%zu object_count=%u branch=%u eb0=%u context_eb0=%u "
		        "replay=%u\n",
		        (unsigned)frame.deferredDirect.setup.entryCount, frame.deferredDirect.visitCount,
		        (unsigned)((uint32_t)SlipRaceSession_objectList[0] | ((uint32_t)SlipRaceSession_objectList[1] << 8) |
		                   ((uint32_t)SlipRaceSession_objectList[2] << 16) |
		                   ((uint32_t)SlipRaceSession_objectList[3] << 24)),
		        (unsigned)frame.deferredDirect.branch, (unsigned)frame.listSetup.renderContextCount,
		        (unsigned)context.renderContextCount, (unsigned)context.shadows);
		for (diagnosticIndex = 0; diagnosticIndex < frame.deferredDirect.visitCount; ++diagnosticIndex) {
			const SlipTrackWorldDeferredListDirectExecuteVisit *const visit =
			    &SlipRaceSession_deferredVisits[diagnosticIndex];
			fprintf(
			    stderr,
			    "track_view_deferred_visit_0003a4cd index=%zu entry=0x%08x record=0x%08x branch=%u xyz=%d,%d,%d "
			    "direct=%u component=0x%04x flags=0x%04x mask_and=0x%04x child=0x%04x replay_count=%u tail_branch=%u "
			    "walk=%u direct_branch=%u records=%u tail_records=%zu\n",
			    diagnosticIndex, visit->entryAddress, visit->recordAddress, (unsigned)visit->prologue.branch,
			    (int32_t)visit->prologue.viewX, (int32_t)visit->prologue.viewY, (int32_t)visit->prologue.viewZ,
			    visit->callTrackWorldDeferredItemDirect ? 1u : 0u, (unsigned)visit->direct.componentOffset,
			    (unsigned)visit->direct.componentDrawMask, (unsigned)visit->direct.enabledComponentDrawMask,
			    (unsigned)visit->direct.componentTail.childOffset, (unsigned)visit->direct.componentSetup.replayCount,
			    visit->direct.componentTail.noChildList ? 1u : 0u,
			    visit->direct.callTrackWorldPrimitiveWalker ? 1u : 0u,
			    visit->direct.primitiveWalkerDirectCallbackBranch ? 1u : 0u,
			    (unsigned)visit->direct.directCallbackCount, visit->direct.componentTailVisitCount);
		}
		for (diagnosticIndex = 0; diagnosticIndex < context.gateTraceCount; ++diagnosticIndex) {
			const TrackViewRawBspGateTrace *const deferredGateTrace = &context.gateTrace[diagnosticIndex];
			fprintf(stderr,
			        "track_view_deferred_gate_0003a5b6 index=%zu leaf=0x%04x objects=%u deferred=%u branch=%u match=%u "
			        "matched=0x%08x append=%u after=%u\n",
			        diagnosticIndex, (unsigned)deferredGateTrace->recordOffset,
			        (unsigned)deferredGateTrace->objectCountBefore, (unsigned)deferredGateTrace->deferredCountBefore,
			        (unsigned)deferredGateTrace->branch, deferredGateTrace->match ? 1u : 0u,
			        deferredGateTrace->matchedRecordToken, deferredGateTrace->appended ? 1u : 0u,
			        (unsigned)deferredGateTrace->deferredCountAfter);
		}
	}

	if (frame.callSetupObjectList) {
		SlipRaceSession_currentTrackRecordAddress = frame.listSetup.selectedRecordAddressOr;
	}
	SlipRaceSession_lastRenderSucceeded = rendered;
	SlipRaceSession_lastRawBspCallbacks = context.callbackCount;
	SlipRaceSession_lastRasterizedPrimitives = context.emitPathRasterizedCount;

	SlipRaceSession_lastTrackViewContext = context;
	Raster_SetClipRect(savedClipMinX, savedClipMinY, savedClipMaxX, savedClipMaxY);
	return rendered;
}

enum {
	SLIP_RACE_MONITOR_OUTLINE_BLACK = 0x8000u,
	SLIP_RACE_MONITOR_PROJECTION_SCALE = 0x4000u,
	SLIP_RACE_MONITOR_TEXT_CENTERED = 2u,
	SLIP_RACE_MONITOR_TEXT_COLOR = 0xffu,
	SLIP_RACE_FRAME_BOUNDS_COLOR = 0xfeu
};

static uint32_t SlipRaceSession_frameBoundsEnabled;

static void SlipRaceSession_PostRender(void) {}

static bool SlipRaceSession_RenderFrame(void) {
	SlipView3DMatrix matrix;
	SlipObjectMatrixCopy copy;
	SlipObjectPosition position;
	SlipRenderer_Begin(&SlipRendererHost_state, &SlipRendererHost_lifecycleCalls);
	(void)SlipObject_MatrixCopy(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, 0, &matrix, &copy);
	(void)SlipObject_Position(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, 0, &position);
	SlipRenderer_SetCamera(
	    &SlipRendererHost_state,
	    (SlipView3DVec32){(int32_t)position.positionX, (int32_t)position.positionY, (int32_t)position.positionZ},
	    &matrix);
	uint16_t overlayEnable = 0;
	if (SlipRaceSession_frameBoundsEnabled != 0)
		overlayEnable = SLIP_RACE_FRAME_BOUNDS_COLOR;
	bool rendered = SlipRaceSession_DrawTrackFrame(overlayEnable);
	SlipRaceSession_PostRender();
	return rendered;
}

static bool SlipRaceSession_WeaponMonitor(int minX, int minY, int maxX, int maxY) {
	const uint16_t projectile = SlipRaceCamera_projectileObject;
	if (projectile == 0)
		return false;
	SlipDraw3DProjectState *const project = &SlipRendererHost_state.projection;
	SlipView3DMatrix matrix;
	SlipObjectPosition position;
	SlipObjectMatrixCopy copy;
	SlipObjectSetPosition setPosition;
	SlipObjectMatrixInstall install;
	char label[24];
	SlipObject_Hide(SlipRaceSession_objectTableHost, projectile);
	Raster_FillRectUnchecked(SLIP_RACE_MONITOR_OUTLINE_BLACK, (int16_t)minX, (int16_t)minY, (int16_t)maxX,
	                         (int16_t)maxY);
	++minX;
	++minY;
	--maxX;
	--maxY;
	SlipDraw3D_SetViewport(project, minX, minY, maxX, maxY, (minX + maxX) >> 1, (minY + maxY) >> 1);
	SlipRenderer_Begin(&SlipRendererHost_state, &SlipRendererHost_lifecycleCalls);
	const uint32_t savedScale = project->projectionMode != 0 ? project->modeOneScale : project->perspectiveScale;
	SlipDraw3D_SetProjectionScale(project, SLIP_RACE_MONITOR_PROJECTION_SCALE);
	(void)SlipObject_Position(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, projectile, &position);
	(void)SlipObject_MatrixCopy(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, projectile, &matrix,
	                            &copy);
	(void)SlipObject_SetPosition(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, 0, position.positionX,
	                             position.positionY, position.positionZ, &setPosition);
	(void)SlipObject_MatrixInstall(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, 0, &matrix, &install);
	SlipRenderer_SetCamera(
	    &SlipRendererHost_state,
	    (SlipView3DVec32){(int32_t)position.positionX, (int32_t)position.positionY, (int32_t)position.positionZ},
	    &matrix);
	(void)SlipRaceSession_DrawTrackFrame(0);
	SlipText_SelectResourceFont(&SlipText_state, SlipRaceSession_hudAssets.timeFont, &SlipRaceHud_fontResources);
	SlipText_SetStyle(&SlipText_state, SLIP_RACE_MONITOR_TEXT_CENTERED, UINT16_MAX, (int16_t)project->minX,
	                  (int16_t)project->maxX);
	SlipText_SetColor(&SlipText_state, SLIP_RACE_MONITOR_TEXT_COLOR);
	const uint32_t weapon = SlipRacePlayer_ProjectileWeaponIndex(SlipRaceCamera_projectileObject);
	SlipRacePlayer_BuildWeaponLabel(SlipRacePlayer_records, weapon, UINT32_MAX, label);
	SlipRaceHud_DrawLayoutHost(g_framebuffer, SLIPSTREAM_SCREEN_WIDTH, 0, project->minY + 2, label, NULL);
	SlipDraw3D_SetProjectionScale(project, savedScale);
	SlipObject_Show(SlipRaceSession_objectTableHost, SlipRaceCamera_projectileObject);
	return true;
}

static bool SlipRaceSession_RearMonitor(uint16_t car, int minX, int minY, int maxX, int maxY) {
	SlipDraw3DProjectState *const project = &SlipRendererHost_state.projection;
	uint32_t savedScale;
	bool rendered;
	SlipObjectPosition position;
	SlipView3DMatrix matrix;
	SlipObject_Hide(SlipRaceSession_objectTableHost, car);
	Raster_FillRectUnchecked(SLIP_RACE_MONITOR_OUTLINE_BLACK, (int16_t)minX, (int16_t)minY, (int16_t)maxX,
	                         (int16_t)maxY);
	++minX;
	++minY;
	--maxX;
	--maxY;
	SlipDraw3D_SetViewport(project, minX, minY, maxX, maxY, (minX + maxX) >> 1, (minY + maxY) >> 1);
	SlipRenderer_Begin(&SlipRendererHost_state, &SlipRendererHost_lifecycleCalls);
	savedScale = project->projectionMode != 0 ? project->modeOneScale : project->perspectiveScale;
	SlipDraw3D_SetProjectionScale(project, SLIP_RACE_MONITOR_PROJECTION_SCALE);
	(void)SlipRaceCamera_RearMonitorTransform(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, car,
	                                          &position, &matrix);
	SlipRenderer_SetCamera(
	    &SlipRendererHost_state,
	    (SlipView3DVec32){(int32_t)position.positionX, (int32_t)position.positionY, (int32_t)position.positionZ},
	    &matrix);
	rendered = SlipRaceSession_DrawTrackFrame(0);
	SlipText_SelectResourceFont(&SlipText_state, SlipRaceSession_hudAssets.timeFont, &SlipRaceHud_fontResources);
	SlipText_SetStyle(&SlipText_state, SLIP_RACE_MONITOR_TEXT_CENTERED, UINT16_MAX, (int16_t)project->minX,
	                  (int16_t)project->maxX);
	SlipText_SetColor(&SlipText_state, SLIP_RACE_MONITOR_TEXT_COLOR);
	SlipRaceHud_DrawLayoutHost(g_framebuffer, SLIPSTREAM_SCREEN_WIDTH, 0, project->minY + 2, "Rear", NULL);
	SlipDraw3D_SetProjectionScale(project, savedScale);
	SlipObject_Show(SlipRaceSession_objectTableHost, car);
	return rendered;
}

bool SlipRaceSession_DebugRenderCapturedState(const uint32_t cameraPosition[3], const uint16_t cameraMatrix[9],
                                              const uint32_t playerPosition[3], const uint16_t playerMatrix[9]) {
	SlipObjectSetPosition setPosition;
	SlipObjectMatrixInstall matrixInstall;
	SlipView3DMatrix objectMatrix;
	size_t i;

	if (!SlipRaceSession_playerReady || cameraPosition == NULL || cameraMatrix == NULL || playerPosition == NULL ||
	    playerMatrix == NULL) {
		return false;
	}
	(void)SlipRaceCamera_MainViewport(1u, SlipRace_gameMode, 0u, SlipRaceSession_cameraState.shake[0],
	                                  SlipRaceSession_cameraState.shake[1], &SlipRendererHost_state.projection);
	for (i = 0; i < 9u; ++i) {
		objectMatrix.m[i] = (int16_t)playerMatrix[i];
	}
	if (!SlipObject_SetPosition(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES,
	                            SlipRacePlayer_playerOneObject, playerPosition[0], playerPosition[1], playerPosition[2],
	                            &setPosition) ||
	    !SlipObject_MatrixInstall(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES,
	                              SlipRacePlayer_playerOneObject, &objectMatrix, &matrixInstall)) {
		return false;
	}

	SlipTrackWorld_UpdateSlots(
	    1u, SlipRaceSession_slotDrawHost, (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u),
	    SlipRaceSession_slotDrawCallbacks,
	    sizeof(SlipRaceSession_slotDrawCallbacks) / sizeof(SlipRaceSession_slotDrawCallbacks[0]),
	    SlipRaceSession_slotDrawBaseAddress, SlipRaceSession_slotDrawFreeHeadAddress,
	    (uint8_t *)(void *)SlipRaceSession_trackSlots, ((0x40u + 8u + 2u) * sizeof(SlipTrackSlotRecord)),
	    SlipRaceSession_playerContext.slotListBaseOffset, SlipRaceSession_playerContext.slotListSentinelOffset,
	    SlipRaceSession_trackBundle.trdPayload.data, SlipRaceSession_trackBundle.trdPayload.size,
	    SlipRaceSession_trackBundle.trdBaseToken, SlipRaceSession_trackBundle.trcPayload.data,
	    SlipRaceSession_trackBundle.trcPayload.size, SlipRaceSession_trackBundle.trcBaseToken,
	    SlipRaceSession_cellTableHost, (SLIP_TRACK_WORLD_CELL_TABLE_BYTES), SlipRaceSession_objectTableHost,
	    SLIP_OBJECT_TABLE_DOS_BYTES);

	for (i = 0; i < 9u; ++i) {
		objectMatrix.m[i] = (int16_t)cameraMatrix[i];
	}
	if (!SlipObject_SetPosition(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, 0, cameraPosition[0],
	                            cameraPosition[1], cameraPosition[2], &setPosition) ||
	    !SlipObject_MatrixInstall(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, 0, &objectMatrix,
	                              &matrixInstall)) {
		return false;
	}
	return SlipRaceSession_RenderFrame();
}

static void SlipRaceSession_DrawView(uint16_t view, uint32_t windowSize, const bool inputHeld[256]) {

	const uint16_t viewCraftObject = view == 1 ? SlipRacePlayer_playerOneObject : SlipRacePlayer_playerTwoObject;

	(void)SlipRaceCamera_MainViewport(view, SlipRace_gameMode, windowSize, SlipRaceSession_cameraState.shake[0],
	                                  SlipRaceSession_cameraState.shake[1], &SlipRendererHost_state.projection);

	if (SlipRaceSession_playerReady) {
		int handlerReturn = 0;
		const uint16_t dispatchedMode =
		    view == 1 ? SlipRaceSession_cameraState.viewOneMode : SlipRaceSession_cameraState.viewTwoMode;
		const SlipRaceRacerState *const hudRacer = SlipRacePlayer_RacerState(viewCraftObject);

		if (dispatchedMode == 0) {
			SlipRaceCameraIntroZoom intro;
			(void)SlipRaceCamera_IntroZoom(
			    &SlipRaceSession_cameraState, SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES,
			    viewCraftObject, view, (uint16_t)SlipFrameTimer_Step(), SlipRace_flybyChaseEnabled,
			    SlipRace_demoChaseEnabled, &SlipRaceSession_maths, SlipRaceSession_trackBundle.trdPayload.data,
			    SlipRaceSession_trackBundle.trdPayload.size, SlipRaceSession_trackBundle.trcPayload.data,
			    SlipRaceSession_trackBundle.trcPayload.size, SlipRaceSession_cellTableHost,
			    (SLIP_TRACK_WORLD_CELL_TABLE_BYTES), SlipRaceSession_trackBundle.trdBaseToken,
			    SlipRaceSession_ActivateCamera, NULL, &intro);

		} else if (dispatchedMode == 7u || dispatchedMode == 1u) {
			(void)SlipRaceCamera_External(
			    &SlipRaceSession_cameraState, SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES,
			    viewCraftObject, SlipRaceSession_trackBundle.trdPayload.data,
			    SlipRaceSession_trackBundle.trdPayload.size, SlipRaceSession_trackBundle.trcPayload.data,
			    SlipRaceSession_trackBundle.trcPayload.size, SlipRaceSession_cellTableHost,
			    (SLIP_TRACK_WORLD_CELL_TABLE_BYTES), SlipRaceSession_trackBundle.trdBaseToken);
		} else if (dispatchedMode == 4) {
			(void)SlipRaceCamera_Chase(
			    &SlipRaceSession_cameraState, SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES,
			    viewCraftObject, view, &SlipRaceSession_maths, SlipRaceSession_trackBundle.trdPayload.data,
			    SlipRaceSession_trackBundle.trdPayload.size, SlipRaceSession_trackBundle.trcPayload.data,
			    SlipRaceSession_trackBundle.trcPayload.size, SlipRaceSession_cellTableHost,
			    (SLIP_TRACK_WORLD_CELL_TABLE_BYTES), SlipRaceSession_trackBundle.trdBaseToken);
		} else if (dispatchedMode == 6) {
			SlipObjectPosition craft;
			uint32_t selected, distance;
			int32_t nearest;
			if (SlipObject_Position(SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, viewCraftObject,
			                        &craft) &&
			    SlipRaceCamera_SelectTv(
			        &SlipRaceSession_cameraState,
			        (SlipView3DVec32){(int32_t)craft.positionX, (int32_t)craft.positionY, (int32_t)craft.positionZ},
			        SlipRaceSession_trackBundle.trdPayload.data, SlipRaceSession_trackBundle.trdPayload.size,
			        SlipRaceSession_trackBundle.trcPayload.data, SlipRaceSession_trackBundle.trcPayload.size,
			        SlipRaceSession_cellTableHost, (SLIP_TRACK_WORLD_CELL_TABLE_BYTES),
			        SlipRaceSession_trackBundle.trdBaseToken, &selected, &nearest)) {
				if (SlipObject_Speed(SlipRaceSession_objectTableHost, viewCraftObject) >= 0x2ba3e &&
				    selected != SlipRaceSession_cameraState.tvSoundCamera &&
				    nearest < SlipRaceSession_cameraState.tvSoundDistance && nearest <= 0x17d40) {
					SlipSoundEffects_PlayFlyby(&SlipRaceSession_soundEffects);
					SlipRaceSession_cameraState.tvSoundCamera = selected;
					SlipRaceSession_cameraState.tvSoundDistance = nearest;
				}
				if (SlipRaceCamera_TvTransform(&SlipRaceSession_cameraState, SlipRaceSession_objectTableHost,
				                               SLIP_OBJECT_TABLE_DOS_BYTES, viewCraftObject, selected, &distance)) {
					const uint32_t savedScale = SlipRendererHost_state.projection.projectionMode != 0
					                                ? SlipRendererHost_state.projection.modeOneScale
					                                : SlipRendererHost_state.projection.perspectiveScale;
					SlipDraw3D_SetProjectionScale(&SlipRendererHost_state.projection, SlipRaceCamera_TvScale(distance));
					(void)SlipRaceSession_RenderFrame();
					SlipDraw3D_SetProjectionScale(&SlipRendererHost_state.projection, savedScale);
				}
			}
		} else if (dispatchedMode == 5) {
			SlipObject_Hide(SlipRaceSession_objectTableHost, viewCraftObject);
			(void)SlipRaceCamera_Rear(
			    SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, viewCraftObject, view,
			    SlipRaceSession_playerContext.articSlotPool, SlipRaceSession_playerContext.articSlotPoolBytes,
			    SlipRaceSession_playerContext.articSlotPoolOffset, SlipRaceSession_playerContext.articData,
			    SlipRaceSession_playerContext.articDataBytes, SlipRaceSession_playerContext.articDataOffset,
			    &SlipRaceSession_maths);
		} else if (dispatchedMode == 8) {
			(void)SlipRaceCamera_Dropped(&SlipRaceSession_cameraState, SlipRaceSession_objectTableHost,
			                             SLIP_OBJECT_TABLE_DOS_BYTES, viewCraftObject, view);
		} else if (dispatchedMode == 3) {
			SlipArticSlotPosition cockpit;

			SlipObject_Hide(SlipRaceSession_objectTableHost, viewCraftObject);
			bool cockpitReady = SlipRaceCamera_Cockpit(
			    SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, viewCraftObject, view,
			    SlipRaceSession_articPool.allocation, SlipRaceSession_articPool.allocationBytes,
			    SlipRaceSession_articPool.allocationAddress, SlipRaceSession_playerContext.articData,
			    SlipRaceSession_playerContext.articDataBytes, SlipRaceSession_playerContext.articDataOffset,
			    &SlipRaceSession_maths, &cockpit);

			if (cockpitReady) {
				uint16_t light;

				(void)SlipRaceSession_RenderFrame();

				SlipRaceSession_DrawCockpitSight(viewCraftObject);
				if (SlipRacePlayer_TrackLight(viewCraftObject, &light)) {
					handlerReturn = SlipRaceHud_DrawUpperConsole(
					    &SlipRaceSession_hudAssets, light, g_framebuffer, SLIPSTREAM_SCREEN_WIDTH,
					    SlipRendererHost_state.projection.minX, SlipRendererHost_state.projection.maxY);
				}
			}

			SlipObject_Show(SlipRaceSession_objectTableHost, viewCraftObject);
		} else if (dispatchedMode == 2 && hudRacer != NULL) {
			(void)SlipRaceCamera_Finish(&SlipRaceSession_cameraState, SlipRaceSession_objectTableHost,
			                            SLIP_OBJECT_TABLE_DOS_BYTES, viewCraftObject, hudRacer->racePosition);
		}
		if (dispatchedMode != 3 && dispatchedMode != 6) {
			(void)SlipRaceSession_RenderFrame();
		}
		if (dispatchedMode == 5u)
			SlipObject_Show(SlipRaceSession_objectTableHost, viewCraftObject);
		if (dispatchedMode == 2)
			SlipRaceHud_DrawFinishPosition(&SlipRaceSession_hudAssets, g_framebuffer, SLIPSTREAM_SCREEN_WIDTH,
			                               SlipRendererHost_state.projection.minY,
			                               SlipRaceSession_cameraState.finishPosition);

		if (dispatchedMode != 2 && hudRacer != NULL && (SlipRaceSession_hudAssets.flags & 1u) == 0) {
			SlipRaceHud_DrawRaceStatus(&SlipRaceSession_hudAssets, g_framebuffer, SLIPSTREAM_SCREEN_WIDTH,
			                           SlipRendererHost_state.projection.minY, SlipRendererHost_state.projection.maxX,
			                           (uint16_t)(hudRacer->racePosition & 0x7fffu), hudRacer->lapNumber,
			                           hudRacer->currentLapTime, SlipRaceSession_cameraState.lapTimeTimer[view - 1],
			                           SlipRaceSession_cameraState.lapTime[view - 1]);
			SlipRaceHud_DrawSpeed(&SlipRaceSession_hudAssets, g_framebuffer, SLIPSTREAM_SCREEN_WIDTH,
			                      SlipRendererHost_state.projection.minX, SlipRendererHost_state.projection.minY,
			                      SlipRacePlayer_Speed(viewCraftObject), SlipConfig_SpeedDisplay(),
			                      SlipRacePlayer_startCountdown);
		}
		if (dispatchedMode == 7u || dispatchedMode == 1u)
			SlipRaceCamera_ExternalControls(&SlipRaceSession_cameraState, (uint16_t)SlipFrameTimer_Step(), inputHeld,
			                                &SlipRaceSession_maths);
		if (dispatchedMode == 4u) {
			(void)SlipRaceCamera_UpdateChaseMatrix(&SlipRaceSession_cameraState, SlipRaceSession_objectTableHost,
			                                       SLIP_OBJECT_TABLE_DOS_BYTES, viewCraftObject, view,
			                                       (uint16_t)SlipFrameTimer_Step(), &SlipRaceSession_maths);
		}

		SlipRaceHud_PublishHandlerResult(&SlipRaceSession_hudState, handlerReturn,
		                                 SlipRendererHost_state.projection.maxY, view);
		SlipRaceSession_DrawCameraName(view, dispatchedMode);
		SlipRaceSession_DrawFinalLapNotification(view);
	}
}

SlipRaceFrameResult SlipRaceSession_RunFrame(uint32_t tick, const bool inputHeld[256], bool inputPressed[256],
                                             uint32_t windowSize, int mouseX, int mouseY) {
	SlipRacePauseAction pauseAction;

	SlipFrameTimer_Update(tick);

	SlipRace_drawPageToggle ^= 1u;

	SlipRace_PreCameraInput(inputHeld, inputPressed, &SlipRace_controls);

	SlipRaceSession_DrawView(1u, windowSize, inputHeld);
	if (SlipRace_gameMode == 1)
		SlipRaceSession_DrawView(2u, windowSize, inputHeld);
	if (SlipRaceSession_playerReady) {

		if (SlipRace_gameMode != 1 && SlipConfig_TrackMapEnabled() != 0 && SlipRacePlayer_track > 0 &&
		    SlipRacePlayer_track < 11u) {
			const uint32_t center = SlipRaceSession_mapCenters[SlipRacePlayer_track];
			const uint16_t rivalObject =
			    SlipRace_gameMode == 1 ? SlipRacePlayer_playerTwoObject : SlipRacePlayer_thirdObject;

			SlipRaceMap_Draw(
			    SlipRaceSession_mapScales[SlipRacePlayer_track], 0xfcu, 0xffu, 0xfbu, SlipRacePlayer_playerOneObject,
			    rivalObject, (int16_t)(center & 0xffffu), (int16_t)(center >> 16), 0xfeu, 0xffu, &SlipRaceSession_maths,
			    &SlipRendererHost_state.projection, SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES,
			    SlipRaceSession_trackBundle.trkPayload.data, SlipRaceSession_trackBundle.trkPayload.size,
			    SlipRaceSession_trackBundle.trdPayload.data, SlipRaceSession_trackBundle.trdPayload.size,
			    SlipRaceSession_trackSlots,
			    ((0x40u + 8u + 2u) * sizeof(SlipTrackSlotRecord)) / sizeof(SlipRaceSession_trackSlots[0]),
			    SlipRaceSession_playerContext.slotListBaseOffset, SlipRaceSession_playerContext.slotListSentinelOffset);
		}
		bool drawPlayerIndicators = SlipRaceSession_hudState.playerOneConsoleRedrawFrames != 0;
		if (SlipRace_gameMode != 1) {
			bool weaponMonitorDrawn = false;
			if (SlipConfig_WeaponsMonitor() != 0)
				weaponMonitorDrawn = SlipRaceSession_WeaponMonitor(210, 99, 301, 147);
			if (!weaponMonitorDrawn && SlipConfig_RearMonitor() != 0)
				(void)SlipRaceSession_RearMonitor(SlipRacePlayer_playerOneObject, 210, 99, 301, 147);
		}

		if (SlipRace_gameMode != 1 && SlipRaceSession_gameSound != NULL) {
			const uint32_t speakingDriver = SlipRaceVoice_SpeakingDriver(SlipRaceSession_gameSound);
			if (speakingDriver != 0) {
				int16_t portraitY = SLIP_RACE_PORTRAIT_TOP;
				if (SlipConfig_WindowSize() != 0)
					portraitY += SLIP_RACE_PORTRAIT_WINDOW_OFFSET;
				SlipRaceHud_DrawSpriteResource(SlipRaceSession_portraits[speakingDriver - 1], g_framebuffer,
				                               SLIPSTREAM_SCREEN_WIDTH, SLIP_RACE_PORTRAIT_LEFT, portraitY);
				SlipText_SelectResourceFont(&SlipText_state, SlipMenu_resources.smallFont, &SlipRaceHud_fontResources);
				SlipText_SetStyle(&SlipText_state, SLIP_RACE_PORTRAIT_LABEL_CENTERED,
				                  SLIP_RACE_PORTRAIT_LABEL_FONT_SPACING, SLIP_RACE_PORTRAIT_LEFT,
				                  SLIP_RACE_PORTRAIT_RIGHT);
				SlipText_SetColor(&SlipText_state, SLIP_RACE_PORTRAIT_LABEL_COLOR);
				SlipRaceFindRacer racer = SlipRace_FindRacer(SlipRace_activeRacerTable, (uint16_t)speakingDriver);
				if (!racer.racerNotFound) {
					const char *label = "FINISHED";
					int16_t rank;
					SlipTextArgument argument;
					const SlipTextArgument *arguments = NULL;
					if (racer.record->finished == 0) {
						rank = (int16_t)racer.record->racePosition;
						argument.word = &rank;
						arguments = &argument;
						label = "%d";
					}
					SlipTextPosition labelPosition = {SLIP_RACE_PORTRAIT_LEFT,
					                                  (int16_t)(portraitY + SLIP_RACE_PORTRAIT_LABEL_OFFSET)};
					SlipText_Draw(&SlipText_state, label, arguments, &labelPosition);
				}
			}
		}

		if (drawPlayerIndicators) {

			(void)SlipRaceCamera_MainViewport(1u, SlipRace_gameMode, windowSize, SlipRaceSession_cameraState.shake[0],
			                                  SlipRaceSession_cameraState.shake[1], &SlipRendererHost_state.projection);
		}
		SlipRaceHud_DrawLowerConsole(&SlipRaceSession_hudState, &SlipRaceSession_hudAssets, g_framebuffer,
		                             SLIPSTREAM_SCREEN_WIDTH, 1u);
		if (drawPlayerIndicators && SlipRace_playerOneFinished == 0) {
			SlipRaceSession_DrawPlayerIndicators(SlipRaceSession_hudState.playerOneConsoleSelection,
			                                     SlipRacePlayer_playerOneObject,
			                                     SlipRaceSession_hudState.playerOneLowerConsoleY);
		}

		if (SlipRaceSession_hudState.playerTwoConsoleRedrawFrames != 0) {
			(void)SlipRaceCamera_MainViewport(2u, SlipRace_gameMode, windowSize, SlipRaceSession_cameraState.shake[0],
			                                  SlipRaceSession_cameraState.shake[1], &SlipRendererHost_state.projection);
			SlipRaceHud_DrawLowerConsole(&SlipRaceSession_hudState, &SlipRaceSession_hudAssets, g_framebuffer,
			                             SLIPSTREAM_SCREEN_WIDTH, 2u);
			if (SlipRace_playerTwoFinished == 0)
				SlipRaceSession_DrawPlayerIndicators(SlipRaceSession_hudState.playerTwoConsoleSelection,
				                                     SlipRacePlayer_playerTwoObject,
				                                     SlipRaceSession_hudState.playerTwoLowerConsoleY);
		}
	}
	SlipRaceHud_DrawBorders(&SlipRaceSession_hudState, SlipRace_gameMode, windowSize, SlipRaceSession_hudAssets.flags);

	SlipRaceSession_DrawPauseMenu();

	{
		SlipTrackWorldCurrentSlot slot;
		uint32_t ambientSound;
		const uint8_t *objectName;

		SlipTrackWorld_CurrentSlot(
		    0u, SlipRacePlayer_playerOneObject, (uint8_t *)(void *)SlipRaceSession_trackSlots,
		    ((0x40u + 8u + 2u) * sizeof(SlipTrackSlotRecord)), SlipRaceSession_playerContext.slotListBaseOffset,
		    SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, SlipRaceSession_trackBundle.trdPayload.data,
		    SlipRaceSession_trackBundle.trdPayload.size, SlipRaceSession_trackBundle.trdBaseToken,
		    SlipRaceSession_trackBundle.trcPayload.data, SlipRaceSession_trackBundle.trcPayload.size,
		    SlipRaceSession_trackBundle.trcBaseToken, SlipRaceSession_cellTableHost,
		    (SLIP_TRACK_WORLD_CELL_TABLE_BYTES), SlipRacePlayer_refuelSection, &slot);
		if (slot.carryOut) {
			ambientSound = 1u;
		} else {
			objectName = SlipTrackWorld_GetCurrentName(
			    0u, SlipRacePlayer_playerOneObject, (uint8_t *)(void *)SlipRaceSession_trackSlots,
			    ((0x40u + 8u + 2u) * sizeof(SlipTrackSlotRecord)), SlipRaceSession_playerContext.slotListBaseOffset,
			    SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES,
			    SlipRaceSession_trackBundle.trdPayload.data, SlipRaceSession_trackBundle.trdPayload.size,
			    SlipRaceSession_trackBundle.trdBaseToken, SlipRaceSession_trackBundle.trcPayload.data,
			    SlipRaceSession_trackBundle.trcPayload.size, SlipRaceSession_trackBundle.trcBaseToken,
			    SlipRaceSession_cellTableHost, (SLIP_TRACK_WORLD_CELL_TABLE_BYTES));
			if (objectName != NULL && (memcmp(objectName, "CROW", 4u) == 0 || memcmp(objectName, "GRID", 4u) == 0))
				ambientSound = 2u;
			else
				ambientSound = 0u;
		}
		SlipSoundEffects_SetAmbient(&SlipRaceSession_soundEffects, ambientSound);
	}

	SlipSoundEffects_EndFrame(&SlipRaceSession_soundEffects, 1u, 0, 0, 0, SlipRacePlayer_playerOneObject,
	                          SlipRacePlayer_playerTwoObject);
	SlipSoundEffects_BeginFrame(&SlipRaceSession_soundEffects);

	SlipMenu_PresentFrame();

	if (SlipRacePlayer_demoMode != 0 && (SlipInput_TestAndClear(inputPressed, SLIP_INPUT_SCAN_ENTER) ||
	                                     SlipInput_TestAndClear(inputPressed, SLIP_INPUT_SCAN_SPACE) ||
	                                     SlipInput_TestAndClear(inputPressed, SLIP_INPUT_MOUSE_LEFT) ||
	                                     (SlipRace_controls.actions & SLIP_ACTION_PAUSE)))
		pauseAction = SLIP_RACE_PAUSE_ACTION_QUIT_RACE;
	else
		pauseAction = SlipRaceSession_UpdatePauseMenu(inputPressed, mouseX, mouseY);
	switch (pauseAction) {
	case SLIP_RACE_PAUSE_ACTION_CONTINUE:
		SlipRace_menuRequest = 1;
		break;
	case SLIP_RACE_PAUSE_ACTION_CONFIGURATION:

		SlipSoundEffects_StopAmbient(&SlipRaceSession_soundEffects);
		return SLIP_RACE_FRAME_OPEN_CONFIGURATION;
	case SLIP_RACE_PAUSE_ACTION_QUIT_RACE:
	case SLIP_RACE_PAUSE_ACTION_REPLAY_END:
		SlipRaceSession_exitRequested = 1;
		break;
	case SLIP_RACE_PAUSE_ACTION_EXIT_TO_DOS:
		return SLIP_RACE_FRAME_EXIT_PROGRAM;
	case SLIP_RACE_PAUSE_ACTION_NONE:
		break;
	}

	if (pauseAction != SLIP_RACE_PAUSE_ACTION_QUIT_RACE && pauseAction != SLIP_RACE_PAUSE_ACTION_REPLAY_END) {
		bool finishDelayExpired = false;
		if (SlipRaceSession_pauseState != SLIP_RACE_PAUSE_RUNNING) {
			return SLIP_RACE_FRAME_CONTINUE;
		}

		SlipRaceDrone_Update();
		{
			SlipFrameTimerValues timerValues = SlipFrameTimer_Values();
			const uint16_t elapsedTime = (uint16_t)timerValues.deltaMilliseconds;

			if (SlipRacePlayer_startCountdown != 0) {
				SlipRaceSession_countdownTimer = (uint16_t)(SlipRaceSession_countdownTimer - elapsedTime);
				if ((int16_t)SlipRaceSession_countdownTimer < 0) {
					SlipRaceSession_countdownTimer = (uint16_t)(SlipRaceSession_countdownTimer + 1000u);
					--SlipRacePlayer_startCountdown;
					if (SlipRacePlayer_startCountdown == 3u)
						SlipSoundEffects_Queue(&SlipRaceSession_soundEffects, 0, 0, 0, 0x0cu, 0, 0);
					if (SlipRacePlayer_startCountdown == 1u) {
						SlipSoundEffects_PlayHigh(&SlipRaceSession_soundEffects);
						SlipSoundEffects_EnableEngineLoops(&SlipRaceSession_soundEffects);
					}
				}
			}
			if (SlipRacePlayer_startCountdown == 0 && SlipRacePlayer_positionBoostTimer != 0) {
				if ((int32_t)(SlipRacePlayer_positionBoostTimer - timerValues.deltaMilliseconds) < 0) {
					SlipRacePlayer_positionBoostTimer = 0;
				} else {
					SlipRacePlayer_positionBoostTimer -= timerValues.deltaMilliseconds;
				}
			}

			if (SlipRaceSession_finishDelay != UINT16_MAX) {
				SlipRaceSession_finishDelay = (uint16_t)(SlipRaceSession_finishDelay - elapsedTime);
				finishDelayExpired = (int16_t)SlipRaceSession_finishDelay < 0;
			}
		}

		if (!finishDelayExpired) {

			{
				const SlipRaceRacerState *const musicRacer = SlipRacePlayer_RacerState(SlipRacePlayer_playerOneObject);
				uint32_t position = 0;
				if (musicRacer != NULL) {
					position = musicRacer->racePosition;
					if (musicRacer->finished != 0)
						position |= 0x8000;
				}
				SlipMenuMusic_RaceUpdate(SlipFrameTimer_Values().deltaMilliseconds, position);
			}

			if (!SlipRaceSession_playerReady) {
				return SLIP_RACE_FRAME_CONTINUE;
			}

			SlipRace_UpdateGlobals();
			SlipRacePlayer_UpdateMinimumPosition(SlipRaceSession_racerStates, SlipRace_activeRacerTable->racerCount);

			SlipObject_DispatchUpdate(0, 0);

			SlipRacePhysics_RunFrame();

			SlipRace_UpdateTimedEffects();
			SlipRace_UpdateTrackFrameState(&SlipRaceSession_playerContext);

			SlipTrackWorld_UpdateSlots(
			    1u, SlipRaceSession_slotDrawHost, (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u),
			    SlipRaceSession_slotDrawCallbacks,
			    sizeof(SlipRaceSession_slotDrawCallbacks) / sizeof(SlipRaceSession_slotDrawCallbacks[0]),
			    SlipRaceSession_slotDrawBaseAddress, SlipRaceSession_slotDrawFreeHeadAddress,
			    (uint8_t *)(void *)SlipRaceSession_trackSlots, ((0x40u + 8u + 2u) * sizeof(SlipTrackSlotRecord)),
			    SlipRaceSession_playerContext.slotListBaseOffset, SlipRaceSession_playerContext.slotListSentinelOffset,
			    SlipRaceSession_trackBundle.trdPayload.data, SlipRaceSession_trackBundle.trdPayload.size,
			    SlipRaceSession_trackBundle.trdBaseToken, SlipRaceSession_trackBundle.trcPayload.data,
			    SlipRaceSession_trackBundle.trcPayload.size, SlipRaceSession_trackBundle.trcBaseToken,
			    SlipRaceSession_cellTableHost, (SLIP_TRACK_WORLD_CELL_TABLE_BYTES), SlipRaceSession_objectTableHost,
			    SLIP_OBJECT_TABLE_DOS_BYTES);

			SlipObject_DispatchPostUpdate(0, 0, 0, 0, 0, 0);

			SlipRaceSession_UpdateProgress();
			if (SlipRace_Finished() == 0) {
				return SLIP_RACE_FRAME_CONTINUE;
			}
		}

		SlipRaceSession_FinalizeRacers(SlipRaceSession_track);
		SlipRaceSession_exitRequested = 0;
	}

	{
		size_t racerIndex;
		TrackViewTrackLifecycleArgs trackLifecycle = {
		    &SlipRaceSession_cloudState,
		    SlipRaceSession_archivesHost,
		    SlipRaceSession_archiveCountHost,
		    &SlipRaceSession_maths,
		};

		for (racerIndex = 0; racerIndex < 10u; ++racerIndex) {
			if (SlipRaceSession_racerArtHandles[racerIndex] != 0) {

				SlipActor_ReleaseResources(SlipRaceSession_racerArtHandles[racerIndex], &SlipActorHost_resourceCalls);
				SlipResourceHost_Release(NULL, SlipRaceSession_racerArtHandles[racerIndex]);
				SlipResource_ReleaseHandle(&SlipRaceSession_racerArtPayload[racerIndex]);
			}
		}

		if (SlipRaceSession_portraits[SLIP_RACE_PORTRAIT_FIRST_INDEX] != 0)
			SlipResourceHost_ReleaseSequence(NULL, SlipRaceSession_portraits, SLIP_RACE_PORTRAIT_COUNT);

		SlipSoundEffects_Shutdown(&SlipRaceSession_soundEffects);

		if (SlipRaceSession_gameSound != NULL)
			SlipRaceVoice_Shutdown(SlipRaceSession_gameSound);

		SlipStringTable_Release(SlipRaceSession_pauseStrings, &SlipRaceSession_pauseStringResources);
		SlipRaceHud_Shutdown(&SlipRaceSession_hudAssets, SlipRaceSession_cleanupCallback, &trackLifecycle);

		SlipRaceSession_ShutdownEffects();
		SlipRaceSession_ReleaseWeaponShapes();
		SlipRaceDrone_Shutdown(SlipRaceSession_droneArtHandle, &SlipRaceSession_droneArtPayload,
		                       &SlipRaceSession_resourceRegistry);
		SlipRaceSession_ShutdownBonuses();
		SlipRaceSession_ShutdownWorld();

		SlipMenuMusic_RaceStop();
		return SLIP_RACE_FRAME_ENDED;
	}
}

static bool SlipRaceSession_IntroProgress(void *context, int32_t *progress) {
	(void)context;
	*progress = (int32_t)SlipTrackWorld_RaceProgress(
	    SlipRacePlayer_playerOneObject, SlipRaceSession_playerContext.slotListBase,
	    SlipRaceSession_playerContext.slotListBytes, SlipRaceSession_playerContext.slotListBaseOffset,
	    SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, SlipRaceSession_trackBundle.trdPayload.data,
	    SlipRaceSession_trackBundle.trdPayload.size, SlipRaceSession_trackBundle.trdBaseToken,
	    SlipRaceSession_trackBundle.trcPayload.data, SlipRaceSession_trackBundle.trcPayload.size,
	    SlipRaceSession_trackBundle.trcBaseToken, SlipRaceSession_cellTableHost, (SLIP_TRACK_WORLD_CELL_TABLE_BYTES));
	return *progress != -1;
}

static bool SlipRaceSession_IntroName(void *context, uint8_t name[8]) {
	(void)context;
	const uint8_t *const source = SlipTrackWorld_GetCurrentName(
	    0u, SlipRacePlayer_playerOneObject, (uint8_t *)(void *)SlipRaceSession_trackSlots,
	    ((0x40u + 8u + 2u) * sizeof(SlipTrackSlotRecord)), SlipRaceSession_playerContext.slotListBaseOffset,
	    SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES, SlipRaceSession_trackBundle.trdPayload.data,
	    SlipRaceSession_trackBundle.trdPayload.size, SlipRaceSession_trackBundle.trdBaseToken,
	    SlipRaceSession_trackBundle.trcPayload.data, SlipRaceSession_trackBundle.trcPayload.size,
	    SlipRaceSession_trackBundle.trcBaseToken, SlipRaceSession_cellTableHost, (SLIP_TRACK_WORLD_CELL_TABLE_BYTES));
	if (source == NULL)
		return false;
	memcpy(name, source, 8);
	return true;
}

static SlipRaceRacerTable introRacers = {.racerCount = 1};

static SlipFont SlipRaceSession_LockFont(void *context, uint16_t resource) {
	SlipResourceHost_Lock(context, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	SlipFont font;
	if (!SlipFont_FromPayload(&payload, &font))
		SlipRuntime_Fatal("Invalid intro font host view.");
	return font;
}

static const SlipFontResourceCalls introFontResources = {.lock = SlipRaceSession_LockFont,
                                                         .unlock = SlipResourceHost_Unlock};

static void SlipRaceSession_DrawCaption(const char *caption) {
	SlipText_SelectResourceFont(&SlipText_state, SlipMenu_resources.shadedFont, &introFontResources);
	SlipText_SetColor(&SlipText_state, UINT16_MAX);
	SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, 8, 311);
	SlipDraw3DClipAndCenter viewport;
	SlipDraw3D_LoadClipAndCenter(&SlipRendererHost_state.projection, &viewport);
	SlipText_DrawCentered(&SlipText_state, caption, NULL, (int16_t)viewport.clipMaxX, 170, 197);
}

void SlipRaceSession_PlayIntro(const char *resPath, uint16_t axTrack, uint16_t selectedDriver, uint16_t language,
                               uint32_t environmentDetail, uint32_t shading, uint32_t textures, uint32_t shadows,
                               uint32_t windowSize, const bool inputHeld[256], bool inputPressed[256],
                               const SlipRaceIntroScriptHost *scriptHost, const SlipRaceIntroResources *resources) {
	char secondaryPath[512], scriptName[13];
	const char *archives[2];
	size_t archiveCount = SlipMenu_BuildArchiveList(resPath, secondaryPath, archives), i;
	SlipTrackWorldSlotDrawInstall slotDraw;
	SlipTrackWorldSlotListInstall slotList;
	SlipRaceCreatePlayer createPlayer;
	TrackViewTrackLifecycleArgs trackLifecycle;
	SlipRaceRacerTable *const racerTable = &introRacers;
	const uint16_t tuningIndex = 1;
	uint32_t artHandle;
	uint16_t scriptResource;
	SlipResourcePayload script;
	SlipStringTableSlot *strings;
	const SlipStringTableResources introStringResources = {.load = SlipResourceHost_Load,
	                                                       .lock = SlipResourceHost_Lock,
	                                                       .unlock = SlipResourceHost_Unlock,
	                                                       .release = SlipResourceHost_Release};
	SlipRaceIntroScript state = {.cursor = 16};
	SlipRaceIntroScriptHost host = *scriptHost;
	host.raceProgress = SlipRaceSession_IntroProgress;
	host.trackName = SlipRaceSession_IntroName;
	const uint32_t song = SlipRandom_Range(3);
	SlipStringTable_SetLanguage(&SlipStringTable_state, (uint8_t)language);
	SlipRace_flybyChaseEnabled = 1;
	SlipRacePlayer_flybyMode = 1;
	if (SlipRaceSession_gameSound != NULL && SlipRaceSession_gameSound->musicCard != 0 &&
	    !SlipMenuMusic_ScriptedStart(song))
		SlipRuntime_Fatal("Could not load intro music.");
	SlipRaceSession_archiveCountHost = archiveCount;
	for (i = 0; i < archiveCount; ++i) {
		const size_t length = strlen(archives[i]);

		if (length >= sizeof(SlipRaceSession_archivePathHost[i])) {
			SlipRuntime_Fatal("Could not initialize intro player.");
		}
		memcpy(SlipRaceSession_archivePathHost[i], archives[i], length + 1u);
		SlipRaceSession_archivesHost[i] = SlipRaceSession_archivePathHost[i];
	}
	archives[0] = SlipRaceSession_archivesHost[0];
	if (archiveCount > 1u) {
		archives[1] = SlipRaceSession_archivesHost[1];
	}

	SlipRaceSession_textureMode = textures == 2u ? 0u : 1u;
	SlipRaceSession_shading = shading;
	SlipRaceSession_shadingSecondary = shading != 0 ? shading - 1u : 0u;
	SlipRaceSession_shadows = shadows;

	if (!SlipRaceSession_LoadWorld(axTrack, archives, archiveCount, &slotDraw, &slotList))
		SlipRuntime_Fatal("Could not load intro world.");

	static const char *const names[10] = {"CHICAGO.ANN",  "HAWAII.ANN", "TOKYO.ANN",  "NORWAY.ANN", "CAVE.ANN",
	                                      "COLORADO.ANN", "AMAZON.ANN", "LONDON.ANN", "EGYPT.ANN",  "NEWYORK.ANN"};
	strcpy(scriptName, names[axTrack - 1]);

	if (!SlipResourceHost_Load(NULL, scriptName, &scriptResource))
		SlipGame_ResourceFailure();
	SlipResourceHost_LockWritable(NULL, scriptResource);
	script = SlipResourceHost_Payload(scriptResource);
	if (script.size < 16)
		SlipRuntime_Fatal("Invalid track ANN host view.");
	const uint16_t countdown = SlipBytes_ReadLE16(script.data + 12);
	char stringBase[9];
	memcpy(stringBase, script.data, 8);
	stringBase[8] = 0;
	if (!SlipStringTable_Load(&SlipStringTable_state, stringBase, &introStringResources, &strings))
		SlipGame_ResourceFailure();
	SlipText_SelectResourceFont(&SlipText_state, SlipMenu_resources.smallFont, &introFontResources);
	Raster_SetClipRect(0, 0, 319, 199);
	Raster_FillRectClipped(0, 0, 0, 319, 199);
	SlipMenu_PresentFrame();
	Raster_FillRectClipped(0, 0, 0, 319, 199);
	SlipRaceSession_resourceRegistry.archives = SlipRaceSession_archivesHost;
	SlipRaceSession_resourceRegistry.archiveCount = archiveCount;
	if (!TrackView_LoadNamedResource(&SlipRaceSession_resourceRegistry, "RACER0.ART", &artHandle))
		SlipRuntime_Fatal("Could not load intro aircraft.");
	SlipRaceSession_racerArtHandles[0] = (uint16_t)artHandle;
	SlipActor_PreloadResources((uint16_t)artHandle, &SlipActorHost_resourceCalls);
	SlipRaceSession_racerArtPayload[0] = SlipResourceHost_Payload((uint16_t)artHandle);
	SlipRace_BuildRacerTable(racerTable, 1, 0);
	racerTable->records[0].racerType = 2;
	racerTable->records[0].powerupRecord = UINT32_MAX;
	racerTable->records[0].powerupFlags = 0;
	memset(&SlipRaceSession_playerContext, 0, sizeof(SlipRaceSession_playerContext));
	SlipRaceSession_playerContext.objectTable = SlipRaceSession_objectTableHost;
	SlipRaceSession_playerContext.objectTableBytes = SLIP_OBJECT_TABLE_DOS_BYTES;
	SlipRaceSession_playerContext.playerStates = SlipRaceSession_playerStates;
	SlipRaceSession_playerContext.playerStateCount =
	    sizeof(SlipRaceSession_playerStates) / sizeof(SlipRaceSession_playerStates[0]);
	SlipRaceSession_racerStates = racerTable->records;
	SlipRaceSession_playerContext.racerStates = SlipRaceSession_racerStates;
	SlipRaceSession_playerContext.racerStateCount = SLIP_RACE_RACER_COUNT;
	SlipRaceSession_playerContext.racerRecordsOffset = 0x00058071u;
	SlipRaceSession_playerContext.weaponRecords = SlipRacePlayer_records;
	SlipRaceSession_playerContext.aiBaseSpeed = SlipRacePlayer_aiBaseSpeed;
	SlipRaceSession_playerContext.aiSpeedScale = SlipRacePlayer_aiSpeedScale;
	SlipRaceSession_playerContext.aiSpeedTableCount =
	    sizeof(SlipRacePlayer_aiBaseSpeed) / sizeof(SlipRacePlayer_aiBaseSpeed[0]);
	SlipRaceSession_playerContext.maths = &SlipRaceSession_maths;
	SlipRaceSession_playerContext.slotListBase = (uint8_t *)(void *)SlipRaceSession_trackSlots;
	SlipRaceSession_playerContext.slotListBytes = ((0x40u + 8u + 2u) * sizeof(SlipTrackSlotRecord));
	SlipRaceSession_playerContext.slotListBaseOffset = slotList.baseAddress;
	SlipRaceSession_playerContext.slotListSentinelOffset = slotList.activeListAddress;
	SlipRaceSession_playerContext.slotDrawCallbacks = SlipRaceSession_slotDrawCallbacks;
	SlipRaceSession_playerContext.slotDrawCallbackCount =
	    sizeof(SlipRaceSession_slotDrawCallbacks) / sizeof(SlipRaceSession_slotDrawCallbacks[0]);
	SlipRaceSession_playerContext.trdBase = SlipRaceSession_trackBundle.trdPayload.data;
	SlipRaceSession_playerContext.trackDataSize = SlipRaceSession_trackBundle.trdPayload.size;
	SlipRaceSession_playerContext.trackDataOffset = SlipRaceSession_trackBundle.trdBaseToken;
	SlipRaceSession_playerContext.trkBase = SlipRaceSession_trackBundle.trkPayload.data;
	SlipRaceSession_playerContext.trkBytes = SlipRaceSession_trackBundle.trkPayload.size;
	SlipRaceSession_playerContext.componentBase = SlipRaceSession_trackBundle.trcPayload.data;
	SlipRaceSession_playerContext.componentBaseBytes = SlipRaceSession_trackBundle.trcPayload.size;
	SlipRaceSession_playerContext.componentBaseOffset = SlipRaceSession_trackBundle.trcBaseToken;
	SlipRaceSession_playerContext.trackTable = SlipRaceSession_cellTableHost;
	SlipRaceSession_playerContext.trackTableBytes = (SLIP_TRACK_WORLD_CELL_TABLE_BYTES);
	SlipRaceSession_playerContext.articSlotPool = SlipRaceSession_articPool.allocation;
	SlipRaceSession_playerContext.articSlotPoolBytes = SlipRaceSession_articPool.allocationBytes;
	SlipRaceSession_playerContext.articSlotPoolOffset = SlipRaceSession_articPool.allocationAddress;
	SlipRaceSession_playerContext.articData = SlipRaceSession_racerArtPayload[tuningIndex - 1u].data;
	SlipRaceSession_playerContext.articDataBytes = SlipRaceSession_racerArtPayload[tuningIndex - 1u].size;
	SlipRaceSession_playerContext.articDataOffset = SlipRaceSession_racerArtPayload[0].address;
	SlipRaceSession_playerContext.soundEffects = &SlipRaceSession_soundEffects;
	SlipRaceSession_playerContext.materialTable = SlipRaceSession_materialTable;
	SlipRaceSession_playerContext.trackStateRecords = SlipTrackWorld_doors;
	SlipRaceSession_playerContext.trackStateRecordCount = SlipTrackWorld_doorCount;
	SlipRaceSession_playerContext.materialTableBytes = SlipRaceSession_materialTableBytes;

	SlipRaceSession_playerContext.transitionFrames = &SlipRaceSession_hudState.playerOneConsoleRedrawFrames;
	SlipRaceSession_playerContext.transitionDuration = &SlipRaceSession_hudState.playerTwoConsoleRedrawFrames;
	SlipRaceSession_playerContext.primaryViewShake = &SlipRaceSession_cameraState.shake[0];
	SlipRaceSession_playerContext.secondaryViewShake = &SlipRaceSession_cameraState.shake[1];
	SlipRacePlayer_BindHostContext(&SlipRaceSession_playerContext);
	if (!SlipRace_CreatePlayer(0u, &racerTable->records[0], SlipRaceSession_racerArtHandles[tuningIndex - 1u],
	                           &SlipRaceSession_playerContext, SlipRaceSession_playerContext.articData,
	                           SlipRaceSession_playerContext.articDataBytes, &SlipRaceSession_articPool,
	                           TrackView_FindNamedResource, &SlipRaceSession_resourceRegistry, 1u,
	                           SlipRaceSession_slotDrawHost, (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u),
	                           slotDraw.baseAddress, slotDraw.ring.baseAddress, slotList.freeListAddress,
	                           &createPlayer)) {
		SlipRuntime_Fatal("Could not initialize intro player.");
	}
	if (createPlayer.creationFailed) {
		SlipRuntime_Fatal("Could not initialize intro player.");
	}

	SlipRaceSession_playerContext.articData = SlipRaceSession_articPool.allocation;
	SlipRaceSession_playerContext.articDataBytes = SlipRaceSession_articPool.allocationBytes;
	SlipRaceSession_playerContext.articDataOffset = SlipRaceSession_articPool.allocationAddress;

	SlipRacePlayer_playerOneObject = createPlayer.objectOffset;
	SlipRacePlayer_playerTwoObject = 0;
	SlipRacePlayer_thirdObject = 0;
	SlipObject_DispatchEvent(SlipRacePlayer_playerOneObject, SLIP_OBJECT_EVENT_SET_CONTROLLER, 2, 0, 0, 0, 0);
	SlipObject_DispatchEvent(SlipRacePlayer_playerOneObject, SLIP_OBJECT_EVENT_BIND_RACER, 0x58071, 0, 0, 0, 0);
	SlipRaceSession_playerReady = true;

	SlipRaceSession_InitializeEffects();

	trackLifecycle =
	    (TrackViewTrackLifecycleArgs){&SlipRaceSession_cloudState, archives, archiveCount, &SlipRaceSession_maths};
	SlipRaceHud_Shutdown(&SlipRaceSession_hudAssets, SlipRaceSession_cleanupCallback, &trackLifecycle);
	SlipRaceSession_frameCallback = g_trackViewFrameCallbacks[axTrack - 1u];
	if (!SlipRaceCamera_LoadPositions(&SlipRaceSession_cameraState, archives, archiveCount, axTrack))
		SlipRuntime_Fatal("Could not initialize intro world resources.");
	SlipRaceSession_cleanupCallback = g_trackViewCleanupCallbacks[axTrack - 1u];
	if (!g_trackViewInitCallbacks[axTrack - 1u](&trackLifecycle)) {
		SlipRuntime_Fatal("Could not initialize intro world resources.");
	}
	if (!SlipRaceTrack_MaterialGlobals(SlipRaceSession_materialTable, SlipRaceSession_materialTableBytes,
	                                   SlipRaceSession_materialGlobal, &SlipRaceSession_materialGlobals)) {
		SlipRuntime_Fatal("Could not initialize intro world resources.");
	}

	SlipRaceCamera_Reset(&SlipRaceSession_cameraState);
	SlipRace_playerOneFinished = 0;
	SlipRace_playerTwoFinished = 0;
	SlipRaceSession_hudAssets.flags = SLIP_RACE_HUD_INTRO_PRESENTATION;
	SlipRaceSession_hudAssets.active = true;

	SlipRaceCamera_Event(&SlipRaceSession_cameraState, 0x3e, SlipRaceSession_ActivateCamera, NULL);
	if (!SlipRaceHud_LoadTimeFont(&SlipRaceSession_hudAssets, archives, archiveCount) ||
	    !SlipRaceHud_LoadNormalSight(&SlipRaceSession_hudAssets, archives, archiveCount) ||
	    !SlipRaceHud_LoadConsoleSprites(&SlipRaceSession_hudAssets, archives, archiveCount, selectedDriver - 1,
	                                    SlipRace_gameMode))
		SlipGame_ResourceFailure();
	SlipRaceHud_ResetConsole(&SlipRaceSession_hudState);
	SlipTrackWorld_UpdateSlots(
	    1u, SlipRaceSession_slotDrawHost, (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u),
	    SlipRaceSession_slotDrawCallbacks,
	    sizeof(SlipRaceSession_slotDrawCallbacks) / sizeof(SlipRaceSession_slotDrawCallbacks[0]),
	    SlipRaceSession_slotDrawBaseAddress, SlipRaceSession_slotDrawFreeHeadAddress,
	    (uint8_t *)(void *)SlipRaceSession_trackSlots, ((0x40u + 8u + 2u) * sizeof(SlipTrackSlotRecord)),
	    SlipRaceSession_playerContext.slotListBaseOffset, SlipRaceSession_playerContext.slotListSentinelOffset,
	    SlipRaceSession_trackBundle.trdPayload.data, SlipRaceSession_trackBundle.trdPayload.size,
	    SlipRaceSession_trackBundle.trdBaseToken, SlipRaceSession_trackBundle.trcPayload.data,
	    SlipRaceSession_trackBundle.trcPayload.size, SlipRaceSession_trackBundle.trcBaseToken,
	    SlipRaceSession_cellTableHost, (SLIP_TRACK_WORLD_CELL_TABLE_BYTES), SlipRaceSession_objectTableHost,
	    SLIP_OBJECT_TABLE_DOS_BYTES);

	SlipRacePlayer_startCountdown = 1;
	SlipRaceSession_countdownTimer = countdown;
	SlipRacePlayer_positionBoostTimer = 0;
	if (SlipRaceSession_gameSound != NULL &&
	    !SlipSoundEffects_Initialize(&SlipRaceSession_soundEffects, archives, archiveCount, axTrack, language,
	                                 SlipRaceSession_gameSound, SlipRaceSession_SoundObjectPosition,
	                                 SlipRaceSession_SoundTrackLight, SlipRaceSession_lockSound,
	                                 SlipRaceSession_unlockSound, SlipRaceSession_soundContext))
		SlipRuntime_Fatal("Could not initialize intro sound.");
	if (!SlipRaceIntro_PreloadResource(scriptResource, language, resources))
		SlipGame_ResourceFailure();
	SlipSoundEffects_EnableEngineLoops(&SlipRaceSession_soundEffects);

	SlipMaterial_SetLimits(&SlipMaterialHost_residency,
	                       SlipResource_GetUsage().availableBytes - SLIP_INTRO_TEXTURE_MEMORY_RESERVE,
	                       SLIP_RACE_MAXIMUM_TEXTURE_BYTES, SLIP_RACE_MAXIMUM_TEXTURE_FRAME);
	SlipRaceSession_SetEnvironmentDetailResident(environmentDetail);

	SlipRacePlayer_ResetMinimumPosition();
	SlipFrameTimer_Reset();
	for (;;) {
		SlipFrameTimer_Update((uint32_t)SlipSdl_TicksMs());
		if (!SlipMenu_PollInput())
			break;
		SlipRaceSession_DrawView(1u, windowSize, inputHeld);

		if (SlipConfig_TrackMapEnabled() != 0 && SlipRacePlayer_track > 0 && SlipRacePlayer_track < 11u) {
			const uint32_t center = SlipRaceSession_mapCenters[SlipRacePlayer_track];
			const uint16_t rivalObject = SlipRacePlayer_playerTwoObject;

			SlipRaceMap_Draw(
			    SlipRaceSession_mapScales[SlipRacePlayer_track], 0xfcu, 0xffu, 0xfbu, SlipRacePlayer_playerOneObject,
			    rivalObject, (int16_t)(center & 0xffffu), (int16_t)(center >> 16), 0xfeu, 0xffu, &SlipRaceSession_maths,
			    &SlipRendererHost_state.projection, SlipRaceSession_objectTableHost, SLIP_OBJECT_TABLE_DOS_BYTES,
			    SlipRaceSession_trackBundle.trkPayload.data, SlipRaceSession_trackBundle.trkPayload.size,
			    SlipRaceSession_trackBundle.trdPayload.data, SlipRaceSession_trackBundle.trdPayload.size,
			    SlipRaceSession_trackSlots,
			    ((0x40u + 8u + 2u) * sizeof(SlipTrackSlotRecord)) / sizeof(SlipRaceSession_trackSlots[0]),
			    SlipRaceSession_playerContext.slotListBaseOffset, SlipRaceSession_playerContext.slotListSentinelOffset);
		}

		bool redrawCaption = SlipRaceSession_hudState.playerOneConsoleRedrawFrames != 0;
		SlipRaceHud_DrawLowerConsole(&SlipRaceSession_hudState, &SlipRaceSession_hudAssets, g_framebuffer, 320, 1u);
		if (redrawCaption && SlipRace_playerOneFinished == 0 && state.caption != 0) {
			const char *const caption = SlipStringTable_Get(strings, state.caption, &introStringResources);
			SlipRaceSession_DrawCaption(caption);
			SlipStringTable_Unlock(strings, &introStringResources);
		}
		SlipRaceHud_DrawBorders(&SlipRaceSession_hudState, SlipRace_gameMode, windowSize,
		                        SlipRaceSession_hudAssets.flags);
		SlipMenu_PresentFrame();
		SlipRaceCamera_PollKeys(&SlipRaceSession_cameraState, SlipRace_gameMode, inputPressed,
		                        SlipRaceSession_ActivateCamera, NULL);

		if (SlipRace_gameMode != 1) {
			if (inputPressed[0x40]) {
				inputPressed[0x40] = false;
				SlipConfig_ToggleRearMonitor();
			}
			if (inputPressed[0x41]) {
				inputPressed[0x41] = false;
				SlipConfig_ToggleWeaponsMonitor();
			}
		}
		SlipRaceSession_TickCameraTimers();
		SlipRace_UpdateGlobals();
		SlipRacePlayer_UpdateMinimumPosition(racerTable->records, racerTable->racerCount);
		SlipObject_DispatchUpdate(0, 0);
		SlipRacePhysics_RunFrame();
		SlipRace_UpdateTimedEffects();
		SlipRace_UpdateTrackFrameState(&SlipRaceSession_playerContext);
		SlipTrackWorld_UpdateSlots(
		    1u, SlipRaceSession_slotDrawHost, (SLIP_TRACK_SLOT_DRAW_RECORD_COUNT * 0x38u),
		    SlipRaceSession_slotDrawCallbacks,
		    sizeof(SlipRaceSession_slotDrawCallbacks) / sizeof(SlipRaceSession_slotDrawCallbacks[0]),
		    SlipRaceSession_slotDrawBaseAddress, SlipRaceSession_slotDrawFreeHeadAddress,
		    (uint8_t *)(void *)SlipRaceSession_trackSlots, ((0x40u + 8u + 2u) * sizeof(SlipTrackSlotRecord)),
		    SlipRaceSession_playerContext.slotListBaseOffset, SlipRaceSession_playerContext.slotListSentinelOffset,
		    SlipRaceSession_trackBundle.trdPayload.data, SlipRaceSession_trackBundle.trdPayload.size,
		    SlipRaceSession_trackBundle.trdBaseToken, SlipRaceSession_trackBundle.trcPayload.data,
		    SlipRaceSession_trackBundle.trcPayload.size, SlipRaceSession_trackBundle.trcBaseToken,
		    SlipRaceSession_cellTableHost, (SLIP_TRACK_WORLD_CELL_TABLE_BYTES), SlipRaceSession_objectTableHost,
		    SLIP_OBJECT_TABLE_DOS_BYTES);

		SlipObject_DispatchPostUpdate(0, 0, 0, 0, 0, 0);
		const uint16_t delta = (uint16_t)SlipFrameTimer_Values().deltaMilliseconds;
		if (SlipRacePlayer_startCountdown != 0) {
			SlipRaceSession_countdownTimer = (uint16_t)(SlipRaceSession_countdownTimer - delta);
			if ((int16_t)SlipRaceSession_countdownTimer < 0) {
				SlipRaceSession_countdownTimer = (uint16_t)(SlipRaceSession_countdownTimer + 1000);
				--SlipRacePlayer_startCountdown;
			}
		}
		const SlipRaceIntroScriptResult result = SlipRaceIntro_Step(&state, script.data, script.size, delta, &host);
		if (result == SLIP_RACE_INTRO_SCRIPT_INVALID)
			SlipRuntime_Fatal("Invalid intro command.");
		if (result == SLIP_RACE_INTRO_SCRIPT_FINISHED)
			break;
		SlipSoundEffects_EndFrame(&SlipRaceSession_soundEffects, 1, 0, 0, 0, SlipRacePlayer_playerOneObject, 0);
		SlipSoundEffects_BeginFrame(&SlipRaceSession_soundEffects);
		if (inputPressed[1]) {
			inputPressed[1] = false;
			break;
		}
		if (inputPressed[0x1c]) {
			inputPressed[0x1c] = false;
			break;
		}
		if (inputPressed[0x80]) {
			inputPressed[0x80] = false;
			break;
		}
	}

	SlipRaceSession_ShutdownEffects();
	SlipRaceHud_Shutdown(&SlipRaceSession_hudAssets, SlipRaceSession_cleanupCallback, &trackLifecycle);
	if (state.voice != 0 && SlipRaceSession_gameSound != NULL &&
	    (SlipRaceSession_lockSound == NULL || SlipRaceSession_lockSound(SlipRaceSession_soundContext))) {
		SlipGameSound_Stop(SlipRaceSession_gameSound, state.voice);
		if (SlipRaceSession_unlockSound != NULL)
			SlipRaceSession_unlockSound(SlipRaceSession_soundContext);
	}
	SlipSoundEffects_Shutdown(&SlipRaceSession_soundEffects);
	SlipRaceIntro_ReleaseResource(scriptResource, resources);
	SlipResourceHost_Unlock(NULL, scriptResource);
	SlipResourceHost_Release(NULL, scriptResource);
	SlipStringTable_Release(strings, &introStringResources);

	SlipActor_ReleaseResources(SlipRaceSession_racerArtHandles[0], &SlipActorHost_resourceCalls);
	SlipResourceHost_Release(NULL, SlipRaceSession_racerArtHandles[0]);
	SlipResource_ReleaseHandle(&SlipRaceSession_racerArtPayload[0]);

	SlipRaceSession_ShutdownWorld();
	SlipRace_flybyChaseEnabled = 0;
	SlipRacePlayer_flybyMode = 0;
	SlipMenuMusic_ScriptedStop();
}
