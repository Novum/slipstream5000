#ifndef SLIPSTREAM5000_VEHICLE_VIEWER_H
#define SLIPSTREAM5000_VEHICLE_VIEWER_H
#include "input.h"
#include "track_world.h"
#include "vehicle_select.h"
#include "vehicle_view_animation.h"

/* Fixed world position shared by the live viewer and the actor preview. */
enum { SLIP_VIEWER_OBJECT_X = 0x374986, SLIP_VIEWER_OBJECT_Y = 0x603e1, SLIP_VIEWER_OBJECT_Z = 0x473546 };

typedef struct SlipVehicleViewParameters {
	uint32_t distance;
	int16_t pitch, centerOffset;
} SlipVehicleViewParameters;

extern const SlipVehicleViewParameters SlipVehicleView_parameters[SLIP_RACE_RACER_COUNT];

typedef struct SlipVehicleViewer {
	uint32_t vehicle, descriptionTag;
	const SlipVehicleViewParameters *parameters;
	SlipStringTableSlot *strings;
	uint16_t font, overlay, actorResource, object;
	uint16_t yaw;
	int16_t cameraStep;
	SlipView3DMatrix cameraMatrix;
	SlipVehicleViewAnimation animation;
} SlipVehicleViewer;

typedef struct SlipVehicleViewerCalls {
	void *context;
	SlipStringTableResources resources;
	SlipVehicleSelectionErrors errors;
	const SlipView3DMatrix *objectTemplate;
	SlipObjectDrawCallback drawActor;
	SlipObjectEventCallback objectEvent;
	void (*language)(void *);
	void (*objects)(void *, uint32_t);
	void (*drawList)(void *, uint32_t);
	void (*renderer)(void *, uint32_t, uint32_t);
	void (*shapes)(void *);
	void (*actors)(void *, uint32_t);
	void (*minimumDepth)(void *, uint32_t);
	void (*maximumDepth)(void *, uint32_t);
	void (*resetLighting)(void *);
	void (*disableDepthFade)(void *, uint32_t);
	void (*setRenderFlags)(void *, uint32_t);
	void (*actorMode)(void *, uint32_t);
	void (*palette)(void *, uint16_t);
	void (*materials)(void *, const uint8_t *);
	void (*residency)(void *);
	void (*light)(void *, int16_t, int16_t, int16_t, int16_t);
	void (*ambientLight)(void *, uint32_t);
	void (*voiceSetup)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
	void (*prepareActor)(void *, uint16_t);
	bool (*createObject)(void *, const SlipView3DMatrix *, uint32_t, uint32_t, uint32_t, SlipObjectDrawCallback,
	                     uint32_t, SlipObjectEventCallback, uint16_t *);
	void (*fatal)(void *, const char *);
	void (*attachActor)(void *, uint16_t object, uint16_t resource);
	void (*viewport)(void *, int16_t, int16_t, int16_t, int16_t, int16_t, int16_t);
	void (*clip)(void *, int16_t, int16_t, int16_t, int16_t);
	void (*voice)(void *, uint32_t);
	void (*resetTimer)(void *);
	void (*updateTimer)(void *);
	void (*cameraMatrix)(void *, SlipVehicleViewer *);
	void (*animate)(void *, SlipVehicleViewAnimation *, uint16_t);
	void (*updateObjects)(void *);
	void (*cameraPosition)(void *, uint16_t);
	void (*begin)(void *);
	void (*selectObject)(void *, uint16_t);
	SlipView3DVec32 (*objectPosition)(void *, uint16_t);
	void (*setCameraPosition)(void *, SlipView3DVec32);
	void (*drawClippedSprite)(void *, uint16_t, int16_t);
	void (*drawObjects)(void *);
	void (*font)(void *, uint16_t);
	void (*style)(void *, uint16_t, uint16_t, int16_t, int16_t);
	void (*setTextColor)(void *, uint16_t);
	void (*text)(void *, const char *, int16_t x, uint32_t top, uint32_t bottom);
	void (*present)(void *);
	void (*poll)(void *);
	bool (*pressed)(void *, SlipInputCode);
	void (*closeObjects)(void *);
	void (*closeActors)(void *);
	void (*closeShapes)(void *);
	void (*closeRenderer)(void *);
	void (*closeDrawList)(void *);
	void (*releaseActor)(void *, uint16_t);
	void (*closeVoice)(void *);
} SlipVehicleViewerCalls;

void SlipVehicleViewer_Run(SlipVehicleViewer *, uint32_t vehicle, SlipStringTableState *,
                           const SlipVehicleViewerCalls *);
#endif
