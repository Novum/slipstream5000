#include "vehicle_viewer_host.h"
#include "actor_host.h"
#include "actor_object_host.h"
#include "actor_resources.h"
#include "config_menu_host.h"
#include "draw_list_host.h"
#include "fixed_point.h"
#include "frame_timer.h"
#include "game_errors.h"
#include "material_host.h"
#include "maths_host.h"
#include "menu.h"
#include "port_app_bridge.h"
#include "race_records_host.h"
#include "race_voice_host.h"
#include "renderer_host.h"
#include "renderer_state.h"
#include "resource_host.h"
#include "shape_host.h"
#include "text_layout.h"
#include "vehicle_view_camera_host.h"
#include <stdlib.h>

enum { VIEWER_OBJECT_CAPACITY = 5 };

static SlipObject objects[VIEWER_OBJECT_CAPACITY];
static SlipVehicleViewer viewer;
static SlipVehicleViewCameraCalls cameraCalls;
static SlipActorRenderState actorRender;
static SlipView3DMatrix cameraMatrix;
static SlipGameSoundState *viewerSound;

static void Objects(void *context, uint32_t count) {
	(void)context;
	SlipObjectInitTable result;
	if (!SlipObject_InitTableFresh(objects, sizeof(objects) / sizeof(objects[0]) * SLIP_OBJECT_DOS_STRIDE,
	                               (uint16_t)count, &result))
		SlipGame_MemoryFailure();
}

static void DrawList(void *context, uint32_t count) {
	(void)context;
	SlipDrawListHost_Initialize((uint16_t)count);
}

static void Actors(void *context, uint32_t count) {
	(void)context;
	if (!SlipActorPool_Initialize(&SlipActorHost_pool, (uint16_t)count, &SlipActorHost_poolCalls))
		SlipGame_MemoryFailure();
}

static void ActorMode(void *context, uint32_t mode) {
	(void)context;
	SlipActorPool_SetMode(&SlipActorHost_pool, mode);
}

static void Residency(void *context) {
	(void)context;
	SlipMaterial_MakeResident(&SlipMaterialHost_residency, &SlipMaterialHost_residencyCalls);
}

static void Light(void *context, int16_t x, int16_t y, int16_t z, int16_t strength) {
	(void)context;
	SlipDraw3D_SetLightVector(x, y, z, (uint16_t)strength);
}

static void Ambient(void *context, uint32_t strength) {
	(void)context;
	SlipDraw3D_SetAmbientLight((uint16_t)strength);
}

static void VoiceSetup(void *context, uint32_t bank, uint32_t onDemand, uint32_t recent, uint32_t reserve) {
	(void)context;
	SlipRaceVoiceCalls calls = SlipRaceVoiceHost_Calls(viewerSound);
	(void)SlipRaceVoice_Setup(viewerSound->digitalCard, bank, onDemand, recent, reserve, &calls);
}

static void Voice(void *context, uint32_t selection) {
	(void)context;
	SlipRaceVoiceCalls calls = SlipRaceVoiceHost_Calls(viewerSound);
	SlipRaceVoice_Play(viewerSound->digitalCard, selection, &calls);
}

static void PrepareActor(void *context, uint16_t resource) {
	(void)context;
	SlipActor_PreloadResources(resource, &SlipActorHost_resourceCalls);
}

static bool CreateObject(void *context, const SlipView3DMatrix *matrix, uint32_t x, uint32_t y, uint32_t z,
                         SlipObjectDrawCallback draw, uint32_t data, SlipObjectEventCallback event, uint16_t *object) {
	(void)context;
	SlipObjectSlotFill result = {0};
	bool ok = SlipObject_SlotFill(matrix, x, y, z, draw, data, event, &result);
	*object = (uint16_t)result.objectOffset;
	return ok && !result.carryOut;
}

static void Fatal(void *context, const char *message) {
	(void)context;
	SlipRuntime_Fatal(message);
}

static void AttachActor(void *context, uint16_t object, uint16_t resource) {
	(void)context;
	if (!SlipActor_Create(&SlipActorHost_pool, &SlipActorHost_construction, object, resource,
	                      &SlipActorHost_constructionCalls))
		SlipGame_MemoryFailure();
}

static void Viewport(void *context, int16_t left, int16_t top, int16_t right, int16_t bottom, int16_t x, int16_t y) {
	(void)context;
	SlipDraw3D_SetViewport(&SlipRendererHost_state.projection, left, top, right, bottom, x, y);
}

static void Clip(void *context, int16_t left, int16_t top, int16_t right, int16_t bottom) {
	(void)context;
	Raster_SetClipRect(left, top, right, bottom);
}

static void ResetTimer(void *context) {
	(void)context;
	SlipFrameTimer_Reset();
}

static void UpdateTimer(void *context) {
	(void)context;
	SlipFrameTimer_Update((uint32_t)SlipSdl_TicksMs());
}

static void CameraMatrix(void *context, SlipVehicleViewer *state) {
	(void)context;
	SlipVehicleView_CameraMatrix(state, &cameraCalls);
}

static bool GetAngle(void *context, uint16_t object, uint32_t tag, uint16_t *angle) {
	(void)context;
	return SlipActor_GetAngle(object, tag, angle, &SlipActorObject_access);
}

static void SetAngle(void *context, uint16_t object, uint32_t tag, uint16_t angle) {
	(void)context;
	SlipActor_SetAngle(object, tag, angle, &SlipActorObject_access);
}

static void Animate(void *context, SlipVehicleViewAnimation *state, uint16_t object) {
	const SlipVehicleViewAnimationCalls calls = {context, GetAngle, SetAngle};
	SlipVehicleView_Animate(state, object, &calls);
}

static void UpdateObjects(void *context) {
	(void)context;
	SlipObject_DispatchUpdate(0, 0);
}

static uint32_t ObjectEvent(uint32_t event, uint32_t payload, uint32_t value, uint32_t flags, uint16_t object,
                            uintptr_t data, uint32_t frame) {
	(void)payload;
	(void)value;
	(void)flags;
	(void)data;
	(void)frame;
	return SlipVehicleView_ObjectEvent(event, object, &cameraCalls);
}

static void CameraPosition(void *context, uint16_t object) {
	(void)context;
	SlipVehicleView_CameraPosition(&viewer, object, &cameraCalls);
}

static void Begin(void *context) {
	(void)context;
	SlipRenderer_Begin(&SlipRendererHost_state, &SlipRendererHost_lifecycleCalls);
}

static void SelectObject(void *context, uint16_t object) {
	(void)context;
	SlipObjectMatrixCopy result;
	SlipObject_MatrixCopy(SlipObject_table, SlipObject_count * SLIP_OBJECT_DOS_STRIDE, object, &cameraMatrix, &result);
}

static void SetCameraPosition(void *context, SlipView3DVec32 position) {
	(void)context;
	SlipRenderer_SetCamera(&SlipRendererHost_state, position, &cameraMatrix);
}

static void Sprite(void *context, uint16_t resource, int16_t x) {
	SlipResourceHost_Lock(context, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	SlipSprite sprite;
	if (!SlipSprite_FromPayload(&payload, &sprite))
		SlipGame_ResourceFailure();
	SlipSprite_DrawClipped(&sprite, g_screenBufferBase, g_screenPitch, x, 0);
	SlipResourceHost_Unlock(context, resource);
}

static uint32_t ProjectionReciprocal(void *context) {
	(void)context;
	return SlipRendererHost_state.projection.inverseProjectionScale;
}

static void DrawPart(void *context, SlipActorPartRecord *part) {
	(void)context;
	SlipActorShape_DrawPart(&SlipShapeHost_state, part, &SlipShapeHost_drawCalls);
}

static bool DrawActor(struct TrackViewRawBspContext *context, uint32_t object) {
	(void)context;
	SlipActorRenderCalls calls = {.transform = SlipActorObject_transform,
	                              .viewPosition = SlipActorObject_ViewPosition,
	                              .projectionReciprocal = ProjectionReciprocal,
	                              .drawPart = DrawPart};
	SlipShapeHost_state.actor = &actorRender;
	SlipActor_Draw(&actorRender, &SlipActorHost_pool, (uint16_t)object, SlipMathsHost_Tables(), &calls);
	return true;
}

static void DrawObjects(void *context) {
	(void)context;
	SlipObject_DrawVisible(NULL);
}

static void Style(void *context, uint16_t mode, uint16_t spacing, int16_t left, int16_t right) {
	(void)context;
	SlipText_SetStyle(&SlipText_state, mode, spacing, left, right);
}

static void Text(void *context, const char *text, int16_t x, uint32_t top, uint32_t bottom) {
	(void)context;
	SlipText_DrawCentered(&SlipText_state, text, NULL, x, (int16_t)top, (int16_t)bottom);
}

static void Present(void *context) {
	(void)context;
	SlipMenu_PresentFrame();
}

static void Poll(void *context) {
	(void)context;
	if (!SlipMenu_PollInput()) {
		SlipRuntime_Shutdown();
		SDL_Quit();
		exit(0);
	}
}

static bool Pressed(void *context, SlipInputCode key) {
	(void)context;
	return SlipInput_TestAndClear(SlipInput_pressed, key);
}

static void CloseObjects(void *context) {
	(void)context;
	SlipObject_Shutdown();
}

static void CloseActors(void *context) {
	(void)context;
	SlipActorPool_Shutdown(&SlipActorHost_pool, &SlipActorHost_poolCalls);
}

static void CloseDrawList(void *context) {
	(void)context;
	SlipDrawListHost_Shutdown();
}

static void ReleaseActor(void *context, uint16_t resource) {
	(void)context;
	SlipActor_ReleaseResources(resource, &SlipActorHost_resourceCalls);
}

static void CloseVoice(void *context) {
	(void)context;
	SlipRaceVoice_Shutdown(viewerSound);
}

void SlipVehicleViewerHost_Run(uint16_t vehicle, SlipGameSoundState *sound) {
	static const SlipView3DMatrix objectTemplate = {{SLIP_Q14_ONE, 0, 0, 0, SLIP_Q14_ONE, 0, 0, 0, SLIP_Q14_ONE}};
	const SlipLapRecordsScreenCalls *base = &SlipLapRecordsHost_lifecycle;
	viewerSound = sound;
	cameraCalls = SlipVehicleViewCamera_NativeCalls(SlipMathsHost_Tables());
	SlipVehicleViewerCalls calls = {.resources = base->resources,
	                                .errors = {NULL, base->resourceFailure, base->resourceFailure},
	                                .objectTemplate = &objectTemplate,
	                                .drawActor = DrawActor,
	                                .objectEvent = ObjectEvent,
	                                .language = base->language,
	                                .objects = Objects,
	                                .drawList = DrawList,
	                                .renderer = base->renderer,
	                                .shapes = base->shapes,
	                                .actors = Actors,
	                                .minimumDepth = base->minimumDepth,
	                                .maximumDepth = base->maximumDepth,
	                                .resetLighting = base->resetLighting,
	                                .disableDepthFade = base->depthFade,
	                                .setRenderFlags = base->rendererFlags,
	                                .actorMode = ActorMode,
	                                .palette = base->palette,
	                                .materials = base->materials,
	                                .residency = Residency,
	                                .light = Light,
	                                .ambientLight = Ambient,
	                                .voiceSetup = VoiceSetup,
	                                .prepareActor = PrepareActor,
	                                .createObject = CreateObject,
	                                .fatal = Fatal,
	                                .attachActor = AttachActor,
	                                .viewport = Viewport,
	                                .clip = Clip,
	                                .voice = Voice,
	                                .resetTimer = ResetTimer,
	                                .updateTimer = UpdateTimer,
	                                .cameraMatrix = CameraMatrix,
	                                .animate = Animate,
	                                .updateObjects = UpdateObjects,
	                                .cameraPosition = CameraPosition,
	                                .begin = Begin,
	                                .selectObject = SelectObject,
	                                .objectPosition = SlipActorObject_Position,
	                                .setCameraPosition = SetCameraPosition,
	                                .drawClippedSprite = Sprite,
	                                .drawObjects = DrawObjects,
	                                .font = SlipConfigHost_calls.selectFont,
	                                .style = Style,
	                                .setTextColor = SlipConfigHost_calls.textColor,
	                                .text = Text,
	                                .present = Present,
	                                .poll = Poll,
	                                .pressed = Pressed,
	                                .closeObjects = CloseObjects,
	                                .closeActors = CloseActors,
	                                .closeShapes = base->closeShapes,
	                                .closeRenderer = base->closeRenderer,
	                                .closeDrawList = CloseDrawList,
	                                .releaseActor = ReleaseActor,
	                                .closeVoice = CloseVoice};
	SlipVehicleViewer_Run(&viewer, vehicle, &SlipStringTable_state, &calls);
}
