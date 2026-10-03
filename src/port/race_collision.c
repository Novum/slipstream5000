#include "race_collision.h"
#include "fixed_point.h"
#include "runtime.h"

#include <stddef.h>

enum {
	/* Face table order follows its local-space outward normals. */
	SLIP_COLLISION_FACE_POSITIVE_Z = 0,
	SLIP_COLLISION_FACE_NEGATIVE_Z = 1,
	SLIP_COLLISION_FACE_POSITIVE_X = 2,
	SLIP_COLLISION_FACE_NEGATIVE_X = 3,
	SLIP_COLLISION_FACE_POSITIVE_Y = 4,
	SLIP_COLLISION_FACE_NEGATIVE_Y = 5,
	SLIP_COLLISION_CORNER_PROJECTED = 1u,
	SLIP_COLLISION_REPEATED_IMPACT_LIMIT = 2,
	SLIP_COLLISION_SEARCH_MARGIN_SHIFT = 2,
	SLIP_COLLISION_REPEATED_CONTACT_LIMIT = 3,
	SLIP_COLLISION_OUTCODE_MINIMUM_X = 0x02,
	SLIP_COLLISION_OUTCODE_MAXIMUM_X = 0x04,
	SLIP_COLLISION_OUTCODE_MINIMUM_Y = 0x08,
	SLIP_COLLISION_OUTCODE_MAXIMUM_Y = 0x10,
	SLIP_COLLISION_OUTCODE_MINIMUM_Z = 0x20,
	SLIP_COLLISION_OUTCODE_MAXIMUM_Z = 0x40,
	SLIP_COLLISION_OUTCODE_BOX_MASK = SLIP_COLLISION_OUTCODE_MINIMUM_X | SLIP_COLLISION_OUTCODE_MAXIMUM_X |
	                                  SLIP_COLLISION_OUTCODE_MINIMUM_Y | SLIP_COLLISION_OUTCODE_MAXIMUM_Y |
	                                  SLIP_COLLISION_OUTCODE_MINIMUM_Z | SLIP_COLLISION_OUTCODE_MAXIMUM_Z,
	SLIP_COLLISION_OUTCODE_PLANE_SIDE = 0x80,
	SLIP_COLLISION_OUTCODE_PRESERVED_MASK = UINT8_MAX ^ SLIP_COLLISION_OUTCODE_BOX_MASK,
	SLIP_COLLISION_OUTCODE_FACE_REJECT_MASK = SLIP_COLLISION_OUTCODE_BOX_MASK | SLIP_COLLISION_OUTCODE_PLANE_SIDE,
	SLIP_COLLISION_RECIPROCAL_FRACTION_BITS = 30,
	SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT = SLIP_COLLISION_RECIPROCAL_FRACTION_BITS - SLIP_Q14_FRACTION_BITS,
	SLIP_COLLISION_RECIPROCAL_ONE = 1u << SLIP_COLLISION_RECIPROCAL_FRACTION_BITS,
	SLIP_COLLISION_RECIPROCAL_HIGH_SHIFT = 32 - SLIP_COLLISION_RECIPROCAL_FRACTION_BITS,
	SLIP_COLLISION_CONTACT_PENETRATION_TIME_TOLERANCE = 128,
	SLIP_COLLISION_FORWARD_FACE_ALIGNMENT_Q14 = SLIP_Q14_ONE - 2,
	SLIP_COLLISION_STOP_EVENT_TOKEN = 0x1459c,
	SLIP_COLLISION_BOUNCE_EVENT_TOKEN = 0x1460c,
	SLIP_COLLISION_PACKED_UPPER_WORD_MASK = 0xffff0000u,
	SLIP_COLLISION_MINIMUM_AXIS_SPEED = 28,
	SLIP_COLLISION_FULL_BOX_MINIMUM_RADIUS = SLIP_Q14_ONE
};

SlipRaceCollisionVertex *SlipRaceCollision_freeList;
SlipRaceCollisionVertex *SlipRaceCollision_activeList;
int32_t SlipRaceCollision_deltaX;
int32_t SlipRaceCollision_deltaY;
int32_t SlipRaceCollision_deltaZ;
int32_t SlipRaceCollision_distance;
int32_t SlipRaceCollision_relativePositionX;
int32_t SlipRaceCollision_relativePositionY;
int32_t SlipRaceCollision_relativePositionZ;
uint32_t SlipRaceCollision_velocityLength;
SlipRaceCollisionWorkspace SlipRaceCollision_workspace;
SlipRaceCollisionCornerState SlipRaceCollision_cornerState[SLIP_TRACK_BOUNDING_CORNER_COUNT];
uint32_t SlipRaceCollision_inverseVelocity;
uint8_t *SlipRaceCollision_sourceBody;
uint8_t *SlipRaceCollision_targetBody;
uint32_t SlipRaceCollision_sourceRadius;
int32_t SlipRaceCollision_sourcePositionX;
int32_t SlipRaceCollision_sourcePositionY;
int32_t SlipRaceCollision_sourcePositionZ;
uint32_t SlipRaceCollision_targetRadius;
SlipRaceCollisionSourceMotion SlipRaceCollision_sourceMotion;
SlipRaceCollisionTargetMotion SlipRaceCollision_targetMotion;
SlipRaceCollisionContact SlipRaceCollision_contact;
SlipObject *SlipRaceCollision_objectTable;
size_t SlipRaceCollision_objectTableBytes;
uint8_t *SlipRaceCollision_physicsTable;
uint32_t SlipRaceCollision_activeBodyOffset;
uint32_t SlipRaceCollision_freeBodyOffset = SLIP_COLLISION_BODY_BYTES;
uint16_t SlipRaceCollision_bodyCount;
int32_t SlipRaceCollision_faces[SLIP_COLLISION_BOX_FACE_COUNT][SLIP_COLLISION_FACE_VALUE_COUNT] = {
    {0, 1, 2, 3, 0, 0, 0, SLIP_Q14_ONE},  {7, 6, 5, 4, 0, 0, 0, -SLIP_Q14_ONE}, {3, 2, 6, 7, 0, SLIP_Q14_ONE, 0, 0},
    {4, 5, 1, 0, 0, -SLIP_Q14_ONE, 0, 0}, {5, 6, 2, 1, 0, 0, SLIP_Q14_ONE, 0},  {0, 3, 7, 4, 0, 0, -SLIP_Q14_ONE, 0}};
uint32_t SlipRaceCollision_faceCommon;
uint32_t SlipRaceCollision_faceCombined;
uint32_t SlipRaceCollision_frameStep;
uint16_t SlipRaceCollision_enabled;
uint16_t SlipRaceCollision_firstTime;
uint32_t SlipRaceCollision_integratedStep;
SlipView3DVec32 SlipRaceCollision_contactOrigin;
SlipView3DVec32 SlipRaceCollision_targetContactPoint;
uint32_t SlipRaceCollision_excludedBodyOffset;
SlipView3DVec32 SlipRaceCollision_savedPosition;
SlipView3DMatrix SlipRaceCollision_savedMatrix;
SlipRaceCollisionStopEvent SlipRaceCollision_stopEvent;
SlipRaceCollisionBounceEvent SlipRaceCollision_bounceEvent;
SlipRaceCollisionPreStep SlipRaceCollision_preStep;
SlipRaceCollisionPostStep SlipRaceCollision_postStep;
SlipRaceCollisionTrackQuery SlipRaceCollision_trackQuery;
SlipRaceCollisionSegmentQuery SlipRaceCollision_segmentQuery;
SlipRaceCollisionLineOfSight SlipRaceCollision_lineOfSight;
static SlipRaceCollisionVertex SlipRaceCollision_vertexPool[SLIP_COLLISION_VERTEX_COUNT];

static uint8_t *SlipRaceCollision_CollisionBodyFromOffset(uint32_t offset) {
	return SlipRaceCollision_physicsTable + offset;
}

static uint32_t SlipRaceCollision_CollisionBodyOffset(const uint8_t *body) {
	return (uint32_t)(body - SlipRaceCollision_physicsTable);
}

static int32_t SlipRaceCollision_ScaleDirectionQ14(int16_t direction, int32_t magnitude);

void SlipRaceCollision_InitializeResourceVertices(SlipRaceCollisionVertex *pool) {
	size_t i;

	for (i = 0; i < SLIP_COLLISION_VERTEX_COUNT - 1u; ++i) {
		pool[i].next = &pool[i + 1u];
		pool[i + 1u].previous = &pool[i];
	}
	pool[SLIP_COLLISION_VERTEX_COUNT - 1u].next = pool;
	pool[0].previous = &pool[SLIP_COLLISION_VERTEX_COUNT - 1u];
	SlipRaceCollision_freeList = pool;
}

void SlipRaceCollision_InitializeVertexPool(void) {
	SlipRaceCollision_InitializeResourceVertices(SlipRaceCollision_vertexPool);
	SlipRaceCollision_activeList = NULL;
}

void SlipRaceCollision_SetCallbacks(SlipRaceCollisionSegmentQuery segmentQuery, SlipRaceCollisionPreStep preStep,
                                    SlipRaceCollisionLineOfSight lineOfSight, SlipRaceCollisionTrackQuery trackQuery,
                                    SlipRaceCollisionPostStep postStep) {
	SlipRaceCollision_preStep = preStep;
	SlipRaceCollision_postStep = postStep;
	SlipRaceCollision_trackQuery = trackQuery;
	SlipRaceCollision_segmentQuery = segmentQuery;
	SlipRaceCollision_lineOfSight = lineOfSight;
}

void SlipRaceCollision_ResetBodyLists(void) { SlipRaceCollision_InitializeBodyLists(); }

void SlipRaceCollision_SaveObjectTransform(uint16_t object) {
	SlipObjectPosition objectPosition;
	SlipObjectMatrixCopy matrixCopy;

	SlipObject_Position(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, object, &objectPosition);
	SlipRaceCollision_savedPosition.x = (int32_t)objectPosition.positionX;
	SlipRaceCollision_savedPosition.y = (int32_t)objectPosition.positionY;
	SlipRaceCollision_savedPosition.z = (int32_t)objectPosition.positionZ;
	SlipObject_MatrixCopy(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, object,
	                      &SlipRaceCollision_savedMatrix, &matrixCopy);
}

void SlipRaceCollision_RestoreObjectTransform(uint16_t object) {
	SlipObjectSetPosition setPosition;
	SlipObjectMatrixInstall matrixInstall;

	SlipObject_SetPosition(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, object,
	                       (uint32_t)SlipRaceCollision_savedPosition.x, (uint32_t)SlipRaceCollision_savedPosition.y,
	                       (uint32_t)SlipRaceCollision_savedPosition.z, &setPosition);
	SlipObject_MatrixInstall(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, object,
	                         &SlipRaceCollision_savedMatrix, &matrixInstall);
}

bool SlipRaceCollision_TestObjectAgainstBodies(uint16_t object, uint16_t *collisionObject) {
	const uint16_t bodyOffset = SlipObject_PhysicsOffset(SlipRaceCollision_objectTable, object);
	uint8_t *body;
	SlipObjectPosition objectPosition;

	if (bodyOffset == 0)
		return false;
	body = SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
	if ((((const SlipRaceCollisionBody *)(const void *)body)->flags & SLIP_COLLISION_BODY_CONTACTS_ENABLED) == 0)
		return false;
	SlipObject_Position(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, object, &objectPosition);
	SlipRaceCollision_sourcePositionX = (int32_t)objectPosition.positionX;
	SlipRaceCollision_sourcePositionY = (int32_t)objectPosition.positionY;
	SlipRaceCollision_sourcePositionZ = (int32_t)objectPosition.positionZ;
	SlipRaceCollision_sourceRadius = ((const SlipRaceCollisionBody *)(const void *)body)->radius;
	return SlipRaceCollision_TestBoundsAgainstBodies(
	    body, (int32_t)((const SlipRaceCollisionBody *)(const void *)body)->minimumBounds.x,
	    (int32_t)((const SlipRaceCollisionBody *)(const void *)body)->minimumBounds.y,
	    (int32_t)((const SlipRaceCollisionBody *)(const void *)body)->minimumBounds.z,
	    (int32_t)((const SlipRaceCollisionBody *)(const void *)body)->maximumBounds.x,
	    (int32_t)((const SlipRaceCollisionBody *)(const void *)body)->maximumBounds.y,
	    (int32_t)((const SlipRaceCollisionBody *)(const void *)body)->maximumBounds.z, collisionObject);
}

bool SlipRaceCollision_TestBoundsAgainstBodies(uint8_t *body, int32_t boundsMinX, int32_t boundsMinY,
                                               int32_t boundsMinZ, int32_t boundsMaxX, int32_t boundsMaxY,
                                               int32_t boundsMaxZ, uint16_t *collisionObject) {
	SlipObjectMatrixCopy matrixCopy;
	SlipObjectPosition objectPosition;
	uint32_t bodyOffset;

	SlipRaceCollision_sourceBody = body;
	SlipRaceCollision_sourceBounds.minX = boundsMinX;
	SlipRaceCollision_sourceBounds.minY = boundsMinY;
	SlipRaceCollision_sourceBounds.minZ = boundsMinZ;
	SlipRaceCollision_sourceBounds.maxX = boundsMaxX;
	SlipRaceCollision_sourceBounds.maxY = boundsMaxY;
	SlipRaceCollision_sourceBounds.maxZ = boundsMaxZ;
	SlipObject_MatrixCopy(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes,
	                      ((const SlipRaceCollisionBody *)(const void *)body)->objectHandle,
	                      &SlipRaceCollision_sourceMatrix, &matrixCopy);
	bodyOffset = SlipRaceCollision_activeBodyOffset;
	for (;;) {
		uint8_t *otherBody;
		uint16_t otherObject;
		SlipView3DVec32 relativePosition;
		SlipDraw3DApproxAbsVectorLength approximate;
		int32_t rejectDistance;
		SlipView3DMatrix otherMatrix;

		bodyOffset =
		    ((const SlipRaceCollisionBody *)(const void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset))
		        ->nextBodyOffset;
		if (bodyOffset == SlipRaceCollision_activeBodyOffset)
			return false;
		otherBody = SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
		if (otherBody == SlipRaceCollision_sourceBody)
			continue;
		if ((((const SlipRaceCollisionBody *)(const void *)otherBody)->flags & SLIP_COLLISION_BODY_CONTACTS_ENABLED) ==
		    0)
			continue;
		otherObject = ((const SlipRaceCollisionBody *)(const void *)otherBody)->objectHandle;
		SlipObject_Position(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, otherObject,
		                    &objectPosition);
		relativePosition.x = (int32_t)(objectPosition.positionX - (uint32_t)SlipRaceCollision_sourcePositionX);
		relativePosition.y = (int32_t)(objectPosition.positionY - (uint32_t)SlipRaceCollision_sourcePositionY);
		relativePosition.z = (int32_t)(objectPosition.positionZ - (uint32_t)SlipRaceCollision_sourcePositionZ);
		SlipRaceCollision_relativePositionX = relativePosition.x;
		SlipRaceCollision_relativePositionY = relativePosition.y;
		SlipRaceCollision_relativePositionZ = relativePosition.z;
		SlipDraw3D_ApproxAbsVectorLength((uint32_t)relativePosition.x, (uint32_t)relativePosition.y,
		                                 (uint32_t)relativePosition.z, &approximate);
		rejectDistance = (int32_t)(((const SlipRaceCollisionBody *)(const void *)otherBody)->radius +
		                           SlipRaceCollision_sourceRadius);
		rejectDistance =
		    (int32_t)((uint32_t)rejectDistance + (uint32_t)(rejectDistance >> SLIP_COLLISION_SEARCH_MARGIN_SHIFT));
		if ((int32_t)approximate.approximateLength > rejectDistance)
			continue;
		relativePosition = SlipView3D_TransformPositionByRows(&SlipRaceCollision_sourceMatrix, relativePosition);
		SlipRaceCollision_relativePositionX = relativePosition.x;
		SlipRaceCollision_relativePositionY = relativePosition.y;
		SlipRaceCollision_relativePositionZ = relativePosition.z;
		SlipObject_MatrixCopy(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, otherObject,
		                      &otherMatrix, &matrixCopy);
		SlipView3D_ComposeMatrix(&otherMatrix, &SlipRaceCollision_sourceMatrix, &SlipRaceCollision_relativeMatrix);
		SlipRaceCollision_BuildBodyCorners(relativePosition, &SlipRaceCollision_relativeMatrix, otherObject);
		if (SlipRaceCollision_TestBoxFaces()) {
			if (collisionObject != NULL)
				*collisionObject = otherObject;
			return true;
		}
	}
}

void SlipRaceCollision_RecordTrackContact(uint16_t object, uint16_t contactTime, uint16_t normalX, uint16_t normalY,
                                          uint16_t normalZ, uint16_t material, const int32_t *contactPoint) {
	uint16_t bodyOffset;
	SlipRaceCollisionBody *body;

	if (contactTime > SlipRaceCollision_firstTime)
		return;
	SlipRaceCollision_firstTime = contactTime;
	bodyOffset = SlipObject_PhysicsOffset(SlipRaceCollision_objectTable, object);
	body = (SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
	if (contactTime == 0 && body->repeatedContacts != SLIP_COLLISION_REPEATED_IMPACT_LIMIT) {
		body->repeatedContacts = (int16_t)((uint16_t)body->repeatedContacts + 1u);
	}
	body->contactType = SLIP_COLLISION_CONTACT_TRACK;
	body->normalX = (int16_t)normalX;
	body->normalY = (int16_t)normalY;
	body->normalZ = (int16_t)normalZ;
	body->material = material;
	body->contactTime = contactTime;
	body->contactPosition.x = contactPoint[0];
	body->contactPosition.y = contactPoint[1];
	body->contactPosition.z = contactPoint[2];
}

void SlipRaceCollision_QueryResult(uint16_t object, SlipRaceCollisionQuery *result) {
	uint16_t flags = SlipRaceCollision_BodyFlags(object);
	uint16_t collisionObject;

	if ((flags & SLIP_COLLISION_BODY_CONTACTS_ENABLED) != 0 &&
	    SlipRaceCollision_TestObjectAgainstBodies(object, &collisionObject)) {
		result->objectOrFlags = collisionObject;
		result->trackHitMask = 0;
		result->collisionFound = true;
		return;
	}
	if (SlipRaceCollision_trackQuery != NULL) {
		flags = SlipRaceCollision_BodyFlags(object);
		if ((flags & SLIP_COLLISION_BODY_TRACK_ENABLED) != 0 && SlipRaceCollision_trackQuery(flags, object)) {
			result->objectOrFlags = 0;
			result->trackHitMask = UINT32_MAX;
			result->collisionFound = true;
			return;
		}
	}
	result->objectOrFlags = flags;
	result->trackHitMask = 0;
	result->collisionFound = false;
}

bool SlipRaceCollision_Query(uint16_t object) {
	SlipRaceCollisionQuery result;

	SlipRaceCollision_QueryResult(object, &result);
	return result.collisionFound;
}

static int32_t SlipRaceCollision_ScaleSegmentComponent(int32_t value, uint32_t multiplier, uint32_t shift) {
	const int64_t product = (int64_t)value * (int64_t)(int32_t)multiplier;
	return (int32_t)((uint64_t)product >> shift);
}

static void SlipRaceCollision_TestSegmentFace(const SlipRaceCollisionBounds *bounds, SlipView3DVec32 localOrigin,
                                              SlipView3DVec16 localDirection, unsigned planeAxis, bool maximumPlane,
                                              unsigned signAxis, uint32_t *bestDistance, uint16_t candidateObject,
                                              uint16_t *bestObject) {
	const int32_t origin[3] = {localOrigin.x, localOrigin.y, localOrigin.z};
	const int16_t direction[3] = {localDirection.x, localDirection.y, localDirection.z};
	const int32_t minimum[3] = {bounds->minX, bounds->minY, bounds->minZ};
	const int32_t maximum[3] = {bounds->maxX, bounds->maxY, bounds->maxZ};
	const int32_t plane = maximumPlane ? maximum[planeAxis] : minimum[planeAxis];
	const int16_t signDirection = direction[signAxis];
	uint32_t absoluteDirection;
	uint32_t inverseDirection;
	uint32_t candidateDistance;
	unsigned firstOtherAxis;
	unsigned secondOtherAxis;
	int32_t firstOtherCoordinate;
	int32_t secondOtherCoordinate;

	if ((!maximumPlane && origin[planeAxis] > plane) || (maximumPlane && origin[planeAxis] < plane)) {
		return;
	}
	if ((!maximumPlane && signDirection < 0) || (maximumPlane && signDirection >= 0)) {
		return;
	}
	absoluteDirection = maximumPlane ? (uint32_t)(-(int32_t)direction[planeAxis]) : (uint32_t)direction[planeAxis];
	if (absoluteDirection == 0) {
		return;
	}
	inverseDirection = SLIP_COLLISION_RECIPROCAL_ONE / absoluteDirection;
	candidateDistance = (uint32_t)SlipRaceCollision_ScaleSegmentComponent(
	    (int32_t)(maximumPlane ? (uint32_t)origin[planeAxis] - (uint32_t)plane
	                           : (uint32_t)plane - (uint32_t)origin[planeAxis]),
	    inverseDirection, SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT);
	if (candidateDistance > *bestDistance) {
		return;
	}
	firstOtherAxis = (planeAxis + 1u) % 3u;
	secondOtherAxis = (planeAxis + 2u) % 3u;
	firstOtherCoordinate = (int32_t)((uint32_t)SlipRaceCollision_ScaleSegmentComponent(
	                                     direction[firstOtherAxis], candidateDistance, SLIP_Q14_FRACTION_BITS) +
	                                 (uint32_t)origin[firstOtherAxis]);
	if (firstOtherCoordinate < minimum[firstOtherAxis] || firstOtherCoordinate > maximum[firstOtherAxis]) {
		return;
	}
	secondOtherCoordinate = (int32_t)((uint32_t)SlipRaceCollision_ScaleSegmentComponent(
	                                      direction[secondOtherAxis], candidateDistance, SLIP_Q14_FRACTION_BITS) +
	                                  (uint32_t)origin[secondOtherAxis]);
	if (secondOtherCoordinate < minimum[secondOtherAxis] || secondOtherCoordinate > maximum[secondOtherAxis]) {
		return;
	}
	*bestDistance = candidateDistance;
	*bestObject = candidateObject;
}

SlipRaceCollisionSegmentHit SlipRaceCollision_QuerySegment(uint16_t excludedObject, SlipView3DVec32 segmentStart,
                                                           SlipView3DVec32 segmentEnd, const uint8_t *trdBase,
                                                           size_t trackDataSize, uint32_t trackDataAddress,
                                                           const uint8_t *componentBase, size_t componentBytes,
                                                           const uint8_t *cellTableBase, size_t cellTableBytes) {
	SlipView3DNormalizeVector3D normalized;
	SlipView3DVec16 direction;
	uint32_t bestDistance = UINT32_MAX;
	uint16_t bestObject = 0;
	uint32_t bodyOffset = SlipRaceCollision_activeBodyOffset;
	SlipTrackWorldSegmentCollision trackHit = {0};
	SlipRaceCollisionSegmentHit result;

	SlipView3D_NormalizeVector3D((uint32_t)segmentEnd.x - (uint32_t)segmentStart.x,
	                             (uint32_t)segmentEnd.y - (uint32_t)segmentStart.y,
	                             (uint32_t)segmentEnd.z - (uint32_t)segmentStart.z, &normalized);
	direction = (SlipView3DVec16){(int16_t)(uint16_t)normalized.unitXQ14, (int16_t)(uint16_t)normalized.unitYQ14,
	                              (int16_t)(uint16_t)normalized.unitZQ14};

	for (;;) {
		uint8_t *body;
		uint16_t candidateObject;
		SlipObjectPosition objectPosition;
		SlipObjectMatrixCopy matrixCopy;
		SlipView3DMatrix objectMatrix;
		SlipView3DVec32 localOrigin;
		SlipView3DVec32 localDirection;
		SlipRaceCollisionBounds sourceBounds;

		bodyOffset =
		    ((const SlipRaceCollisionBody *)(const void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset))
		        ->previousBodyOffset;
		if (bodyOffset == SlipRaceCollision_activeBodyOffset) {
			break;
		}
		body = SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
		if (((const SlipRaceCollisionBody *)(const void *)body)->flags == 0) {
			continue;
		}
		candidateObject = ((const SlipRaceCollisionBody *)(const void *)body)->objectHandle;
		if (candidateObject == excludedObject) {
			continue;
		}
		if (!SlipObject_Position(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, candidateObject,
		                         &objectPosition) ||
		    !SlipObject_MatrixCopy(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, candidateObject,
		                           &objectMatrix, &matrixCopy)) {
			continue;
		}
		sourceBounds =
		    (SlipRaceCollisionBounds){(int32_t)((const SlipRaceCollisionBody *)(const void *)body)->minimumBounds.x,
		                              (int32_t)((const SlipRaceCollisionBody *)(const void *)body)->minimumBounds.y,
		                              (int32_t)((const SlipRaceCollisionBody *)(const void *)body)->minimumBounds.z,
		                              (int32_t)((const SlipRaceCollisionBody *)(const void *)body)->maximumBounds.x,
		                              (int32_t)((const SlipRaceCollisionBody *)(const void *)body)->maximumBounds.y,
		                              (int32_t)((const SlipRaceCollisionBody *)(const void *)body)->maximumBounds.z};
		localOrigin = SlipView3D_TransformPositionByRows(
		    &objectMatrix, (SlipView3DVec32){(int32_t)((uint32_t)segmentStart.x - objectPosition.positionX),
		                                     (int32_t)((uint32_t)segmentStart.y - objectPosition.positionY),
		                                     (int32_t)((uint32_t)segmentStart.z - objectPosition.positionZ)});
		localDirection =
		    SlipView3D_TransformVector(&objectMatrix, (SlipView3DVec32){direction.x, direction.y, direction.z});

		SlipRaceCollision_TestSegmentFace(
		    &sourceBounds, localOrigin,
		    (SlipView3DVec16){(int16_t)localDirection.x, (int16_t)localDirection.y, (int16_t)localDirection.z}, 0u,
		    false, 2u, &bestDistance, candidateObject, &bestObject);
		SlipRaceCollision_TestSegmentFace(
		    &sourceBounds, localOrigin,
		    (SlipView3DVec16){(int16_t)localDirection.x, (int16_t)localDirection.y, (int16_t)localDirection.z}, 0u,
		    true, 0u, &bestDistance, candidateObject, &bestObject);
		SlipRaceCollision_TestSegmentFace(
		    &sourceBounds, localOrigin,
		    (SlipView3DVec16){(int16_t)localDirection.x, (int16_t)localDirection.y, (int16_t)localDirection.z}, 1u,
		    false, 1u, &bestDistance, candidateObject, &bestObject);
		SlipRaceCollision_TestSegmentFace(
		    &sourceBounds, localOrigin,
		    (SlipView3DVec16){(int16_t)localDirection.x, (int16_t)localDirection.y, (int16_t)localDirection.z}, 1u,
		    true, 1u, &bestDistance, candidateObject, &bestObject);
		SlipRaceCollision_TestSegmentFace(
		    &sourceBounds, localOrigin,
		    (SlipView3DVec16){(int16_t)localDirection.x, (int16_t)localDirection.y, (int16_t)localDirection.z}, 2u,
		    false, 2u, &bestDistance, candidateObject, &bestObject);
		SlipRaceCollision_TestSegmentFace(
		    &sourceBounds, localOrigin,
		    (SlipView3DVec16){(int16_t)localDirection.x, (int16_t)localDirection.y, (int16_t)localDirection.z}, 2u,
		    true, 2u, &bestDistance, candidateObject, &bestObject);
	}

	if (SlipRaceCollision_segmentQuery != NULL) {
		SlipRaceCollision_segmentQuery(trdBase, trackDataSize, trackDataAddress, componentBase, componentBytes,
		                               cellTableBase, cellTableBytes, segmentStart, segmentEnd, &trackHit);
		if (bestDistance == UINT32_MAX ||
		    SlipView3D_VectorLength((int32_t)((uint32_t)trackHit.outputPosition.x - (uint32_t)segmentStart.x),
		                            (int32_t)((uint32_t)trackHit.outputPosition.y - (uint32_t)segmentStart.y),
		                            (int32_t)((uint32_t)trackHit.outputPosition.z - (uint32_t)segmentStart.z)) <=
		        bestDistance) {
			return (SlipRaceCollisionSegmentHit){trackHit.outputPosition, 0};
		}
	}
	if (bestDistance == UINT32_MAX) {
		return (SlipRaceCollisionSegmentHit){segmentEnd, 0};
	}
	result.point = SlipView3D_ScaleVector(direction.x, direction.y, direction.z, (int32_t)bestDistance);
	result.point.x = (int32_t)((uint32_t)result.point.x + (uint32_t)segmentStart.x);
	result.point.y = (int32_t)((uint32_t)result.point.y + (uint32_t)segmentStart.y);
	result.point.z = (int32_t)((uint32_t)result.point.z + (uint32_t)segmentStart.z);
	result.objectHandle = bestObject;
	return result;
}

void SlipRaceCollision_FindNearestBody(uint32_t maximumDistance, uint16_t angle, int32_t minimumZ, int16_t offsetX,
                                       int16_t offsetY, int16_t offsetZ, uint16_t object, const SlipView3DMaths *maths,
                                       SlipRaceCollisionNearestBody *result) {
	const uint16_t sineQ14 = (uint16_t)SlipView3D_SinQ14(maths, (int16_t)angle);
	const uint16_t cosineQ14 = (uint16_t)SlipView3D_CosQ14(maths, (int16_t)angle);
	uint32_t expandedMaximumDistance = maximumDistance + (maximumDistance >> SLIP_COLLISION_SEARCH_MARGIN_SHIFT);
	SlipView3DMatrix objectTransformMatrix;
	SlipView3DMatrix collisionProjectionMatrix;
	SlipObjectMatrixCopy matrixCopy;
	SlipObjectPosition objectPosition;
	SlipView3DVec32 transformedOffset;
	SlipView3DVec32 origin;
	uint32_t bestDistance = INT32_MAX;
	uint16_t selectedObject = 0;
	uint32_t bodyOffset = SlipRaceCollision_activeBodyOffset;

	if (expandedMaximumDistance < maximumDistance && (int32_t)expandedMaximumDistance < 0) {
		expandedMaximumDistance = INT32_MAX;
	}
	SlipObject_MatrixCopy(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, object,
	                      &objectTransformMatrix, &matrixCopy);
	SlipView3D_CopyMatrixWords((uint8_t *)&collisionProjectionMatrix, sizeof(collisionProjectionMatrix),
	                           (const uint8_t *)&objectTransformMatrix, sizeof(objectTransformMatrix));
	SlipObject_Position(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, object, &objectPosition);
	transformedOffset =
	    SlipView3D_TransformPosition16(&collisionProjectionMatrix, (SlipView3DVec32){offsetX, offsetY, offsetZ});
	origin = (SlipView3DVec32){(int32_t)(objectPosition.positionX + (uint32_t)transformedOffset.x),
	                           (int32_t)(objectPosition.positionY + (uint32_t)transformedOffset.y),
	                           (int32_t)(objectPosition.positionZ + (uint32_t)transformedOffset.z)};

	for (;;) {
		uint8_t *body;
		uint16_t candidateObject;
		SlipView3DVec32 relative;
		SlipDraw3DApproxAbsVectorLength approximate;
		SlipView3DVec32 transformed;
		int32_t radius;
		int32_t depthConeExtent;
		int32_t projectedLateralCoordinate;
		uint32_t candidateDistance;

		bodyOffset =
		    ((const SlipRaceCollisionBody *)(const void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset))
		        ->nextBodyOffset;
		if (bodyOffset == SlipRaceCollision_activeBodyOffset) {
			break;
		}
		body = SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
		if ((((const SlipRaceCollisionBody *)(const void *)body)->flags & SLIP_COLLISION_BODY_TARGETING_ENABLED) == 0) {
			continue;
		}
		candidateObject = ((const SlipRaceCollisionBody *)(const void *)body)->objectHandle;
		if (candidateObject == object) {
			continue;
		}
		SlipObject_Position(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, candidateObject,
		                    &objectPosition);
		relative = (SlipView3DVec32){(int32_t)(objectPosition.positionX - (uint32_t)origin.x),
		                             (int32_t)(objectPosition.positionY - (uint32_t)origin.y),
		                             (int32_t)(objectPosition.positionZ - (uint32_t)origin.z)};
		SlipDraw3D_ApproxAbsVectorLength((uint32_t)relative.x, (uint32_t)relative.y, (uint32_t)relative.z,
		                                 &approximate);
		if ((int32_t)approximate.approximateLength > (int32_t)expandedMaximumDistance) {
			continue;
		}
		transformed = SlipView3D_TransformPositionByRows(&collisionProjectionMatrix, relative);
		radius = (int32_t)((const SlipRaceCollisionBody *)(const void *)body)->radius;
		if ((int32_t)((uint32_t)transformed.z + (uint32_t)radius) < minimumZ) {
			continue;
		}
		depthConeExtent = SlipRaceCollision_ScaleDirectionQ14((int16_t)sineQ14, transformed.z);
		projectedLateralCoordinate = SlipRaceCollision_ScaleDirectionQ14((int16_t)cosineQ14, transformed.x);
		if ((int32_t)((uint32_t)projectedLateralCoordinate + (uint32_t)depthConeExtent + (uint32_t)radius) < 0) {
			continue;
		}
		if ((int32_t)(0u - (uint32_t)projectedLateralCoordinate + (uint32_t)depthConeExtent + (uint32_t)radius) < 0) {
			continue;
		}
		projectedLateralCoordinate = SlipRaceCollision_ScaleDirectionQ14((int16_t)cosineQ14, transformed.y);
		if ((int32_t)((uint32_t)projectedLateralCoordinate + (uint32_t)depthConeExtent + (uint32_t)radius) < 0) {
			continue;
		}
		if ((int32_t)(0u - (uint32_t)projectedLateralCoordinate + (uint32_t)depthConeExtent + (uint32_t)radius) < 0) {
			continue;
		}
		SlipObject_Position(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, candidateObject,
		                    &objectPosition);
		candidateDistance = SlipView3D_VectorLength((int32_t)(objectPosition.positionX - (uint32_t)origin.x),
		                                            (int32_t)(objectPosition.positionY - (uint32_t)origin.y),
		                                            (int32_t)(objectPosition.positionZ - (uint32_t)origin.z));
		if ((int32_t)candidateDistance > (int32_t)maximumDistance ||
		    (int32_t)candidateDistance >= (int32_t)bestDistance) {
			continue;
		}
		if (SlipRaceCollision_lineOfSight != 0 && SlipRaceCollision_lineOfSight(candidateObject, object)) {
			continue;
		}
		bestDistance = candidateDistance;
		selectedObject = candidateObject;
	}

	if (bestDistance == INT32_MAX) {
		*result = (SlipRaceCollisionNearestBody){0, true};
		return;
	}
	*result = (SlipRaceCollisionNearestBody){selectedObject, false};
}

bool SlipRaceCollision_AllocateBody(SlipRaceCollisionBody **allocatedBody) {
	SlipRaceCollisionBody *const freeHead =
	    (SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(SlipRaceCollision_freeBodyOffset);
	const uint32_t bodyOffset = freeHead->nextBodyOffset;
	SlipRaceCollisionBody *const body =
	    (SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
	*allocatedBody = body;
	if (bodyOffset == SlipRaceCollision_freeBodyOffset)
		return true;
	const uint32_t nextOffset = body->nextBodyOffset;
	freeHead->nextBodyOffset = nextOffset;
	((SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(nextOffset))->previousBodyOffset =
	    SlipRaceCollision_freeBodyOffset;
	SlipRaceCollisionBody *const activeHead =
	    (SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(SlipRaceCollision_activeBodyOffset);
	const uint32_t activeNextOffset = activeHead->nextBodyOffset;
	activeHead->nextBodyOffset = bodyOffset;
	((SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(activeNextOffset))->previousBodyOffset =
	    bodyOffset;
	body->nextBodyOffset = activeNextOffset;
	body->previousBodyOffset = SlipRaceCollision_activeBodyOffset;
	return false;
}

bool SlipRaceCollision_CreateBody(uint16_t object, uint16_t flags) {
	SlipRaceCollisionBody *body;
	bool exhausted = SlipRaceCollision_AllocateBody(&body);
	body->flags = flags;
	if (exhausted)
		return true;
	body->objectHandle = object;
	body->radius = 0;
	body->minimumBounds.x = 0;
	body->maximumBounds.x = 0;
	body->minimumBounds.y = 0;
	body->maximumBounds.y = 0;
	body->minimumBounds.z = 0;
	body->maximumBounds.z = 0;
	body->excludedBodyOffset = 0;
	const uint16_t offset = (uint16_t)((uint8_t *)(void *)body - SlipRaceCollision_physicsTable);
	SlipObject_SetPhysicsOffset(SlipRaceCollision_objectTable, object, offset);
	return false;
}

bool SlipRaceCollision_ExcludePair(uint16_t firstObject, uint16_t secondObject) {
	const uint16_t firstOffset = SlipObject_PhysicsOffset(SlipRaceCollision_objectTable, firstObject);
	if (firstOffset == 0)
		return true;
	SlipRaceCollisionBody *const firstBody =
	    (SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(firstOffset);
	const uint16_t secondOffset = SlipObject_PhysicsOffset(SlipRaceCollision_objectTable, secondObject);
	if (secondOffset == 0)
		return true;
	SlipRaceCollisionBody *const secondBody =
	    (SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(secondOffset);
	firstBody->excludedBodyOffset = secondOffset;
	secondBody->excludedBodyOffset = firstOffset;
	return false;
}

void SlipRaceCollision_SetBodyFlags(uint16_t object, uint16_t flags) {
	const uint16_t offset = SlipObject_PhysicsOffset(SlipRaceCollision_objectTable, object);
	if (offset != 0)
		((SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(offset))->flags = flags;
}

SlipView3DVec16 SlipRaceCollision_ReflectDirection(uint16_t object, SlipView3DVec16 normal) {
	static SlipView3DVec16 reflectionNormal;
	reflectionNormal = normal;
	SlipObjectDirection direction = SlipObject_Direction(SlipRaceCollision_objectTable, object);
	const int16_t negativeX = (int16_t)(0u - (uint16_t)direction.directionXQ14);
	const int16_t negativeY = (int16_t)(0u - (uint16_t)direction.directionYQ14);
	const int16_t negativeZ = (int16_t)(0u - (uint16_t)direction.directionZQ14);
	const int32_t dot = (int16_t)SlipView3D_DotProductQ14((uint16_t)negativeX, (uint16_t)negativeY, (uint16_t)negativeZ,
	                                                      (uint16_t)reflectionNormal.x, (uint16_t)reflectionNormal.y,
	                                                      (uint16_t)reflectionNormal.z, NULL);
	const int32_t z = (int32_t)(((int64_t)reflectionNormal.z * 2 * dot) >> SLIP_Q14_FRACTION_BITS) - negativeZ;
	const int32_t y = (int32_t)(((int64_t)reflectionNormal.y * 2 * dot) >> SLIP_Q14_FRACTION_BITS) - negativeY;
	const int32_t x = (int32_t)(((int64_t)reflectionNormal.x * 2 * dot) >> SLIP_Q14_FRACTION_BITS) - negativeX;
	SlipView3DNormalizeVector3D result;
	(void)SlipView3D_NormalizeVector3D((uint32_t)x, (uint32_t)y, (uint32_t)z, &result);
	return (SlipView3DVec16){(int16_t)result.unitXQ14, (int16_t)result.unitYQ14, (int16_t)result.unitZQ14};
}

uint16_t SlipRaceCollision_BodyFlags(uint16_t object) {
	const uint16_t offset = SlipObject_PhysicsOffset(SlipRaceCollision_objectTable, object);
	if (offset == 0)
		return 0;
	return ((SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(offset))->flags;
}

uint16_t SlipRaceCollision_BodyProperty(uint16_t object, uint16_t fallback) {
	const uint16_t offset = SlipObject_PhysicsOffset(SlipRaceCollision_objectTable, object);
	if (offset == 0)
		return fallback;
	return ((SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(offset))->flags;
}

void SlipRaceCollision_SetBodyBounds(uint16_t object, int32_t minX, int32_t minY, int32_t minZ, int32_t maxX,
                                     int32_t maxY, int32_t maxZ) {
	const uint16_t offset = SlipObject_PhysicsOffset(SlipRaceCollision_objectTable, object);
	if (offset == 0)
		SlipRuntime_Fatal("CollideSlotSetMainCube - not a collide slot");
	SlipRaceCollisionBody *const body =
	    (SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(offset);
	body->minimumBounds.x = minX;
	body->minimumBounds.y = minY;
	body->minimumBounds.z = minZ;
	body->maximumBounds.x = maxX;
	body->maximumBounds.y = maxY;
	body->maximumBounds.z = maxZ;
	body->radius = SlipView3D_BoxRadius(minX, minY, minZ, maxX, maxY, maxZ);
}

SlipRaceCollisionBodyBounds SlipRaceCollision_GetBodyBounds(uint16_t object) {
	const uint16_t offset = SlipObject_PhysicsOffset(SlipRaceCollision_objectTable, object);
	if (offset == 0)
		SlipRuntime_Fatal("CollideSlotSetMainCube - not a collide slot");
	const SlipRaceCollisionBody *const body =
	    (const SlipRaceCollisionBody *)(const void *)SlipRaceCollision_CollisionBodyFromOffset(offset);
	return (SlipRaceCollisionBodyBounds){body->minimumBounds.x, body->minimumBounds.y, body->minimumBounds.z,
	                                     body->maximumBounds.x, body->maximumBounds.y, body->maximumBounds.z};
}

void SlipRaceCollision_InitializeBodyLists(void) {
	SlipRaceCollisionBody *const activeHead =
	    (SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(SlipRaceCollision_activeBodyOffset);
	uint32_t remaining = SlipRaceCollision_bodyCount;
	uint32_t currentOffset = SlipRaceCollision_freeBodyOffset;
	activeHead->nextBodyOffset = SlipRaceCollision_activeBodyOffset;
	activeHead->previousBodyOffset = SlipRaceCollision_activeBodyOffset;
	do {
		const uint32_t nextOffset = currentOffset + SLIP_COLLISION_BODY_BYTES;
		((SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(currentOffset))->nextBodyOffset =
		    nextOffset;
		((SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(nextOffset))->previousBodyOffset =
		    currentOffset;
		currentOffset = nextOffset;
		--remaining;
	} while (remaining != 0);
	((SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(currentOffset))->nextBodyOffset =
	    SlipRaceCollision_freeBodyOffset;
	((SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(SlipRaceCollision_freeBodyOffset))
	    ->previousBodyOffset = currentOffset;
}

void SlipRaceCollision_AdvanceUncollidableObjects(void) {
	uint16_t object = UINT16_MAX;

	for (;;) {
		SlipView3DVec32 position;
		SlipObjectSetPosition setPosition;

		object = SlipObject_Next(object);
		if (object == UINT16_MAX)
			return;
		if (SlipObject_PhysicsOffset(SlipRaceCollision_objectTable, object) != 0) {
			continue;
		}
		if (SlipObject_Speed(SlipRaceCollision_objectTable, object) == 0) {
			continue;
		}
		position = SlipObject_ExtrapolatedPosition(object);
		SlipObject_SetPosition(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, object,
		                       (uint32_t)position.x, (uint32_t)position.y, (uint32_t)position.z, &setPosition);
	}
}

bool SlipRaceCollision_RemoveBody(uint16_t object) {
	if (SlipRaceCollision_enabled == 0)
		return true;
	const uint16_t offset = SlipObject_PhysicsOffset(SlipRaceCollision_objectTable, object);
	if (offset == 0)
		return true;
	SlipObject_SetPhysicsOffset(SlipRaceCollision_objectTable, object, 0);
	SlipRaceCollisionBody *const body =
	    (SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(offset);
	if (body->excludedBodyOffset != 0)
		body->excludedBodyOffset = 0;
	const uint32_t nextOffset = body->nextBodyOffset;
	const uint32_t previousOffset = body->previousBodyOffset;
	((SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(previousOffset))->nextBodyOffset =
	    nextOffset;
	((SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(nextOffset))->previousBodyOffset =
	    previousOffset;
	SlipRaceCollisionBody *const freeHead =
	    (SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(SlipRaceCollision_freeBodyOffset);
	const uint32_t freeNextOffset = freeHead->nextBodyOffset;
	freeHead->nextBodyOffset = offset;
	((SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(freeNextOffset))->previousBodyOffset =
	    offset;
	body->nextBodyOffset = freeNextOffset;
	body->previousBodyOffset = SlipRaceCollision_freeBodyOffset;
	return false;
}

void SlipRaceCollision_Shutdown(void) {
	if (SlipRaceCollision_enabled != 0)
		SlipRaceCollision_enabled = 0;
}

uint32_t SlipRaceCollision_RemoveBodyIfFlagged(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                               uint32_t eventFlags, uint16_t object, uintptr_t dispatchData,
                                               uint32_t dispatchFrame) {
	(void)eventPayload;
	(void)eventValue;
	(void)eventFlags;
	(void)dispatchData;
	(void)dispatchFrame;
	if (object != 0 && ((uint16_t)eventCode & SLIP_OBJECT_SERVER_EVENT_FREE) != 0) {
		SlipRaceCollision_RemoveBody(object);
	}
	return eventCode;
}

void SlipRaceCollision_ClearBodyContacts(void) {
	uint32_t bodyOffset = SlipRaceCollision_activeBodyOffset;

	for (;;) {
		bodyOffset =
		    ((const SlipRaceCollisionBody *)(const void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset))
		        ->nextBodyOffset;
		if (bodyOffset == SlipRaceCollision_activeBodyOffset)
			return;
		((SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset))->repeatedContacts = 0;
	}
}

void SlipRaceCollision_PrepareBodies(void) {
	uint32_t bodyOffset = SlipRaceCollision_activeBodyOffset;

	for (;;) {
		SlipRaceCollisionBody *body;
		uint16_t object;
		uint32_t speed;
		uint64_t intersectionTimeProduct;

		bodyOffset =
		    ((const SlipRaceCollisionBody *)(const void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset))
		        ->nextBodyOffset;
		if (bodyOffset == SlipRaceCollision_activeBodyOffset)
			return;
		body = (SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
		body->contactType = SLIP_COLLISION_CONTACT_NONE;
		body->contactTime = SLIP_COLLISION_NO_CONTACT_TIME;
		object = body->objectHandle;
		speed = (uint32_t)SlipObject_Speed(SlipRaceCollision_objectTable, object);
		intersectionTimeProduct = (uint64_t)SlipRaceCollision_frameStep * speed;
		body->movementDistance = (uint32_t)(intersectionTimeProduct >> SLIP_Q14_FRACTION_BITS);
	}
}

void SlipRaceCollision_IntegrateBodies(void) {
	const int32_t time = (int16_t)SlipRaceCollision_firstTime;
	uint32_t bodyOffset;

	if (time == 0)
		return;
	if (time == -1) {
		SlipRaceCollision_integratedStep = SlipRaceCollision_frameStep;
		SlipRaceCollision_frameStep = 0;
	} else {
		SlipRaceCollision_integratedStep = (uint32_t)time;
		SlipRaceCollision_frameStep -= (uint32_t)time;
	}
	bodyOffset = SlipRaceCollision_activeBodyOffset;
	for (;;) {
		const SlipRaceCollisionBody *body;
		uint16_t object;
		uint32_t speed;
		uint64_t intersectionTimeProduct;
		int32_t distance;
		SlipObjectPosition objectPosition;
		SlipObjectDirection direction;
		SlipView3DVec32 scaled;
		SlipObjectSetPosition setPosition;

		body = (const SlipRaceCollisionBody *)(const void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
		bodyOffset = body->nextBodyOffset;
		if (bodyOffset == SlipRaceCollision_activeBodyOffset)
			return;
		body = (const SlipRaceCollisionBody *)(const void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
		object = body->objectHandle;
		speed = (uint32_t)SlipObject_Speed(SlipRaceCollision_objectTable, object);
		if (speed == 0)
			continue;
		intersectionTimeProduct = (uint64_t)SlipRaceCollision_integratedStep * speed;
		distance = (int32_t)(intersectionTimeProduct >> SLIP_Q14_FRACTION_BITS);
		SlipObject_Position(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, object, &objectPosition);
		direction = SlipObject_Direction(SlipRaceCollision_objectTable, object);
		scaled =
		    SlipView3D_ScaleVector(direction.directionXQ14, direction.directionYQ14, direction.directionZQ14, distance);
		SlipObject_SetPosition(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, object,
		                       objectPosition.positionX + (uint32_t)scaled.x,
		                       objectPosition.positionY + (uint32_t)scaled.y,
		                       objectPosition.positionZ + (uint32_t)scaled.z, &setPosition);
	}
}

void SlipRaceCollision_FindBodyCollisions(void) {
	uint32_t bodyOffset = SlipRaceCollision_activeBodyOffset;
	const SlipRaceCollisionBody *body;

	SlipRaceCollision_firstTime = SLIP_COLLISION_NO_CONTACT_TIME;
	for (;;) {
		if (SlipRaceCollision_firstTime == 0)
			return;
		body = (const SlipRaceCollisionBody *)(const void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
		bodyOffset = body->previousBodyOffset;
		if (bodyOffset == SlipRaceCollision_activeBodyOffset)
			return;
		body = (const SlipRaceCollisionBody *)(const void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
		if ((body->flags & SLIP_COLLISION_BODY_CONTACTS_ENABLED) == 0) {
			continue;
		}
		SlipRaceCollision_sourceBody = SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
		SlipRaceCollision_excludedBodyOffset = body->excludedBodyOffset;
		for (;;) {
			uint16_t flags;

			if (SlipRaceCollision_firstTime == 0)
				return;
			body = (const SlipRaceCollisionBody *)(const void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
			bodyOffset = body->previousBodyOffset;
			if (bodyOffset == SlipRaceCollision_activeBodyOffset) {
				bodyOffset = SlipRaceCollision_CollisionBodyOffset(SlipRaceCollision_sourceBody);
				break;
			}
			body = (const SlipRaceCollisionBody *)(const void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
			if ((body->flags & SLIP_COLLISION_BODY_CONTACTS_ENABLED) == 0) {
				continue;
			}
			if (bodyOffset == SlipRaceCollision_excludedBodyOffset)
				continue;
			SlipRaceCollision_targetBody = SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
			flags = body->flags;

			flags &= body->flags;
			if ((flags & SLIP_COLLISION_BODY_CONTACTS_ENABLED) == 0) {
				SlipRuntime_Fatal("Extent Collide not allowed");
			}
			if (SlipRaceCollision_TestBodies()) {
				SlipRaceCollision_RecordContact(0);
			}
			bodyOffset = SlipRaceCollision_CollisionBodyOffset(SlipRaceCollision_targetBody);
		}
	}
}

void SlipRaceCollision_DispatchBodyEvents(uint32_t contactTimeWord, uint32_t eventValue, uint32_t eventFlags,
                                          uint32_t contactTypeWord) {
	uint32_t bodyOffset = SlipRaceCollision_activeBodyOffset;
	uint32_t nextBodyOffset;

	SlipObject_BeginDeferredSection();
	bodyOffset =
	    ((SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset))->nextBodyOffset;
	for (;;) {
		SlipRaceCollisionBody *const body =
		    (SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);

		nextBodyOffset = body->nextBodyOffset;
		if (bodyOffset == SlipRaceCollision_activeBodyOffset)
			break;
		contactTimeWord = (contactTimeWord & SLIP_COLLISION_PACKED_UPPER_WORD_MASK) | body->contactTime;
		if ((uint16_t)contactTimeWord == SlipRaceCollision_firstTime) {
			contactTypeWord = (contactTypeWord & SLIP_COLLISION_PACKED_UPPER_WORD_MASK) | body->contactType;
			if ((uint16_t)contactTypeWord == SLIP_COLLISION_CONTACT_TRACK) {
				SlipRaceCollision_DispatchTrackContact(bodyOffset, eventFlags);
			} else if ((uint16_t)contactTypeWord == SLIP_COLLISION_CONTACT_BODY) {
				SlipRaceCollision_DispatchBodyContact(bodyOffset, eventFlags);
			}
		}
		bodyOffset = nextBodyOffset;
	}
	SlipObject_EndDeferredSection(contactTimeWord, nextBodyOffset, eventValue, eventFlags, bodyOffset, contactTypeWord);
}

void SlipRaceCollision_DispatchBodyContact(uint32_t bodyOffset, uint32_t eventFlags) {
	SlipRaceCollisionBody *const body =
	    (SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
	const uint16_t object = body->objectHandle;
	uint32_t otherObject;
	uint32_t eventResult;

	SlipRaceCollision_stopEvent.normalX = (int16_t)body->normalX;
	SlipRaceCollision_stopEvent.normalY = (int16_t)body->normalY;
	SlipRaceCollision_stopEvent.normalZ = (int16_t)body->normalZ;
	SlipRaceCollision_stopEvent.impactMagnitude = (int32_t)body->impactMagnitude;
	SlipRaceCollision_stopEvent.contactPosition.x = (int32_t)body->contactPosition.x;
	SlipRaceCollision_stopEvent.contactPosition.y = (int32_t)body->contactPosition.y;
	SlipRaceCollision_stopEvent.contactPosition.z = (int32_t)body->contactPosition.z;
	SlipRaceCollision_stopEvent.impactFlag = body->impactFlag;

	otherObject = body->otherObject;
	eventResult = SlipObject_DispatchEvent(
	    object, (body->impactFlag & SLIP_OBJECT_EVENT_UPPER_WORD_MASK) | SLIP_OBJECT_EVENT_COLLISION_STOP, otherObject,
	    SLIP_COLLISION_STOP_EVENT_TOKEN, eventFlags, bodyOffset, SLIP_COLLISION_STOP_EVENT_TOKEN);
	if ((uint16_t)eventResult != 0) {
		SlipObject_Stop(object, eventResult, otherObject, SLIP_COLLISION_STOP_EVENT_TOKEN, eventFlags, bodyOffset,
		                SLIP_COLLISION_STOP_EVENT_TOKEN);
	}
}

void SlipRaceCollision_DispatchTrackContact(uint32_t bodyOffset, uint32_t eventFlags) {
	SlipRaceCollisionBody *const body =
	    (SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
	const uint16_t object = body->objectHandle;
	uint32_t contactZ;
	uint32_t eventResult;

	SlipRaceCollision_bounceEvent.normalX = (int16_t)body->normalX;
	SlipRaceCollision_bounceEvent.normalY = (int16_t)body->normalY;
	SlipRaceCollision_bounceEvent.normalZ = (int16_t)body->normalZ;
	SlipRaceCollision_bounceEvent.material = (int16_t)body->material;
	SlipRaceCollision_bounceEvent.contactPosition.x = (int32_t)body->contactPosition.x;
	SlipRaceCollision_bounceEvent.contactPosition.y = (int32_t)body->contactPosition.y;
	contactZ = body->contactPosition.z;
	SlipRaceCollision_bounceEvent.contactPosition.z = (int32_t)contactZ;
	eventResult = SlipObject_DispatchEvent(
	    object,
	    ((uint32_t)body->contactPosition.x & SLIP_OBJECT_EVENT_UPPER_WORD_MASK) | SLIP_OBJECT_EVENT_COLLISION_BOUNCE,
	    SLIP_COLLISION_BOUNCE_EVENT_TOKEN, contactZ, eventFlags, bodyOffset, SLIP_COLLISION_BOUNCE_EVENT_TOKEN);
	if ((uint16_t)eventResult != 0) {
		SlipObject_Stop(object, eventResult, SLIP_COLLISION_BOUNCE_EVENT_TOKEN, contactZ, eventFlags, bodyOffset,
		                SLIP_COLLISION_BOUNCE_EVENT_TOKEN);
	}
}

void SlipRaceCollision_PrepareRemainingStep(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                            uint32_t eventFlags, uint32_t dispatchFrame) {
	uint32_t bodyOffset = SlipRaceCollision_activeBodyOffset;

	for (;;) {
		SlipRaceCollisionBody *body;
		int16_t bodyCounter;
		uint16_t object;

		body = (SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
		bodyOffset = body->nextBodyOffset;
		if (bodyOffset == SlipRaceCollision_activeBodyOffset)
			return;
		body = (SlipRaceCollisionBody *)(void *)SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
		if (body->contactTime != 0)
			continue;
		bodyCounter = body->repeatedContacts;
		if (body->impactFlag != 0) {
			if (bodyCounter < SLIP_COLLISION_REPEATED_IMPACT_LIMIT) {
				body->repeatedContacts = (int16_t)((uint16_t)bodyCounter + 1u);
				continue;
			}
		} else {
			bodyCounter = (int16_t)((uint16_t)bodyCounter + 1u);
			body->repeatedContacts = bodyCounter;
			if (bodyCounter < SLIP_COLLISION_REPEATED_CONTACT_LIMIT)
				continue;
		}
		object = body->objectHandle;
		SlipObject_Stop(object, eventCode, eventPayload, eventValue, eventFlags, bodyOffset, dispatchFrame);
		body->repeatedContacts = 0;
	}
}

void SlipRaceCollision_FinalizeBodyCollisions(void) {
	uint32_t bodyOffset;
	SlipRaceCollisionBody *firstBody;
	SlipRaceCollisionBody *secondBody;

	if (SlipRaceCollision_firstTime == SLIP_COLLISION_NO_CONTACT_TIME)
		return;
	SlipRaceCollision_sourceBody = SlipRaceCollision_CollisionBodyFromOffset(SlipRaceCollision_activeBodyOffset);
	for (;;) {
		uint16_t collisionType;
		uint16_t otherObject;
		uint32_t otherBodyOffset;
		uint32_t savedMovementDistance;
		bool contactFound;

		firstBody = (SlipRaceCollisionBody *)(void *)SlipRaceCollision_sourceBody;
		bodyOffset = firstBody->nextBodyOffset;
		if (bodyOffset == SlipRaceCollision_activeBodyOffset)
			return;
		SlipRaceCollision_sourceBody = SlipRaceCollision_CollisionBodyFromOffset(bodyOffset);
		firstBody = (SlipRaceCollisionBody *)(void *)SlipRaceCollision_sourceBody;
		if (firstBody->contactTime != SlipRaceCollision_firstTime) {
			continue;
		}
		collisionType = firstBody->contactType;
		if (collisionType == SLIP_COLLISION_CONTACT_TRACK) {
			firstBody->impactFlag = UINT32_MAX;
		}
		if (collisionType != SLIP_COLLISION_CONTACT_BODY)
			continue;
		otherObject = (uint16_t)firstBody->otherObject;
		otherBodyOffset = SlipObject_PhysicsOffset(SlipRaceCollision_objectTable, otherObject);
		if (otherBodyOffset <= SlipRaceCollision_CollisionBodyOffset(SlipRaceCollision_sourceBody)) {
			continue;
		}
		SlipRaceCollision_targetBody = SlipRaceCollision_CollisionBodyFromOffset(otherBodyOffset);
		secondBody = (SlipRaceCollisionBody *)(void *)SlipRaceCollision_targetBody;
		if (SlipObject_Speed(SlipRaceCollision_objectTable, firstBody->objectHandle) == 0) {
			secondBody->impactFlag = UINT32_MAX;
			firstBody->impactFlag = 0;
			continue;
		}
		if (SlipObject_Speed(SlipRaceCollision_objectTable, secondBody->objectHandle) == 0) {
			firstBody->impactFlag = UINT32_MAX;
			secondBody->impactFlag = 0;
			continue;
		}
		secondBody->impactFlag = 0;
		savedMovementDistance = firstBody->movementDistance;
		firstBody->movementDistance = 0;
		contactFound = SlipRaceCollision_TestBodies();
		firstBody = (SlipRaceCollisionBody *)(void *)SlipRaceCollision_sourceBody;
		secondBody = (SlipRaceCollisionBody *)(void *)SlipRaceCollision_targetBody;
		if (contactFound) {
			secondBody->impactFlag = UINT32_MAX;
		}
		firstBody->movementDistance = savedMovementDistance;
		firstBody->impactFlag = 0;
		savedMovementDistance = secondBody->movementDistance;
		secondBody->movementDistance = 0;
		contactFound = SlipRaceCollision_TestBodies();
		firstBody = (SlipRaceCollisionBody *)(void *)SlipRaceCollision_sourceBody;
		secondBody = (SlipRaceCollisionBody *)(void *)SlipRaceCollision_targetBody;
		if (contactFound) {
			firstBody->impactFlag = UINT32_MAX;
		}
		secondBody->movementDistance = savedMovementDistance;
		if ((firstBody->impactFlag | secondBody->impactFlag) == 0) {
			firstBody->impactFlag = UINT32_MAX;
			secondBody->impactFlag = UINT32_MAX;
		}
	}
}

static int32_t SlipRaceCollision_ScaleDifferenceQ30(int32_t value, uint32_t fraction) {
	const int64_t product = (int64_t)value * (int64_t)(int32_t)fraction;
	return (int32_t)((uint64_t)product >> SLIP_COLLISION_RECIPROCAL_FRACTION_BITS);
}

static int32_t SlipRaceCollision_ScaleDirectionQ14(int16_t direction, int32_t magnitude) {
	const int64_t product = (int64_t)direction * magnitude;
	return (int32_t)((uint64_t)product >> SLIP_Q14_FRACTION_BITS);
}

void SlipRaceCollision_CopyVertex(const SlipRaceCollisionCopySource *source, SlipRaceCollisionVertex *destination) {
	destination->x = source->x;
	destination->y = source->y;
	destination->z = source->z;
	destination->time = source->time;
	destination->outcode = source->outcode;
}

void SlipRaceCollision_SelectContact(void) {
	const SlipRaceCollisionVertex *vertex = SlipRaceCollision_activeList;

	if (vertex != NULL) {
		do {
			const int32_t time = vertex->time;
			if (time < 0) {
				if (time > -SLIP_COLLISION_CONTACT_PENETRATION_TIME_TOLERANCE) {
					SlipRaceCollision_contact.time = 0;
					return;
				}
			} else if (time <= SlipRaceCollision_contact.time) {
				SlipRaceCollision_contact.time = time;
				SlipRaceCollision_contact.x = vertex->x;
				SlipRaceCollision_contact.y = vertex->y;
				SlipRaceCollision_contact.z = vertex->z;
			}
			vertex = vertex->next;
		} while (vertex != SlipRaceCollision_activeList);
	}
}

void SlipRaceCollision_BuildSourceMotion(void) {
	const SlipRaceCollisionBody *const body = (const SlipRaceCollisionBody *)(const void *)SlipRaceCollision_sourceBody;
	const uint16_t objectHandle = body->objectHandle;
	SlipObjectDirection direction;
	SlipRaceCollisionSourceMotion *const motion = &SlipRaceCollision_sourceMotion;

	motion->movementDistance = (int32_t)body->movementDistance;
	direction = SlipObject_Direction(SlipRaceCollision_objectTable, objectHandle);
	motion->directionX = direction.directionXQ14;
	motion->directionY = direction.directionYQ14;
	motion->directionZ = direction.directionZQ14;
	motion->displacementX = SlipRaceCollision_ScaleDirectionQ14(motion->directionX, motion->movementDistance);
	motion->displacementY = SlipRaceCollision_ScaleDirectionQ14(motion->directionY, motion->movementDistance);
	motion->displacementZ = SlipRaceCollision_ScaleDirectionQ14(motion->directionZ, motion->movementDistance);
}

void SlipRaceCollision_BuildTargetMotion(void) {
	const SlipRaceCollisionBody *const body = (const SlipRaceCollisionBody *)(const void *)SlipRaceCollision_targetBody;
	const uint16_t objectHandle = body->objectHandle;
	SlipObjectDirection direction;
	SlipRaceCollisionTargetMotion *const motion = &SlipRaceCollision_targetMotion;

	motion->movementDistance = (int32_t)body->movementDistance;
	direction = SlipObject_Direction(SlipRaceCollision_objectTable, objectHandle);
	motion->directionX = direction.directionXQ14;
	motion->directionY = direction.directionYQ14;
	motion->directionZ = direction.directionZQ14;
	motion->displacementX = SlipRaceCollision_ScaleDirectionQ14(motion->directionX, motion->movementDistance);
	motion->displacementY = SlipRaceCollision_ScaleDirectionQ14(motion->directionY, motion->movementDistance);
	motion->displacementZ = SlipRaceCollision_ScaleDirectionQ14(motion->directionZ, motion->movementDistance);
}

void SlipRaceCollision_InterpolateTime(const SlipRaceCollisionVertex *insideVertex,
                                       SlipRaceCollisionVertex *intersectionVertex, uint32_t fractionQ30) {
	const int32_t timeDifference = (int32_t)((uint32_t)insideVertex->time - (uint32_t)intersectionVertex->time);
	const int64_t timeProduct = (int64_t)timeDifference * (int64_t)(int32_t)fractionQ30;
	const uint32_t productLow = (uint32_t)timeProduct;
	const uint32_t scaledTimeDifference =
	    (productLow >> SLIP_COLLISION_RECIPROCAL_FRACTION_BITS) |
	    ((uint32_t)((uint64_t)timeProduct >> 32) << SLIP_COLLISION_RECIPROCAL_HIGH_SHIFT);
	const uint32_t roundingCarry = (productLow >> (SLIP_COLLISION_RECIPROCAL_FRACTION_BITS - 1)) & 1u;

	intersectionVertex->time = (int32_t)((uint32_t)intersectionVertex->time + scaledTimeDifference + roundingCarry);
}

uint32_t SlipRaceCollision_CornerOutcode(uint32_t cornerIndex, SlipRaceCollisionVertex *vertex) {
	uint32_t outcode;
	SlipRaceCollisionBounds *const bounds = &SlipRaceCollision_sourceBounds;

	vertex->x = SlipRaceCollision_corners[cornerIndex][0];
	vertex->y = SlipRaceCollision_corners[cornerIndex][1];
	vertex->z = SlipRaceCollision_corners[cornerIndex][2];
	outcode = vertex->outcode & SLIP_COLLISION_OUTCODE_PRESERVED_MASK;
	if (vertex->x < bounds->minX)
		outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_X;
	if (vertex->x > bounds->maxX)
		outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_X;
	if (vertex->y < bounds->minY)
		outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Y;
	if (vertex->y > bounds->maxY)
		outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Y;
	if (vertex->z < bounds->minZ)
		outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Z;
	if (vertex->z > bounds->maxZ)
		outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Z;
	vertex->outcode = outcode;
	return vertex->outcode;
}

SlipRaceCollisionOutcodes SlipRaceCollision_AccumulateOutcodes(void) {
	SlipRaceCollisionVertex *vertex = SlipRaceCollision_activeList;
	SlipRaceCollisionOutcodes result = {SLIP_COLLISION_OUTCODE_BOX_MASK, 0};

	do {
		const uint32_t outcode = vertex->outcode;
		result.commonOutcode &= outcode;
		result.combinedOutcode |= outcode;
		vertex = vertex->next;
	} while (vertex != SlipRaceCollision_activeList);
	return result;
}

SlipRaceCollisionVertex *SlipRaceCollision_AllocateVertex(void) {
	SlipRaceCollisionVertex *const sentinel = SlipRaceCollision_freeList;
	SlipRaceCollisionVertex *const vertex = sentinel->next;

	if (sentinel != vertex) {
		SlipRaceCollisionVertex *const next = vertex->next;
		sentinel->next = next;
		next->previous = sentinel;
		return vertex;
	}
	SlipRuntime_Fatal("GetFreeFacePoint - out of Face points!");
}

SlipRaceCollisionCrossings SlipRaceCollision_FindCrossings(uint32_t planeOutcode) {
	SlipRaceCollisionVertex *currentVertex = SlipRaceCollision_activeList;
	SlipRaceCollisionVertex *adjacentVertex = currentVertex->next;
	SlipRaceCollisionCrossings result;

	while ((currentVertex->outcode & planeOutcode) == 0 || (adjacentVertex->outcode & planeOutcode) != 0) {
		currentVertex = adjacentVertex;
		adjacentVertex = currentVertex->next;
	}
	result.firstOutside = currentVertex;
	result.firstInside = adjacentVertex;
	adjacentVertex = currentVertex->previous;
	if ((adjacentVertex->outcode & planeOutcode) == 0) {
		SlipRaceCollisionVertex *const duplicateOutsideVertex = SlipRaceCollision_AllocateVertex();
		SlipRaceCollisionVertex *const previous = currentVertex->previous;

		duplicateOutsideVertex->next = currentVertex;
		currentVertex->previous = duplicateOutsideVertex;
		previous->next = duplicateOutsideVertex;
		duplicateOutsideVertex->previous = previous;
		duplicateOutsideVertex->x = currentVertex->x;
		duplicateOutsideVertex->y = currentVertex->y;
		duplicateOutsideVertex->z = currentVertex->z;
		duplicateOutsideVertex->time = currentVertex->time;
		duplicateOutsideVertex->outcode = currentVertex->outcode;
		result.secondOutside = duplicateOutsideVertex;
		result.secondInside = previous;
	} else {
		currentVertex = adjacentVertex;
		adjacentVertex = currentVertex->previous;
		while ((adjacentVertex->outcode & planeOutcode) != 0) {
			SlipRaceCollision_ReleaseVertex(currentVertex);
			currentVertex = adjacentVertex;
			adjacentVertex = currentVertex->previous;
		}
		result.secondOutside = currentVertex;
		result.secondInside = adjacentVertex;
	}
	SlipRaceCollision_activeList = result.firstInside;
	return result;
}

bool SlipRaceCollision_ClipPolygon(uint32_t combinedOutcode) {
	uint32_t remainingOutcodes = combinedOutcode;
	SlipRaceCollisionCrossings crossings;
	SlipRaceCollisionOutcodes outcodes;

	if ((combinedOutcode & SLIP_COLLISION_OUTCODE_MINIMUM_X) != 0) {
		crossings = SlipRaceCollision_FindCrossings(SLIP_COLLISION_OUTCODE_MINIMUM_X);
		SlipRaceCollision_InterpolateX(crossings.firstInside, crossings.firstOutside,
		                               SlipRaceCollision_sourceBounds.minX);
		SlipRaceCollision_InterpolateX(crossings.secondInside, crossings.secondOutside,
		                               SlipRaceCollision_sourceBounds.minX);
	}
	if ((remainingOutcodes & SLIP_COLLISION_OUTCODE_MAXIMUM_X) != 0) {
		crossings = SlipRaceCollision_FindCrossings(SLIP_COLLISION_OUTCODE_MAXIMUM_X);
		SlipRaceCollision_InterpolateX(crossings.firstInside, crossings.firstOutside,
		                               SlipRaceCollision_sourceBounds.maxX);
		SlipRaceCollision_InterpolateX(crossings.secondInside, crossings.secondOutside,
		                               SlipRaceCollision_sourceBounds.maxX);
	}
	outcodes = SlipRaceCollision_AccumulateOutcodes();
	remainingOutcodes = outcodes.combinedOutcode;
	if ((outcodes.commonOutcode & SLIP_COLLISION_OUTCODE_BOX_MASK) != 0)
		return true;
	if ((outcodes.combinedOutcode & SLIP_COLLISION_OUTCODE_MINIMUM_Y) != 0) {
		crossings = SlipRaceCollision_FindCrossings(SLIP_COLLISION_OUTCODE_MINIMUM_Y);
		SlipRaceCollision_InterpolateY(crossings.firstInside, crossings.firstOutside,
		                               SlipRaceCollision_sourceBounds.minY);
		SlipRaceCollision_InterpolateY(crossings.secondInside, crossings.secondOutside,
		                               SlipRaceCollision_sourceBounds.minY);
	}
	if ((remainingOutcodes & SLIP_COLLISION_OUTCODE_MAXIMUM_Y) != 0) {
		crossings = SlipRaceCollision_FindCrossings(SLIP_COLLISION_OUTCODE_MAXIMUM_Y);
		SlipRaceCollision_InterpolateY(crossings.firstInside, crossings.firstOutside,
		                               SlipRaceCollision_sourceBounds.maxY);
		SlipRaceCollision_InterpolateY(crossings.secondInside, crossings.secondOutside,
		                               SlipRaceCollision_sourceBounds.maxY);
	}
	outcodes = SlipRaceCollision_AccumulateOutcodes();
	remainingOutcodes = outcodes.combinedOutcode;
	if ((outcodes.commonOutcode & SLIP_COLLISION_OUTCODE_BOX_MASK) != 0)
		return true;
	if ((outcodes.combinedOutcode & SLIP_COLLISION_OUTCODE_MINIMUM_Z) != 0) {
		crossings = SlipRaceCollision_FindCrossings(SLIP_COLLISION_OUTCODE_MINIMUM_Z);
		SlipRaceCollision_InterpolateZ(crossings.firstInside, crossings.firstOutside,
		                               SlipRaceCollision_sourceBounds.minZ);
		SlipRaceCollision_InterpolateZ(crossings.secondInside, crossings.secondOutside,
		                               SlipRaceCollision_sourceBounds.minZ);
	}
	if ((remainingOutcodes & SLIP_COLLISION_OUTCODE_MAXIMUM_Z) != 0) {
		crossings = SlipRaceCollision_FindCrossings(SLIP_COLLISION_OUTCODE_MAXIMUM_Z);
		SlipRaceCollision_InterpolateZ(crossings.firstInside, crossings.firstOutside,
		                               SlipRaceCollision_sourceBounds.maxZ);
		SlipRaceCollision_InterpolateZ(crossings.secondInside, crossings.secondOutside,
		                               SlipRaceCollision_sourceBounds.maxZ);
	}
	outcodes = SlipRaceCollision_AccumulateOutcodes();
	return (outcodes.commonOutcode & SLIP_COLLISION_OUTCODE_BOX_MASK) != 0;
}

bool SlipRaceCollision_TestBoxFaces(void) {
	const int32_t (*corner)[3] = SlipRaceCollision_corners;
	uint32_t remainingCount = SLIP_TRACK_BOUNDING_CORNER_COUNT;
	uint32_t common = UINT32_MAX;
	const int32_t (*face)[SLIP_COLLISION_FACE_VALUE_COUNT];
	SlipRaceCollisionBounds *const bounds = &SlipRaceCollision_sourceBounds;

	do {
		uint32_t outcode = 0;
		int32_t coordinate = (*corner)[0];

		if (coordinate < bounds->minX)
			outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_X;
		if (coordinate > bounds->maxX)
			outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_X;
		coordinate = (*corner)[1];
		if (coordinate < bounds->minY)
			outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Y;
		if (coordinate > bounds->maxY)
			outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Y;
		coordinate = (*corner)[2];
		if (coordinate < bounds->minZ)
			outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Z;
		if (coordinate > bounds->maxZ)
			outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Z;
		if (outcode == 0)
			return true;
		common &= outcode;
		++corner;
		--remainingCount;
	} while (remainingCount != 0);
	if (common != 0)
		return false;
	face = SlipRaceCollision_faces;
	remainingCount = SLIP_COLLISION_BOX_FACE_COUNT;
	do {
		uint32_t remainingFaceCorners = SLIP_COLLISION_FACE_CORNER_COUNT;
		const int32_t *cornerIndexPointer = *face;
		SlipRaceCollisionVertex *firstVertex;
		SlipRaceCollisionVertex *vertex;

		SlipRaceCollision_faceCommon = UINT32_MAX;
		SlipRaceCollision_faceCombined = 0;
		vertex = SlipRaceCollision_AllocateVertex();
		SlipRaceCollision_activeList = vertex;
		firstVertex = vertex;
		do {
			const uint32_t outcode = SlipRaceCollision_CornerOutcode((uint32_t)*cornerIndexPointer, vertex);
			SlipRaceCollision_faceCombined |= outcode;
			SlipRaceCollision_faceCommon &= outcode;
			++cornerIndexPointer;
			--remainingFaceCorners;
			if (remainingFaceCorners != 0) {
				SlipRaceCollisionVertex *const previous = vertex;
				vertex = SlipRaceCollision_AllocateVertex();
				previous->next = vertex;
				vertex->previous = previous;
			}
		} while (remainingFaceCorners != 0);
		vertex->next = firstVertex;
		firstVertex->previous = vertex;
		if ((SlipRaceCollision_faceCommon & SLIP_COLLISION_OUTCODE_BOX_MASK) == 0 &&
		    (SlipRaceCollision_faceCombined == 0 || !SlipRaceCollision_ClipPolygon(SlipRaceCollision_faceCombined))) {
			SlipRaceCollision_ReleasePolygon();
			return true;
		}
		SlipRaceCollision_ReleasePolygon();
		++face;
		--remainingCount;
	} while (remainingCount != 0);
	return false;
}

void SlipRaceCollision_TestMinZ(void) {
	uint32_t commonFlags;
	uint32_t remainingCount;
	uint32_t cornerIndex;
	int32_t (*face)[SLIP_COLLISION_FACE_VALUE_COUNT];
	SlipRaceCollisionBounds *const bounds = &SlipRaceCollision_sourceBounds;

	if (SlipRaceCollision_deltaZ == 0 || SlipRaceCollision_deltaZ < 0) {
		return;
	}
	commonFlags = UINT32_MAX;
	remainingCount = SLIP_TRACK_BOUNDING_CORNER_COUNT;
	cornerIndex = 0;
	do {
		SlipRaceCollisionCornerState *const state = &SlipRaceCollision_cornerState[cornerIndex];
		uint32_t flags = 0;

		state->originalX = SlipRaceCollision_corners[cornerIndex][0];
		state->originalY = SlipRaceCollision_corners[cornerIndex][1];
		state->originalZ = SlipRaceCollision_corners[cornerIndex][2];
		if (state->originalZ > bounds->minZ)
			flags = SLIP_COLLISION_OUTCODE_PLANE_SIDE;
		state->flags = flags;
		commonFlags &= flags;
		++cornerIndex;
		--remainingCount;
	} while (remainingCount != 0);
	if (commonFlags != 0)
		return;
	remainingCount = SLIP_TRACK_BOUNDING_CORNER_COUNT;
	cornerIndex = 0;
	do {
		int32_t coordinate = SlipRaceCollision_cornerState[cornerIndex].originalZ;
		if (coordinate > bounds->minZ)
			break;
		coordinate = (int32_t)(0u - ((uint32_t)coordinate - (uint32_t)bounds->minZ));
		if (coordinate < SlipRaceCollision_contact.time)
			break;
		++cornerIndex;
		--remainingCount;
	} while (remainingCount != 0);
	if (remainingCount == 0)
		return;
	if ((uint16_t)SlipRaceCollision_velocityZ < SLIP_COLLISION_MINIMUM_AXIS_SPEED)
		return;
	SlipRaceCollision_inverseVelocity = SLIP_COLLISION_RECIPROCAL_ONE / (uint16_t)SlipRaceCollision_velocityZ;
	face = SlipRaceCollision_faces;
	remainingCount = SLIP_COLLISION_BOX_FACE_COUNT;
	do {
		if ((*face)[SLIP_COLLISION_FACE_ENABLED_INDEX] != 0) {
			SlipView3DVec16 normal = {(int16_t)(*face)[SLIP_COLLISION_FACE_NORMAL_X_INDEX],
			                          (int16_t)(*face)[SLIP_COLLISION_FACE_NORMAL_Y_INDEX],
			                          (int16_t)(*face)[SLIP_COLLISION_FACE_NORMAL_Z_INDEX]};
			const int16_t normalAlongAxisQ14 =
			    (int16_t)SlipView3D_ProjectColumn2(&SlipRaceCollision_relativeMatrix, normal);

			if (normalAlongAxisQ14 != 0 && normalAlongAxisQ14 > 0) {
				uint32_t faceCommon = UINT32_MAX;
				uint32_t faceCombined = 0;
				uint32_t remainingFaceCorners = SLIP_COLLISION_FACE_CORNER_COUNT;
				int32_t *cornerIndexPointer = *face;
				SlipRaceCollisionVertex *vertex = SlipRaceCollision_AllocateVertex();
				SlipRaceCollisionVertex *const firstVertex = vertex;

				SlipRaceCollision_activeList = vertex;
				do {
					const uint32_t stateIndex = (uint32_t)*cornerIndexPointer;
					SlipRaceCollisionCornerState *const state = &SlipRaceCollision_cornerState[stateIndex];
					const int32_t distanceToPlane =
					    (int32_t)(0u - ((uint32_t)state->originalZ - (uint32_t)bounds->minZ));
					const int64_t intersectionTimeProduct =
					    (int64_t)distanceToPlane * (int64_t)(int32_t)SlipRaceCollision_inverseVelocity;
					const uint32_t productLow = (uint32_t)intersectionTimeProduct;
					int32_t intersectionTime = (int32_t)((productLow >> SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT) |
					                                     ((uint32_t)((uint64_t)intersectionTimeProduct >> 32)
					                                      << (32 - SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT)));
					uint32_t outcode;

					intersectionTime = (int32_t)((uint32_t)intersectionTime +
					                             ((productLow >> (SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT - 1)) & 1u));
					state->time = intersectionTime;
					vertex->time = intersectionTime;
					state->projectedX =
					    (int32_t)((uint32_t)state->originalX + (uint32_t)SlipRaceCollision_ScaleDirectionQ14(
					                                               SlipRaceCollision_velocityX, intersectionTime));
					vertex->x = state->projectedX;
					state->projectedY =
					    (int32_t)((uint32_t)state->originalY + (uint32_t)SlipRaceCollision_ScaleDirectionQ14(
					                                               SlipRaceCollision_velocityY, intersectionTime));
					vertex->y = state->projectedY;
					state->projectedZ = bounds->minZ;
					vertex->z = bounds->minZ;
					state->flags |= SLIP_COLLISION_CORNER_PROJECTED;
					outcode = state->flags & SLIP_COLLISION_OUTCODE_PRESERVED_MASK;
					if (vertex->x < bounds->minX)
						outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_X;
					if (vertex->x > bounds->maxX)
						outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_X;
					if (vertex->y < bounds->minY)
						outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Y;
					if (vertex->y > bounds->maxY)
						outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Y;
					if (vertex->z < bounds->minZ)
						outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Z;
					if (vertex->z > bounds->maxZ)
						outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Z;
					vertex->outcode = outcode;
					faceCombined |= outcode;
					faceCommon &= outcode;
					++cornerIndexPointer;
					--remainingFaceCorners;
					if (remainingFaceCorners != 0) {
						SlipRaceCollisionVertex *const previous = vertex;
						vertex = SlipRaceCollision_AllocateVertex();
						previous->next = vertex;
						vertex->previous = previous;
					}
				} while (remainingFaceCorners != 0);
				vertex->next = firstVertex;
				firstVertex->previous = vertex;
				if ((faceCommon & SLIP_COLLISION_OUTCODE_FACE_REJECT_MASK) == 0 &&
				    (faceCombined == 0 || !SlipRaceCollision_ClipPolygon(faceCombined))) {
					SlipRaceCollision_SelectContact();
				}
			}
		}
		SlipRaceCollision_ReleasePolygon();
		++face;
		--remainingCount;
	} while (remainingCount != 0);
}

void SlipRaceCollision_TestMaxZ(void) {
	uint32_t commonFlags = UINT32_MAX;
	uint32_t remainingCount = SLIP_TRACK_BOUNDING_CORNER_COUNT;
	uint32_t cornerIndex = 0;
	int32_t (*face)[SLIP_COLLISION_FACE_VALUE_COUNT];
	SlipRaceCollisionBounds *const bounds = &SlipRaceCollision_sourceBounds;

	if (SlipRaceCollision_deltaZ >= 0)
		return;
	do {
		SlipRaceCollisionCornerState *const state = &SlipRaceCollision_cornerState[cornerIndex];
		uint32_t flags = 0;

		state->originalX = SlipRaceCollision_corners[cornerIndex][0];
		state->originalY = SlipRaceCollision_corners[cornerIndex][1];
		state->originalZ = SlipRaceCollision_corners[cornerIndex][2];
		if (state->originalZ < bounds->maxZ)
			flags = SLIP_COLLISION_OUTCODE_PLANE_SIDE;
		state->flags = flags;
		commonFlags &= flags;
		++cornerIndex;
		--remainingCount;
	} while (remainingCount != 0);
	if (commonFlags != 0)
		return;
	remainingCount = SLIP_TRACK_BOUNDING_CORNER_COUNT;
	cornerIndex = 0;
	do {
		int32_t coordinate = SlipRaceCollision_cornerState[cornerIndex].originalZ;
		if (coordinate < bounds->maxZ)
			break;
		coordinate = (int32_t)((uint32_t)coordinate - (uint32_t)bounds->maxZ);
		if (coordinate < SlipRaceCollision_contact.time)
			break;
		++cornerIndex;
		--remainingCount;
	} while (remainingCount != 0);
	if (remainingCount == 0)
		return;
	{
		const int32_t speedAlongAxis = -(int32_t)SlipRaceCollision_velocityZ;
		if (speedAlongAxis < SLIP_COLLISION_MINIMUM_AXIS_SPEED)
			return;
		SlipRaceCollision_inverseVelocity = SLIP_COLLISION_RECIPROCAL_ONE / (uint32_t)speedAlongAxis;
	}
	face = SlipRaceCollision_faces;
	remainingCount = SLIP_COLLISION_BOX_FACE_COUNT;
	do {
		if ((*face)[SLIP_COLLISION_FACE_ENABLED_INDEX] != 0) {
			SlipView3DVec16 normal = {(int16_t)(*face)[SLIP_COLLISION_FACE_NORMAL_X_INDEX],
			                          (int16_t)(*face)[SLIP_COLLISION_FACE_NORMAL_Y_INDEX],
			                          (int16_t)(*face)[SLIP_COLLISION_FACE_NORMAL_Z_INDEX]};
			const int16_t normalAlongAxisQ14 =
			    (int16_t)SlipView3D_ProjectColumn2(&SlipRaceCollision_relativeMatrix, normal);

			if (normalAlongAxisQ14 < 0) {
				uint32_t faceCommon = UINT32_MAX;
				uint32_t faceCombined = 0;
				uint32_t remainingFaceCorners = SLIP_COLLISION_FACE_CORNER_COUNT;
				int32_t *cornerIndexPointer = *face;
				SlipRaceCollisionVertex *vertex = SlipRaceCollision_AllocateVertex();
				SlipRaceCollisionVertex *const firstVertex = vertex;

				SlipRaceCollision_activeList = vertex;
				do {
					const uint32_t stateIndex = (uint32_t)*cornerIndexPointer;
					SlipRaceCollisionCornerState *const state = &SlipRaceCollision_cornerState[stateIndex];
					const int32_t distanceToPlane = (int32_t)((uint32_t)state->originalZ - (uint32_t)bounds->maxZ);
					const int64_t intersectionTimeProduct =
					    (int64_t)distanceToPlane * (int64_t)(int32_t)SlipRaceCollision_inverseVelocity;
					const uint32_t productLow = (uint32_t)intersectionTimeProduct;
					int32_t intersectionTime = (int32_t)((productLow >> SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT) |
					                                     ((uint32_t)((uint64_t)intersectionTimeProduct >> 32)
					                                      << (32 - SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT)));
					uint32_t outcode;

					intersectionTime = (int32_t)((uint32_t)intersectionTime +
					                             ((productLow >> (SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT - 1)) & 1u));
					state->time = intersectionTime;
					vertex->time = intersectionTime;
					state->projectedX =
					    (int32_t)((uint32_t)state->originalX + (uint32_t)SlipRaceCollision_ScaleDirectionQ14(
					                                               SlipRaceCollision_velocityX, intersectionTime));
					vertex->x = state->projectedX;
					state->projectedY =
					    (int32_t)((uint32_t)state->originalY + (uint32_t)SlipRaceCollision_ScaleDirectionQ14(
					                                               SlipRaceCollision_velocityY, intersectionTime));
					vertex->y = state->projectedY;
					state->projectedZ = bounds->maxZ;
					vertex->z = bounds->maxZ;
					state->flags |= SLIP_COLLISION_CORNER_PROJECTED;
					outcode = state->flags & SLIP_COLLISION_OUTCODE_PRESERVED_MASK;
					if (vertex->x < bounds->minX)
						outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_X;
					if (vertex->x > bounds->maxX)
						outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_X;
					if (vertex->y < bounds->minY)
						outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Y;
					if (vertex->y > bounds->maxY)
						outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Y;
					if (vertex->z < bounds->minZ)
						outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Z;
					if (vertex->z > bounds->maxZ)
						outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Z;
					vertex->outcode = outcode;
					faceCombined |= outcode;
					faceCommon &= outcode;
					++cornerIndexPointer;
					--remainingFaceCorners;
					if (remainingFaceCorners != 0) {
						SlipRaceCollisionVertex *const previous = vertex;
						vertex = SlipRaceCollision_AllocateVertex();
						previous->next = vertex;
						vertex->previous = previous;
					}
				} while (remainingFaceCorners != 0);
				vertex->next = firstVertex;
				firstVertex->previous = vertex;
				if ((faceCommon & SLIP_COLLISION_OUTCODE_FACE_REJECT_MASK) == 0 &&
				    (faceCombined == 0 || !SlipRaceCollision_ClipPolygon(faceCombined))) {
					SlipRaceCollision_SelectContact();
				}
			}
		}
		SlipRaceCollision_ReleasePolygon();
		++face;
		--remainingCount;
	} while (remainingCount != 0);
}

void SlipRaceCollision_TestMinX(void) {
	uint32_t commonFlags;
	uint32_t remainingCount;
	uint32_t cornerIndex;
	int32_t (*face)[SLIP_COLLISION_FACE_VALUE_COUNT];
	SlipRaceCollisionBounds *const bounds = &SlipRaceCollision_sourceBounds;

	if (SlipRaceCollision_deltaX == 0 || SlipRaceCollision_deltaX < 0)
		return;
	commonFlags = UINT32_MAX;
	remainingCount = SLIP_TRACK_BOUNDING_CORNER_COUNT;
	cornerIndex = 0;
	do {
		SlipRaceCollisionCornerState *const state = &SlipRaceCollision_cornerState[cornerIndex];
		uint32_t flags = 0;

		state->originalX = SlipRaceCollision_corners[cornerIndex][0];
		state->originalY = SlipRaceCollision_corners[cornerIndex][1];
		state->originalZ = SlipRaceCollision_corners[cornerIndex][2];
		if (state->originalX > bounds->minX)
			flags = SLIP_COLLISION_OUTCODE_PLANE_SIDE;
		state->flags = flags;
		commonFlags &= flags;
		++cornerIndex;
		--remainingCount;
	} while (remainingCount != 0);
	if (commonFlags != 0)
		return;
	remainingCount = SLIP_TRACK_BOUNDING_CORNER_COUNT;
	cornerIndex = 0;
	do {
		int32_t coordinate = SlipRaceCollision_cornerState[cornerIndex].originalX;
		if (coordinate > bounds->minX)
			break;
		coordinate = (int32_t)(0u - ((uint32_t)coordinate - (uint32_t)bounds->minX));
		if (coordinate < SlipRaceCollision_contact.time)
			break;
		++cornerIndex;
		--remainingCount;
	} while (remainingCount != 0);
	if (remainingCount == 0)
		return;
	if ((uint16_t)SlipRaceCollision_velocityX < SLIP_COLLISION_MINIMUM_AXIS_SPEED)
		return;
	SlipRaceCollision_inverseVelocity = SLIP_COLLISION_RECIPROCAL_ONE / (uint16_t)SlipRaceCollision_velocityX;
	face = SlipRaceCollision_faces;
	remainingCount = SLIP_COLLISION_BOX_FACE_COUNT;
	do {
		if ((*face)[SLIP_COLLISION_FACE_ENABLED_INDEX] != 0) {
			SlipView3DVec16 normal = {(int16_t)(*face)[SLIP_COLLISION_FACE_NORMAL_X_INDEX],
			                          (int16_t)(*face)[SLIP_COLLISION_FACE_NORMAL_Y_INDEX],
			                          (int16_t)(*face)[SLIP_COLLISION_FACE_NORMAL_Z_INDEX]};
			const int16_t normalAlongAxisQ14 =
			    (int16_t)SlipView3D_ProjectColumn0(&SlipRaceCollision_relativeMatrix, normal);

			if (normalAlongAxisQ14 != 0 && normalAlongAxisQ14 > 0) {
				uint32_t faceCommon = UINT32_MAX;
				uint32_t faceCombined = 0;
				uint32_t remainingFaceCorners = SLIP_COLLISION_FACE_CORNER_COUNT;
				int32_t *cornerIndexPointer = *face;
				SlipRaceCollisionVertex *vertex = SlipRaceCollision_AllocateVertex();
				SlipRaceCollisionVertex *const firstVertex = vertex;

				SlipRaceCollision_activeList = vertex;
				do {
					const uint32_t stateIndex = (uint32_t)*cornerIndexPointer;
					SlipRaceCollisionCornerState *const state = &SlipRaceCollision_cornerState[stateIndex];
					const int32_t distanceToPlane =
					    (int32_t)(0u - ((uint32_t)state->originalX - (uint32_t)bounds->minX));
					const int64_t intersectionTimeProduct =
					    (int64_t)distanceToPlane * (int64_t)(int32_t)SlipRaceCollision_inverseVelocity;
					const uint32_t productLow = (uint32_t)intersectionTimeProduct;
					int32_t intersectionTime = (int32_t)((productLow >> SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT) |
					                                     ((uint32_t)((uint64_t)intersectionTimeProduct >> 32)
					                                      << (32 - SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT)));
					uint32_t outcode;

					intersectionTime = (int32_t)((uint32_t)intersectionTime +
					                             ((productLow >> (SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT - 1)) & 1u));
					state->time = intersectionTime;
					vertex->time = intersectionTime;
					state->projectedZ =
					    (int32_t)((uint32_t)state->originalZ + (uint32_t)SlipRaceCollision_ScaleDirectionQ14(
					                                               SlipRaceCollision_velocityZ, intersectionTime));
					vertex->z = state->projectedZ;
					state->projectedY =
					    (int32_t)((uint32_t)state->originalY + (uint32_t)SlipRaceCollision_ScaleDirectionQ14(
					                                               SlipRaceCollision_velocityY, intersectionTime));
					vertex->y = state->projectedY;
					state->projectedX = bounds->minX;
					vertex->x = bounds->minX;
					state->flags |= SLIP_COLLISION_CORNER_PROJECTED;
					outcode = state->flags & SLIP_COLLISION_OUTCODE_PRESERVED_MASK;
					if (vertex->x < bounds->minX)
						outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_X;
					if (vertex->x > bounds->maxX)
						outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_X;
					if (vertex->y < bounds->minY)
						outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Y;
					if (vertex->y > bounds->maxY)
						outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Y;
					if (vertex->z < bounds->minZ)
						outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Z;
					if (vertex->z > bounds->maxZ)
						outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Z;
					vertex->outcode = outcode;
					faceCombined |= outcode;
					faceCommon &= outcode;
					++cornerIndexPointer;
					--remainingFaceCorners;
					if (remainingFaceCorners != 0) {
						SlipRaceCollisionVertex *const previous = vertex;
						vertex = SlipRaceCollision_AllocateVertex();
						previous->next = vertex;
						vertex->previous = previous;
					}
				} while (remainingFaceCorners != 0);
				vertex->next = firstVertex;
				firstVertex->previous = vertex;
				if ((faceCommon & SLIP_COLLISION_OUTCODE_FACE_REJECT_MASK) == 0 &&
				    (faceCombined == 0 || !SlipRaceCollision_ClipPolygon(faceCombined))) {
					SlipRaceCollision_SelectContact();
				}
			}
		}
		SlipRaceCollision_ReleasePolygon();
		++face;
		--remainingCount;
	} while (remainingCount != 0);
}

void SlipRaceCollision_TestMaxX(void) {
	uint32_t commonFlags = UINT32_MAX;
	uint32_t remainingCount = SLIP_TRACK_BOUNDING_CORNER_COUNT;
	uint32_t cornerIndex = 0;
	int32_t (*face)[SLIP_COLLISION_FACE_VALUE_COUNT];
	SlipRaceCollisionBounds *const bounds = &SlipRaceCollision_sourceBounds;

	if (SlipRaceCollision_deltaX >= 0)
		return;
	do {
		SlipRaceCollisionCornerState *const state = &SlipRaceCollision_cornerState[cornerIndex];
		uint32_t flags = 0;

		state->originalX = SlipRaceCollision_corners[cornerIndex][0];
		state->originalY = SlipRaceCollision_corners[cornerIndex][1];
		state->originalZ = SlipRaceCollision_corners[cornerIndex][2];
		if (state->originalX < bounds->maxX)
			flags = SLIP_COLLISION_OUTCODE_PLANE_SIDE;
		state->flags = flags;
		commonFlags &= flags;
		++cornerIndex;
		--remainingCount;
	} while (remainingCount != 0);
	if (commonFlags != 0)
		return;
	remainingCount = SLIP_TRACK_BOUNDING_CORNER_COUNT;
	cornerIndex = 0;
	do {
		int32_t coordinate = SlipRaceCollision_cornerState[cornerIndex].originalX;
		if (coordinate < bounds->maxX)
			break;
		coordinate = (int32_t)((uint32_t)coordinate - (uint32_t)bounds->maxX);
		if (coordinate < SlipRaceCollision_contact.time)
			break;
		++cornerIndex;
		--remainingCount;
	} while (remainingCount != 0);
	if (remainingCount == 0)
		return;
	{
		const int32_t speedAlongAxis = -(int32_t)SlipRaceCollision_velocityX;
		if (speedAlongAxis < SLIP_COLLISION_MINIMUM_AXIS_SPEED)
			return;
		SlipRaceCollision_inverseVelocity = SLIP_COLLISION_RECIPROCAL_ONE / (uint32_t)speedAlongAxis;
	}
	face = SlipRaceCollision_faces;
	remainingCount = SLIP_COLLISION_BOX_FACE_COUNT;
	do {
		if ((*face)[SLIP_COLLISION_FACE_ENABLED_INDEX] != 0) {
			SlipView3DVec16 normal = {(int16_t)(*face)[SLIP_COLLISION_FACE_NORMAL_X_INDEX],
			                          (int16_t)(*face)[SLIP_COLLISION_FACE_NORMAL_Y_INDEX],
			                          (int16_t)(*face)[SLIP_COLLISION_FACE_NORMAL_Z_INDEX]};
			const int16_t normalAlongAxisQ14 =
			    (int16_t)SlipView3D_ProjectColumn0(&SlipRaceCollision_relativeMatrix, normal);

			if (normalAlongAxisQ14 < 0) {
				uint32_t faceCommon = UINT32_MAX;
				uint32_t faceCombined = 0;
				uint32_t remainingFaceCorners = SLIP_COLLISION_FACE_CORNER_COUNT;
				int32_t *cornerIndexPointer = *face;
				SlipRaceCollisionVertex *vertex = SlipRaceCollision_AllocateVertex();
				SlipRaceCollisionVertex *const firstVertex = vertex;

				SlipRaceCollision_activeList = vertex;
				do {
					const uint32_t stateIndex = (uint32_t)*cornerIndexPointer;
					SlipRaceCollisionCornerState *const state = &SlipRaceCollision_cornerState[stateIndex];
					const int32_t distanceToPlane = (int32_t)((uint32_t)state->originalX - (uint32_t)bounds->maxX);
					const int64_t intersectionTimeProduct =
					    (int64_t)distanceToPlane * (int64_t)(int32_t)SlipRaceCollision_inverseVelocity;
					const uint32_t productLow = (uint32_t)intersectionTimeProduct;
					int32_t intersectionTime = (int32_t)((productLow >> SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT) |
					                                     ((uint32_t)((uint64_t)intersectionTimeProduct >> 32)
					                                      << (32 - SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT)));
					uint32_t outcode;

					intersectionTime = (int32_t)((uint32_t)intersectionTime +
					                             ((productLow >> (SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT - 1)) & 1u));
					state->time = intersectionTime;
					vertex->time = intersectionTime;
					state->projectedZ =
					    (int32_t)((uint32_t)state->originalZ + (uint32_t)SlipRaceCollision_ScaleDirectionQ14(
					                                               SlipRaceCollision_velocityZ, intersectionTime));
					vertex->z = state->projectedZ;
					state->projectedY =
					    (int32_t)((uint32_t)state->originalY + (uint32_t)SlipRaceCollision_ScaleDirectionQ14(
					                                               SlipRaceCollision_velocityY, intersectionTime));
					vertex->y = state->projectedY;
					state->projectedX = bounds->maxX;
					vertex->x = bounds->maxX;
					state->flags |= SLIP_COLLISION_CORNER_PROJECTED;
					outcode = state->flags & SLIP_COLLISION_OUTCODE_PRESERVED_MASK;
					if (vertex->x < bounds->minX)
						outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_X;
					if (vertex->x > bounds->maxX)
						outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_X;
					if (vertex->y < bounds->minY)
						outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Y;
					if (vertex->y > bounds->maxY)
						outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Y;
					if (vertex->z < bounds->minZ)
						outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Z;
					if (vertex->z > bounds->maxZ)
						outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Z;
					vertex->outcode = outcode;
					faceCombined |= outcode;
					faceCommon &= outcode;
					++cornerIndexPointer;
					--remainingFaceCorners;
					if (remainingFaceCorners != 0) {
						SlipRaceCollisionVertex *const previous = vertex;
						vertex = SlipRaceCollision_AllocateVertex();
						previous->next = vertex;
						vertex->previous = previous;
					}
				} while (remainingFaceCorners != 0);
				vertex->next = firstVertex;
				firstVertex->previous = vertex;
				if ((faceCommon & SLIP_COLLISION_OUTCODE_FACE_REJECT_MASK) == 0 &&
				    (faceCombined == 0 || !SlipRaceCollision_ClipPolygon(faceCombined))) {
					SlipRaceCollision_SelectContact();
				}
			}
		}
		SlipRaceCollision_ReleasePolygon();
		++face;
		--remainingCount;
	} while (remainingCount != 0);
}

void SlipRaceCollision_TestMinY(void) {
	uint32_t commonFlags = UINT32_MAX;
	uint32_t remainingCount = SLIP_TRACK_BOUNDING_CORNER_COUNT;
	uint32_t cornerIndex = 0;
	int32_t (*face)[SLIP_COLLISION_FACE_VALUE_COUNT];
	SlipRaceCollisionBounds *const bounds = &SlipRaceCollision_sourceBounds;

	if (SlipRaceCollision_deltaY == 0 || SlipRaceCollision_deltaY < 0)
		return;
	do {
		SlipRaceCollisionCornerState *const state = &SlipRaceCollision_cornerState[cornerIndex];
		uint32_t flags = 0;

		state->originalX = SlipRaceCollision_corners[cornerIndex][0];
		state->originalY = SlipRaceCollision_corners[cornerIndex][1];
		state->originalZ = SlipRaceCollision_corners[cornerIndex][2];
		if (state->originalY > bounds->minY)
			flags = SLIP_COLLISION_OUTCODE_PLANE_SIDE;
		state->flags = flags;
		commonFlags &= flags;
		++cornerIndex;
		--remainingCount;
	} while (remainingCount != 0);
	if (commonFlags != 0)
		return;
	remainingCount = SLIP_TRACK_BOUNDING_CORNER_COUNT;
	cornerIndex = 0;
	do {
		int32_t coordinate = SlipRaceCollision_cornerState[cornerIndex].originalY;
		if (coordinate > bounds->minY)
			break;
		coordinate = (int32_t)(0u - ((uint32_t)coordinate - (uint32_t)bounds->minY));
		if (coordinate < SlipRaceCollision_contact.time)
			break;
		++cornerIndex;
		--remainingCount;
	} while (remainingCount != 0);
	if (remainingCount == 0)
		return;
	if ((uint16_t)SlipRaceCollision_velocityY < SLIP_COLLISION_MINIMUM_AXIS_SPEED)
		return;
	SlipRaceCollision_inverseVelocity = SLIP_COLLISION_RECIPROCAL_ONE / (uint16_t)SlipRaceCollision_velocityY;
	face = SlipRaceCollision_faces;
	remainingCount = SLIP_COLLISION_BOX_FACE_COUNT;
	do {
		if ((*face)[SLIP_COLLISION_FACE_ENABLED_INDEX] != 0) {
			SlipView3DVec16 normal = {(int16_t)(*face)[SLIP_COLLISION_FACE_NORMAL_X_INDEX],
			                          (int16_t)(*face)[SLIP_COLLISION_FACE_NORMAL_Y_INDEX],
			                          (int16_t)(*face)[SLIP_COLLISION_FACE_NORMAL_Z_INDEX]};
			const int16_t normalAlongAxisQ14 =
			    (int16_t)SlipView3D_ProjectColumn1(&SlipRaceCollision_relativeMatrix, normal);

			if (normalAlongAxisQ14 != 0 && normalAlongAxisQ14 > 0) {
				uint32_t faceCommon = UINT32_MAX;
				uint32_t faceCombined = 0;
				uint32_t remainingFaceCorners = SLIP_COLLISION_FACE_CORNER_COUNT;
				int32_t *cornerIndexPointer = *face;
				SlipRaceCollisionVertex *vertex = SlipRaceCollision_AllocateVertex();
				SlipRaceCollisionVertex *const firstVertex = vertex;

				SlipRaceCollision_activeList = vertex;
				do {
					const uint32_t stateIndex = (uint32_t)*cornerIndexPointer;
					SlipRaceCollisionCornerState *const state = &SlipRaceCollision_cornerState[stateIndex];
					const int32_t distanceToPlane =
					    (int32_t)(0u - ((uint32_t)state->originalY - (uint32_t)bounds->minY));
					const int64_t intersectionTimeProduct =
					    (int64_t)distanceToPlane * (int64_t)(int32_t)SlipRaceCollision_inverseVelocity;
					const uint32_t productLow = (uint32_t)intersectionTimeProduct;
					int32_t intersectionTime = (int32_t)((productLow >> SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT) |
					                                     ((uint32_t)((uint64_t)intersectionTimeProduct >> 32)
					                                      << (32 - SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT)));
					uint32_t outcode;

					intersectionTime = (int32_t)((uint32_t)intersectionTime +
					                             ((productLow >> (SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT - 1)) & 1u));
					state->time = intersectionTime;
					vertex->time = intersectionTime;
					state->projectedZ =
					    (int32_t)((uint32_t)state->originalZ + (uint32_t)SlipRaceCollision_ScaleDirectionQ14(
					                                               SlipRaceCollision_velocityZ, intersectionTime));
					vertex->z = state->projectedZ;
					state->projectedX =
					    (int32_t)((uint32_t)state->originalX + (uint32_t)SlipRaceCollision_ScaleDirectionQ14(
					                                               SlipRaceCollision_velocityX, intersectionTime));
					vertex->x = state->projectedX;
					state->projectedY = bounds->minY;
					vertex->y = bounds->minY;
					state->flags |= SLIP_COLLISION_CORNER_PROJECTED;
					outcode = state->flags & SLIP_COLLISION_OUTCODE_PRESERVED_MASK;
					if (vertex->x < bounds->minX)
						outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_X;
					if (vertex->x > bounds->maxX)
						outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_X;
					if (vertex->y < bounds->minY)
						outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Y;
					if (vertex->y > bounds->maxY)
						outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Y;
					if (vertex->z < bounds->minZ)
						outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Z;
					if (vertex->z > bounds->maxZ)
						outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Z;
					vertex->outcode = outcode;
					faceCombined |= outcode;
					faceCommon &= outcode;
					++cornerIndexPointer;
					--remainingFaceCorners;
					if (remainingFaceCorners != 0) {
						SlipRaceCollisionVertex *const previous = vertex;
						vertex = SlipRaceCollision_AllocateVertex();
						previous->next = vertex;
						vertex->previous = previous;
					}
				} while (remainingFaceCorners != 0);
				vertex->next = firstVertex;
				firstVertex->previous = vertex;
				if ((faceCommon & SLIP_COLLISION_OUTCODE_FACE_REJECT_MASK) == 0 &&
				    (faceCombined == 0 || !SlipRaceCollision_ClipPolygon(faceCombined))) {
					SlipRaceCollision_SelectContact();
				}
			}
		}
		SlipRaceCollision_ReleasePolygon();
		++face;
		--remainingCount;
	} while (remainingCount != 0);
}

void SlipRaceCollision_TestMaxY(void) {
	uint32_t commonFlags = UINT32_MAX;
	uint32_t remainingCount = SLIP_TRACK_BOUNDING_CORNER_COUNT;
	uint32_t cornerIndex = 0;
	int32_t (*face)[SLIP_COLLISION_FACE_VALUE_COUNT];
	SlipRaceCollisionBounds *const bounds = &SlipRaceCollision_sourceBounds;

	if (SlipRaceCollision_deltaY >= 0)
		return;
	do {
		SlipRaceCollisionCornerState *const state = &SlipRaceCollision_cornerState[cornerIndex];
		uint32_t flags = 0;

		state->originalX = SlipRaceCollision_corners[cornerIndex][0];
		state->originalY = SlipRaceCollision_corners[cornerIndex][1];
		state->originalZ = SlipRaceCollision_corners[cornerIndex][2];
		if (state->originalY < bounds->maxY)
			flags = SLIP_COLLISION_OUTCODE_PLANE_SIDE;
		state->flags = flags;
		commonFlags &= flags;
		++cornerIndex;
		--remainingCount;
	} while (remainingCount != 0);
	if (commonFlags != 0)
		return;
	remainingCount = SLIP_TRACK_BOUNDING_CORNER_COUNT;
	cornerIndex = 0;
	do {
		int32_t coordinate = SlipRaceCollision_cornerState[cornerIndex].originalY;
		if (coordinate < bounds->maxY)
			break;
		coordinate = (int32_t)((uint32_t)coordinate - (uint32_t)bounds->maxY);
		if (coordinate < SlipRaceCollision_contact.time)
			break;
		++cornerIndex;
		--remainingCount;
	} while (remainingCount != 0);
	if (remainingCount == 0)
		return;
	{
		const int32_t speedAlongAxis = -(int32_t)SlipRaceCollision_velocityY;
		if (speedAlongAxis < SLIP_COLLISION_MINIMUM_AXIS_SPEED)
			return;
		SlipRaceCollision_inverseVelocity = SLIP_COLLISION_RECIPROCAL_ONE / (uint32_t)speedAlongAxis;
	}
	face = SlipRaceCollision_faces;
	remainingCount = SLIP_COLLISION_BOX_FACE_COUNT;
	do {
		if ((*face)[SLIP_COLLISION_FACE_ENABLED_INDEX] != 0) {
			SlipView3DVec16 normal = {(int16_t)(*face)[SLIP_COLLISION_FACE_NORMAL_X_INDEX],
			                          (int16_t)(*face)[SLIP_COLLISION_FACE_NORMAL_Y_INDEX],
			                          (int16_t)(*face)[SLIP_COLLISION_FACE_NORMAL_Z_INDEX]};
			const int16_t normalAlongAxisQ14 =
			    (int16_t)SlipView3D_ProjectColumn1(&SlipRaceCollision_relativeMatrix, normal);

			if (normalAlongAxisQ14 < 0) {
				uint32_t faceCommon = UINT32_MAX;
				uint32_t faceCombined = 0;
				uint32_t remainingFaceCorners = SLIP_COLLISION_FACE_CORNER_COUNT;
				int32_t *cornerIndexPointer = *face;
				SlipRaceCollisionVertex *vertex = SlipRaceCollision_AllocateVertex();
				SlipRaceCollisionVertex *const firstVertex = vertex;

				SlipRaceCollision_activeList = vertex;
				do {
					const uint32_t stateIndex = (uint32_t)*cornerIndexPointer;
					SlipRaceCollisionCornerState *const state = &SlipRaceCollision_cornerState[stateIndex];
					uint32_t outcode;

					if ((state->flags & SLIP_COLLISION_CORNER_PROJECTED) == 0) {
						const int32_t distanceToPlane = (int32_t)((uint32_t)state->originalY - (uint32_t)bounds->maxY);
						const int64_t intersectionTimeProduct =
						    (int64_t)distanceToPlane * (int64_t)(int32_t)SlipRaceCollision_inverseVelocity;
						const uint32_t productLow = (uint32_t)intersectionTimeProduct;
						int32_t intersectionTime = (int32_t)((productLow >> SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT) |
						                                     ((uint32_t)((uint64_t)intersectionTimeProduct >> 32)
						                                      << (32 - SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT)));

						intersectionTime =
						    (int32_t)((uint32_t)intersectionTime +
						              ((productLow >> (SLIP_COLLISION_RECIPROCAL_TO_Q14_SHIFT - 1)) & 1u));
						state->time = intersectionTime;
						vertex->time = intersectionTime;
						state->projectedZ =
						    (int32_t)((uint32_t)state->originalZ + (uint32_t)SlipRaceCollision_ScaleDirectionQ14(
						                                               SlipRaceCollision_velocityZ, intersectionTime));
						vertex->z = state->projectedZ;
						state->projectedX =
						    (int32_t)((uint32_t)state->originalX + (uint32_t)SlipRaceCollision_ScaleDirectionQ14(
						                                               SlipRaceCollision_velocityX, intersectionTime));
						vertex->x = state->projectedX;
						state->projectedY = bounds->maxY;
						vertex->y = bounds->maxY;
						outcode = 1u;
						if (vertex->x < bounds->minX)
							outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_X;
						if (vertex->x > bounds->maxX)
							outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_X;
						if (vertex->y < bounds->minY)
							outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Y;
						if (vertex->y > bounds->maxY)
							outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Y;
						if (vertex->z < bounds->minZ)
							outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Z;
						if (vertex->z > bounds->maxZ)
							outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Z;
						vertex->outcode = outcode;
					} else {
						SlipRaceCollision_CopyVertex((const SlipRaceCollisionCopySource *)state, vertex);
						outcode = vertex->outcode;
					}
					faceCombined |= outcode;
					faceCommon &= outcode;
					++cornerIndexPointer;
					--remainingFaceCorners;
					if (remainingFaceCorners != 0) {
						SlipRaceCollisionVertex *const previous = vertex;
						vertex = SlipRaceCollision_AllocateVertex();
						previous->next = vertex;
						vertex->previous = previous;
					}
				} while (remainingFaceCorners != 0);
				vertex->next = firstVertex;
				firstVertex->previous = vertex;
				if ((faceCommon & SLIP_COLLISION_OUTCODE_FACE_REJECT_MASK) == 0 &&
				    (faceCombined == 0 || !SlipRaceCollision_ClipPolygon(faceCombined))) {
					SlipRaceCollision_SelectContact();
				}
			}
		}
		SlipRaceCollision_ReleasePolygon();
		++face;
		--remainingCount;
	} while (remainingCount != 0);
}

void SlipRaceCollision_ReleasePolygon(void) {
	SlipRaceCollisionVertex *vertex = SlipRaceCollision_activeList;

	if (vertex != NULL) {
		do {
			SlipRaceCollisionVertex *const nextTraversalVertex = vertex->next;
			SlipRaceCollisionVertex *const nextLinkedVertex = vertex->next;
			SlipRaceCollisionVertex *const previous = vertex->previous;
			SlipRaceCollisionVertex *freeInsertionPredecessor;
			SlipRaceCollisionVertex *freeInsertionSuccessor;

			previous->next = nextLinkedVertex;
			nextLinkedVertex->previous = previous;
			freeInsertionPredecessor = SlipRaceCollision_freeList->next;
			freeInsertionSuccessor = freeInsertionPredecessor->next;
			freeInsertionSuccessor->previous = vertex;
			freeInsertionPredecessor->next = vertex;
			vertex->previous = freeInsertionPredecessor;
			vertex->next = freeInsertionSuccessor;
			vertex = nextTraversalVertex;
		} while (vertex != SlipRaceCollision_activeList);
		SlipRaceCollision_activeList = NULL;
	}
}

void SlipRaceCollision_ReleaseVertex(SlipRaceCollisionVertex *vertex) {
	SlipRaceCollisionVertex *const next = vertex->next;
	SlipRaceCollisionVertex *const previous = vertex->previous;
	SlipRaceCollisionVertex *firstFreeVertex;

	previous->next = next;
	next->previous = previous;
	firstFreeVertex = SlipRaceCollision_freeList->next;
	SlipRaceCollision_freeList->next = vertex;
	firstFreeVertex->previous = vertex;
	vertex->next = firstFreeVertex;
	vertex->previous = SlipRaceCollision_freeList;
}

static uint32_t SlipRaceCollision_IntersectionFractionQ30(int32_t target, int32_t from, int32_t to) {
	uint32_t numerator = (uint32_t)target - (uint32_t)from;
	uint32_t denominator = (uint32_t)to - (uint32_t)from;

	if ((int32_t)denominator < 0) {
		denominator = 0u - denominator;
		numerator = 0u - numerator;
	}
	return (uint32_t)(((uint64_t)numerator << SLIP_COLLISION_RECIPROCAL_FRACTION_BITS) / denominator);
}

void SlipRaceCollision_InterpolateX(const SlipRaceCollisionVertex *insideVertex,
                                    SlipRaceCollisionVertex *intersectionVertex, int32_t planeX) {
	uint32_t fraction;
	uint32_t outcode;
	SlipRaceCollisionBounds *const bounds = &SlipRaceCollision_sourceBounds;

	fraction = SlipRaceCollision_IntersectionFractionQ30(planeX, intersectionVertex->x, insideVertex->x);
	intersectionVertex->y =
	    (int32_t)((uint32_t)intersectionVertex->y +
	              (uint32_t)SlipRaceCollision_ScaleDifferenceQ30(
	                  (int32_t)((uint32_t)insideVertex->y - (uint32_t)intersectionVertex->y), fraction));
	intersectionVertex->z =
	    (int32_t)((uint32_t)intersectionVertex->z +
	              (uint32_t)SlipRaceCollision_ScaleDifferenceQ30(
	                  (int32_t)((uint32_t)insideVertex->z - (uint32_t)intersectionVertex->z), fraction));
	SlipRaceCollision_InterpolateTime(insideVertex, intersectionVertex, fraction);
	intersectionVertex->x = planeX;
	outcode = intersectionVertex->outcode & SLIP_COLLISION_OUTCODE_PRESERVED_MASK;
	if (intersectionVertex->x < bounds->minX)
		outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_X;
	if (intersectionVertex->x > bounds->maxX)
		outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_X;
	if (intersectionVertex->y < bounds->minY)
		outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Y;
	if (intersectionVertex->y > bounds->maxY)
		outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Y;
	if (intersectionVertex->z < bounds->minZ)
		outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Z;
	if (intersectionVertex->z > bounds->maxZ)
		outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Z;
	intersectionVertex->outcode = outcode;
}

void SlipRaceCollision_InterpolateY(const SlipRaceCollisionVertex *insideVertex,
                                    SlipRaceCollisionVertex *intersectionVertex, int32_t planeY) {
	uint32_t fraction;
	uint32_t outcode;
	SlipRaceCollisionBounds *const bounds = &SlipRaceCollision_sourceBounds;

	fraction = SlipRaceCollision_IntersectionFractionQ30(planeY, intersectionVertex->y, insideVertex->y);
	intersectionVertex->x =
	    (int32_t)((uint32_t)intersectionVertex->x +
	              (uint32_t)SlipRaceCollision_ScaleDifferenceQ30(
	                  (int32_t)((uint32_t)insideVertex->x - (uint32_t)intersectionVertex->x), fraction));
	intersectionVertex->z =
	    (int32_t)((uint32_t)intersectionVertex->z +
	              (uint32_t)SlipRaceCollision_ScaleDifferenceQ30(
	                  (int32_t)((uint32_t)insideVertex->z - (uint32_t)intersectionVertex->z), fraction));
	SlipRaceCollision_InterpolateTime(insideVertex, intersectionVertex, fraction);
	intersectionVertex->y = planeY;
	outcode = intersectionVertex->outcode & SLIP_COLLISION_OUTCODE_PRESERVED_MASK;
	if (intersectionVertex->x < bounds->minX)
		outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_X;
	if (intersectionVertex->x > bounds->maxX)
		outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_X;
	if (intersectionVertex->y < bounds->minY)
		outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Y;
	if (intersectionVertex->y > bounds->maxY)
		outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Y;
	if (intersectionVertex->z < bounds->minZ)
		outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Z;
	if (intersectionVertex->z > bounds->maxZ)
		outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Z;
	intersectionVertex->outcode = outcode;
}

void SlipRaceCollision_InterpolateZ(const SlipRaceCollisionVertex *insideVertex,
                                    SlipRaceCollisionVertex *intersectionVertex, int32_t planeZ) {
	uint32_t fraction;
	uint32_t outcode;
	SlipRaceCollisionBounds *const bounds = &SlipRaceCollision_sourceBounds;

	fraction = SlipRaceCollision_IntersectionFractionQ30(planeZ, intersectionVertex->z, insideVertex->z);
	intersectionVertex->x =
	    (int32_t)((uint32_t)intersectionVertex->x +
	              (uint32_t)SlipRaceCollision_ScaleDifferenceQ30(
	                  (int32_t)((uint32_t)insideVertex->x - (uint32_t)intersectionVertex->x), fraction));
	intersectionVertex->y =
	    (int32_t)((uint32_t)intersectionVertex->y +
	              (uint32_t)SlipRaceCollision_ScaleDifferenceQ30(
	                  (int32_t)((uint32_t)insideVertex->y - (uint32_t)intersectionVertex->y), fraction));
	SlipRaceCollision_InterpolateTime(insideVertex, intersectionVertex, fraction);
	intersectionVertex->z = planeZ;
	outcode = intersectionVertex->outcode & SLIP_COLLISION_OUTCODE_PRESERVED_MASK;
	if (intersectionVertex->x < bounds->minX)
		outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_X;
	if (intersectionVertex->x > bounds->maxX)
		outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_X;
	if (intersectionVertex->y < bounds->minY)
		outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Y;
	if (intersectionVertex->y > bounds->maxY)
		outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Y;
	if (intersectionVertex->z < bounds->minZ)
		outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Z;
	if (intersectionVertex->z > bounds->maxZ)
		outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Z;
	intersectionVertex->outcode = outcode;
}

bool SlipRaceCollision_RejectMovingBox(uint32_t *commonOutcode) {
	uint32_t remainingCount = SLIP_TRACK_BOUNDING_CORNER_COUNT;
	uint32_t sharedOutcode = UINT32_MAX;
	const int32_t (*corner)[3] = SlipRaceCollision_corners;
	SlipRaceCollisionBounds *const bounds = &SlipRaceCollision_sourceBounds;

	do {
		uint32_t outcode = 0;
		int32_t coordinate = (*corner)[0];

		if (coordinate < bounds->minX)
			outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_X;
		if (coordinate > bounds->maxX)
			outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_X;
		coordinate = (*corner)[1];
		if (coordinate < bounds->minY)
			outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Y;
		if (coordinate > bounds->maxY)
			outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Y;
		coordinate = (*corner)[2];
		if (coordinate < bounds->minZ)
			outcode |= SLIP_COLLISION_OUTCODE_MINIMUM_Z;
		if (coordinate > bounds->maxZ)
			outcode |= SLIP_COLLISION_OUTCODE_MAXIMUM_Z;
		sharedOutcode &= outcode;
		++corner;
		--remainingCount;
	} while (remainingCount != 0);
	*commonOutcode = sharedOutcode;
	if (SlipRaceCollision_deltaX != 0) {
		if (SlipRaceCollision_deltaX > 0) {
			if ((sharedOutcode & SLIP_COLLISION_OUTCODE_MAXIMUM_X) != 0)
				return true;
		} else if ((sharedOutcode & SLIP_COLLISION_OUTCODE_MINIMUM_X) != 0) {
			return true;
		}
	}
	if (SlipRaceCollision_deltaY != 0) {
		if (SlipRaceCollision_deltaY > 0) {
			if ((sharedOutcode & SLIP_COLLISION_OUTCODE_MAXIMUM_Y) != 0)
				return true;
		} else if ((sharedOutcode & SLIP_COLLISION_OUTCODE_MINIMUM_Y) != 0) {
			return true;
		}
	}
	if (SlipRaceCollision_deltaZ != 0) {
		if (SlipRaceCollision_deltaZ > 0) {
			if ((sharedOutcode & SLIP_COLLISION_OUTCODE_MAXIMUM_Z) != 0)
				return true;
		} else if ((sharedOutcode & SLIP_COLLISION_OUTCODE_MINIMUM_Z) != 0) {
			return true;
		}
	}
	return false;
}

void SlipRaceCollision_TestAllFaces(void) {
	uint32_t commonOutcode;

	if (SlipRaceCollision_RejectMovingBox(&commonOutcode))
		return;
	SlipRaceCollision_TestMinZ();
	SlipRaceCollision_TestMaxZ();
	SlipRaceCollision_TestMinX();
	SlipRaceCollision_TestMaxX();
	SlipRaceCollision_TestMaxY();
	SlipRaceCollision_TestMinY();
}

bool SlipRaceCollision_TestBodies(void) {
	const uint8_t *body = SlipRaceCollision_sourceBody;
	SlipObjectPosition objectPosition;
	SlipObjectMatrixCopy matrixCopy;
	SlipView3DMatrix objectTransformMatrix;
	SlipView3DMatrix secondMatrix;
	SlipView3DVec32 transformedVector;
	SlipView3DNormalizeVector3D normalized;
	uint16_t object;
	uint32_t rejectDistance;
	int32_t coordinate;
	uint32_t dot;
	uint32_t forwardFaceEnabled;
	uint32_t oppositeFaceEnabled;

	SlipRaceCollision_BuildSourceMotion();
	object = ((const SlipRaceCollisionBody *)(const void *)body)->objectHandle;
	SlipObject_Position(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, object, &objectPosition);
	SlipRaceCollision_sourcePositionX = (int32_t)objectPosition.positionX;
	SlipRaceCollision_sourcePositionY = (int32_t)objectPosition.positionY;
	SlipRaceCollision_sourcePositionZ = (int32_t)objectPosition.positionZ;
	SlipRaceCollision_sourceRadius = ((const SlipRaceCollisionBody *)(const void *)body)->radius;
	SlipRaceCollision_sourceBounds.minX = (int32_t)((const SlipRaceCollisionBody *)(const void *)body)->minimumBounds.x;
	SlipRaceCollision_sourceBounds.minY = (int32_t)((const SlipRaceCollisionBody *)(const void *)body)->minimumBounds.y;
	SlipRaceCollision_sourceBounds.minZ = (int32_t)((const SlipRaceCollisionBody *)(const void *)body)->minimumBounds.z;
	SlipRaceCollision_sourceBounds.maxX = (int32_t)((const SlipRaceCollisionBody *)(const void *)body)->maximumBounds.x;
	SlipRaceCollision_sourceBounds.maxY = (int32_t)((const SlipRaceCollisionBody *)(const void *)body)->maximumBounds.y;
	SlipRaceCollision_sourceBounds.maxZ = (int32_t)((const SlipRaceCollisionBody *)(const void *)body)->maximumBounds.z;

	body = SlipRaceCollision_targetBody;
	SlipRaceCollision_BuildTargetMotion();
	object = ((const SlipRaceCollisionBody *)(const void *)body)->objectHandle;
	SlipObject_Position(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, object, &objectPosition);
	SlipRaceCollision_workspace.position[0] = (int32_t)objectPosition.positionX;
	SlipRaceCollision_workspace.position[1] = (int32_t)objectPosition.positionY;
	SlipRaceCollision_workspace.position[2] = (int32_t)objectPosition.positionZ;
	SlipRaceCollision_targetRadius = ((const SlipRaceCollisionBody *)(const void *)body)->radius;
	rejectDistance = SlipRaceCollision_sourceRadius + SlipRaceCollision_targetRadius +
	                 (uint32_t)SlipRaceCollision_sourceMotion.movementDistance +
	                 (uint32_t)SlipRaceCollision_targetMotion.movementDistance;
	SlipRaceCollision_relativePositionX =
	    (int32_t)((uint32_t)SlipRaceCollision_workspace.position[0] - (uint32_t)SlipRaceCollision_sourcePositionX);
	SlipRaceCollision_relativePositionY =
	    (int32_t)((uint32_t)SlipRaceCollision_workspace.position[1] - (uint32_t)SlipRaceCollision_sourcePositionY);
	SlipRaceCollision_relativePositionZ =
	    (int32_t)((uint32_t)SlipRaceCollision_workspace.position[2] - (uint32_t)SlipRaceCollision_sourcePositionZ);
	coordinate = SlipRaceCollision_relativePositionX;
	if (coordinate < 0)
		coordinate = (int32_t)(0u - (uint32_t)coordinate);
	if (coordinate > (int32_t)rejectDistance)
		return false;
	coordinate = SlipRaceCollision_relativePositionY;
	if (coordinate < 0)
		coordinate = (int32_t)(0u - (uint32_t)coordinate);
	if (coordinate > (int32_t)rejectDistance)
		return false;
	coordinate = SlipRaceCollision_relativePositionZ;
	if (coordinate < 0)
		coordinate = (int32_t)(0u - (uint32_t)coordinate);
	if (coordinate > (int32_t)rejectDistance)
		return false;
	SlipRaceCollision_distance = (int32_t)SlipView3D_VectorLength(
	    SlipRaceCollision_relativePositionX, SlipRaceCollision_relativePositionY, SlipRaceCollision_relativePositionZ);
	if (SlipRaceCollision_distance > (int32_t)rejectDistance)
		return false;

	SlipRaceCollision_deltaX = (int32_t)((uint32_t)SlipRaceCollision_targetMotion.displacementX -
	                                     (uint32_t)SlipRaceCollision_sourceMotion.displacementX);
	SlipRaceCollision_deltaY = (int32_t)((uint32_t)SlipRaceCollision_targetMotion.displacementY -
	                                     (uint32_t)SlipRaceCollision_sourceMotion.displacementY);
	SlipRaceCollision_deltaZ = (int32_t)((uint32_t)SlipRaceCollision_targetMotion.displacementZ -
	                                     (uint32_t)SlipRaceCollision_sourceMotion.displacementZ);
	if (((uint32_t)SlipRaceCollision_deltaX | (uint32_t)SlipRaceCollision_deltaY |
	     (uint32_t)SlipRaceCollision_deltaZ) == 0)
		return false;

	object = ((const SlipRaceCollisionBody *)(const void *)SlipRaceCollision_sourceBody)->objectHandle;
	SlipObject_MatrixCopy(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, object,
	                      &objectTransformMatrix, &matrixCopy);
	transformedVector = (SlipView3DVec32){SlipRaceCollision_relativePositionX, SlipRaceCollision_relativePositionY,
	                                      SlipRaceCollision_relativePositionZ};
	transformedVector = SlipView3D_TransformPositionByRows(&objectTransformMatrix, transformedVector);
	SlipRaceCollision_relativePositionX = transformedVector.x;
	SlipRaceCollision_relativePositionY = transformedVector.y;
	SlipRaceCollision_relativePositionZ = transformedVector.z;
	transformedVector = (SlipView3DVec32){SlipRaceCollision_deltaX, SlipRaceCollision_deltaY, SlipRaceCollision_deltaZ};
	transformedVector = SlipView3D_TransformPositionByRows(&objectTransformMatrix, transformedVector);
	SlipRaceCollision_deltaX = transformedVector.x;
	SlipRaceCollision_deltaY = transformedVector.y;
	SlipRaceCollision_deltaZ = transformedVector.z;
	SlipRaceCollision_velocityLength =
	    SlipView3D_VectorLength(SlipRaceCollision_deltaX, SlipRaceCollision_deltaY, SlipRaceCollision_deltaZ);
	SlipView3D_CopyMatrixWords((uint8_t *)&SlipRaceCollision_sourceMatrix, sizeof(SlipRaceCollision_sourceMatrix),
	                           (const uint8_t *)&objectTransformMatrix, sizeof(objectTransformMatrix));
	object = ((const SlipRaceCollisionBody *)(const void *)SlipRaceCollision_targetBody)->objectHandle;
	SlipObject_MatrixCopy(SlipRaceCollision_objectTable, SlipRaceCollision_objectTableBytes, object, &secondMatrix,
	                      &matrixCopy);
	SlipView3D_ComposeMatrix(&secondMatrix, &SlipRaceCollision_sourceMatrix, &SlipRaceCollision_relativeMatrix);
	SlipView3D_NormalizeVector3D((uint32_t)SlipRaceCollision_deltaX, (uint32_t)SlipRaceCollision_deltaY,
	                             (uint32_t)SlipRaceCollision_deltaZ, &normalized);
	SlipRaceCollision_velocityX = (int16_t)(uint16_t)normalized.unitXQ14;
	SlipRaceCollision_velocityY = (int16_t)(uint16_t)normalized.unitYQ14;
	SlipRaceCollision_velocityZ = (int16_t)(uint16_t)normalized.unitZQ14;

	dot = SlipView3D_DotProductQ14(
	    (uint16_t)SlipRaceCollision_velocityX, (uint16_t)SlipRaceCollision_velocityY,
	    (uint16_t)SlipRaceCollision_velocityZ, (uint16_t)SlipRaceCollision_relativeMatrix.m[6],
	    (uint16_t)SlipRaceCollision_relativeMatrix.m[7], (uint16_t)SlipRaceCollision_relativeMatrix.m[8], NULL);
	if ((int16_t)(uint16_t)dot >= SLIP_COLLISION_FORWARD_FACE_ALIGNMENT_Q14) {
		SlipRaceCollision_faces[SLIP_COLLISION_FACE_POSITIVE_Z][SLIP_COLLISION_FACE_ENABLED_INDEX] = -1;
		SlipRaceCollision_faces[SLIP_COLLISION_FACE_NEGATIVE_Z][SLIP_COLLISION_FACE_ENABLED_INDEX] = 0;
		SlipRaceCollision_faces[SLIP_COLLISION_FACE_POSITIVE_X][SLIP_COLLISION_FACE_ENABLED_INDEX] = 0;
		SlipRaceCollision_faces[SLIP_COLLISION_FACE_NEGATIVE_X][SLIP_COLLISION_FACE_ENABLED_INDEX] = 0;
		SlipRaceCollision_faces[SLIP_COLLISION_FACE_POSITIVE_Y][SLIP_COLLISION_FACE_ENABLED_INDEX] = 0;
		SlipRaceCollision_faces[SLIP_COLLISION_FACE_NEGATIVE_Y][SLIP_COLLISION_FACE_ENABLED_INDEX] = 0;
	} else {
		oppositeFaceEnabled = 0;
		forwardFaceEnabled = UINT32_MAX;
		if ((int16_t)(uint16_t)dot < 0) {
			const uint32_t exchange = forwardFaceEnabled;
			forwardFaceEnabled = oppositeFaceEnabled;
			oppositeFaceEnabled = exchange;
		}
		SlipRaceCollision_faces[SLIP_COLLISION_FACE_POSITIVE_Z][SLIP_COLLISION_FACE_ENABLED_INDEX] =
		    (int32_t)forwardFaceEnabled;
		SlipRaceCollision_faces[SLIP_COLLISION_FACE_NEGATIVE_Z][SLIP_COLLISION_FACE_ENABLED_INDEX] =
		    (int32_t)oppositeFaceEnabled;
		dot = SlipView3D_DotProductQ14(
		    (uint16_t)SlipRaceCollision_velocityX, (uint16_t)SlipRaceCollision_velocityY,
		    (uint16_t)SlipRaceCollision_velocityZ, (uint16_t)SlipRaceCollision_relativeMatrix.m[0],
		    (uint16_t)SlipRaceCollision_relativeMatrix.m[1], (uint16_t)SlipRaceCollision_relativeMatrix.m[2], NULL);
		oppositeFaceEnabled = 0;
		forwardFaceEnabled = UINT32_MAX;
		if ((int16_t)(uint16_t)dot < 0) {
			const uint32_t exchange = forwardFaceEnabled;
			forwardFaceEnabled = oppositeFaceEnabled;
			oppositeFaceEnabled = exchange;
		}
		SlipRaceCollision_faces[SLIP_COLLISION_FACE_POSITIVE_X][SLIP_COLLISION_FACE_ENABLED_INDEX] =
		    (int32_t)forwardFaceEnabled;
		SlipRaceCollision_faces[SLIP_COLLISION_FACE_NEGATIVE_X][SLIP_COLLISION_FACE_ENABLED_INDEX] =
		    (int32_t)oppositeFaceEnabled;
		dot = SlipView3D_DotProductQ14(
		    (uint16_t)SlipRaceCollision_velocityX, (uint16_t)SlipRaceCollision_velocityY,
		    (uint16_t)SlipRaceCollision_velocityZ, (uint16_t)SlipRaceCollision_relativeMatrix.m[3],
		    (uint16_t)SlipRaceCollision_relativeMatrix.m[4], (uint16_t)SlipRaceCollision_relativeMatrix.m[5], NULL);
		oppositeFaceEnabled = 0;
		forwardFaceEnabled = UINT32_MAX;
		if ((int16_t)(uint16_t)dot < 0) {
			const uint32_t exchange = forwardFaceEnabled;
			forwardFaceEnabled = oppositeFaceEnabled;
			oppositeFaceEnabled = exchange;
		}
		SlipRaceCollision_faces[SLIP_COLLISION_FACE_POSITIVE_Y][SLIP_COLLISION_FACE_ENABLED_INDEX] =
		    (int32_t)forwardFaceEnabled;
		SlipRaceCollision_faces[SLIP_COLLISION_FACE_NEGATIVE_Y][SLIP_COLLISION_FACE_ENABLED_INDEX] =
		    (int32_t)oppositeFaceEnabled;
	}
	SlipRaceCollision_contact.time = (int32_t)SlipRaceCollision_velocityLength;
	SlipRaceCollision_BuildBodyCorners(
	    (SlipView3DVec32){SlipRaceCollision_relativePositionX, SlipRaceCollision_relativePositionY,
	                      SlipRaceCollision_relativePositionZ},
	    &SlipRaceCollision_relativeMatrix,
	    ((const SlipRaceCollisionBody *)(const void *)SlipRaceCollision_targetBody)->objectHandle);
	SlipRaceCollision_TestAllFaces();
	return (uint32_t)SlipRaceCollision_contact.time < SlipRaceCollision_velocityLength;
}

void SlipRaceCollision_RecordContact(uint32_t otherObjectHighBits) {
	const uint32_t contactDistance = (uint32_t)SlipRaceCollision_contact.time;
	uint64_t distanceDividendQ30;
	uint32_t contactFraction;
	uint32_t frameTimeProduct;
	uint16_t contactTime;
	SlipRaceCollisionBody *firstBody;
	SlipRaceCollisionBody *secondBody;

	if (contactDistance >= SlipRaceCollision_velocityLength)
		return;
	distanceDividendQ30 = (uint64_t)contactDistance << SLIP_COLLISION_RECIPROCAL_FRACTION_BITS;
	contactFraction = (uint32_t)(distanceDividendQ30 / SlipRaceCollision_velocityLength);
	contactFraction >>= SLIP_COLLISION_RECIPROCAL_FRACTION_BITS - SLIP_Q14_FRACTION_BITS;
	frameTimeProduct = (uint32_t)(uint16_t)contactFraction * (uint16_t)SlipRaceCollision_frameStep;
	contactTime = (uint16_t)(((uint16_t)frameTimeProduct >> SLIP_Q14_FRACTION_BITS) |
	                         ((uint16_t)(frameTimeProduct >> 16) << SLIP_Q14_WORD_HIGH_SHIFT));
	if (contactTime >= SlipRaceCollision_firstTime)
		return;
	SlipRaceCollision_firstTime = contactTime;
	firstBody = (SlipRaceCollisionBody *)(void *)SlipRaceCollision_sourceBody;
	secondBody = (SlipRaceCollisionBody *)(void *)SlipRaceCollision_targetBody;
	firstBody->contactTime = contactTime;
	firstBody->otherObject = (otherObjectHighBits & SLIP_COLLISION_PACKED_UPPER_WORD_MASK) | secondBody->objectHandle;
	firstBody->contactType = SLIP_COLLISION_CONTACT_BODY;
	secondBody->contactTime = contactTime;
	secondBody->otherObject = (otherObjectHighBits & SLIP_COLLISION_PACKED_UPPER_WORD_MASK) | firstBody->objectHandle;
	secondBody->contactType = SLIP_COLLISION_CONTACT_BODY;
	SlipRaceCollision_BuildContactResponse();
}

void SlipRaceCollision_BuildContactResponse(void) {
	SlipView3DVec32 contactPoint = {SlipRaceCollision_contact.x, SlipRaceCollision_contact.y,
	                                SlipRaceCollision_contact.z};
	SlipObjectDirection direction;
	SlipView3DVec32 scaled;
	SlipView3DVec32 sourceVelocity;
	SlipView3DVec32 targetVelocity;
	SlipView3DNormalizeVector3D normalized;
	uint16_t object;
	uint32_t contactTime;
	int32_t speed;
	int64_t timeSpeedProduct;
	int32_t travelDistance;
	SlipView3DVec32 contactPosition;
	SlipRaceCollisionBody *const firstBody = (SlipRaceCollisionBody *)(void *)SlipRaceCollision_sourceBody;
	SlipRaceCollisionBody *const secondBody = (SlipRaceCollisionBody *)(void *)SlipRaceCollision_targetBody;

	contactPoint = SlipView3D_TransformPositionByColumns(&SlipRaceCollision_sourceMatrix, contactPoint);
	SlipRaceCollision_contactOrigin.x =
	    (int32_t)((uint32_t)contactPoint.x + (uint32_t)SlipRaceCollision_sourcePositionX);
	SlipRaceCollision_contactOrigin.y =
	    (int32_t)((uint32_t)contactPoint.y + (uint32_t)SlipRaceCollision_sourcePositionY);
	SlipRaceCollision_contactOrigin.z =
	    (int32_t)((uint32_t)contactPoint.z + (uint32_t)SlipRaceCollision_sourcePositionZ);
	object = firstBody->objectHandle;
	contactTime = firstBody->contactTime;
	speed = SlipObject_Speed(SlipRaceCollision_objectTable, object);
	timeSpeedProduct = (int64_t)(int32_t)contactTime * speed;
	travelDistance = (int32_t)(((uint32_t)timeSpeedProduct >> SLIP_Q14_FRACTION_BITS) |
	                           ((uint32_t)((uint64_t)timeSpeedProduct >> 32) << SLIP_Q14_DWORD_HIGH_SHIFT));
	direction = SlipObject_Direction(SlipRaceCollision_objectTable, object);
	scaled = SlipView3D_ScaleVector(direction.directionXQ14, direction.directionYQ14, direction.directionZQ14,
	                                travelDistance);
	contactPosition.x = (int32_t)((uint32_t)SlipRaceCollision_contactOrigin.x + (uint32_t)scaled.x);
	contactPosition.y = (int32_t)((uint32_t)SlipRaceCollision_contactOrigin.y + (uint32_t)scaled.y);
	contactPosition.z = (int32_t)((uint32_t)SlipRaceCollision_contactOrigin.z + (uint32_t)scaled.z);
	firstBody->contactPosition.x = contactPosition.x;
	firstBody->contactPosition.y = contactPosition.y;
	firstBody->contactPosition.z = contactPosition.z;
	secondBody->contactPosition.x = contactPosition.x;
	secondBody->contactPosition.y = contactPosition.y;
	secondBody->contactPosition.z = contactPosition.z;

	object = secondBody->objectHandle;
	contactTime = secondBody->contactTime;
	speed = SlipObject_Speed(SlipRaceCollision_objectTable, object);
	timeSpeedProduct = (int64_t)(int32_t)contactTime * speed;
	travelDistance = (int32_t)(((uint32_t)timeSpeedProduct >> SLIP_Q14_FRACTION_BITS) |
	                           ((uint32_t)((uint64_t)timeSpeedProduct >> 32) << SLIP_Q14_DWORD_HIGH_SHIFT));
	travelDistance = (int32_t)(0u - (uint32_t)travelDistance);
	direction = SlipObject_Direction(SlipRaceCollision_objectTable, object);
	scaled = SlipView3D_ScaleVector(direction.directionXQ14, direction.directionYQ14, direction.directionZQ14,
	                                travelDistance);
	SlipRaceCollision_targetContactPoint.x = (int32_t)((uint32_t)contactPosition.x + (uint32_t)scaled.x);
	SlipRaceCollision_targetContactPoint.y = (int32_t)((uint32_t)contactPosition.y + (uint32_t)scaled.y);
	SlipRaceCollision_targetContactPoint.z = (int32_t)((uint32_t)contactPosition.z + (uint32_t)scaled.z);
	targetVelocity = SlipObject_Velocity(SlipRaceCollision_objectTable, secondBody->objectHandle);
	sourceVelocity = SlipObject_Velocity(SlipRaceCollision_objectTable, firstBody->objectHandle);
	SlipView3D_NormalizeVector3D((uint32_t)sourceVelocity.x - (uint32_t)targetVelocity.x,
	                             (uint32_t)sourceVelocity.y - (uint32_t)targetVelocity.y,
	                             (uint32_t)sourceVelocity.z - (uint32_t)targetVelocity.z, &normalized);
	secondBody->normalX = (int16_t)normalized.unitXQ14;
	secondBody->normalY = (int16_t)normalized.unitYQ14;
	secondBody->normalZ = (int16_t)normalized.unitZQ14;
	secondBody->impactMagnitude = (int32_t)normalized.vectorLength;
	firstBody->normalX = (int16_t)(0u - normalized.unitXQ14);
	firstBody->normalY = (int16_t)(0u - normalized.unitYQ14);
	firstBody->normalZ = (int16_t)(0u - normalized.unitZQ14);
	firstBody->impactMagnitude = (int32_t)normalized.vectorLength;
}

void SlipRaceCollision_BuildBodyCorners(SlipView3DVec32 translation, const SlipView3DMatrix *matrix, uint16_t object) {
	const uint16_t physicsOffset = SlipObject_PhysicsOffset(SlipRaceCollision_objectTable, object);
	const SlipRaceCollisionBody *body;
	int32_t *const boundsAndCorners = (int32_t *)&SlipRaceCollision_workspace.targetBounds;

	if (physicsOffset == 0) {
		SlipRuntime_Fatal("CollideSlotReadMainCubePoints - not a collision slot");
	}
	body = (const SlipRaceCollisionBody *)(const void *)SlipRaceCollision_CollisionBodyFromOffset(physicsOffset);
	SlipRaceCollision_targetBounds.minX = body->minimumBounds.x;
	SlipRaceCollision_targetBounds.minY = body->minimumBounds.y;
	SlipRaceCollision_targetBounds.minZ = body->minimumBounds.z;
	SlipRaceCollision_targetBounds.maxX = body->maximumBounds.x;
	SlipRaceCollision_targetBounds.maxY = body->maximumBounds.y;
	SlipRaceCollision_targetBounds.maxZ = body->maximumBounds.z;
	if ((int32_t)body->radius > SLIP_COLLISION_FULL_BOX_MINIMUM_RADIUS) {
		SlipView3D_BuildBoxCorners(matrix, boundsAndCorners, translation);
	} else {
		SlipView3D_BuildBoxCornersThunk(matrix, boundsAndCorners, translation);
	}
}
