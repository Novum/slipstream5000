#include "vehicle_viewer.h"
#include "fixed_point.h"
#include "race_voice.h"
#include "sprite_format.h"
#include "string_tags.h"
#include "text_layout.h"

enum {
	SLIP_VIEWER_MATERIAL_NAME_VEHICLE_INDEX = 4,
	SLIP_VIEWER_OVERLAY_NAME_VEHICLE_INDEX = 7,
	SLIP_VIEWER_ACTOR_NAME_VEHICLE_INDEX = 5,
	SLIP_VIEWER_OBJECT_CAPACITY = 5,
	SLIP_VIEWER_VERTEX_CAPACITY = 512,
	SLIP_VIEWER_ACTOR_CAPACITY = 2,
	SLIP_VIEWER_NEAR_DEPTH = 12,
	SLIP_VIEWER_LIGHT_DIAGONAL_Q14 = -11594,
	SLIP_VIEWER_AMBIENT_LIGHT_Q14 = 3 * SLIP_Q14_ONE / 16,
	SLIP_VIEWER_VOICE_BUFFER_BYTES = 0x40000,
	SLIP_VIEWER_LEFT = 31,
	SLIP_VIEWER_TOP = 14,
	SLIP_VIEWER_RIGHT = 287,
	SLIP_VIEWER_BOTTOM = 183,
	SLIP_VIEWER_CENTER_X = 159,
	SLIP_VIEWER_CENTER_Y = 98,
	SLIP_VIEWER_TEXT_LEFT = 33,
	SLIP_VIEWER_TEXT_RIGHT = 285,
	SLIP_VIEWER_TEXT_TOP = 130,
	SLIP_VIEWER_TEXT_BOTTOM = 179
};

const SlipVehicleViewParameters SlipVehicleView_parameters[SLIP_RACE_RACER_COUNT] = {
    {14640, -4096, -17}, {14152, -4096, -17}, {15128, -4096, -17}, {13664, -4096, -17}, {15616, -4096, -17},
    {13176, -4096, -17}, {17080, -3072, -18}, {15616, -4096, -20}, {17568, -4096, -17}, {12200, -4096, -17}};

void SlipVehicleViewer_Run(SlipVehicleViewer *viewer, uint32_t vehicle, SlipStringTableState *strings,
                           const SlipVehicleViewerCalls *calls) {
	void *const context = calls->context;
	const SlipStringTableResources *const resources = &calls->resources;
	char materialName[] = "VIEW0.MAT", overlayName[] = "VIEWCAR0.SPR", actorName[] = "RACER0.ART";
	viewer->vehicle = vehicle;
	const char digit = (char)(uint8_t)(vehicle + ('0' - 1));
	materialName[SLIP_VIEWER_MATERIAL_NAME_VEHICLE_INDEX] = overlayName[SLIP_VIEWER_OVERLAY_NAME_VEHICLE_INDEX] =
	    actorName[SLIP_VIEWER_ACTOR_NAME_VEHICLE_INDEX] = digit;
	viewer->descriptionTag = SLIP_STRING_VEHICLE_DESCRIPTION_PREFIX | (uint8_t)digit;
	viewer->parameters = &SlipVehicleView_parameters[vehicle - 1];
	calls->language(context);
	if (!SlipStringTable_Load(strings, "VIEWCAR ", resources, &viewer->strings))
		calls->errors.resourceError(calls->errors.context);
	if (!resources->load(resources->context, "VIEWDESC.FNT", &viewer->font))
		calls->errors.resourceError(calls->errors.context);
	calls->objects(context, SLIP_VIEWER_OBJECT_CAPACITY);
	calls->drawList(context, SLIP_VIEWER_OBJECT_CAPACITY);
	calls->renderer(context, SLIP_VIEWER_VERTEX_CAPACITY, 0);
	calls->shapes(context);
	calls->actors(context, SLIP_VIEWER_ACTOR_CAPACITY);
	calls->minimumDepth(context, SLIP_VIEWER_NEAR_DEPTH);
	calls->maximumDepth(context, INT32_MAX);
	calls->resetLighting(context);
	calls->maximumDepth(context, INT32_MAX);
	calls->disableDepthFade(context, 0);
	calls->setRenderFlags(context, 0);
	calls->actorMode(context, 0);
	if (!resources->load(resources->context, overlayName, &viewer->overlay))
		calls->errors.fatalError(calls->errors.context);
	calls->palette(context, viewer->overlay);
	uint16_t materialResource;
	if (!resources->load(resources->context, materialName, &materialResource))
		calls->errors.fatalError(calls->errors.context);
	const uint8_t *const materialFile = resources->lock(resources->context, materialResource);
	calls->materials(context, materialFile);
	calls->residency(context);
	resources->unlock(resources->context, materialResource);
	resources->release(resources->context, materialResource);
	calls->light(context, SLIP_VIEWER_LIGHT_DIAGONAL_Q14, SLIP_VIEWER_LIGHT_DIAGONAL_Q14, 0, SLIP_Q14_ONE);
	calls->ambientLight(context, SLIP_VIEWER_AMBIENT_LIGHT_Q14);
	calls->voiceSetup(context, SLIP_RACE_VOICE_BANK_ALTERNATE, 1, 0, SLIP_VIEWER_VOICE_BUFFER_BYTES);
	if (!resources->load(resources->context, actorName, &viewer->actorResource))
		calls->errors.fatalError(calls->errors.context);
	calls->prepareActor(context, viewer->actorResource);
	uint16_t object;
	if (!calls->createObject(context, calls->objectTemplate, SLIP_VIEWER_OBJECT_X, SLIP_VIEWER_OBJECT_Y,
	                         SLIP_VIEWER_OBJECT_Z, calls->drawActor, 0, calls->objectEvent, &object))
		calls->fatal(context, "DoViewCar: Error.");
	calls->attachActor(context, object, viewer->actorResource);
	viewer->object = object;
	calls->viewport(context, SLIP_VIEWER_LEFT, SLIP_VIEWER_TOP, SLIP_VIEWER_RIGHT, SLIP_VIEWER_BOTTOM,
	                SLIP_VIEWER_CENTER_X, (int16_t)(SLIP_VIEWER_CENTER_Y + viewer->parameters->centerOffset));
	calls->clip(context, SLIP_VIEWER_LEFT, SLIP_VIEWER_TOP, SLIP_VIEWER_RIGHT, SLIP_VIEWER_BOTTOM);
	viewer->yaw = 0;
	viewer->animation.jetAngle = 0;
	viewer->animation.jetDirection = 0;

	calls->voice(context, vehicle - 1);
	calls->resetTimer(context);
	bool done;
	do {
		calls->updateTimer(context);
		calls->cameraMatrix(context, viewer);
		calls->animate(context, &viewer->animation, viewer->object);
		calls->updateObjects(context);
		calls->cameraPosition(context, viewer->object);
		calls->begin(context);
		calls->selectObject(context, 0);
		SlipView3DVec32 position = calls->objectPosition(context, 0);
		calls->setCameraPosition(context, position);
		calls->drawClippedSprite(context, viewer->overlay, SLIP_SPRITE_USE_STORED_POSITION);
		calls->drawObjects(context);
		calls->font(context, viewer->font);
		calls->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_VIEWER_TEXT_LEFT, SLIP_VIEWER_TEXT_RIGHT);
		calls->setTextColor(context, UINT16_MAX);
		const char *const description = SlipStringTable_Get(viewer->strings, viewer->descriptionTag, resources);
		calls->text(context, description, SLIP_VIEWER_TEXT_LEFT, SLIP_VIEWER_TEXT_TOP, SLIP_VIEWER_TEXT_BOTTOM);
		SlipStringTable_Unlock(viewer->strings, resources);
		calls->present(context);
		calls->poll(context);
		done = calls->pressed(context, SLIP_INPUT_SCAN_ENTER);
		if (!done)
			done = calls->pressed(context, SLIP_INPUT_MOUSE_LEFT);
		if (!done)
			done = calls->pressed(context, SLIP_INPUT_SCAN_ESCAPE);
	} while (!done);
	calls->closeObjects(context);
	calls->closeActors(context);
	calls->closeShapes(context);
	calls->closeRenderer(context);
	calls->closeDrawList(context);
	calls->releaseActor(context, viewer->actorResource);
	resources->release(resources->context, viewer->actorResource);
	calls->closeVoice(context);
	resources->release(resources->context, viewer->overlay);
	SlipStringTable_Release(viewer->strings, resources);
	resources->release(resources->context, viewer->font);
}
