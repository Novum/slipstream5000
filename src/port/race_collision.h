#ifndef SLIPSTREAM5000_RACE_COLLISION_H
#define SLIPSTREAM5000_RACE_COLLISION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "track_world.h"

enum {
	SLIP_COLLISION_BODY_TRACK_ENABLED = 1,
	SLIP_COLLISION_BODY_CONTACTS_ENABLED = 2,
	SLIP_COLLISION_BODY_TARGETING_ENABLED = 4,
	SLIP_COLLISION_BODY_PROJECTILE = SLIP_COLLISION_BODY_TRACK_ENABLED | SLIP_COLLISION_BODY_CONTACTS_ENABLED,
	SLIP_COLLISION_BODY_RACER = SLIP_COLLISION_BODY_PROJECTILE | SLIP_COLLISION_BODY_TARGETING_ENABLED
};

enum {
	SLIP_COLLISION_CONTACT_NONE = 0,
	SLIP_COLLISION_CONTACT_BODY = 1,
	SLIP_COLLISION_CONTACT_TRACK = 3,
	SLIP_COLLISION_NO_CONTACT_TIME = UINT16_MAX,
	SLIP_COLLISION_SUBSTEP_LIMIT = 32,
	SLIP_COLLISION_BODY_BYTES = 88
};

enum { SLIP_COLLISION_VERTEX_COUNT = 32, SLIP_COLLISION_VERTEX_DOS_BYTES = 28 };

/* Each box face stores four corner indices, an enabled flag and a Q14 normal. */
enum {
	SLIP_COLLISION_BOX_FACE_COUNT = 6,
	SLIP_COLLISION_FACE_CORNER_COUNT = 4,
	SLIP_COLLISION_FACE_ENABLED_INDEX = SLIP_COLLISION_FACE_CORNER_COUNT,
	SLIP_COLLISION_FACE_NORMAL_X_INDEX = SLIP_COLLISION_FACE_ENABLED_INDEX + 1,
	SLIP_COLLISION_FACE_NORMAL_Y_INDEX = SLIP_COLLISION_FACE_NORMAL_X_INDEX + 1,
	SLIP_COLLISION_FACE_NORMAL_Z_INDEX = SLIP_COLLISION_FACE_NORMAL_Y_INDEX + 1,
	SLIP_COLLISION_FACE_VALUE_COUNT = SLIP_COLLISION_FACE_NORMAL_Z_INDEX + 1
};

typedef struct SlipRaceCollisionBody {
	uint32_t movementDistance;
	uint32_t nextBodyOffset;
	uint32_t previousBodyOffset;
	SlipView3DVec32 minimumBounds;
	SlipView3DVec32 maximumBounds;
	uint32_t radius;
	uint32_t otherObject;
	SlipView3DVec32 contactPosition;
	uint32_t impactFlag;
	int32_t impactMagnitude;
	uint16_t objectHandle;
	int16_t repeatedContacts;
	uint16_t flags;
	uint16_t contactTime;
	uint16_t contactType;
	uint16_t reserved;
	int16_t normalX;
	int16_t normalY;
	int16_t normalZ;
	uint16_t material;
	uint32_t excludedBodyOffset;
} SlipRaceCollisionBody;

typedef char SlipRaceCollisionBodySize[(sizeof(SlipRaceCollisionBody) == SLIP_COLLISION_BODY_BYTES) ? 1 : -1];
typedef char SlipRaceCollisionBodyContactOffset[(offsetof(SlipRaceCollisionBody, contactPosition) == 0x2c) ? 1 : -1];
typedef char SlipRaceCollisionBodyCounterOffset[(offsetof(SlipRaceCollisionBody, repeatedContacts) == 0x42) ? 1 : -1];
typedef char SlipRaceCollisionBodyNormalOffset[(offsetof(SlipRaceCollisionBody, normalX) == 0x4c) ? 1 : -1];

typedef struct SlipRaceCollisionVertex {
	int32_t x;
	int32_t y;
	int32_t z;
	int32_t time;
	uint32_t outcode;
	struct SlipRaceCollisionVertex *next;
	struct SlipRaceCollisionVertex *previous;
	uint32_t flags;
} SlipRaceCollisionVertex;

typedef struct SlipRaceCollisionCopySource {
	uint32_t originalPosition[3];
	int32_t x;
	int32_t y;
	int32_t z;
	int32_t time;
	uint32_t outcode;
} SlipRaceCollisionCopySource;

typedef struct SlipRaceCollisionCornerState {
	int32_t originalX;
	int32_t originalY;
	int32_t originalZ;
	int32_t projectedX;
	int32_t projectedY;
	int32_t projectedZ;
	int32_t time;
	uint32_t flags;
} SlipRaceCollisionCornerState;

typedef struct SlipRaceCollisionBounds {
	int32_t minX;
	int32_t minY;
	int32_t minZ;
	int32_t maxX;
	int32_t maxY;
	int32_t maxZ;
} SlipRaceCollisionBounds;

typedef struct SlipRaceCollisionWorkspace {
	SlipRaceCollisionBounds sourceBounds;
	int32_t position[3];
	SlipView3DMatrix relativeMatrix;
	uint8_t padding[2];
	SlipView3DMatrix sourceMatrix;
	int16_t velocityX;
	int16_t velocityY;
	int16_t velocityZ;
	SlipRaceCollisionBounds targetBounds;
	int32_t corners[SLIP_TRACK_BOUNDING_CORNER_COUNT][SLIP_TRACK_CORNER_COORDINATE_COUNT];
} SlipRaceCollisionWorkspace;

typedef char
    SlipRaceCollisionWorkspacePositionOffset[(offsetof(SlipRaceCollisionWorkspace, position) == 0x18u) ? 1 : -1];
typedef char SlipRaceCollisionWorkspaceRelativeMatrixOffset
    [(offsetof(SlipRaceCollisionWorkspace, relativeMatrix) == 0x24u) ? 1 : -1];
typedef char SlipRaceCollisionWorkspaceSourceMatrixOffset[(offsetof(SlipRaceCollisionWorkspace, sourceMatrix) == 0x38u)
                                                              ? 1
                                                              : -1];
typedef char SlipRaceCollisionWorkspaceTargetBoundsOffset[(offsetof(SlipRaceCollisionWorkspace, targetBounds) == 0x50u)
                                                              ? 1
                                                              : -1];
typedef char SlipRaceCollisionWorkspaceCornersOffset[(offsetof(SlipRaceCollisionWorkspace, corners) == 0x68u) ? 1 : -1];

typedef struct SlipRaceCollisionSourceMotion {
	int32_t movementDistance;
	int16_t directionX;
	int16_t directionY;
	int16_t directionZ;
	int32_t displacementX;
	int32_t displacementY;
	int32_t displacementZ;
} SlipRaceCollisionSourceMotion;

typedef struct SlipRaceCollisionTargetMotion {
	int32_t movementDistance;
	int32_t displacementX;
	int32_t displacementY;
	int32_t displacementZ;
	int16_t directionX;
	int16_t directionY;
	int16_t directionZ;
} SlipRaceCollisionTargetMotion;

typedef struct SlipRaceCollisionContact {
	int32_t time;
	int32_t x;
	int32_t y;
	int32_t z;
} SlipRaceCollisionContact;

#pragma pack(push, 1)

typedef struct SlipRaceCollisionStopEvent {
	int32_t impactMagnitude;
	int16_t normalX;
	int16_t normalY;
	int16_t normalZ;
	SlipView3DVec32 contactPosition;
	uint32_t impactFlag;
} SlipRaceCollisionStopEvent;

typedef struct SlipRaceCollisionBounceEvent {
	int16_t normalX;
	int16_t normalY;
	int16_t normalZ;
	int16_t material;
	SlipView3DVec32 contactPosition;
} SlipRaceCollisionBounceEvent;

#pragma pack(pop)

typedef char SlipRaceCollisionStopEventSize[sizeof(SlipRaceCollisionStopEvent) == 0x1au ? 1 : -1];
typedef char SlipRaceCollisionBounceEventSize[sizeof(SlipRaceCollisionBounceEvent) == 0x14u ? 1 : -1];

typedef struct SlipRaceCollisionOutcodes {
	uint32_t commonOutcode;
	uint32_t combinedOutcode;
} SlipRaceCollisionOutcodes;

typedef struct SlipRaceCollisionCrossings {
	SlipRaceCollisionVertex *firstOutside;
	SlipRaceCollisionVertex *firstInside;
	SlipRaceCollisionVertex *secondOutside;
	SlipRaceCollisionVertex *secondInside;
} SlipRaceCollisionCrossings;

typedef bool (*SlipRaceCollisionTrackQuery)(uint32_t bodyFlags, uint16_t objectHandle);
typedef void (*SlipRaceCollisionPreStep)(uint32_t frameStep);
typedef void (*SlipRaceCollisionPostStep)(void);
typedef bool (*SlipRaceCollisionLineOfSight)(uint16_t firstObject, uint16_t secondObject);
typedef void (*SlipRaceCollisionSegmentQuery)(const uint8_t *trdBase, size_t trackDataSize, uint32_t trackDataAddress,
                                              const uint8_t *componentBase, size_t componentBytes,
                                              const uint8_t *cellTableBase, size_t cellTableBytes,
                                              SlipView3DVec32 segmentStart, SlipView3DVec32 segmentEnd,
                                              SlipTrackWorldSegmentCollision *result);

typedef struct SlipRaceCollisionNearestBody {
	uint16_t objectHandle;
	bool noBodyFound;
} SlipRaceCollisionNearestBody;

typedef struct SlipRaceCollisionQuery {
	uint32_t objectOrFlags;
	uint32_t trackHitMask;
	bool collisionFound;
} SlipRaceCollisionQuery;

typedef struct SlipRaceCollisionSegmentHit {
	SlipView3DVec32 point;
	uint16_t objectHandle;
} SlipRaceCollisionSegmentHit;

extern SlipRaceCollisionVertex *SlipRaceCollision_freeList;
extern SlipRaceCollisionVertex *SlipRaceCollision_activeList;
extern int32_t SlipRaceCollision_deltaX;
extern int32_t SlipRaceCollision_deltaY;
extern int32_t SlipRaceCollision_deltaZ;
extern int32_t SlipRaceCollision_distance;
extern int32_t SlipRaceCollision_relativePositionX;
extern int32_t SlipRaceCollision_relativePositionY;
extern int32_t SlipRaceCollision_relativePositionZ;
extern uint32_t SlipRaceCollision_velocityLength;
extern SlipRaceCollisionWorkspace SlipRaceCollision_workspace;
#define SlipRaceCollision_sourceBounds (SlipRaceCollision_workspace.sourceBounds)
#define SlipRaceCollision_targetBounds (SlipRaceCollision_workspace.targetBounds)
#define SlipRaceCollision_corners (SlipRaceCollision_workspace.corners)
#define SlipRaceCollision_relativeMatrix (SlipRaceCollision_workspace.relativeMatrix)
#define SlipRaceCollision_sourceMatrix (SlipRaceCollision_workspace.sourceMatrix)
#define SlipRaceCollision_velocityX (SlipRaceCollision_workspace.velocityX)
#define SlipRaceCollision_velocityY (SlipRaceCollision_workspace.velocityY)
#define SlipRaceCollision_velocityZ (SlipRaceCollision_workspace.velocityZ)
extern SlipRaceCollisionCornerState SlipRaceCollision_cornerState[SLIP_TRACK_BOUNDING_CORNER_COUNT];
extern uint32_t SlipRaceCollision_inverseVelocity;
extern uint8_t *SlipRaceCollision_sourceBody;
extern uint8_t *SlipRaceCollision_targetBody;
extern uint32_t SlipRaceCollision_sourceRadius;
extern int32_t SlipRaceCollision_sourcePositionX;
extern int32_t SlipRaceCollision_sourcePositionY;
extern int32_t SlipRaceCollision_sourcePositionZ;
extern uint32_t SlipRaceCollision_targetRadius;
extern SlipRaceCollisionSourceMotion SlipRaceCollision_sourceMotion;
extern SlipRaceCollisionTargetMotion SlipRaceCollision_targetMotion;
extern SlipRaceCollisionContact SlipRaceCollision_contact;
extern SlipObject *SlipRaceCollision_objectTable;
extern size_t SlipRaceCollision_objectTableBytes;
extern uint8_t *SlipRaceCollision_physicsTable;
extern uint32_t SlipRaceCollision_activeBodyOffset;
extern uint32_t SlipRaceCollision_freeBodyOffset;
extern uint16_t SlipRaceCollision_bodyCount;
extern int32_t SlipRaceCollision_faces[SLIP_COLLISION_BOX_FACE_COUNT][SLIP_COLLISION_FACE_VALUE_COUNT];
extern uint32_t SlipRaceCollision_faceCommon;
extern uint32_t SlipRaceCollision_faceCombined;
extern uint32_t SlipRaceCollision_frameStep;
extern uint16_t SlipRaceCollision_enabled;
extern uint16_t SlipRaceCollision_firstTime;
extern uint32_t SlipRaceCollision_integratedStep;
extern SlipView3DVec32 SlipRaceCollision_contactOrigin;
extern SlipView3DVec32 SlipRaceCollision_targetContactPoint;
extern uint32_t SlipRaceCollision_excludedBodyOffset;
extern SlipView3DVec32 SlipRaceCollision_savedPosition;
extern SlipView3DMatrix SlipRaceCollision_savedMatrix;
extern SlipRaceCollisionStopEvent SlipRaceCollision_stopEvent;
extern SlipRaceCollisionBounceEvent SlipRaceCollision_bounceEvent;
extern SlipRaceCollisionPreStep SlipRaceCollision_preStep;
extern SlipRaceCollisionPostStep SlipRaceCollision_postStep;
extern SlipRaceCollisionTrackQuery SlipRaceCollision_trackQuery;
extern SlipRaceCollisionSegmentQuery SlipRaceCollision_segmentQuery;
extern SlipRaceCollisionLineOfSight SlipRaceCollision_lineOfSight;

void SlipRaceCollision_SetCallbacks(SlipRaceCollisionSegmentQuery segmentQuery, SlipRaceCollisionPreStep preStep,
                                    SlipRaceCollisionLineOfSight lineOfSight, SlipRaceCollisionTrackQuery trackQuery,
                                    SlipRaceCollisionPostStep postStep);

void SlipRaceCollision_InitializeVertexPool(void);
void SlipRaceCollision_InitializeResourceVertices(SlipRaceCollisionVertex *pool);

void SlipRaceCollision_ResetBodyLists(void);

void SlipRaceCollision_SaveObjectTransform(uint16_t object);

void SlipRaceCollision_RestoreObjectTransform(uint16_t object);

void SlipRaceCollision_CopyVertex(const SlipRaceCollisionCopySource *source, SlipRaceCollisionVertex *destination);

void SlipRaceCollision_SelectContact(void);

void SlipRaceCollision_BuildSourceMotion(void);

void SlipRaceCollision_BuildTargetMotion(void);

uint32_t SlipRaceCollision_CornerOutcode(uint32_t cornerIndex, SlipRaceCollisionVertex *vertex);

SlipRaceCollisionOutcodes SlipRaceCollision_AccumulateOutcodes(void);

SlipRaceCollisionVertex *SlipRaceCollision_AllocateVertex(void);

SlipRaceCollisionCrossings SlipRaceCollision_FindCrossings(uint32_t planeOutcode);

bool SlipRaceCollision_ClipPolygon(uint32_t combinedOutcode);

bool SlipRaceCollision_TestBoxFaces(void);

bool SlipRaceCollision_TestObjectAgainstBodies(uint16_t object, uint16_t *collisionObject);

bool SlipRaceCollision_TestBoundsAgainstBodies(uint8_t *body, int32_t boundsMinX, int32_t boundsMinY,
                                               int32_t boundsMinZ, int32_t boundsMaxX, int32_t boundsMaxY,
                                               int32_t boundsMaxZ, uint16_t *collisionObject);

void SlipRaceCollision_RecordTrackContact(uint16_t object, uint16_t contactTime, uint16_t normalX, uint16_t normalY,
                                          uint16_t normalZ, uint16_t material, const int32_t *contactPoint);

bool SlipRaceCollision_Query(uint16_t object);

SlipRaceCollisionSegmentHit SlipRaceCollision_QuerySegment(uint16_t excludedObject, SlipView3DVec32 segmentStart,
                                                           SlipView3DVec32 segmentEnd, const uint8_t *trdBase,
                                                           size_t trackDataSize, uint32_t trackDataAddress,
                                                           const uint8_t *componentBase, size_t componentBytes,
                                                           const uint8_t *cellTableBase, size_t cellTableBytes);

void SlipRaceCollision_QueryResult(uint16_t object, SlipRaceCollisionQuery *result);

void SlipRaceCollision_FindNearestBody(uint32_t maximumDistance, uint16_t angle, int32_t minimumZ, int16_t offsetX,
                                       int16_t offsetY, int16_t offsetZ, uint16_t object, const SlipView3DMaths *maths,
                                       SlipRaceCollisionNearestBody *result);

void SlipRaceCollision_TestMinZ(void);
void SlipRaceCollision_TestMaxZ(void);
void SlipRaceCollision_TestMinX(void);
void SlipRaceCollision_TestMaxX(void);
void SlipRaceCollision_TestMinY(void);
void SlipRaceCollision_TestMaxY(void);

void SlipRaceCollision_TestAllFaces(void);

bool SlipRaceCollision_TestBodies(void);

void SlipRaceCollision_RecordContact(uint32_t otherObjectHighBits);

void SlipRaceCollision_BuildContactResponse(void);

/* Returns carry; the output names the free sentinel on exhaustion. */
bool SlipRaceCollision_AllocateBody(SlipRaceCollisionBody **allocatedBody);

bool SlipRaceCollision_CreateBody(uint16_t object, uint16_t flags);

bool SlipRaceCollision_ExcludePair(uint16_t firstObject, uint16_t secondObject);

uint16_t SlipRaceCollision_BodyFlags(uint16_t object);
void SlipRaceCollision_SetBodyFlags(uint16_t object, uint16_t flags);
SlipView3DVec16 SlipRaceCollision_ReflectDirection(uint16_t object, SlipView3DVec16 normal);

uint16_t SlipRaceCollision_BodyProperty(uint16_t object, uint16_t fallback);

void SlipRaceCollision_SetBodyBounds(uint16_t object, int32_t minX, int32_t minY, int32_t minZ, int32_t maxX,
                                     int32_t maxY, int32_t maxZ);

typedef struct SlipRaceCollisionBodyBounds {
	int32_t minX;
	int32_t minY;
	int32_t minZ;
	int32_t maxX;
	int32_t maxY;
	int32_t maxZ;
} SlipRaceCollisionBodyBounds;

SlipRaceCollisionBodyBounds SlipRaceCollision_GetBodyBounds(uint16_t object);

void SlipRaceCollision_InitializeBodyLists(void);

void SlipRaceCollision_AdvanceUncollidableObjects(void);

bool SlipRaceCollision_RemoveBody(uint16_t object);

void SlipRaceCollision_Shutdown(void);

uint32_t SlipRaceCollision_RemoveBodyIfFlagged(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                               uint32_t eventFlags, uint16_t object, uintptr_t dispatchData,
                                               uint32_t dispatchFrame);

void SlipRaceCollision_FindBodyCollisions(void);

void SlipRaceCollision_ClearBodyContacts(void);

void SlipRaceCollision_PrepareBodies(void);

void SlipRaceCollision_IntegrateBodies(void);

void SlipRaceCollision_DispatchBodyEvents(uint32_t contactTimeWord, uint32_t eventValue, uint32_t eventFlags,
                                          uint32_t contactTypeWord);

void SlipRaceCollision_DispatchBodyContact(uint32_t bodyOffset, uint32_t eventFlags);

void SlipRaceCollision_DispatchTrackContact(uint32_t bodyOffset, uint32_t eventFlags);

void SlipRaceCollision_PrepareRemainingStep(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                            uint32_t eventFlags, uint32_t dispatchFrame);

void SlipRaceCollision_FinalizeBodyCollisions(void);

void SlipRaceCollision_BuildBodyCorners(SlipView3DVec32 translation, const SlipView3DMatrix *matrix, uint16_t object);

void SlipRaceCollision_ReleasePolygon(void);

void SlipRaceCollision_ReleaseVertex(SlipRaceCollisionVertex *vertex);

void SlipRaceCollision_InterpolateX(const SlipRaceCollisionVertex *insideVertex,
                                    SlipRaceCollisionVertex *intersectionVertex, int32_t planeX);

void SlipRaceCollision_InterpolateY(const SlipRaceCollisionVertex *insideVertex,
                                    SlipRaceCollisionVertex *intersectionVertex, int32_t planeY);

void SlipRaceCollision_InterpolateZ(const SlipRaceCollisionVertex *insideVertex,
                                    SlipRaceCollisionVertex *intersectionVertex, int32_t planeZ);

void SlipRaceCollision_InterpolateTime(const SlipRaceCollisionVertex *insideVertex,
                                       SlipRaceCollisionVertex *intersectionVertex, uint32_t fractionQ30);

bool SlipRaceCollision_RejectMovingBox(uint32_t *commonOutcode);

#endif
