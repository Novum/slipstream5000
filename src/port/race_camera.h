#ifndef SLIPSTREAM_RACE_CAMERA_H
#define SLIPSTREAM_RACE_CAMERA_H

#include "artic_slot.h"
#include "draw3d.h"
#include "view3d.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct SlipRaceCameraState {

	uint16_t viewOneMode;
	uint16_t viewTwoMode;

	int32_t viewOneZoomDistance;
	int32_t viewTwoZoomDistance;

	uint16_t viewOneModeNameTimer;
	uint16_t viewTwoModeNameTimer;

	uint16_t lapNotificationTimer[2];
	uint16_t lapTimeTimer[2];
	uint32_t lapTime[2];
	uint16_t lapNotificationValue[2];
	uint16_t shake[2];
	SlipView3DMatrix chaseMatrix[2];
	SlipView3DVec32 chasePosition[2];
	int32_t tvSoundDistance;
	uint32_t tvSoundCamera;
	SlipView3DVec32 tvPositions[60];
	uint32_t tvPreviousCamera;
	SlipView3DMatrix externalMatrix;
	int32_t externalDistance;
	SlipView3DMatrix finishMatrix;
	uint16_t finishPosition;
} SlipRaceCameraState;

bool SlipRaceCamera_Finish(SlipRaceCameraState *state, SlipObject *objects, size_t objectBytes, uint16_t racerObject,
                           uint16_t racePosition);

bool SlipRaceCamera_External(SlipRaceCameraState *state, SlipObject *objects, size_t objectBytes, uint16_t racerObject,
                             const uint8_t *trd, size_t trdBytes, const uint8_t *components, size_t componentBytes,
                             const uint8_t *cells, size_t cellBytes, uint32_t trackDataAddress);

void SlipRaceCamera_ExternalControls(SlipRaceCameraState *state, uint16_t frameStep, const bool held[256],
                                     const SlipView3DMaths *maths);

bool SlipRaceCamera_SelectTv(SlipRaceCameraState *state, SlipView3DVec32 craft, const uint8_t *trd, size_t trdBytes,
                             const uint8_t *components, size_t componentBytes, const uint8_t *cells, size_t cellBytes,
                             uint32_t trackDataAddress, uint32_t *selectedCameraAddressOut, int32_t *distance);

bool SlipRaceCamera_LoadPositions(SlipRaceCameraState *state, const char *const *archives, size_t archiveCount,
                                  uint16_t track);

void SlipRaceCamera_ActivateTv(SlipRaceCameraState *state);

uint32_t SlipRaceCamera_TvScale(uint32_t distance);

bool SlipRaceCamera_TvTransform(const SlipRaceCameraState *state, SlipObject *objects, size_t objectBytes,
                                uint16_t racerObject, uint32_t selectedCameraAddress, uint32_t *distance);

bool SlipRaceCamera_ActivateChase(SlipRaceCameraState *state, const SlipObject *objects, size_t objectBytes,
                                  uint16_t racerObject, uint16_t view);

bool SlipRaceCamera_Dropped(const SlipRaceCameraState *state, SlipObject *objects, size_t objectBytes,
                            uint16_t racerObject, uint16_t view);

bool SlipRaceCamera_Chase(SlipRaceCameraState *state, SlipObject *objects, size_t objectBytes, uint16_t racerObject,
                          uint16_t view, const SlipView3DMaths *maths, const uint8_t *trd, size_t trdBytes,
                          const uint8_t *components, size_t componentBytes, const uint8_t *cells, size_t cellBytes,
                          uint32_t trackDataAddress);
bool SlipRaceCamera_UpdateChaseMatrix(SlipRaceCameraState *state, const SlipObject *objects, size_t objectBytes,
                                      uint16_t racerObject, uint16_t view, uint16_t frameStep,
                                      const SlipView3DMaths *maths);

void SlipRaceCamera_ViewportShake(uint16_t shakeTimer, int32_t *centerX, int32_t *centerY);

bool SlipRaceCamera_MainViewport(uint16_t viewId, uint32_t gameMode, uint32_t splitView, uint16_t shakeView1,
                                 uint16_t shakeView2, SlipDraw3DProjectState *projectState);

void SlipRaceCamera_Reset(SlipRaceCameraState *state);
extern uint16_t SlipRaceCamera_projectileObject;
void SlipRaceCamera_TrackProjectile(uint16_t projectile, uint16_t shooter, uint16_t playerOne);

void SlipRaceCamera_DestroyRacer(SlipRaceCameraState *state, uint16_t racerObject, uint16_t playerOneObject,
                                 uint16_t playerTwoObject, uint16_t thirdObject);

void SlipRaceCamera_FinishRacer(SlipRaceCameraState *state, uint16_t racerObject, uint16_t playerOneObject,
                                uint16_t playerTwoObject);
void SlipRaceCamera_StoreLapTime(SlipRaceCameraState *state, uint32_t playerId, uint32_t lapTime);
void SlipRaceCamera_StoreLapNumber(SlipRaceCameraState *state, uint32_t playerId, uint16_t lapNumber);

typedef bool (*SlipRaceCameraActivation)(void *context, uint16_t mode, uint16_t view);

bool SlipRaceCamera_Event(SlipRaceCameraState *state, uint16_t selectionEvent, SlipRaceCameraActivation activate,
                          void *context);

const char *SlipRaceCamera_ModeName(uint16_t mode);

bool SlipRaceCamera_PollKeys(SlipRaceCameraState *state, uint32_t gameMode, bool pressed[256],
                             SlipRaceCameraActivation activate, void *context);

bool SlipRaceCamera_GroundClearance(const uint8_t *trdBase, size_t trdBytes, const uint8_t *componentBase,
                                    size_t componentBaseBytes, const uint8_t *cellTable, size_t cellTableBytes,
                                    uint32_t trackDataBaseAddress, SlipView3DVec32 query, int16_t dirX, int16_t dirY,
                                    int16_t dirZ, int32_t *clearance);

typedef struct SlipRaceCameraIntroZoom {
	SlipView3DVec32 carPosition;
	SlipView3DMatrix pitchedOffsetMatrix;
	SlipView3DVec32 direction;
	int32_t clearance;
	int32_t distanceAfter;
	bool clamped;
	uint16_t event;
	int32_t appliedDistance;
	SlipView3DVec32 cameraPosition;
} SlipRaceCameraIntroZoom;

bool SlipRaceCamera_IntroZoom(SlipRaceCameraState *state, SlipObject *objectTable, size_t objectTableBytes,
                              uint16_t racerObject, uint16_t viewId, uint16_t frameStep, uint32_t flybyChaseEnabled,
                              uint32_t demoChaseEnabled, const SlipView3DMaths *maths, const uint8_t *trdBase,
                              size_t trdBytes, const uint8_t *componentBase, size_t componentBaseBytes,
                              const uint8_t *cellTable, size_t cellTableBytes, uint32_t trackDataBaseAddress,
                              SlipRaceCameraActivation activate, void *context, SlipRaceCameraIntroZoom *result);

bool SlipRaceCamera_Cockpit(SlipObject *objectTable, size_t objectTableBytes, uint16_t racerObject, uint32_t viewIndex,
                            uint8_t *slotPool, size_t slotPoolBytes, uint32_t slotPoolAddress, uint8_t *artData,
                            size_t artDataBytes, uint32_t artDataAddress, const SlipView3DMaths *maths,
                            SlipArticSlotPosition *cameraPosition);

struct SlipObjectPosition;
bool SlipRaceCamera_RearMonitorTransform(SlipObject *objects, size_t objectBytes, uint16_t racerObject,
                                         struct SlipObjectPosition *cameraPosition, SlipView3DMatrix *cameraMatrix);

bool SlipRaceCamera_Rear(SlipObject *objects, size_t objectBytes, uint16_t racerObject, uint32_t viewIndex,
                         uint8_t *slots, size_t slotBytes, uint32_t slotPoolAddress, uint8_t *art, size_t artBytes,
                         uint32_t artDataAddress, const SlipView3DMaths *maths);

#endif /* SLIPSTREAM_RACE_CAMERA_H */
