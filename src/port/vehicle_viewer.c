#include "vehicle_viewer.h"

const SlipVehicleViewParameters SlipVehicleView_parameters[10] = {
    {14640, -4096, -17}, {14152, -4096, -17}, {15128, -4096, -17}, {13664, -4096, -17}, {15616, -4096, -17},
    {13176, -4096, -17}, {17080, -3072, -18}, {15616, -4096, -20}, {17568, -4096, -17}, {12200, -4096, -17}};

void SlipVehicleViewer_Run(SlipVehicleViewer *viewer, uint32_t vehicle, SlipStringTableState *strings,
                           const SlipVehicleViewerCalls *calls) {
	void *const context = calls->context;
	const SlipStringTableResources *const resources = &calls->resources;
	char materialName[] = "VIEW0.MAT", overlayName[] = "VIEWCAR0.SPR", actorName[] = "RACER0.ART";
	viewer->vehicle = vehicle;
	const char digit = (char)(uint8_t)(vehicle + 0x2f);
	materialName[4] = overlayName[7] = actorName[5] = digit;
	viewer->descriptionTag = 0x43415200u | (uint8_t)digit;
	viewer->parameters = &SlipVehicleView_parameters[vehicle - 1];
	calls->language(context);
	if (!SlipStringTable_Load(strings, "VIEWCAR ", resources, &viewer->strings))
		calls->errors.resourceError(calls->errors.context);
	if (!resources->load(resources->context, "VIEWDESC.FNT", &viewer->font))
		calls->errors.resourceError(calls->errors.context);
	calls->objects(context, 5);
	calls->drawList(context, 5);
	calls->renderer(context, 512, 0);
	calls->shapes(context);
	calls->actors(context, 2);
	calls->minimumDepth(context, 12);
	calls->maximumDepth(context, 0x7fffffff);
	calls->resetLighting(context);
	calls->maximumDepth(context, 0x7fffffff);
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
	calls->light(context, (int16_t)0xd2b6, (int16_t)0xd2b6, 0, 0x4000);
	calls->ambientLight(context, 0xc00);
	calls->voiceSetup(context, 2, 1, 0, 0x40000);
	if (!resources->load(resources->context, actorName, &viewer->actorResource))
		calls->errors.fatalError(calls->errors.context);
	calls->prepareActor(context, viewer->actorResource);
	uint16_t object;
	if (!calls->createObject(context, calls->objectTemplate, 0x374986, 0x603e1, 0x473546, calls->drawActor, 0,
	                         calls->objectEvent, &object))
		calls->fatal(context, "DoViewCar: Error.");
	calls->attachActor(context, object, viewer->actorResource);
	viewer->object = object;
	calls->viewport(context, 31, 14, 287, 183, 159, (int16_t)(98 + viewer->parameters->centerOffset));
	calls->clip(context, 31, 14, 287, 183);
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
		calls->drawClippedSprite(context, viewer->overlay, 0x7fff);
		calls->drawObjects(context);
		calls->font(context, viewer->font);
		calls->style(context, 2, 0xffff, 33, 285);
		calls->setTextColor(context, 0xffff);
		const char *const description = SlipStringTable_Get(viewer->strings, viewer->descriptionTag, resources);
		calls->text(context, description, 33, 130, 179);
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
