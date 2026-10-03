#ifndef SLIPSTREAM5000_DRAW3D_H
#define SLIPSTREAM5000_DRAW3D_H

#include "resource.h"

typedef void (*SlipDraw3DMaterialCallback)(void);
void SlipDraw3D_RegisterMaterialCallback(SlipDraw3DMaterialCallback callback);
void SlipDraw3D_NotifyMaterials(void);

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
extern uint32_t SlipDraw3D_materialCallbackCount;

void SlipDraw3D_BindPointBuffer(uint8_t *points);
uint8_t *SlipDraw3D_PointBuffer(void);

#include "material_format.h"
#include "raster/raster.h"
#include "view3d.h"

extern uint32_t SlipDraw3D_minimumDepth;
extern uint32_t SlipDraw3D_maximumDepth;
extern uint32_t SlipDraw3D_fadeStart;
extern uint32_t SlipDraw3D_fadeEnd;
extern uint32_t SlipDraw3D_fadeRange;
extern uint32_t SlipDraw3D_fadeColour;
extern uint32_t SlipDraw3D_directLight;
extern uint32_t SlipDraw3D_ambientLight;
extern int32_t SlipDraw3D_lightX;
extern int32_t SlipDraw3D_lightY;
extern int32_t SlipDraw3D_lightZ;

void SlipDraw3D_SetMinimumDepth(uint32_t minimumDepth);
void SlipDraw3D_SetMaximumDepth(uint32_t maximumDepth);
void SlipDraw3D_SetDepthFade(uint32_t fadeStart, uint32_t fadeEnd, uint16_t fadeColour);
void SlipDraw3D_ResetLighting(void);
void SlipDraw3D_NormalizeLighting(void);
void SlipDraw3D_SetAmbientLight(uint16_t ambientLight);
void SlipDraw3D_SetLightVector(int32_t lightX, int32_t lightY, int32_t lightZ, uint16_t directLight);

/* Projection modes, scale formats, and legacy viewport defaults. */
enum {
	SLIP_DRAW3D_PROJECTION_PERSPECTIVE = 0,
	SLIP_DRAW3D_PROJECTION_ORTHOGRAPHIC = 1,
	SLIP_DRAW3D_DEFAULT_NEAR_DEPTH = 64,
	SLIP_DRAW3D_DEFAULT_FOCAL_LENGTH_SHIFT = 8,
	SLIP_DRAW3D_DEFAULT_FOCAL_LENGTH = 1 << SLIP_DRAW3D_DEFAULT_FOCAL_LENGTH_SHIFT,
	SLIP_DRAW3D_SCALE_FRACTION_BITS = 16,
	SLIP_DRAW3D_SCALE_ONE_Q16 = 1 << SLIP_DRAW3D_SCALE_FRACTION_BITS,
	SLIP_DRAW3D_ORTHOGRAPHIC_RECIPROCAL_FRACTION_BITS = 30,
	SLIP_DRAW3D_ORTHOGRAPHIC_RECIPROCAL_ONE_Q30 = 1u << SLIP_DRAW3D_ORTHOGRAPHIC_RECIPROCAL_FRACTION_BITS,
	SLIP_DRAW3D_ORTHOGRAPHIC_SCALE_MAXIMUM = 1 << 20,
	SLIP_DRAW3D_FOCAL_LENGTH_MINIMUM = 64,
	SLIP_DRAW3D_FOCAL_LENGTH_MAXIMUM = 16384,
	SLIP_DRAW3D_SHORT_COORDINATE_RADIUS_LIMIT = 16384,
	SLIP_DRAW3D_SQUARE_PIXEL_SCALE_NUMERATOR = 5,
	SLIP_DRAW3D_SQUARE_PIXEL_SCALE_DENOMINATOR = 6
};

enum {
	SLIP_DRAW3D_VERTEX_RECORD_SIZE = 0x40,
	SLIP_DRAW3D_VERTEX_BUFFER_GROWTH_RESERVE = 64,
	SLIP_DRAW3D_ACTIVE_BOUNDS_MAXIMUM_INITIAL = -28672,
	SLIP_DRAW3D_POST_PLANE_MINIMUM_LIGHT_DOT_Q14 = 512,
	SLIP_DRAW3D_DRAW_RECORD_SIZE = 0x38,
	SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE = 0x3c,
	SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT = 0x40,
	SLIP_DRAW3D_RECORD_POOL_TOTAL_COUNT = SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT + 1,
	SLIP_DRAW3D_RECORD_POOL_BYTES = SLIP_DRAW3D_RECORD_POOL_TOTAL_COUNT * SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE,
	SLIP_DRAW3D_LIST_NODE_SIZE = 0x14,
	SLIP_DRAW3D_LIST_MAX_FRAMES = 16,
	SLIP_DRAW3D_CLIP_MASK = 0x21f8,
	SLIP_DRAW3D_MATERIAL_KEY_BYTES = 0x10,
	SLIP_DRAW3D_MATERIAL_INDEX_MASK = 0x7fff,
	SLIP_DRAW3D_SPECULAR_SQUARING_STEPS = 5,
	SLIP_DRAW3D_RAW_MATERIAL_RECORD_SIZE = 0x2e,
	SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE = 0x54,
	SLIP_DRAW3D_TEXTURE_HANDLE_BYTES = 4,
	SLIP_DRAW3D_MATERIAL_FRAME_COUNT = 4,
	SLIP_DRAW3D_VERTEX_RECORD_WORLD_OFFSET = 0x00,
	SLIP_DRAW3D_VERTEX_RECORD_SCREEN_OFFSET = 0x0c,
	SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET = 0x14,
	SLIP_DRAW3D_VERTEX_RECORD_DEPTH_OFFSET = 0x18,
	/* World and screen positions, flags, and depth shared by vertex and draw records. */
	SLIP_DRAW3D_VERTEX_DRAW_PREFIX_BYTES = SLIP_DRAW3D_VERTEX_RECORD_DEPTH_OFFSET + sizeof(uint32_t),
	SLIP_DRAW3D_VERTEX_DRAW_PREFIX_DWORDS = SLIP_DRAW3D_VERTEX_DRAW_PREFIX_BYTES / sizeof(uint32_t),
	SLIP_DRAW3D_VERTEX_RECORD_SOURCE_OFFSET = 0x24,
	SLIP_DRAW3D_DRAW_RECORD_SOURCE_END = SLIP_DRAW3D_VERTEX_RECORD_SOURCE_OFFSET + sizeof(SlipView3DVec16),
	SLIP_DRAW3D_VERTEX_RECORD_SOURCE_END = SLIP_DRAW3D_DRAW_RECORD_SOURCE_END + sizeof(uint16_t),
	SLIP_DRAW3D_DRAW_RECORD_TEXTURE_OFFSET = 0x24,
	SLIP_DRAW3D_RECORD_NEXT_OFFSET = 0x34,
	SLIP_DRAW3D_RECORD_PREV_OFFSET = 0x38
};

/* Scratch fields used while clipping projected linked draw records. */
enum {
	SLIP_DRAW3D_PLANE_DISTANCE_OFFSET = 0x1c,
	SLIP_DRAW3D_EDGE_NORMAL_X_OFFSET = 0x2c,
	SLIP_DRAW3D_EDGE_NORMAL_Y_OFFSET = 0x30
};

/* Serialized background strips: shade, centre offset, then rectangle bounds. */
enum {
	SLIP_BACKGROUND_STRIP_TABLE_HEADER_BYTES = 2,
	SLIP_BACKGROUND_STRIP_BYTES = 12,
	SLIP_BACKGROUND_STRIP_SHADE_OFFSET = 0,
	SLIP_BACKGROUND_STRIP_CENTRE_OFFSET = 2,
	SLIP_BACKGROUND_STRIP_LEFT_OFFSET = 4,
	SLIP_BACKGROUND_STRIP_TOP_OFFSET = 6,
	SLIP_BACKGROUND_STRIP_RIGHT_OFFSET = 8,
	SLIP_BACKGROUND_STRIP_BOTTOM_OFFSET = 10,
	SLIP_BACKGROUND_STRIP_INITIAL_CENTRE_Q14 = 8192,
	SLIP_BACKGROUND_STRIP_FINAL_CENTRE_Q14 = -8192,
	SLIP_BACKGROUND_STRIP_ANGLE_SPAN = 0x2000,
	SLIP_BACKGROUND_STRIP_CURVATURE_DIVISOR = 8192,
	SLIP_BACKGROUND_FIXED_FILL_TILT_BIAS_Q14 = 7936,
	SLIP_BACKGROUND_MATERIAL_FILL_TILT_MAXIMUM_Q14 = -12288,
	SLIP_BACKGROUND_TILT_CENTRE_BIAS_Q14 = 192,
	SLIP_BACKGROUND_STRIP_DIAGNOSTIC_CAPACITY = 16
};

/* DOS data identities retained in diagnostic results and pointer-table mappings. */
enum {
	SLIP_DRAW3D_VERTEX_ALLOCATION_ERROR_MESSAGE_DOS_OFFSET = 0x1adb6u,
	SLIP_DRAW3D_VERTEX_GROW_ERROR_MESSAGE_DOS_OFFSET = 0x1de1fu,
	SLIP_DRAW3D_TEXTURED_RING_STATUS_FAILURE_DOS_STAGE = 0x1995bu,
	SLIP_DRAW3D_TEXTURED_RING_BUILD_FAILURE_DOS_STAGE = 0x1c753u,
	SLIP_DRAW3D_TEXTURED_RING_CLIP_FAILURE_DOS_STAGE = 0x1bc7fu,
	SLIP_BACKGROUND_FIXED_STRIP_TABLE_DOS_ADDRESS = 0x18260u,
	SLIP_BACKGROUND_FIXED_STRIP_FIRST_DOS_ADDRESS =
	    SLIP_BACKGROUND_FIXED_STRIP_TABLE_DOS_ADDRESS + SLIP_BACKGROUND_STRIP_TABLE_HEADER_BYTES,
	SLIP_BACKGROUND_MATERIAL_STRIP_TABLE_DOS_ADDRESS = 0x18562u,
	SLIP_BACKGROUND_STRIP_CORNER_POINTER_TABLE_DOS_ADDRESS = 0x1da30u
};

typedef struct SlipDraw3DVec32 {
	int32_t x;
	int32_t y;
	int32_t z;
} SlipDraw3DVec32;

typedef union SlipDraw3DVertexRecord {
	struct {
		SlipDraw3DVec32 world;
		int32_t screenX;
		int32_t screenY;
		uint32_t flags;
		int32_t depth;
		uint32_t clipMask;
		uint32_t depthFadeBlend;
		int16_t sourceX;
		int16_t sourceY;
		int16_t sourceZ;
		uint16_t sourceFollowingWord;
		uint8_t unusedTail[SLIP_DRAW3D_VERTEX_RECORD_SIZE - SLIP_DRAW3D_VERTEX_RECORD_SOURCE_END];
	};

	uint8_t bytes[SLIP_DRAW3D_VERTEX_RECORD_SIZE];
} SlipDraw3DVertexRecord;

typedef union SlipDraw3DDrawRecord {
	struct {
		uint8_t beforeTexture[SLIP_DRAW3D_DRAW_RECORD_TEXTURE_OFFSET];
		uint32_t textureU;
		uint32_t textureV;
	};

	struct {
		SlipDraw3DVec32 world;
		int32_t screenX;
		int32_t screenY;
		uint32_t flags;
		int32_t depth;
		uint32_t clipMask;
		uint16_t shade;
		uint16_t unusedShadeWord;
		int16_t sourceX;
		int16_t sourceY;
		int16_t sourceZ;
		uint8_t unusedSourceTail[SLIP_DRAW3D_DRAW_RECORD_SIZE - SLIP_DRAW3D_DRAW_RECORD_SOURCE_END];
	};

	uint8_t bytes[SLIP_DRAW3D_DRAW_RECORD_SIZE];
} SlipDraw3DDrawRecord;

typedef char SlipDraw3DVertexRecordSizeCheck[sizeof(SlipDraw3DVertexRecord) == SLIP_DRAW3D_VERTEX_RECORD_SIZE ? 1 : -1];
typedef char SlipDraw3DDrawRecordSizeCheck[sizeof(SlipDraw3DDrawRecord) == SLIP_DRAW3D_DRAW_RECORD_SIZE ? 1 : -1];

typedef struct SlipDraw3DLinkedDrawRecordLinks {
	uint8_t drawPayload[SLIP_DRAW3D_RECORD_NEXT_OFFSET];
	uint32_t nextOffset;
	uint32_t prevOffset;
} SlipDraw3DLinkedDrawRecordLinks;

typedef union SlipDraw3DLinkedDrawRecord {
	SlipDraw3DLinkedDrawRecordLinks links;
	SlipDraw3DDrawRecord drawRecord;
	uint8_t bytes[SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE];
} SlipDraw3DLinkedDrawRecord;

typedef char
    SlipDraw3DLinkedDrawRecordSizeCheck[sizeof(SlipDraw3DLinkedDrawRecord) == SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE ? 1
                                                                                                                  : -1];

typedef struct SlipDraw3DRecordPool {
	uint16_t usableRecordCount;
	uint32_t freeHeadOffset;
	uint32_t inputActiveHeadOffset;
	SlipDraw3DLinkedDrawRecord records[SLIP_DRAW3D_RECORD_POOL_TOTAL_COUNT];
} SlipDraw3DRecordPool;

typedef SlipDraw3DVec32 (*SlipDraw3DTransformFn)(uint32_t sourceX, uint32_t sourceY, uint32_t sourceZ,
                                                 SlipDraw3DVertexRecord *record, void *userData);
typedef SlipView3DVec32 (*SlipDraw3DSourcePointFn)(int16_t sourceX, int16_t sourceY, int16_t sourceZ, void *userData);

typedef struct SlipDraw3DStateRecord {
	uint32_t vertexBufferCursor;
	SlipDraw3DTransformFn transform;
	SlipDraw3DSourcePointFn sourcePoint;
	SlipDraw3DVec32 origin;
	SlipDraw3DVec32 lightVector;
	SlipView3DMatrix matrix;
} SlipDraw3DStateRecord;

typedef struct SlipDraw3DRasterPoint {
	int32_t x;
	int32_t y;
} SlipDraw3DRasterPoint;

typedef void (*SlipDraw3DProjectFn)(SlipDraw3DVec32 world, int32_t *screenX, int32_t *screenY, void *userData);

typedef struct SlipDraw3DProjectState {
	int32_t minZ;
	int32_t maxZ;
	int32_t minX;
	int32_t maxX;
	int32_t minY;
	int32_t maxY;
	int32_t centerX;
	int32_t centerY;
	int32_t projectionScale;
	bool squarePixels;
	uint32_t projectionMode;
	uint32_t perspectiveScale;
	uint32_t projectionScaleFactor;
	uint32_t modeOneScale;
	uint32_t modeOneReciprocal;
	uint32_t inverseProjectionScale;
	int32_t modeOneMaximumXScaled;
	int32_t modeOneMinimumXScaled;
	int32_t modeOneMinimumYScaled;
	int32_t modeOneMaximumYScaled;
	uint32_t renderFlags;
	SlipDraw3DVec32 depthOrigin;
	SlipDraw3DVec32 depthNormal;
	uint32_t auxiliaryClipPlaneEnabled;
	SlipDraw3DVec32 auxiliaryClipPlaneOrigin;
	SlipDraw3DVec32 auxiliaryClipPlaneNormal;
	SlipDraw3DProjectFn projectPrimary;
	SlipDraw3DProjectFn projectSecondary;
	uint32_t (*projectMask)(SlipDraw3DVec32 point, const struct SlipDraw3DProjectState *state);
	bool (*sphereOutside)(SlipDraw3DVec32 center, int32_t radius, const struct SlipDraw3DProjectState *state);
	int32_t rightSlope, leftSlope, topSlope, bottomSlope;
	int16_t leftNormalX, leftNormalZ, rightNormalX, rightNormalZ;
	int16_t bottomNormalY, bottomNormalZ, topNormalY, topNormalZ;
} SlipDraw3DProjectState;

int32_t SlipDraw3D_HorizontalProjectionScale(const SlipDraw3DProjectState *state);

bool SlipDraw3D_SphereOutsidePerspective(SlipDraw3DVec32, int32_t radius, const SlipDraw3DProjectState *);
bool SlipDraw3D_SphereOutsideOrthographic(SlipDraw3DVec32, int32_t radius, const SlipDraw3DProjectState *);

uint32_t SlipDraw3D_ProjectDrawRecordPoint(SlipDraw3DDrawRecord *record, SlipDraw3DVec32 point,
                                           SlipDraw3DProjectState *state);

uint32_t SlipDraw3D_ProjectMaskPerspective(SlipDraw3DVec32 point, const SlipDraw3DProjectState *state);
uint32_t SlipDraw3D_ProjectMaskOrthographic(SlipDraw3DVec32 point, const SlipDraw3DProjectState *state);

void SlipDraw3D_SetAuxiliaryClipPlane(SlipDraw3DProjectState *state, SlipDraw3DVec32 origin, int16_t normalX,
                                      int16_t normalY, int16_t normalZ);
void SlipDraw3D_ClearAuxiliaryClipPlane(SlipDraw3DProjectState *state);

uint32_t SlipDraw3D_ClassifyShapeBounds(SlipDraw3DVec32 center, int32_t radius, const SlipDraw3DProjectState *state);

typedef struct SlipDraw3DClipAndCenter {
	uint32_t clipMinX;
	uint32_t clipMinY;
	uint32_t clipMaxX;
	uint32_t clipMaxY;
	uint32_t clipCenterX;
	uint32_t clipCenterY;
	bool returned;
} SlipDraw3DClipAndCenter;

typedef struct SlipDraw3DRefreshMode1Projection {
	uint32_t mode;
	uint32_t scale;
	uint32_t reciprocal;
	int32_t maxXDeltaScaled;
	int32_t minXDeltaScaled;
	int32_t minYDeltaScaled;
	int32_t maxYDeltaScaled;
	bool returned;
} SlipDraw3DRefreshMode1Projection;

typedef struct SlipDraw3DNormalizeVector2D {
	int16_t unitXQ14;
	int16_t unitYQ14;
	uint16_t length;
	bool returned;
} SlipDraw3DNormalizeVector2D;

typedef struct SlipDraw3DApproxAbsVectorLength {
	uint32_t absX;
	uint32_t absY;
	uint32_t absZ;
	bool swappedLargestWithY;
	bool swappedLargestWithZ;
	uint32_t largest;
	uint32_t otherSum;
	uint32_t otherQuarter;
	uint32_t approximateLength;
	bool returned;
} SlipDraw3DApproxAbsVectorLength;

typedef enum SlipDraw3DLightDepthBlendBranch {
	SLIP_DRAW3D_LIGHT_DEPTH_BLEND_BRANCH_ZERO,
	SLIP_DRAW3D_LIGHT_DEPTH_BLEND_BRANCH_LIMIT,
	SLIP_DRAW3D_LIGHT_DEPTH_BLEND_BRANCH_INTERIOR
} SlipDraw3DLightDepthBlendBranch;

typedef struct SlipDraw3DLightDepthBlend {
	uint32_t depth;
	uint32_t start;
	uint32_t end;
	uint32_t range;
	SlipDraw3DLightDepthBlendBranch branch;
	uint32_t delta;
	int64_t shiftedDividend;
	int32_t quotient;
	uint32_t fadeBlendQ14;
	bool returned;
} SlipDraw3DLightDepthBlend;

typedef enum SlipDraw3DLightingMaterialBranch {
	SLIP_DRAW3D_LIGHTING_MATERIAL_BRANCH_FIXED,
	SLIP_DRAW3D_LIGHTING_MATERIAL_BRANCH_AMBIENT,
	SLIP_DRAW3D_LIGHTING_MATERIAL_BRANCH_VECTOR
} SlipDraw3DLightingMaterialBranch;

typedef struct SlipDraw3DMaterialRecord {
	char name[SLIP_MAT_NAME_BYTES];
	uint32_t rampStart, rampEnd;
	int16_t textureTransparency, skipFlatPolygon;
	uint32_t fixedShade, ambientCoefficient, diffuseCoefficient, specularCoefficient;
	uint32_t ditherBits, vertexShading;
	char textureName[SLIP_MAT_TEXTURE_NAME_BYTES];
	uint32_t textureHandles[SLIP_DRAW3D_MATERIAL_FRAME_COUNT];
	uint32_t importedMaterialByte;
} SlipDraw3DMaterialRecord;

typedef char SlipDraw3DMaterialRecordSizeCheck
    [sizeof(SlipDraw3DMaterialRecord) == SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE ? 1 : -1];

typedef struct SlipDraw3DMaterialTable {
	uint32_t count;
	SlipDraw3DMaterialRecord records[];
} SlipDraw3DMaterialTable;

enum {
	SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES = offsetof(SlipDraw3DMaterialTable, records),
	SLIP_DRAW3D_MATERIAL_RAMP_START_OFFSET = offsetof(SlipDraw3DMaterialRecord, rampStart),
	SLIP_DRAW3D_MATERIAL_RAMP_END_OFFSET = offsetof(SlipDraw3DMaterialRecord, rampEnd),
	SLIP_DRAW3D_MATERIAL_RAMP_END_FIRST_BYTE_END = SLIP_DRAW3D_MATERIAL_RAMP_END_OFFSET + sizeof(uint8_t)
};

typedef struct SlipDraw3DVertexLighting {
	SlipDraw3DVec32 light, origin;
	uint32_t flags, direct, ambient;
	uint32_t fadeStart, fadeEnd, fadeRange, fadeShade;
	uint32_t overrideRamp, rampStart, rampEnd;
	SlipDraw3DTransformFn transform;
	void *transformContext;
} SlipDraw3DVertexLighting;

void SlipDraw3D_BindSpecularTable(const uint16_t *table, uint32_t threshold);
void SlipDraw3D_InstallSpecularTable(void);
uint32_t SlipDraw3D_VertexColor(const SlipDraw3DMaterialRecord *material, SlipDraw3DVertexRecord *vertex,
                                int16_t normalX, int16_t normalY, int16_t normalZ,
                                const SlipDraw3DVertexLighting *state);

int SlipDraw3D_PolygonColor(const SlipDraw3DMaterialRecord *material, int16_t normalX, int16_t normalY, int16_t normalZ,
                            const SlipDraw3DVertexLighting *state, SlipDraw3DVertexRecord *vertices, size_t vertexCount,
                            const uint8_t *indices, size_t indexBytes, uint16_t count, uint32_t inverseProjectionScale,
                            uint32_t *color);

RasterPoint SlipDraw3D_ProjectUnclippedVertex(SlipDraw3DVertexRecord *vertex, const SlipDraw3DProjectState *projection,
                                              SlipDraw3DTransformFn transform, SlipDraw3DProjectFn project,
                                              void *context);
int SlipDraw3D_DrawUnclippedPolygon(const SlipDraw3DMaterialTable *materials, uint16_t materialIndex,
                                    uint16_t countAndFlags, int16_t normalX, int16_t normalY, int16_t normalZ,
                                    SlipDraw3DVertexRecord *vertices, size_t vertexCount, const uint8_t *stream,
                                    size_t streamBytes, const SlipDraw3DProjectState *projection,
                                    SlipDraw3DTransformFn transform, SlipDraw3DProjectFn project,
                                    const SlipDraw3DVertexLighting *lighting, uint32_t *materialColor);

typedef struct SlipDraw3DLightingMaterial {
	uint32_t depthFadeBlend;
	uint16_t diffuseLight;
	uint16_t specularLight;
	uint32_t fixedValue;
	uint32_t ambientCoefficient;
	uint32_t diffuseCoefficient;
	uint32_t specularCoefficient;
	uint32_t directLight;
	uint32_t ambientLight;
	uint32_t fadeColour;
	uint32_t fadeStart;
	SlipDraw3DLightingMaterialBranch branch;
	uint32_t shadeBeforeFade;
	uint32_t fadeBlend;
	bool clampedUnsigned;
	bool skippedZeroFadeBlend;
	bool skippedBlendNoStart;
	bool fullBlend;
	bool interpolatedBlend;
	bool clampedNegative;
	bool clampedHigh;
	uint32_t shade;
	bool returned;
} SlipDraw3DLightingMaterial;

typedef struct SlipDraw3DRefreshMode0Projection {
	uint32_t mode;
	uint32_t projectionScale;
	int32_t maxXStep;
	int32_t minXStep;
	int32_t maxYStep;
	int32_t minYStep;
	int16_t minXPlaneDepthQ;
	int16_t minXPlaneNegXQ;
	int16_t maxXPlaneNegDepthQ;
	int16_t maxXPlaneXQ;
	int16_t maxYPlaneDepthQ;
	int16_t maxYPlaneYQ;
	int16_t minYPlaneNegDepthQ;
	int16_t minYPlaneNegYQ;
	bool returned;
} SlipDraw3DRefreshMode0Projection;

int SlipDraw3D_RefreshProjectFrustum(const SlipDraw3DProjectState *state, uint32_t mode,
                                     SlipDraw3DRefreshMode0Projection *result);

typedef struct SlipDraw3DBuildResult {
	uint32_t allClipFlags;
	uint32_t anyClipFlags;
	uint32_t drawMode;
	uint32_t materialDitherBits;
	uint8_t ditherBits;
} SlipDraw3DBuildResult;

typedef struct SlipDraw3DProjectIndex {
	uint16_t vertexIndex;
	uint32_t vertexRecordOffset;
	SlipDraw3DVertexRecord *vertexRecord;
	uint32_t flagsBefore;
	bool alreadyTransformed;
	bool calledTransform;
	uint32_t sourceX;
	uint32_t sourceY;
	uint32_t sourceZ;
	SlipDraw3DVec32 world;
} SlipDraw3DProjectIndex;

typedef struct SlipDraw3DPolygonStatusVisit {
	uint16_t vertexIndex;
	SlipDraw3DProjectIndex project;
	uint32_t flagsBeforeStatus;
	bool statusAlreadyComputed;
	uint32_t depthClipFlags;
	uint32_t flagsAfterStatus;
	bool reversePassVisited;
	uint32_t reversePassOrder;
	uint32_t projectMask;
	uint32_t allMaskAfter;
	uint32_t anyMaskAfter;
} SlipDraw3DPolygonStatusVisit;

typedef struct SlipDraw3DPolygonStatus {
	uint16_t countAndFlags;
	uint32_t vertexCount;
	uint32_t initialAllClipFlags;
	uint32_t initialAnyClipFlags;
	size_t firstPassVisitCount;
	size_t reversePassVisitCount;
	uint32_t allClipFlags;
	uint32_t anyClipFlags;
	bool firstPassReject;
	uint32_t finalAllMask;
	uint32_t finalAnyMask;
	int32_t clipClassification;
	bool signFlagAfterReturn;
} SlipDraw3DPolygonStatus;

typedef struct SlipDraw3DPerspectiveDepthVisit {
	uint16_t vertexIndex;
	uint32_t vertexOffset;
	bool alreadyTransformed;
	bool calledTransform;
	uint32_t sourceX;
	uint32_t sourceY;
	uint32_t sourceZ;
	SlipDraw3DVec32 world;
	uint32_t minDepthBefore;
	uint32_t minDepthAfter;
	bool negativeDepthExit;
} SlipDraw3DPerspectiveDepthVisit;

typedef struct SlipDraw3DPerspectiveDepth {
	uint16_t countAndFlags;
	uint16_t vertexCount;
	uint32_t initialMinDepth;
	size_t visitCount;
	uint32_t minDepth;
	uint32_t projectionFactor;
	uint32_t fadeDepth;
	bool negativeDepthExit;
} SlipDraw3DPerspectiveDepth;

typedef enum SlipDraw3DClipStep {
	SLIP_DRAW3D_CLIP_REJECT = 0,
	SLIP_DRAW3D_CLIP_SCREEN = 1,
	SLIP_DRAW3D_CLIP_DEPTH_THEN_SCREEN = 2
} SlipDraw3DClipStep;

typedef enum SlipDraw3DDispatchKind {
	SLIP_DRAW3D_DISPATCH_UNSUPPORTED = 0,
	SLIP_DRAW3D_DISPATCH_WIREFRAME = 1,
	SLIP_DRAW3D_DISPATCH_SOLID_FLAT_POLYGON = 2,
	SLIP_DRAW3D_DISPATCH_DITHERED_FLAT_POLYGON = 3,
	SLIP_DRAW3D_DISPATCH_SOLID_RECT = 4,
	SLIP_DRAW3D_DISPATCH_SOLID_LINE = 5,
	SLIP_DRAW3D_DISPATCH_SHADED_FLAT_POLYGON = 6
} SlipDraw3DDispatchKind;

typedef struct SlipDraw3DDispatchResult {
	SlipDraw3DDispatchKind kind;
	uint16_t pointCount;
} SlipDraw3DDispatchResult;

typedef struct SlipDraw3DFlatRingDispatch {
	uint32_t inputActiveHeadOffset;
	uint32_t linkOffset;
	uint32_t drawMode;
	uint32_t renderFlags;
	uint32_t materialColor;
	uint32_t materialDitherBits;
	uint16_t pointCount;
	SlipDraw3DDispatchResult dispatch;
	bool rasterized;
	bool returned;
} SlipDraw3DFlatRingDispatch;

typedef enum SlipDraw3DTexturedDispatchCall {
	SLIP_DRAW3D_TEXTURED_DISPATCH_CALL_NONE = 0,
	SLIP_DRAW3D_TEXTURED_DISPATCH_TRANSPARENT_AFFINE,
	SLIP_DRAW3D_TEXTURED_DISPATCH_OPAQUE_AFFINE,
	SLIP_DRAW3D_TEXTURED_DISPATCH_TRANSPARENT_PERSPECTIVE,
	SLIP_DRAW3D_TEXTURED_DISPATCH_OPAQUE_PERSPECTIVE
} SlipDraw3DTexturedDispatchCall;

typedef struct SlipDraw3DTexturedDispatchPoint {
	int32_t screenX;
	int32_t screenY;
	uint32_t textureU;
	uint32_t textureV;
	int32_t depth;
} SlipDraw3DTexturedDispatchPoint;

typedef struct SlipDraw3DTexturedDispatchVisit {
	uint32_t recordOffset;
	uint32_t pointBufferOffset;
	SlipDraw3DTexturedDispatchPoint point;
	uint32_t nextRecordOffset;
	uint32_t pointCountAfterInc;
	bool loop;
} SlipDraw3DTexturedDispatchVisit;

typedef struct SlipDraw3DTexturedDispatch {
	bool savedGeneralState;
	uint32_t linkOffsetInitial;
	uint32_t reverseTraversal;
	uint32_t linkOffset;
	uint32_t drawMode;
	bool lineBranch;
	bool flatBranch;
	bool ditheredBranch;
	bool shadedBranch;
	bool branchDefaultTextured;
	uint32_t pointBufferBase;
	uint32_t inputActiveHeadOffset;
	uint32_t textureHandle;
	uint32_t pointBufferReset;
	uint32_t renderFlags;
	size_t pointCount;
	SlipDraw3DTexturedDispatchCall rasterizerCall;
	bool calledTransparentAffineRasterizer;
	bool calledOpaqueAffineRasterizer;
	bool calledTransparentPerspectiveRasterizer;
	bool calledOpaquePerspectiveRasterizer;
	bool restoredGeneralState;
	bool returned;
} SlipDraw3DTexturedDispatch;

typedef struct SlipDraw3DConditionalPolygonEmission {
	bool savedGeneralState;
	uint32_t postPlaneHead;
	uint32_t renderFlags;
	bool postPlaneBranch;
	bool insideViewBranch;
	bool callReturnActiveRing;
	bool callMaterialGate;
	bool materialGateCarry;
	bool jumpOnCarry;
	bool callRasterizeFlatDispatch;
	bool clearedIndexedPathCarry;
	uint32_t countAndFlags;
	bool calledUnclippedPolygon;
	bool clearedUnclippedPathCarry;
	bool restoredGeneralState;
	bool returned;
	bool carryOut;
} SlipDraw3DConditionalPolygonEmission;

typedef struct SlipDraw3DActiveMaterialPolygonEmission {
	bool savedGeneralState;
	bool callReturnActiveRing;
	bool calledBuildActiveMaterialRing;
	bool materialRingRejected;
	bool jumpOnCarry;
	bool callRasterizeFlatDispatch;
	bool clearedRejectCarry;
	bool restoredGeneralState;
	bool returned;
	bool carryOut;
} SlipDraw3DActiveMaterialPolygonEmission;

typedef struct SlipDraw3DIndexedTexturedPolygonEmission {
	bool savedGeneralState;
	bool callReturnActiveRing;
	bool calledBuildTexturedRing;
	bool texturedRingRejected;
	bool jumpOnCarry;
	bool callRasterizeFlatDispatch;
	bool flatDispatchCarry;
	bool restoredGeneralState;
	bool returned;
	bool carryOut;
} SlipDraw3DIndexedTexturedPolygonEmission;

typedef struct SlipDraw3DPointPolygonEmission {
	bool savedGeneralState;
	bool callReturnActiveRing;
	bool callDraw3DPointPolygon;
	bool draw3DPointPolygonCarry;
	bool jumpOnCarry;
	bool callRasterizeFlatDispatch;
	bool clearedRejectCarry;
	bool restoredGeneralState;
	bool returned;
	bool carryOut;
} SlipDraw3DPointPolygonEmission;

typedef struct SlipDraw3DLinePairEmission {
	bool savedGeneralState;
	bool callReturnActiveRing;
	bool calledBuildLinePair;
	bool linePairRejected;
	bool jumpOnCarry;
	bool callRasterizeFlatDispatch;
	bool clearedRejectCarry;
	bool restoredGeneralState;
	bool returned;
	bool carryOut;
} SlipDraw3DLinePairEmission;

typedef struct SlipDraw3DLineEmission {
	bool savedGeneralState;
	bool callReturnActiveRing;
	bool callRendererBuildLine;
	bool rendererBuildLineCarry;
	bool jumpOnCarry;
	bool callRasterizeFlatDispatch;
	bool clearedRejectCarry;
	bool restoredGeneralState;
	bool returned;
	bool carryOut;
} SlipDraw3DLineEmission;

typedef enum SlipDraw3DMaterialGateBranch {
	SLIP_DRAW3D_MATERIAL_GATE_BRANCH_REJECT,
	SLIP_DRAW3D_MATERIAL_GATE_BRANCH_REGULAR,
	SLIP_DRAW3D_MATERIAL_GATE_BRANCH_INDEXED
} SlipDraw3DMaterialGateBranch;

typedef struct SlipDraw3DMaterialGate {
	uint16_t materialCount;
	bool selectedIndexedMaterial;
	uint32_t materialRecordOffset;
	const uint8_t *materialRecord;
	uint16_t rejectWord;
	bool setRejectCarry;
	const uint8_t *storedMaterialRecord;
	uint32_t vertexShading;
	uint32_t flags;
	bool flagsBlockedIndexedPath;
	uint32_t countAndFlags;
	bool hasIndexedShadingFlag;
	uint32_t maskedVertexCount;
	const uint8_t *indexedRecordPointer;
	uint32_t indexedRecordIndex;
	uint32_t mode;
	uint32_t drawMode;
	SlipDraw3DMaterialGateBranch branch;
} SlipDraw3DMaterialGate;

typedef int (*SlipDraw3DResourceFindNameRecord)(void *user, const char name[SLIP_RESOURCE_NAME_BUFFER_BYTES],
                                                uint32_t *resourceHandle);

typedef struct SlipDraw3DMaterialFrameSlots {
	uint32_t materialTableCount;
	uint32_t recordsVisited;
	uint32_t recordsWithTextureName;
	uint32_t recordsWithWildcard;
	uint32_t resourceLookups;
	uint32_t resourceHits;
	uint32_t slotWrites;
	bool nullTable;
	bool returned;
} SlipDraw3DMaterialFrameSlots;

typedef struct SlipDraw3DTexturedEmitGate {
	uint32_t savedRenderFlags;
	uint32_t savedCountAndFlags;
	uint16_t normalX;
	uint16_t normalY;
	uint16_t normalZ;
	bool flagsBlockTexturedBranch;
	bool lacksTextureFlag;
	uint16_t materialCount;
	bool selectedIndexedMaterial;
	uint32_t materialRecordOffset;
	uint32_t frameIndex;
	uint32_t frameSlotOffset;
	uint32_t textureHandle;
	bool fallback;
	bool calledBuildTexturedRing;
	uint16_t normalDepthX;
	bool normalDepthXRoundingCarry;
	uint16_t normalDepthY;
	bool normalDepthYRoundingCarry;
	uint16_t normalDepthZ;
	bool normalDepthZRoundingCarry;
	uint16_t normalDepth;
	uint32_t renderFlagsAfter;
	uint16_t transparentWord;
	uint32_t renderFlagsForDispatch;
	const uint8_t *storedMaterialRecord;
	bool calledTexturedDispatch;
} SlipDraw3DTexturedEmitGate;

typedef struct SlipDraw3DMaterialNumber {
	uint16_t materialGlobal;
	bool jumpNoMaterials;
	uint8_t normalizedKey[SLIP_DRAW3D_MATERIAL_KEY_BYTES];
	uint16_t keyBytesCopied;
	uint16_t keyBytesFilled;
	bool sourceTerminator;
	uint32_t materialTableCount;
	uint16_t materialIndex;
	uint16_t materialRecordOffset;
	uint16_t recordsCompared;
	bool clearedRejectCarry;
	bool setRejectCarry;
	bool carryOut;
} SlipDraw3DMaterialNumber;

typedef struct SlipDraw3DMaterialInstall {
	bool savedGeneralState;
	bool clearGlobal;
	uint16_t count;
	uint16_t version;
	bool jumpWrongVersion;
	uint16_t existingMaterialGlobal;
	bool noExistingMaterialsBranch;
	uint32_t allocationBytes;
	uint16_t storedMaterialGlobal;
	uint32_t tableCount;
	uint16_t recordsExpanded;
	bool callDraw3DNotifyMaterials;
	bool calledLoadMaterialFrameSlots;
	bool restoredGeneralState;
	bool returned;
} SlipDraw3DMaterialInstall;

typedef struct SlipDraw3DMaterialAppend {
	bool savedGeneralState;
	bool clearGlobal;
	uint16_t count;
	uint16_t version;
	bool jumpWrongVersion;
	uint16_t existingMaterialGlobal;
	bool appendBranch;
	uint32_t existingTableCount;
	uint32_t appendedTableCount;
	uint32_t existingTableAllocationBytes;
	uint32_t outputAllocationBytes;
	uint16_t storedMaterialGlobal;
	uint32_t copiedExistingRecords;
	uint16_t recordsExpanded;
	bool callDraw3DNotifyMaterials;
	bool calledLoadMaterialFrameSlots;
	bool restoredGeneralState;
	bool returned;
} SlipDraw3DMaterialAppend;

typedef struct SlipDraw3DFreeRecordPop {
	uint32_t freeHeadOffset;
	uint32_t poppedRecordOffset;
	uint32_t nextFreeOffset;
	uint32_t freeHeadNextAfter;
	uint32_t nextFreePrevAfter;
} SlipDraw3DFreeRecordPop;

typedef struct SlipDraw3DRecordPoolInit {
	uint16_t usableRecordCount;
	uint32_t allocationBytes;
	uint32_t freeHeadOffset;
	uint32_t firstUsableOffset;
	uint32_t lastRecordOffset;
	uint32_t inputActiveHeadOffset;
	bool clearedRejectCarry;
	bool carryOut;
} SlipDraw3DRecordPoolInit;

typedef struct SlipDraw3DIndexedRecordCopy {
	uint16_t indexWord;
	uint32_t vertexRecordOffset;
	const uint8_t *vertexRecordPointer;
	bool callProjectVertex;
	uint32_t flagsFromProjectVertex;
	uint32_t copiedDwords;
	uint32_t allClipFlagsAfter;
	uint32_t anyClipFlagsAfter;
} SlipDraw3DIndexedRecordCopy;

typedef struct SlipDraw3DSolidLoopInit {
	uint32_t firstRecordOffset;
	uint32_t allClipFlagsInitial;
	uint32_t anyClipFlagsInitial;
} SlipDraw3DSolidLoopInit;

typedef struct SlipDraw3DSolidRecordCopy {
	uint32_t loopCountEntry;
	uint16_t indexWord;
	uint32_t vertexRecordOffset;
	const uint8_t *vertexRecordPointer;
	bool callProjectVertex;
	uint32_t flagsFromProjectVertex;
	uint32_t copiedDwords;
	uint32_t allClipFlagsAfter;
	uint32_t anyClipFlagsAfter;
	uint32_t loopCountAfterDec;
	bool branchToClose;
} SlipDraw3DSolidRecordCopy;

typedef struct SlipDraw3DMaterialBytes {
	const uint8_t *materialInputPointer;
	uint16_t normalX;
	uint16_t normalY;
	uint16_t normalZ;
	const uint8_t *materialRecord;
	bool callMaterialColor;
	uint16_t materialColor;
	uint8_t shadeHighByte;
	uint8_t shadeLowByte;
} SlipDraw3DMaterialBytes;

typedef struct SlipDraw3DRegularSetup {
	const uint8_t *materialRecord;
	uint32_t drawModeInitial;
	uint32_t materialDitherBits;
	bool flatBranch;
	uint32_t drawMode;
	uint32_t countAndFlags;
	uint32_t maskedIndex;
	bool callMaterialColor;
	uint32_t materialColor;
	uint32_t storedCountAndFlags;
	uint32_t storedMaterialColor;
	uint32_t mode;
} SlipDraw3DRegularSetup;

typedef struct SlipDraw3DFirstActiveRecord {
	SlipDraw3DFreeRecordPop pop;
	uint32_t inputActiveHeadOffset;
} SlipDraw3DFirstActiveRecord;

typedef struct SlipDraw3DAppendSolidRecord {
	uint32_t previousRecordOffset;
	SlipDraw3DFreeRecordPop pop;
	uint32_t appendedRecordOffset;
	uint32_t previousNextAfter;
	uint32_t appendedPrevAfter;
	uint32_t indexStreamOffsetAfter;
	bool jumpToLoop;
} SlipDraw3DAppendSolidRecord;

typedef struct SlipDraw3DAppendRecord {
	uint32_t previousRecordOffset;
	SlipDraw3DFreeRecordPop pop;
	uint32_t appendedRecordOffset;
	uint32_t previousNextAfter;
	uint32_t appendedPrevAfter;
	uint32_t indexStreamOffsetAfter;
	uint32_t materialInputOffsetAfter;
} SlipDraw3DAppendRecord;

typedef struct SlipDraw3DCloseRecordRing {
	uint32_t lastRecordOffset;
	uint32_t firstRecordOffset;
	uint32_t lastNextAfter;
	uint32_t firstPrevAfter;
} SlipDraw3DCloseRecordRing;

typedef struct SlipDraw3DReturnActiveVisit {
	uint32_t currentRecordOffset;
	uint32_t savedNextOffset;
	uint32_t previousOffset;
	uint32_t nextAfterUnlink;
	uint32_t prevNextAfterUnlink;
	uint32_t nextPrevAfterUnlink;
	uint32_t freeHeadOffset;
	uint32_t freeFirstOffset;
	uint32_t freeHeadNextAfter;
	uint32_t freeFirstPrevAfter;
	uint32_t currentNextAfter;
	uint32_t currentPrevAfter;
	uint32_t nextCurrentAfterXchg;
	bool loop;
} SlipDraw3DReturnActiveVisit;

typedef struct SlipDraw3DReturnActiveRing {
	uint32_t activeHeadEntry;
	bool activeHeadZero;
	size_t visitCount;
	uint32_t activeHeadAfter;
	bool returned;
} SlipDraw3DReturnActiveRing;

typedef struct SlipDraw3DActiveRingVisit {
	uint32_t savedLoopCount;
	uint16_t indexWord;
	uint32_t vertexRecordOffset;
	uint32_t drawRecordOffset;
	bool callProjectVertex;
	uint32_t flagsFromProjectVertex;
	uint32_t copiedDwords;
	uint32_t allClipFlagsAfter;
	uint32_t anyClipFlagsAfter;
	uint32_t loopCountAfterDec;
	bool appendNextRecord;
	SlipDraw3DFreeRecordPop appendPop;
	uint32_t appendedPrevAfter;
	uint32_t indexStreamOffsetAfter;
} SlipDraw3DActiveRingVisit;

typedef struct SlipDraw3DActiveRingBuild {
	uint16_t countAndFlags;
	uint32_t vertexCount;
	uint32_t mode;
	SlipDraw3DFreeRecordPop firstPop;
	uint32_t inputActiveHeadOffset;
	uint32_t savedFirstRecordOffset;
	uint32_t allClipFlagsInitial;
	uint32_t anyClipFlagsInitial;
	size_t visitCount;
	uint32_t lastRecordOffset;
	uint32_t firstRecordOffset;
	uint32_t lastNextAfter;
	uint32_t firstPrevAfter;
	uint32_t anyFlagsBeforeDispatch;
	bool anyMaskedZero;
	uint32_t allFlagsBeforeDispatch;
	bool allMaskedNonzero;
	bool calledClipDepth;
	bool depthClipRejected;
	bool calledClipScreen;
	bool screenClipRejected;
	bool setRejectCarry;
	bool returned;
	bool carryOut;
} SlipDraw3DActiveRingBuild;

typedef struct SlipDraw3DTexturedRingVisit {
	uint32_t savedLoopCount;
	uint16_t indexWord;
	uint32_t vertexRecordOffset;
	uint32_t drawRecordOffset;
	bool callProjectVertex;
	uint32_t flagsFromProjectVertex;
	uint32_t copiedDwords;
	uint32_t allClipFlagsAfter;
	uint32_t anyClipFlagsAfter;
	uint32_t textureCoordOffset;
	uint16_t textureCoordWord;
	uint16_t textureV;
	uint32_t loopCountAfterDec;
	bool appendNextRecord;
	SlipDraw3DFreeRecordPop appendPop;
	uint32_t appendedPrevAfter;
	uint32_t indexStreamOffsetAfter;
	uint32_t textureCoordOffsetAfter;
} SlipDraw3DTexturedRingVisit;

typedef struct SlipDraw3DTexturedRingBuild {
	bool calledPolygonStatus;
	bool signReject;
	uint16_t countAndFlags;
	uint32_t vertexCount;
	bool wideTextureCoordStride;
	uint32_t textureCoordBaseOffset;
	uint32_t textureCoordStreamOffset;
	uint32_t textureHandle;
	uint32_t mode;
	uint32_t drawMode;
	SlipDraw3DFreeRecordPop firstPop;
	uint32_t inputActiveHeadOffset;
	uint32_t savedFirstRecordOffset;
	uint32_t allClipFlagsInitial;
	uint32_t anyClipFlagsInitial;
	size_t visitCount;
	uint32_t lastRecordOffset;
	uint32_t firstRecordOffset;
	uint32_t lastNextAfter;
	uint32_t firstPrevAfter;
	uint32_t anyFlagsBeforeDispatch;
	bool anyMaskedZero;
	uint32_t allFlagsBeforeDispatch;
	bool allMaskedNonzero;
	bool calledClipDepth;
	bool depthClipRejected;
	bool calledClipScreen;
	bool screenClipRejected;
	bool setRejectCarry;
	bool returned;
	bool carryOut;
} SlipDraw3DTexturedRingBuild;

typedef struct SlipDraw3DPointPointerRingVisit {
	uint32_t savedLoopCount;
	uint32_t pointPointerToken;
	bool callDraw3DScreenPointFlags;
	uint32_t flagsFrom;
	uint32_t allClipFlagsAfter;
	uint32_t anyClipFlagsAfter;
	uint32_t loopCountAfterDec;
	bool appendNextRecord;
	SlipDraw3DFreeRecordPop appendPop;
	uint32_t appendedPrevAfter;
	uint32_t pointPointerTableOffsetAfter;
	bool resolvedPointPointer;
	uint32_t pointPointerHostOffset;
	int32_t pointScreenX;
	int32_t pointScreenY;
} SlipDraw3DPointPointerRingVisit;

typedef struct SlipDraw3DPointPointerRing {
	uint16_t countAndFlags;
	uint32_t pointCount;
	uint32_t mode;
	uint32_t drawMode;
	uint32_t materialColor;
	SlipDraw3DFreeRecordPop firstPop;
	uint32_t inputActiveHeadOffset;
	uint32_t savedFirstRecordOffset;
	uint32_t allClipFlagsInitial;
	uint32_t anyClipFlagsInitial;
	size_t visitCount;
	uint32_t lastRecordOffset;
	uint32_t firstRecordOffset;
	uint32_t lastNextAfter;
	uint32_t firstPrevAfter;
	uint32_t anyFlagsBeforeDispatch;
	bool anyMaskedZero;
	uint32_t allFlagsBeforeDispatch;
	bool allMaskedNonzero;
	bool calledClipDepth;
	bool depthClipRejected;
	bool calledClipScreen;
	bool screenClipRejected;
	bool setRejectCarry;
	bool returned;
	bool carryOut;
} SlipDraw3DPointPointerRing;

typedef struct SlipDraw3DScreenPoint16 {
	int16_t x, y;
} SlipDraw3DScreenPoint16;

typedef struct SlipDraw3DSpritePointTextureRingVisit {
	uint32_t savedLoopCount;
	const SlipDraw3DScreenPoint16 *point;
	int32_t pointScreenX;
	int32_t pointScreenY;
	bool callDraw3DScreenPointFlags;
	uint32_t flagsFrom;
	uint32_t allClipFlagsAfter;
	uint32_t anyClipFlagsAfter;
	uint16_t textureU;
	uint16_t textureV;
	uint32_t loopCountAfterDec;
	bool appendNextRecord;
	SlipDraw3DFreeRecordPop appendPop;
	uint32_t appendedPrevAfter;
	uint32_t pointPointerTableOffsetAfter;
	uint32_t textureCoordOffsetAfter;
} SlipDraw3DSpritePointTextureRingVisit;

typedef struct SlipDraw3DSpritePointTextureRing {
	uint16_t countAndFlags;
	uint32_t pointCount;
	uint32_t mode;
	uint32_t drawMode;
	uint32_t textureHandle;
	SlipDraw3DFreeRecordPop firstPop;
	uint32_t inputActiveHeadOffset;
	uint32_t savedFirstRecordOffset;
	uint32_t allClipFlagsInitial;
	uint32_t anyClipFlagsInitial;
	size_t visitCount;
	uint32_t lastRecordOffset;
	uint32_t firstRecordOffset;
	uint32_t lastNextAfter;
	uint32_t firstPrevAfter;
	uint32_t anyFlagsBeforeDispatch;
	bool anyMaskedZero;
	uint32_t allFlagsBeforeDispatch;
	bool allMaskedNonzero;
	bool calledClipScreen;
	bool screenClipRejected;
	bool setRejectCarry;
	bool returned;
	bool carryOut;
} SlipDraw3DSpritePointTextureRing;

typedef struct SlipDraw3DActiveBoundsVisit {
	uint32_t recordOffset;
	int32_t screenX;
	int32_t minXAfter;
	int32_t maxXAfter;
	int32_t screenY;
	int32_t minYAfter;
	int32_t maxYAfter;
	uint32_t nextRecordOffset;
	bool loop;
} SlipDraw3DActiveBoundsVisit;

typedef struct SlipDraw3DActiveBounds {
	uint32_t activeHeadOffset;
	int32_t minXInitial;
	int32_t minYInitial;
	int32_t maxXInitial;
	int32_t maxYInitial;
	size_t visitCount;
	int32_t minX;
	int32_t minY;
	int32_t maxX;
	int32_t maxY;
	bool returned;
} SlipDraw3DActiveBounds;

typedef struct SlipDraw3DPostPlaneCaptureVisit {
	uint32_t currentOffset;
	uint32_t nextOffset;
	int32_t edgeX;
	int32_t edgeY;
	SlipDraw3DNormalizeVector2D normal;
	bool removedShortEdge;
	uint32_t activeHeadAfterRemove;
	uint32_t remainingCount;
	bool keptEdge;
	int32_t storedNormalX;
	int32_t storedNormalY;
	bool loop;
} SlipDraw3DPostPlaneCaptureVisit;

typedef struct SlipDraw3DPostPlaneCapture {
	uint32_t postPlaneHeadIn;
	uint32_t sourceOffset;
	int32_t planePointX;
	int32_t planePointY;
	int32_t planePointZ;
	int32_t planeNormalX;
	int32_t planeNormalY;
	int32_t planeNormalZ;
	uint32_t lightX;
	uint32_t lightY;
	uint32_t lightZ;
	SlipView3DDotProductQ14 dot;
	int16_t planeLightDot;
	uint32_t planeScale;
	bool calledActiveBounds;
	SlipDraw3DActiveBounds bounds;
	int32_t limitXMin;
	int32_t limitYMin;
	int32_t limitXMax;
	int32_t limitYMax;
	uint32_t activeHeadIn;
	uint32_t activeHeadOut;
	uint32_t freeHead;
	uint32_t postPlaneHeadOut;
	uint32_t anyEdgeDelta;
	size_t visitCount;
	bool existingPostPlane;
	bool setRejectCarry;
	bool clearedRejectCarry;
	bool returned;
	bool carryOut;
} SlipDraw3DPostPlaneCapture;

typedef struct SlipDraw3DPostPlaneReleaseVisit {
	uint32_t movedOffset;
	uint32_t savedNextOffset;
	uint32_t previousOffset;
	uint32_t nextOffset;
	uint32_t freeFirstOffset;
	bool loop;
} SlipDraw3DPostPlaneReleaseVisit;

typedef struct SlipDraw3DPostPlaneRelease {
	uint32_t postPlaneHeadIn;
	uint32_t freeHead;
	uint32_t postPlaneHeadOut;
	size_t visitCount;
	bool returned;
} SlipDraw3DPostPlaneRelease;

typedef struct SlipDraw3DPrimitivePath {
	bool savedIndexStream;
	bool savedMaterialStream;
	bool savedCountAndFlags;
	bool callReturnActiveRing;
	SlipDraw3DReturnActiveRing returnActive;
	bool callDraw3DBuildActiveRing;
	SlipDraw3DActiveRingBuild activeRing;
	bool draw3DBuildActiveRingCarry;
	bool callDraw3DActiveBounds;
	SlipDraw3DActiveBounds bounds;
	int32_t minX;
	int32_t minY;
	int32_t maxX;
	int32_t maxY;
	bool clearedRejectCarry;
	bool setRejectCarry;
	bool returned;
	bool carryOut;
} SlipDraw3DPrimitivePath;

typedef struct SlipDraw3DClipFlagVisit {
	uint32_t recordOffset;
	uint32_t flags;
	uint32_t allFlagsAfterAnd;
	uint32_t anyFlagsAfter;
	uint32_t nextRecordOffset;
	bool loop;
} SlipDraw3DClipFlagVisit;

typedef struct SlipDraw3DClipFlags {
	uint32_t headOffset;
	uint32_t loopHeadOffset;
	uint32_t allFlagsInitial;
	uint32_t anyFlagsInitial;
	uint32_t visitCount;
	uint32_t allFlagsOut;
	uint32_t anyFlagsOut;
	bool returned;
} SlipDraw3DClipFlags;

typedef enum SlipDraw3DClipDispatchBranch {
	SLIP_DRAW3D_CLIP_DISPATCH_BRANCH_REJECT,
	SLIP_DRAW3D_CLIP_DISPATCH_BRANCH_CLIPPED_RETURN,
	SLIP_DRAW3D_CLIP_DISPATCH_BRANCH_CLIPPED_THEN_UNCLIPPED,
	SLIP_DRAW3D_CLIP_DISPATCH_BRANCH_UNCLIPPED
} SlipDraw3DClipDispatchBranch;

typedef struct SlipDraw3DClipDispatch {
	uint32_t anyFlags;
	bool anyMaskedZero;
	uint32_t allFlags;
	bool allMaskedNonzero;
	bool calledClipDepth;
	bool depthClipRejected;
	bool calledClipScreen;
	bool screenClipRejected;
	bool setRejectCarry;
	bool returned;
	bool carryOut;
	SlipDraw3DClipDispatchBranch branch;
} SlipDraw3DClipDispatch;

typedef struct SlipDraw3DClippedDepthInputs {
	uint32_t allFlagsEntry;
	uint32_t renderFlags;
	uint32_t anyFlagsEntry;
	int rejectedAuxiliaryClip;
	uint32_t targetOffsetAfterAuxiliaryClip;
	uint32_t otherOffsetAfterAuxiliaryClip;
	uint32_t allFlagsFromAfter;
	uint32_t anyFlagsFromAfter;
	int rejectedNearClip;
	uint32_t targetOffsetAfterNearClip;
	uint32_t otherOffsetAfterNearClip;
	uint32_t minimumDepth;
	int rejectedFarClip;
	uint32_t targetOffsetAfterFarClip;
	uint32_t otherOffsetAfterFarClip;
	uint32_t maximumDepth;
	uint32_t allFlagsFromFinal;
	uint32_t anyFlagsFromFinal;
} SlipDraw3DClippedDepthInputs;

typedef enum SlipDraw3DClippedDepthBranch {
	SLIP_DRAW3D_CLIPPED_DEPTH_BRANCH_REJECT_2000,
	SLIP_DRAW3D_CLIPPED_DEPTH_BRANCH_REJECT_AFTER_2000_SCAN,
	SLIP_DRAW3D_CLIPPED_DEPTH_BRANCH_REJECT_0080,
	SLIP_DRAW3D_CLIPPED_DEPTH_BRANCH_REJECT_0100,
	SLIP_DRAW3D_CLIPPED_DEPTH_BRANCH_REJECT_FINAL_SCAN,
	SLIP_DRAW3D_CLIPPED_DEPTH_BRANCH_ACCEPT
} SlipDraw3DClippedDepthBranch;

typedef struct SlipDraw3DClippedDepthDispatch {
	bool auxiliaryClipEnabled;
	uint32_t anyFlagsCurrent;
	bool needsAuxiliaryClip;
	bool calledClipAuxiliaryEdge;
	bool rejectedAuxiliaryClip;
	uint32_t targetOffsetAfterAuxiliaryClip;
	uint32_t otherOffsetAfterAuxiliaryClip;
	bool calledSplitFirstAuxiliaryIntersection;
	bool calledSplitSecondAuxiliaryIntersection;
	bool calledScanAuxiliaryClipFlags;
	uint32_t allFlagsAfterAuxiliaryScan;
	uint32_t anyFlagsAfterAuxiliaryScan;
	bool rejectAfterAuxiliaryScan;
	bool needsDepthClip;
	bool needsNearClip;
	bool savedAnyClipFlags;
	bool calledClipNearEdge;
	bool rejectedNearClip;
	uint32_t targetOffsetAfterNearClip;
	uint32_t otherOffsetAfterNearClip;
	uint32_t firstNearDepthLimit;
	uint32_t secondNearDepthLimit;
	bool calledSplitFirstNearIntersection;
	bool calledSplitSecondNearIntersection;
	bool needsFarClip;
	bool calledClipFarEdge;
	bool rejectedFarClip;
	uint32_t targetOffsetAfterFarClip;
	uint32_t otherOffsetAfterFarClip;
	uint32_t firstFarDepthLimit;
	uint32_t secondFarDepthLimit;
	bool calledSplitFirstFarIntersection;
	bool calledSplitSecondFarIntersection;
	bool calledScanDepthClipFlags;
	uint32_t allFlagsFinalCheck;
	uint32_t anyFlagsAfterFinalScan;
	bool rejectFinalScan;
	bool clearedRejectCarry;
	bool setRejectCarry;
	bool returned;
	bool carryOut;
	SlipDraw3DClippedDepthBranch branch;
} SlipDraw3DClippedDepthDispatch;

typedef struct SlipDraw3DScreenPlaneInputs {
	uint32_t anyFlagsEntry;
	int rejectedLeftClip;
	uint32_t targetOffsetAfterLeftClip;
	uint32_t otherOffsetAfterLeftClip;
	uint32_t clipMinX;
	int rejectedRightClip;
	uint32_t targetOffsetAfterRightClip;
	uint32_t otherOffsetAfterRightClip;
	uint32_t clipMaxX;
	uint32_t allFlagsFromAfter;
	uint32_t anyFlagsFromAfter;
	int rejectedTopClip;
	uint32_t targetOffsetAfterTopClip;
	uint32_t otherOffsetAfterTopClip;
	uint32_t clipMinY;
	int rejectedBottomClip;
	uint32_t targetOffsetAfterBottomClip;
	uint32_t otherOffsetAfterBottomClip;
	uint32_t clipMaxY;
	uint32_t postPlaneHead;
	int postPlaneBoundsRejected;
	int postPlaneClipRejected;
} SlipDraw3DScreenPlaneInputs;

typedef enum SlipDraw3DScreenPlaneBranch {
	SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_0008,
	SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_0010,
	SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_AFTER_0018_SCAN,
	SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_0020,
	SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_0040,
	SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_1DE5C,
	SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_1DEB6,
	SLIP_DRAW3D_SCREEN_PLANE_BRANCH_ACCEPT
} SlipDraw3DScreenPlaneBranch;

typedef struct SlipDraw3DScreenPlaneDispatch {
	bool hasScreenClipFlags;
	uint32_t anyFlagsInitial;
	bool needsLeftClip;
	bool calledClipLeftEdge;
	bool rejectedLeftClip;
	uint32_t targetOffsetAfterLeftClip;
	uint32_t otherOffsetAfterLeftClip;
	uint32_t firstLeftScreenLimit;
	uint32_t secondLeftScreenLimit;
	bool calledSplitFirstLeftIntersection;
	bool calledSplitSecondLeftIntersection;
	bool needsRightClip;
	bool calledClipRightEdge;
	bool rejectedRightClip;
	uint32_t targetOffsetAfterRightClip;
	uint32_t otherOffsetAfterRightClip;
	uint32_t firstRightScreenLimit;
	uint32_t secondRightScreenLimit;
	bool calledSplitFirstRightIntersection;
	bool calledSplitSecondRightIntersection;
	bool calledScanHorizontalClipFlags;
	uint32_t allFlagsAfterHorizontalScan;
	uint32_t anyFlagsAfterHorizontalScan;
	bool rejectAfterHorizontalScan;
	bool needsTopClip;
	bool calledClipTopEdge;
	bool rejectedTopClip;
	uint32_t targetOffsetAfterTopClip;
	uint32_t otherOffsetAfterTopClip;
	uint32_t firstTopScreenLimit;
	uint32_t secondTopScreenLimit;
	bool calledSplitFirstTopIntersection;
	bool calledSplitSecondTopIntersection;
	bool needsBottomClip;
	bool calledClipBottomEdge;
	bool rejectedBottomClip;
	uint32_t targetOffsetAfterBottomClip;
	uint32_t otherOffsetAfterBottomClip;
	uint32_t firstBottomScreenLimit;
	uint32_t secondBottomScreenLimit;
	bool calledSplitFirstBottomIntersection;
	bool calledSplitSecondBottomIntersection;
	bool hasPostPlanes;
	bool calledPostPlaneBounds;
	bool postPlaneBoundsRejected;
	bool calledPostPlaneClip;
	bool postPlaneClipRejected;
	bool clearedRejectCarry;
	bool setRejectCarry;
	bool returned;
	bool carryOut;
	SlipDraw3DScreenPlaneBranch branch;
} SlipDraw3DScreenPlaneDispatch;

typedef enum SlipDraw3DClipEdgeBranch {
	SLIP_DRAW3D_CLIP_EDGE_BRANCH_REJECT_MULTIPLE_SPANS,
	SLIP_DRAW3D_CLIP_EDGE_BRANCH_BORROW_AND_COPY_PREVIOUS_OUTSIDE,
	SLIP_DRAW3D_CLIP_EDGE_BRANCH_MOVE_INTERIOR_PREVIOUS_INSIDE
} SlipDraw3DClipEdgeBranch;

typedef struct SlipDraw3DClipEdge {
	bool savedRingHead;
	uint32_t clipMask;
	uint32_t headOffsetIn;
	uint32_t freeHeadOffset;
	uint32_t firstInsideOffset;
	uint32_t firstOutsideOffset;
	uint32_t nextInsideOffset;
	uint32_t compareOffset;
	bool setRejectCarry;
	bool savedOtherRecord;
	bool savedTargetRecord;
	uint32_t previousOfFirstInside;
	bool previousOfFirstInsideMasked;
	bool borrowedFromFreeList;
	uint32_t borrowedOffset;
	uint32_t freeNextAfterBorrow;
	uint32_t borrowedDrawRecordOffset;
	bool copiedDrawPayload;
	uint32_t movedInteriorCount;
	uint32_t targetOffsetOut;
	uint32_t otherOffsetOut;
	uint32_t headCandidateOffsetOut;
	uint32_t headOffsetOut;
	bool clearedRejectCarry;
	bool returned;
	bool carryOut;
	SlipDraw3DClipEdgeBranch branch;
} SlipDraw3DClipEdge;

typedef struct SlipDraw3DProjectFlags {
	bool calledPrimaryProjection;
	bool calledSecondaryProjection;
	int32_t screenX;
	int32_t screenY;
	uint32_t screenClipFlagsInitial;
	uint32_t screenClipFlagsOut;
	uint32_t flagsIn;
	uint32_t flagsOut;
	bool xBelow;
	bool xAbove;
	bool yBelow;
	bool yAbove;
	bool hasScreenClipFlags;
	bool withinPositiveSentinel;
	bool withinNegativeSentinel;
	bool finiteOffscreenFlag;
	bool returned;
} SlipDraw3DProjectFlags;

typedef struct SlipDraw3DScreenPointFlags {
	int32_t screenX;
	int32_t screenY;
	uint32_t screenClipFlagsInitial;
	uint32_t screenClipFlagsOut;
	bool xBelow;
	bool xAbove;
	bool yBelow;
	bool yAbove;
	bool hasScreenClipFlags;
	bool withinPositiveSentinel;
	bool withinNegativeSentinel;
	bool finiteOffscreenFlag;
	bool returned;
} SlipDraw3DScreenPointFlags;

typedef struct SlipDraw3DExtraFieldInterpolation {
	uint32_t targetOffset;
	uint32_t otherOffset;
	uint32_t projectionMode;
	uint32_t ratio;
	bool interpolatedTexture;
	int32_t textureUDelta;
	int32_t textureUStep;
	int32_t textureVDelta;
	int32_t textureVStep;
	bool returned;
} SlipDraw3DExtraFieldInterpolation;

typedef struct SlipDraw3DDepthExtraInterpolation {
	uint32_t targetOffset;
	uint32_t otherOffset;
	uint32_t projectionMode;
	uint32_t entryRatio;
	bool interpolatedDepth;
	uint32_t firstMultiplyRatio;
	int32_t targetDepthBefore;
	int32_t otherDepth;
	int32_t depthDeltaTargetMinusOther;
	int32_t roundedTargetMinusOtherStep;
	int32_t denominatorDepth;
	int64_t targetDepthTimesRatio;
	int32_t depthAdjustedRatio;
	int32_t depthDeltaOtherMinusTarget;
	int32_t roundedOtherMinusTargetStep;
	int32_t targetDepthAfter;
	bool calledInterpolateTextureQ14;
	bool calledInterpolateTextureQ30;
	SlipDraw3DExtraFieldInterpolation textureInterpolation;
	bool returned;
} SlipDraw3DDepthExtraInterpolation;

typedef struct SlipDraw3DSplitDepth {
	bool highPrecisionPath;
	uint32_t targetOffset;
	uint32_t otherOffset;
	int32_t clipPlaneZ;
	int32_t zDeltaToPlane;
	int32_t zDeltaBetweenRecords;
	bool negatedDeltas;
	uint32_t interpolationRatio;
	int32_t xStep;
	int32_t yStep;
	bool interpolatedShade;
	uint16_t shadeStep;
	bool calledInterpolateTextureQ14;
	bool calledInterpolateTextureQ30;
	SlipDraw3DExtraFieldInterpolation textureInterpolation;
	bool wroteClipPlaneZ;
	bool calledProjectFlags;
	SlipDraw3DProjectFlags projectFlags;
	bool returned;
} SlipDraw3DSplitDepth;

typedef struct SlipDraw3DSplitDepthMath {
	bool highPrecisionPath;
	uint32_t targetOffset;
	uint32_t otherOffset;
	int32_t inputDeltaX;
	int32_t inputDeltaY;
	int32_t inputDeltaZ;
	int32_t targetDepth;
	int32_t otherDepth;
	bool calledIntersectPlaneQ14;
	bool calledIntersectPlaneQ30;
	int32_t xStepApplied;
	int32_t yStepApplied;
	int32_t zStepApplied;
	uint32_t interpolationRatio;
	bool interpolatedShade;
	uint16_t shadeStep;
	bool calledInterpolateTextureQ14;
	bool calledInterpolateTextureQ30;
	SlipDraw3DExtraFieldInterpolation textureInterpolation;
	uint32_t flagsAfterClear;
	int32_t zAfterAdd;
	bool zBelow;
	bool zAbove;
	bool calledProjectFlags;
	SlipDraw3DProjectFlags projectFlags;
	bool returned;
} SlipDraw3DSplitDepthMath;

typedef struct SlipDraw3DClippedDepthExecute {
	SlipDraw3DClippedDepthDispatch dispatch;
	uint32_t activeHeadOffsetIn;
	uint32_t activeHeadOffsetOut;
	uint32_t freeHeadOffset;
	uint32_t allFlagsOut;
	uint32_t anyFlagsOut;
	SlipDraw3DClipEdge auxiliaryClip;
	SlipDraw3DSplitDepthMath firstAuxiliarySplit;
	SlipDraw3DSplitDepthMath secondAuxiliarySplit;
	SlipDraw3DClipFlags clipFlagsAfter;
	SlipDraw3DClipEdge nearClip;
	SlipDraw3DSplitDepth firstNearSplit;
	SlipDraw3DSplitDepth secondNearSplit;
	SlipDraw3DClipEdge farClip;
	SlipDraw3DSplitDepth firstFarSplit;
	SlipDraw3DSplitDepth secondFarSplit;
	SlipDraw3DClipFlags clipFlagsFinal;
} SlipDraw3DClippedDepthExecute;

typedef struct SlipDraw3DSplitScreenX {
	bool highPrecisionPath;
	uint32_t targetOffset;
	uint32_t otherOffset;
	int32_t clipPlaneX;
	int32_t xDeltaToPlane;
	int32_t xDeltaBetweenRecords;
	bool negatedDeltas;
	uint32_t interpolationRatio;
	int32_t yStep;
	bool interpolatedShade;
	uint16_t shadeStep;
	bool calledInterpolateDepthAndTextureQ14;
	bool calledInterpolateDepthAndTextureQ30;
	SlipDraw3DDepthExtraInterpolation depthAndTextureInterpolation;
	bool wroteClipPlaneX;
	uint32_t flagsBefore;
	uint32_t flagsAfterClear;
	bool yBelow;
	bool yAbove;
	bool yInsideFiniteSentinel;
	uint32_t flagsOut;
	bool returned;
} SlipDraw3DSplitScreenX;

typedef struct SlipDraw3DSplitScreenY {
	bool highPrecisionPath;
	uint32_t targetOffset;
	uint32_t otherOffset;
	int32_t clipPlaneY;
	int32_t yDeltaToPlane;
	int32_t yDeltaBetweenRecords;
	bool negatedDeltas;
	uint32_t interpolationRatio;
	int32_t xStep;
	bool interpolatedShade;
	uint16_t shadeStep;
	bool calledInterpolateDepthAndTextureQ14;
	bool calledInterpolateDepthAndTextureQ30;
	SlipDraw3DDepthExtraInterpolation depthAndTextureInterpolation;
	bool wroteClipPlaneY;
	bool returned;
} SlipDraw3DSplitScreenY;

typedef struct SlipDraw3DPostPlaneBoundsVisit {
	uint32_t recordOffset;
	int32_t screenX;
	int32_t screenY;
	uint32_t screenFlags;
	uint32_t allFlagsAfterAnd;
	uint32_t nextRecordOffset;
	bool loop;
} SlipDraw3DPostPlaneBoundsVisit;

typedef struct SlipDraw3DPostPlaneBounds {
	uint32_t headOffset;
	uint32_t allFlagsInitial;
	int32_t limitXMin;
	int32_t limitXMax;
	int32_t limitYMin;
	int32_t limitYMax;
	uint32_t allFlagsOut;
	size_t visitCount;
	bool clearedRejectCarry;
	bool setRejectCarry;
	bool returned;
	bool carryOut;
} SlipDraw3DPostPlaneBounds;

typedef struct SlipDraw3DSplitPostPlane {
	uint32_t targetOffset;
	uint32_t otherOffset;
	int32_t targetDepth;
	int32_t otherDepth;
	uint32_t depthDelta;
	uint32_t interpolationRatio;
	int32_t xStep;
	int32_t yStep;
	bool returned;
} SlipDraw3DSplitPostPlane;

typedef struct SlipDraw3DPostPlaneClipRecordVisit {
	uint32_t planeOffset;
	uint32_t recordOffset;
	int32_t screenXDelta;
	int16_t planeNormalX;
	int32_t screenYDelta;
	int16_t planeNormalY;
	int32_t planeDepth;
	uint32_t flag;
	uint32_t allFlagsAfterAnd;
	uint32_t anyFlagsAfter;
	uint32_t nextRecordOffset;
	bool loop;
} SlipDraw3DPostPlaneClipRecordVisit;

typedef struct SlipDraw3DPostPlaneClipPlaneVisit {
	uint32_t planeOffset;
	uint32_t activeHeadOffset;
	uint32_t allFlagsAfterLoop;
	uint32_t anyFlagsAfterLoop;
	bool rejectAllOutside;
	bool anyFlagsNonzero;
	bool calledClipEdge;
	SlipDraw3DClipEdge clipEdge;
	bool clipEdgeRejected;
	bool calledSplitFirstIntersection;
	SlipDraw3DSplitPostPlane splitFirst;
	bool calledSplitSecondIntersection;
	SlipDraw3DSplitPostPlane splitSecond;
	uint32_t nextPlaneOffset;
	bool loop;
} SlipDraw3DPostPlaneClipPlaneVisit;

typedef enum SlipDraw3DPostPlaneClipBranch {
	SLIP_DRAW3D_POST_PLANE_CLIP_BRANCH_REJECT_ALL_OUTSIDE,
	SLIP_DRAW3D_POST_PLANE_CLIP_BRANCH_REJECT_CLIP_EDGE,
	SLIP_DRAW3D_POST_PLANE_CLIP_BRANCH_ACCEPT
} SlipDraw3DPostPlaneClipBranch;

typedef struct SlipDraw3DPostPlaneClip {
	uint32_t planeHeadOffset;
	uint32_t activeHeadOffsetIn;
	uint32_t activeHeadOffsetOut;
	uint32_t freeHeadOffset;
	size_t planeVisitCount;
	size_t recordVisitCount;
	bool clearedRejectCarry;
	bool setRejectCarry;
	bool returned;
	bool carryOut;
	SlipDraw3DPostPlaneClipBranch branch;
} SlipDraw3DPostPlaneClip;

typedef struct SlipDraw3DScreenPlaneExecute {
	SlipDraw3DScreenPlaneDispatch dispatch;
	uint32_t activeHeadOffsetIn;
	uint32_t activeHeadOffsetOut;
	uint32_t freeHeadOffset;
	SlipDraw3DClipEdge leftClip;
	SlipDraw3DClipEdge rightClip;
	SlipDraw3DClipFlags clipFlagsAfter;
	SlipDraw3DClipEdge topClip;
	SlipDraw3DClipEdge bottomClip;
	SlipDraw3DPostPlaneBounds postPlaneBounds;
	SlipDraw3DPostPlaneClip postPlaneClip;
} SlipDraw3DScreenPlaneExecute;

typedef struct SlipDraw3DClipDispatchExecute {
	SlipDraw3DClipDispatch dispatch;
	uint32_t activeHeadOffsetIn;
	uint32_t activeHeadOffsetOut;
	uint32_t freeHeadOffset;
	uint32_t anyFlagsOut;
	SlipDraw3DClippedDepthExecute clippedDepth;
	SlipDraw3DScreenPlaneExecute screenPlane;
} SlipDraw3DClipDispatchExecute;

typedef struct SlipDraw3DActiveRingExecute {
	SlipDraw3DActiveRingBuild build;
	SlipDraw3DClipDispatchExecute dispatch;
	uint32_t activeHeadOffsetOut;
	bool returned;
	bool carryOut;
} SlipDraw3DActiveRingExecute;

typedef struct SlipDraw3DActiveMaterialRingExecute {
	bool specialBranch;
	bool hasVertexShadingFlag;
	SlipDraw3DActiveRingBuild build;
	SlipDraw3DClipDispatchExecute dispatch;
	uint32_t mode;
	uint32_t drawMode;
	uint32_t materialColor;
	uint32_t activeHeadOffsetOut;
	bool returned;
	bool carryOut;
} SlipDraw3DActiveMaterialRingExecute;

typedef struct SlipDraw3DSolidRingExecute {
	SlipDraw3DBuildResult build;
	SlipDraw3DClipDispatchExecute dispatch;
	uint32_t activeHeadOffsetOut;
	bool returned;
	bool carryOut;
} SlipDraw3DSolidRingExecute;

typedef struct SlipDraw3DLineClipSelect {
	uint32_t clipMask;
	uint32_t inputActiveHeadOffset;
	uint32_t pairedOffset;
	bool activeHeadHasMask;
	uint32_t targetOffset;
	uint32_t otherOffset;
	bool returned;
} SlipDraw3DLineClipSelect;

typedef enum SlipDraw3DLineClipBranch {
	SLIP_DRAW3D_LINE_CLIP_BRANCH_REJECT_DEPTH_PLANE,
	SLIP_DRAW3D_LINE_CLIP_BRANCH_REJECT_SCREEN_X,
	SLIP_DRAW3D_LINE_CLIP_BRANCH_REJECT_SCREEN_Y,
	SLIP_DRAW3D_LINE_CLIP_BRANCH_ACCEPT
} SlipDraw3DLineClipBranch;

typedef struct SlipDraw3DLineClipExecute {
	uint32_t inputActiveHeadOffset;
	uint32_t anyFlagsEntry;
	uint32_t renderFlags;
	bool testRenderFlagsBit;
	bool testDepthPlane;
	SlipDraw3DLineClipSelect selectDepthPlane;
	SlipDraw3DSplitDepthMath splitDepthPlane;
	SlipDraw3DClipFlags clipFlagsAfterDepthPlane;
	uint32_t allFlagsAfterDepthPlane;
	uint32_t anyFlagsAfterDepthPlane;
	bool testNear;
	SlipDraw3DLineClipSelect selectNear;
	SlipDraw3DSplitDepth splitNear;
	bool testFar;
	SlipDraw3DLineClipSelect selectFar;
	SlipDraw3DSplitDepth splitFar;
	SlipDraw3DClipFlags clipFlagsAfterDepthRange;
	uint32_t allFlagsAfterDepthRange;
	uint32_t anyFlagsAfterDepthRange;
	bool testLeft;
	SlipDraw3DLineClipSelect selectLeft;
	SlipDraw3DSplitScreenX splitLeft;
	bool testRight;
	SlipDraw3DLineClipSelect selectRight;
	SlipDraw3DSplitScreenX splitRight;
	SlipDraw3DClipFlags clipFlagsAfterScreenX;
	uint32_t allFlagsAfterScreenX;
	uint32_t anyFlagsAfterScreenX;
	bool testTop;
	SlipDraw3DLineClipSelect selectTop;
	SlipDraw3DSplitScreenY splitTop;
	bool testBottom;
	SlipDraw3DLineClipSelect selectBottom;
	SlipDraw3DSplitScreenY splitBottom;
	bool clearedRejectCarry;
	bool setRejectCarry;
	bool returned;
	bool carryOut;
	SlipDraw3DLineClipBranch branch;
} SlipDraw3DLineClipExecute;

typedef struct SlipDraw3DLinePairExecute {
	SlipDraw3DBuildResult build;
	uint32_t activeHeadOffsetOut;
	bool callRendererClipLine;
	SlipDraw3DLineClipExecute lineClip;
	bool clearedRejectCarry;
	bool setRejectCarry;
	bool returned;
	bool carryOut;
} SlipDraw3DLinePairExecute;

typedef struct SlipDraw3DTexturedRingExecute {
	bool calledPolygonStatus;
	SlipDraw3DPolygonStatus status;
	SlipDraw3DTexturedRingBuild build;
	SlipDraw3DClipDispatchExecute dispatch;
	uint32_t activeHeadOffsetOut;
	bool returned;
	bool carryOut;
	uint32_t failureStage;
} SlipDraw3DTexturedRingExecute;

typedef struct SlipDraw3DTextureCoordinates {
	uint16_t u, v;
} SlipDraw3DTextureCoordinates;

typedef struct SlipDraw3DPointPolygon {
	uint32_t mode, drawMode, color;
	uint32_t allFlags, anyFlags;
	bool calledClipDepth, calledClipScreen, carryOut;
	SlipDraw3DClippedDepthExecute depth;
	SlipDraw3DScreenPlaneExecute screen;
} SlipDraw3DPointPolygon;

int SlipDraw3D_PointPolygon(SlipDraw3DRecordPool *pool, const SlipDraw3DVec32 *const *points, const uint16_t *shades,
                            uint16_t count, uint32_t color, SlipDraw3DProjectState *state, int hasPostPlanes,
                            const uint8_t *postPlanes, size_t postPlaneBytes, uint32_t postPlaneHead, int32_t postMinX,
                            int32_t postMaxX, int32_t postMinY, int32_t postMaxY, size_t maxClipEdges,
                            SlipDraw3DClipFlagVisit *flagVisits, size_t flagCapacity,
                            SlipDraw3DPostPlaneBoundsVisit *boundsVisits, size_t boundsCapacity,
                            SlipDraw3DPostPlaneClipRecordVisit *recordVisits, size_t recordCapacity,
                            SlipDraw3DPostPlaneClipPlaneVisit *planeVisits, size_t planeCapacity,
                            SlipDraw3DPointPolygon *result);

typedef struct SlipDraw3DSpritePolygon {
	uint32_t mode, drawMode, textureHandle;
	uint32_t allFlags, anyFlags;
	bool calledClipDepth, calledClipScreen, carryOut;
	SlipDraw3DClippedDepthExecute depth;
	SlipDraw3DScreenPlaneExecute screen;
} SlipDraw3DSpritePolygon;

int SlipDraw3D_SpritePolygon(SlipDraw3DRecordPool *pool, const SlipDraw3DVec32 *const *points,
                             const SlipDraw3DTextureCoordinates *textureCoordinates, uint16_t count,
                             uint32_t textureHandle, SlipDraw3DProjectState *state, int hasPostPlanes,
                             const uint8_t *postPlanes, size_t postPlaneBytes, uint32_t postPlaneHead, int32_t postMinX,
                             int32_t postMaxX, int32_t postMinY, int32_t postMaxY, size_t maxClipEdges,
                             SlipDraw3DClipFlagVisit *flagVisits, size_t flagCapacity,
                             SlipDraw3DPostPlaneBoundsVisit *boundsVisits, size_t boundsCapacity,
                             SlipDraw3DPostPlaneClipRecordVisit *recordVisits, size_t recordCapacity,
                             SlipDraw3DPostPlaneClipPlaneVisit *planeVisits, size_t planeCapacity,
                             SlipDraw3DSpritePolygon *result);

typedef struct SlipDraw3DPrimitivePathExecute {
	bool savedIndexStream;
	bool savedMaterialStream;
	bool savedCountAndFlags;
	bool callReturnActiveRing;
	SlipDraw3DReturnActiveRing returnActive;
	bool callDraw3DBuildActiveRing;
	SlipDraw3DActiveRingExecute activeRing;
	bool draw3DBuildActiveRingCarry;
	bool callDraw3DActiveBounds;
	SlipDraw3DActiveBounds bounds;
	int32_t minX;
	int32_t minY;
	int32_t maxX;
	int32_t maxY;
	bool clearedRejectCarry;
	bool setRejectCarry;
	bool returned;
	bool carryOut;
} SlipDraw3DPrimitivePathExecute;

typedef struct SlipDraw3DStateLoad {
	uint32_t recordIndex;
	const SlipDraw3DStateRecord *recordPointer;
} SlipDraw3DStateLoad;

typedef struct SlipDraw3DOriginSetup {
	SlipDraw3DVec32 drawPosition;
	const SlipView3DMatrix *copyMatrix;
	const SlipView3DMatrix *originMatrix;
	bool callView3DCopyMatrixWords;
	bool directOriginBranch;
	SlipDraw3DVec32 cameraOrigin;
	SlipDraw3DVec32 originDelta;
	SlipDraw3DVec32 origin;
	bool lightEnabled;
	SlipDraw3DVec32 lightInput;
	SlipDraw3DVec32 lightStore;
	bool returned;
} SlipDraw3DOriginSetup;

typedef struct SlipDraw3DRestoreVertexBufferCursor {
	uint32_t vertexBufferCursorBefore;
	uint32_t savedVertexBufferCursor;
	uint32_t discardedVertexBufferBytes;
	uint32_t vertexBufferCursorAfter;
} SlipDraw3DRestoreVertexBufferCursor;

typedef struct SlipDraw3DInitVertexBuffer {
	bool savedGeneralState;
	uint32_t capacityAfter;
	uint32_t vertexRecordBytes;
	uint32_t allocationBytesLow;
	uint32_t allocationBytesHigh;
	uint32_t allocationBytes;
	bool clearedAllocationFlags;
	bool callResourceAllocateAnonymous;
	bool resourceAllocateAnonymousCarry;
	uint16_t allocationHandle;
	uint16_t handleAfter;
	bool calledLockVertexBuffer;
	uint32_t cursorAfter;
	uint32_t baseAfter;
	uint32_t limitAfter;
	bool restoredGeneralState;
	bool returned;
	uint32_t allocationErrorMessageOffset;
	bool jumpedToAllocationError;
} SlipDraw3DInitVertexBuffer;

typedef struct SlipDraw3DBuildVertexRecords {
	bool savedGeneralState;
	uint32_t vertexCount;
	uint32_t capacity;
	bool calledInitializeVertexBuffer;
	uint32_t initialBufferCapacity;
	bool savedSourcePointCallback;
	bool savedTransformCallback;
	SlipDraw3DStateRecord *stateRecordPointer;
	uint32_t vertexBufferCursorBefore;
	uint32_t byteCount;
	uint32_t proposedVertexBufferCursor;
	uint32_t vertexBufferLimit;
	bool cursorOverflow;
	bool calledGrowVertexBuffer;
	uint32_t grownBufferCapacity;
	uint32_t vertexBufferCursorAfter;
	uint32_t vertexRecordBase;
	uint32_t storedVertexBufferCursor;
	SlipDraw3DTransformFn stateRecordTransform;
	SlipDraw3DTransformFn transformCallback;
	SlipDraw3DSourcePointFn stateRecordSourcePoint;
	SlipDraw3DSourcePointFn sourcePointCallback;
	uint32_t sourceTailBytesUnsigned;
	uint32_t vertexRecordTailBytes;
	uint32_t initialLoopCount;
	uint32_t initialVertexFlags;
	int32_t sourceTailBytesSigned;
	uint32_t loopCount;
	bool restoredGeneralState;
	bool returned;
} SlipDraw3DBuildVertexRecords;

typedef struct SlipDraw3DGrowVertexBuffer {
	bool savedGeneralState;
	uint16_t pushedHandle;
	uint32_t pushedLimit;
	uint32_t pushedBase;
	uint32_t capacityAfter;
	uint32_t vertexRecordBytes;
	uint32_t allocationBytesLow;
	uint32_t allocationBytesHigh;
	uint32_t allocationBytes;
	bool clearedAllocationFlags;
	bool callResourceAllocateAnonymous;
	bool resourceAllocateAnonymousCarry;
	uint16_t allocationHandle;
	uint16_t handleAfter;
	bool calledLockVertexBuffer;
	uint32_t baseAfter;
	uint32_t limitAfter;
	uint32_t baseDelta;
	uint16_t stateRecordCount;
	uint32_t stateRecordBase;
	uint32_t stateRecordUpdates;
	uint32_t cursorAfter;
	uint32_t copyBytes;
	uint16_t poppedHandle;
	bool callResourceUnlock;
	bool calledFreeVertexBuffer;
	bool restoredGeneralState;
	bool returned;
	uint32_t allocationErrorMessageOffset;
	bool jumpedToAllocationError;
} SlipDraw3DGrowVertexBuffer;

typedef enum SlipDraw3DBackgroundSetupBranch {
	SLIP_DRAW3D_BACKGROUND_SETUP_STRIPS,
	SLIP_DRAW3D_BACKGROUND_SETUP_MATERIAL_FILL,
	SLIP_DRAW3D_BACKGROUND_SETUP_FIXED_FILL
} SlipDraw3DBackgroundSetupBranch;

typedef struct SlipDraw3DBackgroundSetup {
	uint32_t backgroundDistance;
	uint32_t backgroundSpan;
	uint16_t backgroundMaterialIndex;
	uint8_t materialStripCount;
	uint32_t projectionScale;
	uint32_t cachedProjectionScaleIn;
	bool callRendererAdvanceBackground;
	uint32_t cachedProjectionScaleOut;
	uint16_t projectionRevisionAfterScaleCheck;
	uint16_t stripMaterialIndex;
	uint16_t materialCount;
	uint32_t stripMaterialRecordOffset;
	uint8_t materialStartValueByte;
	uint8_t materialEndValueByte;
	uint8_t fixedStripCount;
	uint8_t cachedFixedStripCount;
	uint8_t cachedStartValueByte;
	uint8_t cachedEndValueByte;
	uint16_t stripCurvature;
	uint16_t cachedStripCurvature;
	bool callDraw3DBackgroundStripBuild;
	bool callDraw3DLoadStateRecord;
	bool callDraw3DSetOrigin;
	uint16_t viewPitchComponent;
	uint16_t fixedFillPitchThreshold;
	SlipDraw3DBackgroundSetupBranch branch;
	bool callDraw3DNormalizeVector2D;
	uint16_t viewRollX;
	uint16_t viewRollY;
	SlipDraw3DNormalizeVector2D normalize;
	uint16_t spriteScaleX;
	uint16_t spriteScaleY;
	uint16_t spriteHalfWidth;
	uint16_t spriteHalfHeight;
	uint16_t tiltComponent;
	uint32_t tiltComponentSquare;
	uint32_t complementSquare;
	uint16_t complementComponent;
	bool callDraw3DBackgroundStripTableFixed;
	bool callDraw3DBackgroundStripTableMaterial;
	bool callDraw3DBackgroundMaterial;
	bool callDraw3DBackgroundValue;
	uint16_t fixedFillFlag;
	uint16_t materialFillFlag;
	uint16_t materialFillValue;
	bool returned;
} SlipDraw3DBackgroundSetup;

typedef struct SlipDraw3DBackgroundStripBuild {
	uint8_t requestedStripCount;
	uint8_t materialStartValueByte;
	uint8_t materialEndValueByte;
	uint16_t stripCurvature;
	uint32_t scale;
	uint16_t count;
	uint16_t projectionRevisionAfterBuild;
	uint16_t diagnosticCount;
	uint16_t diagnosticAngles[SLIP_BACKGROUND_STRIP_DIAGNOSTIC_CAPACITY];
	uint16_t diagnosticTangents[SLIP_BACKGROUND_STRIP_DIAGNOSTIC_CAPACITY];
	uint16_t diagnosticStripOffsets[SLIP_BACKGROUND_STRIP_DIAGNOSTIC_CAPACITY];
	bool returned;
} SlipDraw3DBackgroundStripBuild;

typedef enum SlipDraw3DBackgroundPassBranch {
	SLIP_DRAW3D_BACKGROUND_PASS_SKIP,
	SLIP_DRAW3D_BACKGROUND_PASS_FILL,
	SLIP_DRAW3D_BACKGROUND_PASS_STRIP
} SlipDraw3DBackgroundPassBranch;

typedef struct SlipDraw3DBackgroundPassMaterial {
	uint16_t fixedFillFlag;
	uint16_t materialFillFlag;
	uint16_t materialFillTag;
	SlipDraw3DBackgroundPassBranch branch;
	bool callTrackWorldLoadClipRegisters;
	uint16_t fillValue;
	bool callRasterFillRectClipped;
	uint32_t stripTableAddress;
	bool callDraw3DStripDispatch;
	bool returned;
} SlipDraw3DBackgroundPassMaterial;

typedef struct SlipDraw3DBackgroundPassFixed {
	uint16_t fixedFillFlag;
	uint16_t materialFillFlag;
	uint16_t fixedFillTag;
	SlipDraw3DBackgroundPassBranch branch;
	bool callDraw3DLoadClipAndCenter;
	uint16_t fillValue;
	bool callRasterFillRectClipped;
	uint32_t stripTableAddress;
	bool callDraw3DStripDispatch;
	bool returned;
} SlipDraw3DBackgroundPassFixed;

typedef struct SlipDraw3DBackgroundDispatch {
	bool savedRegisters;
	bool callDraw3DBackgroundSetup;
	bool callDraw3DBackgroundPassMaterial;
	SlipDraw3DBackgroundPassMaterial materialPass;
	bool callDraw3DBackgroundPassFixed;
	SlipDraw3DBackgroundPassFixed fixedPass;
	bool restoredRegisters;
	bool returned;
} SlipDraw3DBackgroundDispatch;

extern uint16_t g_spriteScaleX;
extern uint16_t g_spriteScaleY;
extern uint16_t g_spriteHalfWidth;
extern uint16_t g_spriteHalfHeight;

void SlipDraw3D_ProjectSpriteCorner(int16_t cornerSide, int16_t cornerOffset, uint16_t originX, uint16_t originY,
                                    int16_t *outX, int16_t *outY);

typedef struct SlipDraw3DBackgroundCenter {
	uint32_t spanPosition;
	uint32_t backgroundDistance;
	uint16_t tiltComponent;
	uint16_t complementComponent;
	uint32_t scale;
	uint32_t viewportX;
	uint32_t viewportY;
	uint16_t scaleX;
	uint16_t scaleY;
	uint32_t forwardLow;
	uint32_t forwardHigh;
	uint32_t shiftedForward;
	bool carryForward;
	uint32_t depthLow;
	uint32_t depthHigh;
	uint32_t shiftedDepth;
	bool carryDepth;
	uint32_t scaledForwardLow;
	uint32_t scaledForwardHigh;
	uint32_t doubledLow;
	uint32_t doubledHigh;
	uint32_t quotient;
	uint32_t rounded;
	uint16_t projectedOffset;
	uint16_t shiftedX;
	bool carryX;
	uint16_t shiftedY;
	bool carryY;
	uint16_t centerX;
	uint16_t centerY;
	bool returned;
} SlipDraw3DBackgroundCenter;

typedef struct SlipDraw3DBackgroundMaterial {
	uint16_t materialIndex;
	uint16_t materialCount;
	bool materialIndexInRange;
	uint32_t materialRecordOffset;
	uint32_t selectedMaterialRecordOffset;
	uint32_t detailScale;
	bool zeroDetailBranch;
	uint16_t lightVectorX;
	uint32_t lightVectorY;
	uint32_t lightVectorZ;
	uint16_t projectedLight;
	uint32_t negatedProjectedLight;
	bool negativeBranch;
	uint16_t scaledLight;
	uint16_t lightValue;
	bool returned;
} SlipDraw3DBackgroundMaterial;

typedef enum SlipDraw3DBackgroundValueBranch {
	SLIP_DRAW3D_BACKGROUND_VALUE_BRANCH_MATERIAL,
	SLIP_DRAW3D_BACKGROUND_VALUE_BRANCH_LIMIT
} SlipDraw3DBackgroundValueBranch;

typedef struct SlipDraw3DBackgroundValue {
	bool callDraw3DLightDepthBlend;
	uint16_t lightValue;
	uint16_t blendFactor;
	bool callDraw3DLightingMaterial;
	uint32_t limitEnabled;
	SlipDraw3DBackgroundValueBranch branch;
	uint32_t baseValue;
	uint32_t endValue;
	uint32_t valueDifference;
	uint16_t scaledDifferenceLow;
	uint32_t mergedDifference;
	uint32_t interpolatedValue;
	uint16_t value;
	bool returned;
} SlipDraw3DBackgroundValue;

typedef struct SlipDraw3DBackgroundValueExecute {
	uint32_t depth;
	bool callDraw3DLightDepthBlend;
	SlipDraw3DLightDepthBlend depthBlend;
	uint16_t lightValue;
	bool clearedLightDepth;
	bool callDraw3DLightingMaterial;
	SlipDraw3DLightingMaterial lighting;
	SlipDraw3DBackgroundValue interpolation;
	uint16_t value;
	bool returned;
} SlipDraw3DBackgroundValueExecute;

typedef struct SlipDraw3DBackgroundStripEntry {
	uint16_t stripOffset;
	bool zeroOffsetBranch;
	uint16_t centerX;
	uint16_t centerY;
	uint16_t scaleX;
	uint16_t scaleY;
	uint16_t halfWidth;
	uint16_t halfHeight;
	uint16_t shiftedX;
	uint16_t shiftedY;
	bool carryX;
	bool carryY;
	uint16_t adjustedX;
	uint16_t adjustedY;
	uint16_t recordRight;
	uint16_t recordBottom;
	uint16_t recordLeft;
	uint16_t recordTop;
	bool returnedWithOffset;
	bool returnedWithoutOffset;
} SlipDraw3DBackgroundStripEntry;

typedef struct SlipDraw3DBackgroundStripTableVisitFixed {
	uint32_t entryAddress;
	uint16_t stripOffset;
	bool callDraw3DBackgroundStripEntry;
	SlipDraw3DBackgroundStripEntry entry;
	uint32_t nextEntryAddress;
	uint32_t remainingStripCount;
	bool loop;
} SlipDraw3DBackgroundStripTableVisitFixed;

typedef struct SlipDraw3DBackgroundStripTableFixed {
	uint32_t spanPosition;
	bool callDraw3DBackgroundCenter;
	SlipDraw3DBackgroundCenter center;
	uint16_t centerX;
	uint16_t centerY;
	uint16_t cachedCenterX;
	uint16_t cachedCenterY;
	uint32_t tableAddress;
	uint16_t sourceCount;
	uint32_t firstEntryAddress;
	size_t visitCount;
	bool returned;
} SlipDraw3DBackgroundStripTableFixed;

typedef enum SlipDraw3DBackgroundStripTableBranch {
	SLIP_DRAW3D_BACKGROUND_STRIP_TABLE_BRANCH_MULTI,
	SLIP_DRAW3D_BACKGROUND_STRIP_TABLE_BRANCH_FALLBACK
} SlipDraw3DBackgroundStripTableBranch;

typedef struct SlipDraw3DBackgroundStripTableVisitMaterial {
	uint32_t entryAddress;
	uint32_t spanPosition;
	bool callDraw3DBackgroundCenter;
	SlipDraw3DBackgroundCenter center;
	bool centerUnchanged;
	bool callDraw3DBackgroundStripEntry;
	SlipDraw3DBackgroundStripEntry entry;
	bool callDraw3DApproxAbsVectorLength;
	SlipDraw3DApproxAbsVectorLength approx;
	bool callDraw3DBackgroundValue;
	SlipDraw3DBackgroundValueExecute backgroundValue;
	uint16_t previousValue;
	bool duplicateValue;
	uint32_t nextEntryAddress;
	uint32_t nextSpanPosition;
	uint32_t remainingStripCount;
	bool loop;
} SlipDraw3DBackgroundStripTableVisitMaterial;

typedef struct SlipDraw3DBackgroundStripTableMaterial {
	uint32_t entryBaseAddress;
	uint16_t count;
	uint32_t fadeStart;
	SlipDraw3DBackgroundStripTableBranch branch;
	uint32_t spanStep;
	uint32_t spanPosition;
	bool callDraw3DBackgroundMaterial;
	SlipDraw3DBackgroundMaterial material;
	bool callInitialBackgroundCenter;
	SlipDraw3DBackgroundCenter initialCenter;
	uint16_t centerX;
	uint16_t centerY;
	bool callInitialBackgroundStripEntry;
	SlipDraw3DBackgroundStripEntry initialEntry;
	bool callInitialBackgroundValue;
	SlipDraw3DBackgroundValueExecute initialValue;
	uint16_t outputCount;
	uint32_t entryAddressAfterInitial;
	uint16_t previousValue;
	size_t visitCount;
	bool callFallbackBackgroundStripEntry;
	SlipDraw3DBackgroundStripEntry fallbackSecondEntry;
	bool returned;
} SlipDraw3DBackgroundStripTableMaterial;

typedef struct SlipDraw3DBackgroundSetupExecute {
	SlipDraw3DBackgroundSetup setup;
	bool callDraw3DBackgroundStripBuild;
	SlipDraw3DBackgroundStripBuild stripBuild;
	bool callBackgroundMaterialForFill;
	SlipDraw3DBackgroundMaterial materialForFill;
	bool callBackgroundValueForFill;
	SlipDraw3DBackgroundValueExecute valueForFill;
	bool callDraw3DBackgroundStripTableFixed;
	SlipDraw3DBackgroundStripTableFixed fixedStripTable;
	bool callDraw3DBackgroundStripTableMaterial;
	SlipDraw3DBackgroundStripTableMaterial materialStripTable;
	uint16_t fixedFillFlag;
	uint16_t materialFillFlag;
	uint16_t materialFillValue;
	uint16_t fixedFillValue;
	bool returned;
} SlipDraw3DBackgroundSetupExecute;

typedef struct SlipDraw3DStripDispatchVisit {
	uint32_t entryAddress;
	uint32_t remainingStripCountBefore;
	uint32_t firstPointAddress;
	uint32_t secondPointAddress;
	uint32_t thirdPointAddress;
	uint32_t fourthPointAddress;
	uint32_t pointCount;
	uint16_t stripValue;
	uint32_t pointPointerTableAddress;
	bool callReturnActiveRing;
	bool callDraw3DPointPointerRing;
	bool draw3DPointPointerRingCarry;
	bool callRasterizeFlatDispatch;
	SlipDraw3DFlatRingDispatch flatDispatch;
	SlipDraw3DRasterPoint flatPoints[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	uint32_t nextEntryAddress;
	uint32_t remainingStripCount;
	bool loop;
	uint32_t rasterValue;
	size_t returnActiveVisitCountBefore;
	size_t returnActiveVisitCountAfter;
} SlipDraw3DStripDispatchVisit;

typedef struct SlipDraw3DStripDispatch {
	uint32_t pushedRenderFlags;
	uint32_t renderFlagsDuringLoop;
	uint32_t stripTableAddress;
	uint16_t sourceCount;
	uint32_t loopCountInitial;
	uint32_t firstEntryAddress;
	size_t visitCount;
	uint32_t restoredRenderFlags;
	bool returned;
	size_t returnActiveVisitCount;
	size_t rasterizedCount;
} SlipDraw3DStripDispatch;

typedef struct SlipDraw3DBackgroundPassExecuteMaterial {
	SlipDraw3DBackgroundPassMaterial materialPass;
	bool callDraw3DStripDispatch;
	SlipDraw3DStripDispatch strip;
	size_t returnActiveVisitCount;
	size_t pointRingVisitCount;
	size_t stripVisitCount;
} SlipDraw3DBackgroundPassExecuteMaterial;

typedef struct SlipDraw3DBackgroundPassExecuteFixed {
	SlipDraw3DBackgroundPassFixed fixedPass;
	bool callDraw3DStripDispatch;
	SlipDraw3DStripDispatch strip;
	size_t returnActiveVisitCount;
	size_t pointRingVisitCount;
	size_t stripVisitCount;
} SlipDraw3DBackgroundPassExecuteFixed;

typedef struct SlipDraw3DBackgroundDispatchExecute {
	bool savedRegisters;
	bool callDraw3DBackgroundSetup;
	bool callDraw3DBackgroundPassMaterial;
	SlipDraw3DBackgroundPassExecuteMaterial materialPass;
	bool callDraw3DBackgroundPassFixed;
	SlipDraw3DBackgroundPassExecuteFixed fixedPass;
	bool restoredRegisters;
	bool returned;
} SlipDraw3DBackgroundDispatchExecute;

typedef struct SlipDraw3DBackgroundDispatchSetupExecute {
	bool savedRegisters;
	bool callDraw3DBackgroundSetup;
	SlipDraw3DBackgroundSetupExecute setup;
	bool callDraw3DBackgroundPassMaterial;
	bool callDraw3DBackgroundPassFixed;
	SlipDraw3DBackgroundDispatchExecute dispatch;
	bool restoredRegisters;
	bool returned;
} SlipDraw3DBackgroundDispatchSetupExecute;

typedef struct SlipDraw3DLimitState {
	uint32_t limitStart;
	uint32_t limitEnd;
	uint32_t limitEnabled;
} SlipDraw3DLimitState;

typedef struct SlipDraw3DListState {
	uint32_t capacity;
	uint32_t remaining;
	uint32_t currentOffset;
	uint32_t baseOffset;
	uint32_t frameDepth;
	uint32_t frameRootOffsets[SLIP_DRAW3D_LIST_MAX_FRAMES];
} SlipDraw3DListState;

struct TrackViewRawBspContext;

typedef bool (*SlipDraw3DListCallback)(struct TrackViewRawBspContext *context, uint32_t payload);

typedef struct SlipDraw3DListNode {
	uint32_t sortKey;
	SlipDraw3DListCallback callback;
	uint32_t payload;
	uint32_t leftOffset;
	uint32_t rightOffset;
} SlipDraw3DListNode;

extern SlipDraw3DListState SlipDraw3D_listState;
extern SlipDraw3DListNode *SlipDraw3D_listPool;
extern uint8_t SlipDraw3D_listInitialized;

bool SlipDraw3D_InitList(SlipDraw3DListNode *pool, uint16_t count);
void SlipDraw3D_FreeList(void);

typedef uint32_t (*SlipDraw3DProjectMaskFn)(SlipDraw3DVec32 world, void *userData);

int SlipDraw3D_BuildVertexRecords(SlipDraw3DVertexRecord *records, size_t recordCapacity, const uint8_t *source,
                                  size_t sourceSize, uint16_t vertexCount, int16_t sourceStride,
                                  SlipDraw3DTransformFn transform, SlipDraw3DSourcePointFn sourcePoint,
                                  SlipDraw3DStateRecord *stateRecord, uint32_t vertexBufferCursor,
                                  uint32_t vertexBufferLimit, SlipDraw3DBuildVertexRecords *result);

int SlipDraw3D_InitVertexBuffer(uint32_t requestedCapacity, uint16_t allocatedResourceHandle, uint32_t baseFrom,
                                bool resourceAllocateAnonymousCarry, SlipDraw3DInitVertexBuffer *result);

int SlipDraw3D_GrowVertexBuffer(uint32_t requestedCapacity, uint16_t handle, uint32_t base, uint32_t limit,
                                uint32_t cursor, uint32_t stateRecordBase, uint16_t stateRecordCount,
                                const uint8_t *oldBuffer, size_t oldBufferBytes, uint8_t *newBuffer,
                                size_t newBufferBytes, SlipDraw3DStateRecord *stateRecords,
                                uint16_t allocatedResourceHandle, uint32_t newBaseFrom,
                                bool resourceAllocateAnonymousCarry, SlipDraw3DGrowVertexBuffer *result);

int SlipDraw3D_ProjectIndex(SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount, uint16_t vertexIndex,
                            SlipDraw3DTransformFn transform, void *userData, SlipDraw3DProjectIndex *result);

int SlipDraw3D_PolygonStatus(SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount,
                             const uint8_t *indexStream, size_t indexStreamBytes, uint16_t countAndFlags,
                             const SlipDraw3DProjectState *state, SlipDraw3DTransformFn transform,
                             SlipDraw3DProjectMaskFn projectMask, void *userData, SlipDraw3DPolygonStatusVisit *visits,
                             size_t visitCapacity, SlipDraw3DPolygonStatus *result);

int SlipDraw3D_PerspectiveDepth(SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount,
                                const uint8_t *indexStream, size_t indexStreamBytes, uint16_t countAndFlags,
                                uint32_t projectionFactor, SlipDraw3DTransformFn transform, void *userData,
                                SlipDraw3DPerspectiveDepthVisit *visits, size_t visitCapacity,
                                SlipDraw3DPerspectiveDepth *result);

uint32_t SlipDraw3D_ProjectVertex(SlipDraw3DVertexRecord *record, const SlipDraw3DProjectState *state,
                                  SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectPrimary,
                                  SlipDraw3DProjectFn projectSecondary, void *userData);

int SlipDraw3D_BuildSolidDrawRecords(SlipDraw3DDrawRecord *drawRecords, size_t drawRecordCapacity,
                                     SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount,
                                     const uint8_t *indices, size_t indexBytes, uint16_t vertexCount,
                                     uint32_t materialDitherBits, const SlipDraw3DProjectState *state,
                                     SlipDraw3DTransformFn transform, SlipDraw3DProjectFn project, void *userData,
                                     SlipDraw3DBuildResult *result);

int SlipDraw3D_BuildSolidRingExecute(
    SlipDraw3DRecordPool *pool, SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount, const uint8_t *indices,
    size_t indexBytes, uint16_t vertexCount, uint32_t materialDitherBits, const SlipDraw3DProjectState *state,
    SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectPrimary, SlipDraw3DProjectFn projectSecondary,
    void *userData, int hasPostPlanes, const uint8_t *planeBase, size_t planeBytes, uint32_t planeHeadOffset,
    int32_t postLimitXMin, int32_t postLimitXMax, int32_t postLimitYMin, int32_t postLimitYMax,
    size_t maxClipEdgeVisits, SlipDraw3DClipFlagVisit *clipFlagVisits, size_t clipFlagVisitCapacity,
    SlipDraw3DPostPlaneBoundsVisit *postBoundsVisits, size_t postBoundsVisitCapacity,
    SlipDraw3DPostPlaneClipRecordVisit *postClipRecordVisits, size_t postClipRecordVisitCapacity,
    SlipDraw3DPostPlaneClipPlaneVisit *postClipPlaneVisits, size_t postClipPlaneVisitCapacity,
    SlipDraw3DSolidRingExecute *result);

int SlipDraw3D_BuildLinePairExecute(SlipDraw3DRecordPool *pool, SlipDraw3DVertexRecord *vertexRecords,
                                    size_t vertexRecordCount, const uint8_t *indices, size_t indexBytes,
                                    uint32_t materialColor, const SlipDraw3DProjectState *state,
                                    SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectPrimary,
                                    SlipDraw3DProjectFn projectSecondary, void *userData,
                                    SlipDraw3DLinePairExecute *result);

SlipDraw3DClipStep SlipDraw3D_ClassifyClip(uint32_t allClipFlags, uint32_t anyClipFlags);

int SlipDraw3D_SetLimitState(uint32_t limitStart, uint32_t limitEnd, SlipDraw3DLimitState *state);
int SlipDraw3D_ClearLimitState(SlipDraw3DLimitState *state);
int SlipDraw3D_ListPushFrame(SlipDraw3DListState *state, SlipDraw3DListNode *nodePool, size_t nodePoolBytes);
int SlipDraw3D_ListPopFrame(SlipDraw3DListState *state);
int SlipDraw3D_ListInsert(SlipDraw3DListState *state, SlipDraw3DListNode *nodePool, size_t nodePoolBytes,
                          uint32_t sortKey, SlipDraw3DListCallback callback, uint32_t payload);
int SlipDraw3D_ListTraverse(const SlipDraw3DListState *state, const SlipDraw3DListNode *nodePool, size_t nodePoolBytes,
                            struct TrackViewRawBspContext *context);

void SlipDraw3D_InitDefaultProjectState(SlipDraw3DProjectState *state);
void SlipDraw3D_SetViewport(SlipDraw3DProjectState *state, int32_t minX, int32_t minY, int32_t maxX, int32_t maxY,
                            int32_t centerX, int32_t centerY);
bool SlipDraw3D_LoadClipAndCenter(const SlipDraw3DProjectState *state, SlipDraw3DClipAndCenter *result);
void SlipDraw3D_StoreClipBounds(SlipDraw3DProjectState *state, uint32_t minimumX, uint32_t minimumY, uint32_t maximumX,
                                uint32_t maximumY);
uint32_t SlipDraw3D_DetailValue(uint32_t mode, uint32_t minDepth, uint32_t detailScale, uint32_t depth);
int SlipDraw3D_RefreshMode1Projection(uint32_t mode, uint32_t scale, uint32_t minX, uint32_t maxX, uint32_t minY,
                                      uint32_t maxY, uint32_t centerX, uint32_t centerY,
                                      SlipDraw3DRefreshMode1Projection *result);
void SlipDraw3D_SetProjectionMode(SlipDraw3DProjectState *state, uint16_t mode);
void SlipDraw3D_SetProjectionScale(SlipDraw3DProjectState *state, uint32_t scale);
void SlipDraw3D_SetProjectionScaleFactor(SlipDraw3DProjectState *state, uint16_t scaleFactor);
void SlipDraw3D_SetCameraDistance(SlipDraw3DProjectState *state, uint32_t cameraDistance);
void SlipDraw3D_ProjectModeOne(const SlipDraw3DProjectState *state, int32_t horizontal, int32_t vertical,
                               int32_t *screenX, int32_t *screenY);
int32_t SlipDraw3D_ClassifyPoints(const SlipDraw3DVec32 *const *points, uint16_t pointCount,
                                  const SlipDraw3DProjectState *state);
int SlipDraw3D_ProjectVisiblePoint(SlipDraw3DVec32 point, const SlipDraw3DProjectState *state, int32_t *screenX,
                                   int32_t *screenY);
void SlipDraw3D_DrawPoint(SlipDraw3DVec32 point, uint16_t color, const SlipDraw3DProjectState *state);
uint16_t SlipDraw3D_Root32(uint32_t value);
uint32_t SlipDraw3D_Root64(uint32_t valueLow, uint32_t valueHigh);
int SlipDraw3D_ApproxAbsVectorLength(uint32_t inputX, uint32_t inputY, uint32_t inputZ,
                                     SlipDraw3DApproxAbsVectorLength *result);
int SlipDraw3D_LightDepthBlend(uint32_t depth, uint32_t fadeStart, uint32_t fadeEnd, uint32_t fadeRange,
                               SlipDraw3DLightDepthBlend *result);
int SlipDraw3D_LightingMaterial(const SlipDraw3DMaterialRecord *materialRecord, size_t materialRecordBytes,
                                uint32_t inputFadeBlend, uint16_t diffuseLight, uint16_t specularLight,
                                uint32_t directLight, uint32_t ambientLight, uint32_t fadeColour, uint32_t fadeStart,
                                SlipDraw3DLightingMaterial *result);
int SlipDraw3D_NormalizeVector2D(uint32_t inputX, uint32_t inputY, SlipDraw3DNormalizeVector2D *result);
int SlipDraw3D_RefreshMode0Projection(uint32_t mode, uint32_t projectionScale, uint32_t minX, uint32_t maxX,
                                      uint32_t minY, uint32_t maxY, uint32_t centerX, uint32_t centerY,
                                      SlipDraw3DRefreshMode0Projection *result);
int SlipDraw3D_ProjectScreen(SlipDraw3DVec32 world, const SlipDraw3DProjectState *state, int32_t *screenX,
                             int32_t *screenY);
void SlipDraw3D_ProjectOrthographicCallback(SlipDraw3DVec32 world, int32_t *screenX, int32_t *screenY, void *userData);
void SlipDraw3D_ProjectCheckedPerspectiveCallback(SlipDraw3DVec32 world, int32_t *screenX, int32_t *screenY,
                                                  void *userData);
void SlipDraw3D_ProjectPerspective32Callback(SlipDraw3DVec32 world, int32_t *screenX, int32_t *screenY, void *userData);
void SlipDraw3D_ProjectPerspective16Callback(SlipDraw3DVec32 world, int32_t *screenX, int32_t *screenY, void *userData);

int SlipDraw3D_PrepareFlatDispatch(const SlipDraw3DDrawRecord *drawRecords, uint16_t drawRecordCount, uint32_t drawMode,
                                   uint32_t renderFlags, SlipDraw3DRasterPoint *points, size_t pointCapacity,
                                   SlipDraw3DDispatchResult *result);

int SlipDraw3D_RasterizeFlatDispatch(uint8_t color, uint8_t ditherBits, const SlipDraw3DRasterPoint *points,
                                     const SlipDraw3DDispatchResult *dispatch);

int SlipDraw3D_RasterizeFlatRing(const SlipDraw3DRecordPool *pool, uint32_t inputActiveHeadOffset, uint32_t drawMode,
                                 uint32_t renderFlags, uint32_t materialColor, uint32_t materialDitherBits,
                                 uint32_t linkOffset, SlipDraw3DRasterPoint *points, size_t pointCapacity,
                                 SlipDraw3DFlatRingDispatch *result);

int SlipDraw3D_PrepareTexturedDispatch(const SlipDraw3DRecordPool *pool, uint32_t inputActiveHeadOffset,
                                       uint32_t drawMode, uint32_t renderFlags, uint32_t textureHandle,
                                       uint32_t reverseTraversal, uint32_t pointBufferBase,
                                       SlipDraw3DTexturedDispatchPoint *points, size_t pointCapacity,
                                       SlipDraw3DTexturedDispatchVisit *visits, size_t visitCapacity,
                                       SlipDraw3DTexturedDispatch *result);

int SlipDraw3D_EmitConditionalPolygon(uint32_t postPlaneHead, uint32_t renderFlags, uint16_t countAndFlags,
                                      int materialGateCarry, SlipDraw3DConditionalPolygonEmission *result);

int SlipDraw3D_EmitActiveMaterialPolygon(int carryFrom, SlipDraw3DActiveMaterialPolygonEmission *result);

int SlipDraw3D_EmitIndexedTexturedPolygon(int indexedRingRejected, int flatDispatchCarry,
                                          SlipDraw3DIndexedTexturedPolygonEmission *result);

int SlipDraw3D_EmitPointPolygon(int draw3DPointPolygonCarry, SlipDraw3DPointPolygonEmission *result);

int SlipDraw3D_EmitLinePair(int carryFrom, SlipDraw3DLinePairEmission *result);

int SlipDraw3D_EmitLine(int rendererBuildLineCarry, SlipDraw3DLineEmission *result);
SlipDraw3DRecordPool *SlipDraw3D_GlobalRecordPool(void);
int SlipDraw3D_EnsureRecordPool(void);

int SlipDraw3D_InitRecordPool(SlipDraw3DRecordPool *pool, SlipDraw3DRecordPoolInit *result);

uint8_t *SlipDraw3D_RecordPoolBytes(SlipDraw3DRecordPool *pool);

const uint8_t *SlipDraw3D_RecordPoolConstBytes(const SlipDraw3DRecordPool *pool);

size_t SlipDraw3D_RecordPoolByteSize(void);

SlipDraw3DDrawRecord *SlipDraw3D_RecordPoolDrawRecord(SlipDraw3DRecordPool *pool, uint32_t recordOffset);

const SlipDraw3DDrawRecord *SlipDraw3D_RecordPoolConstDrawRecord(const SlipDraw3DRecordPool *pool,
                                                                 uint32_t recordOffset);

int SlipDraw3D_MaterialGate(const uint8_t *materialTable, size_t materialTableBytes, uint16_t materialIndex,
                            uint32_t flags, uint32_t countAndFlags, const uint8_t *indexedRecordBase,
                            size_t indexedRecordBytes, SlipDraw3DMaterialGate *result);

int SlipDraw3D_LoadMaterialFrameSlots(uint8_t *materialTable, size_t materialTableBytes,
                                      SlipDraw3DResourceFindNameRecord findNameRecord, void *findNameRecordUser,
                                      SlipDraw3DMaterialFrameSlots *result);

int SlipDraw3D_TexturedEmitGate(const uint8_t *materialTable, size_t materialTableBytes,
                                const SlipDraw3DStateRecord *drawStateRecord, uint32_t renderFlags,
                                uint32_t countAndFlags, uint16_t normalX, uint16_t normalY, uint16_t normalZ,
                                uint16_t materialIndex, uint32_t frameIndex, SlipDraw3DTexturedEmitGate *result);

int SlipDraw3D_GetMaterialNumber(const uint8_t *materialTable, size_t materialTableBytes, uint16_t materialGlobal,
                                 const uint8_t *sourceName, size_t sourceNameBytes, SlipDraw3DMaterialNumber *result);

const uint8_t *SlipDraw3D_GetMaterialName(const uint8_t *table, size_t tableBytes, uint16_t materialHandle);

bool SlipDraw3D_GetMaterialValues(const uint8_t *materialTable, size_t materialTableBytes, uint16_t materialIndex,
                                  uint32_t *materialColor, uint32_t *materialControl);

int SlipDraw3D_SetMaterialsNoExisting(const uint8_t *rawMaterialPayload, size_t rawMaterialPayloadBytes,
                                      uint16_t existingMaterialGlobal, uint16_t allocatedResourceHandle,
                                      uint8_t *expandedMaterialTable, size_t expandedMaterialTableBytes,
                                      SlipDraw3DMaterialInstall *result);

int SlipDraw3D_SetMaterialsAppend(const uint8_t *existingMaterialTable, size_t existingMaterialTableBytes,
                                  uint16_t existingMaterialGlobal, const uint8_t *rawMaterialPayload,
                                  size_t rawMaterialPayloadBytes, uint16_t allocatedResourceHandle,
                                  uint8_t *expandedMaterialTable, size_t expandedMaterialTableBytes,
                                  SlipDraw3DMaterialAppend *result);

int SlipDraw3D_PopFreeRecord(uint8_t *drawRecordPool, size_t recordBytes, uint32_t freeHeadOffset,
                             SlipDraw3DFreeRecordPop *result);

int SlipDraw3D_CopyIndexedRecord(SlipDraw3DDrawRecord *drawRecord, const uint8_t *vertexRecordBase,
                                 size_t vertexRecordBytes, const uint8_t *indexStream, size_t indexStreamBytes,
                                 uint32_t indexStreamOffset, uint32_t allClipFlagsIn, uint32_t anyClipFlagsIn,
                                 uint32_t projectedVertexFlags, SlipDraw3DIndexedRecordCopy *result);

int SlipDraw3D_InitSolidLoop(uint32_t firstRecordOffset, SlipDraw3DSolidLoopInit *result);

int SlipDraw3D_CopySolidRecord(SlipDraw3DDrawRecord *drawRecord, const uint8_t *vertexRecordBase,
                               size_t vertexRecordBytes, const uint8_t *indexStream, size_t indexStreamBytes,
                               uint32_t indexStreamOffset, uint32_t remainingVertices, uint32_t allClipFlagsIn,
                               uint32_t anyClipFlagsIn, uint32_t projectedVertexFlags,
                               SlipDraw3DSolidRecordCopy *result);

int SlipDraw3D_StoreMaterialBytes(SlipDraw3DDrawRecord *drawRecord, const uint8_t *materialInputStream,
                                  size_t materialInputBytes, uint32_t materialInputOffset,
                                  const uint8_t *materialRecord, uint16_t returnedMaterialColor,
                                  SlipDraw3DMaterialBytes *result);

int SlipDraw3D_RegularSetup(const uint8_t *materialRecord, size_t materialRecordBytes, uint32_t countAndFlags,
                            uint32_t materialColor, SlipDraw3DRegularSetup *result);

int SlipDraw3D_AllocateFirstActiveRecord(uint8_t *drawRecordPool, size_t recordBytes, uint32_t freeHeadOffset,
                                         SlipDraw3DFirstActiveRecord *result);

int SlipDraw3D_AppendSolidRecord(uint8_t *drawRecordPool, size_t recordBytes, uint32_t freeHeadOffset,
                                 uint32_t previousRecordOffset, uint32_t indexStreamOffset,
                                 SlipDraw3DAppendSolidRecord *result);

int SlipDraw3D_AppendRecord(uint8_t *drawRecordPool, size_t recordBytes, uint32_t freeHeadOffset,
                            uint32_t previousRecordOffset, uint32_t indexStreamOffset, uint32_t materialInputOffset,
                            SlipDraw3DAppendRecord *result);

int SlipDraw3D_CloseRecordRing(uint8_t *drawRecordPool, size_t recordBytes, uint32_t lastRecordOffset,
                               uint32_t firstRecordOffset, SlipDraw3DCloseRecordRing *result);

int SlipDraw3D_ReturnActiveRing(SlipDraw3DRecordPool *pool, SlipDraw3DReturnActiveVisit *visits, size_t visitCapacity,
                                SlipDraw3DReturnActiveRing *result);

int SlipDraw3D_BuildActiveRing(SlipDraw3DRecordPool *pool, SlipDraw3DVertexRecord *vertexRecords,
                               size_t vertexRecordCount, const uint8_t *indexStream, size_t indexStreamBytes,
                               uint16_t countAndFlags, const SlipDraw3DProjectState *state,
                               SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectPrimary,
                               SlipDraw3DProjectFn projectSecondary, void *userData, int depthClipRejected,
                               int screenClipRejected, SlipDraw3DActiveRingVisit *visits, size_t visitCapacity,
                               SlipDraw3DActiveRingBuild *result);

int SlipDraw3D_BuildTexturedRing(SlipDraw3DRecordPool *pool, SlipDraw3DVertexRecord *vertexRecords,
                                 size_t vertexRecordCount, const uint8_t *indexStream, size_t indexStreamBytes,
                                 uint16_t countAndFlags, uint32_t textureHandle, const SlipDraw3DProjectState *state,
                                 SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectPrimary,
                                 SlipDraw3DProjectFn projectSecondary, void *userData, int signFlagFrom,
                                 int depthClipRejected, int screenClipRejected, SlipDraw3DTexturedRingVisit *visits,
                                 size_t visitCapacity, SlipDraw3DTexturedRingBuild *result);

int SlipDraw3D_BuildTexturedRingExecute(
    SlipDraw3DRecordPool *pool, SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount,
    const uint8_t *indexStream, size_t indexStreamBytes, uint16_t countAndFlags, uint32_t textureHandle,
    const SlipDraw3DProjectState *state, SlipDraw3DTransformFn transform, SlipDraw3DProjectMaskFn projectMask,
    SlipDraw3DProjectFn projectPrimary, SlipDraw3DProjectFn projectSecondary, void *userData, int hasPostPlanes,
    const uint8_t *planeBase, size_t planeBytes, uint32_t planeHeadOffset, int32_t postLimitXMin, int32_t postLimitXMax,
    int32_t postLimitYMin, int32_t postLimitYMax, size_t maxClipEdgeVisits, SlipDraw3DClipFlagVisit *clipFlagVisits,
    size_t clipFlagVisitCapacity, SlipDraw3DPostPlaneBoundsVisit *postBoundsVisits, size_t postBoundsVisitCapacity,
    SlipDraw3DPostPlaneClipRecordVisit *postClipRecordVisits, size_t postClipRecordVisitCapacity,
    SlipDraw3DPostPlaneClipPlaneVisit *postClipPlaneVisits, size_t postClipPlaneVisitCapacity,
    SlipDraw3DPolygonStatusVisit *statusVisits, size_t statusVisitCapacity, SlipDraw3DTexturedRingVisit *visits,
    size_t visitCapacity, SlipDraw3DTexturedRingExecute *result);

int SlipDraw3D_PointPointerRing(SlipDraw3DRecordPool *pool, const uint8_t *pointPointerTable,
                                size_t pointPointerTableBytes, uint16_t countAndFlags, uint32_t materialColor,
                                const uint32_t *flagsFromByVisit, size_t flagCount, int depthClipRejected,
                                int screenClipRejected, SlipDraw3DPointPointerRingVisit *visits, size_t visitCapacity,
                                SlipDraw3DPointPointerRing *result);

int SlipDraw3D_PointPointerRingWithScreenPointFlags(SlipDraw3DRecordPool *pool, const uint8_t *pointPointerTable,
                                                    size_t pointPointerTableBytes, uint32_t pointCoordinateBaseAddress,
                                                    const uint8_t *pointCoordinateMemory,
                                                    size_t pointCoordinateMemoryBytes, uint16_t countAndFlags,
                                                    uint32_t materialColor, int32_t minX, int32_t maxX, int32_t minY,
                                                    int32_t maxY, int depthClipRejected, int screenClipRejected,
                                                    SlipDraw3DPointPointerRingVisit *visits, size_t visitCapacity,
                                                    SlipDraw3DPointPointerRing *result);

int SlipDraw3D_SpritePointTextureRing(SlipDraw3DRecordPool *pool, const SlipDraw3DScreenPoint16 *const *points,
                                      const SlipDraw3DTextureCoordinates *textureCoordinates, uint16_t countAndFlags,
                                      uint32_t textureHandle, int32_t minX, int32_t maxX, int32_t minY, int32_t maxY,
                                      int screenClipRejected, SlipDraw3DSpritePointTextureRingVisit *visits,
                                      size_t visitCapacity, SlipDraw3DSpritePointTextureRing *result);

int SlipDraw3D_BuildActiveRingExecute(
    SlipDraw3DRecordPool *pool, SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount,
    const uint8_t *indexStream, size_t indexStreamBytes, uint16_t countAndFlags, const SlipDraw3DProjectState *state,
    SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectPrimary, SlipDraw3DProjectFn projectSecondary,
    void *userData, int hasPostPlanes, const uint8_t *planeBase, size_t planeBytes, uint32_t planeHeadOffset,
    int32_t postLimitXMin, int32_t postLimitXMax, int32_t postLimitYMin, int32_t postLimitYMax,
    size_t maxClipEdgeVisits, SlipDraw3DClipFlagVisit *clipFlagVisits, size_t clipFlagVisitCapacity,
    SlipDraw3DPostPlaneBoundsVisit *postBoundsVisits, size_t postBoundsVisitCapacity,
    SlipDraw3DPostPlaneClipRecordVisit *postClipRecordVisits, size_t postClipRecordVisitCapacity,
    SlipDraw3DPostPlaneClipPlaneVisit *postClipPlaneVisits, size_t postClipPlaneVisitCapacity,
    SlipDraw3DActiveRingVisit *visits, size_t visitCapacity, SlipDraw3DActiveRingExecute *result);

int SlipDraw3D_BuildActiveMaterialRingExecute(
    SlipDraw3DRecordPool *pool, SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount,
    const uint8_t *indexStream, size_t indexStreamBytes, const uint8_t *materialControl, size_t materialControlBytes,
    uint16_t countAndFlags, uint32_t materialColor, uint32_t renderFlags, const SlipDraw3DProjectState *state,
    SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectPrimary, SlipDraw3DProjectFn projectSecondary,
    void *userData, int hasPostPlanes, const uint8_t *planeBase, size_t planeBytes, uint32_t planeHeadOffset,
    int32_t postLimitXMin, int32_t postLimitXMax, int32_t postLimitYMin, int32_t postLimitYMax,
    size_t maxClipEdgeVisits, SlipDraw3DClipFlagVisit *clipFlagVisits, size_t clipFlagVisitCapacity,
    SlipDraw3DPostPlaneBoundsVisit *postBoundsVisits, size_t postBoundsVisitCapacity,
    SlipDraw3DPostPlaneClipRecordVisit *postClipRecordVisits, size_t postClipRecordVisitCapacity,
    SlipDraw3DPostPlaneClipPlaneVisit *postClipPlaneVisits, size_t postClipPlaneVisitCapacity,
    SlipDraw3DActiveRingVisit *visits, size_t visitCapacity, SlipDraw3DActiveMaterialRingExecute *result);

int SlipDraw3D_ActiveBounds(const SlipDraw3DRecordPool *pool, SlipDraw3DActiveBoundsVisit *visits, size_t visitCapacity,
                            SlipDraw3DActiveBounds *result);

int SlipDraw3D_CapturePostPlaneRing(SlipDraw3DRecordPool *pool, uint32_t postPlaneHead, uint32_t sourceOffset,
                                    uint32_t planePointX, uint32_t planePointY, uint32_t planePointZ,
                                    uint16_t planeNormalX, uint16_t planeNormalY, uint16_t planeNormalZ,
                                    uint32_t cameraLightX, uint32_t cameraLightY, uint32_t cameraLightZ,
                                    SlipDraw3DPostPlaneCaptureVisit *visits, size_t visitCapacity,
                                    SlipDraw3DPostPlaneCapture *result);

int SlipDraw3D_ReleasePostPlaneRing(SlipDraw3DRecordPool *pool, uint32_t postPlaneHead,
                                    SlipDraw3DPostPlaneReleaseVisit *visits, size_t visitCapacity,
                                    SlipDraw3DPostPlaneRelease *result);

int SlipDraw3D_PrimitivePath(SlipDraw3DRecordPool *pool, SlipDraw3DVertexRecord *vertexRecords,
                             size_t vertexRecordCount, const uint8_t *indexStream, size_t indexStreamBytes,
                             uint16_t countAndFlags, const SlipDraw3DProjectState *state,
                             SlipDraw3DTransformFn transform, SlipDraw3DProjectFn project, void *userData,
                             int depthClipRejected, int screenClipRejected, SlipDraw3DReturnActiveVisit *returnVisits,
                             size_t returnVisitCapacity, SlipDraw3DActiveRingVisit *activeVisits,
                             size_t activeVisitCapacity, SlipDraw3DActiveBoundsVisit *boundsVisits,
                             size_t boundsVisitCapacity, SlipDraw3DPrimitivePath *result);

int SlipDraw3D_PrimitivePathExecute(
    SlipDraw3DRecordPool *pool, SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount,
    const uint8_t *indexStream, size_t indexStreamBytes, uint16_t countAndFlags, const SlipDraw3DProjectState *state,
    SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectPrimary, SlipDraw3DProjectFn projectSecondary,
    void *userData, int hasPostPlanes, const uint8_t *planeBase, size_t planeBytes, uint32_t planeHeadOffset,
    int32_t postLimitXMin, int32_t postLimitXMax, int32_t postLimitYMin, int32_t postLimitYMax,
    size_t maxClipEdgeVisits, SlipDraw3DReturnActiveVisit *returnVisits, size_t returnVisitCapacity,
    SlipDraw3DClipFlagVisit *clipFlagVisits, size_t clipFlagVisitCapacity,
    SlipDraw3DPostPlaneBoundsVisit *postBoundsVisits, size_t postBoundsVisitCapacity,
    SlipDraw3DPostPlaneClipRecordVisit *postClipRecordVisits, size_t postClipRecordVisitCapacity,
    SlipDraw3DPostPlaneClipPlaneVisit *postClipPlaneVisits, size_t postClipPlaneVisitCapacity,
    SlipDraw3DActiveRingVisit *activeVisits, size_t activeVisitCapacity, SlipDraw3DActiveBoundsVisit *boundsVisits,
    size_t boundsVisitCapacity, SlipDraw3DPrimitivePathExecute *result);

int SlipDraw3D_CollectClipFlags(const uint8_t *drawRecordBase, size_t drawRecordBytes, uint32_t headOffset,
                                SlipDraw3DClipFlagVisit *visits, size_t visitCapacity, SlipDraw3DClipFlags *result);

int SlipDraw3D_ClipDispatch(uint32_t anyFlags, uint32_t allFlags, int depthClipRejected, int screenClipRejected,
                            SlipDraw3DClipDispatch *result);

int SlipDraw3D_ClippedDepthDispatch(const SlipDraw3DClippedDepthInputs *inputs, SlipDraw3DClippedDepthDispatch *result);

int SlipDraw3D_ClippedDepthExecute(uint8_t *recordBase, size_t recordBytes, uint32_t inputActiveHeadOffset,
                                   uint32_t freeHeadOffset, uint32_t allFlagsEntry, uint32_t anyFlagsEntry,
                                   uint32_t renderFlags, uint32_t projectionMode, int32_t limitZMin, int32_t limitZMax,
                                   int32_t limitXMin, int32_t limitXMax, int32_t limitYMin, int32_t limitYMax,
                                   SlipDraw3DProjectFn projectPrimary, SlipDraw3DProjectFn projectSecondary,
                                   void *userData, size_t maxClipEdgeVisits, SlipDraw3DClipFlagVisit *clipFlagVisits,
                                   size_t clipFlagVisitCapacity, SlipDraw3DClippedDepthExecute *result);

int SlipDraw3D_ScreenPlaneDispatch(const SlipDraw3DScreenPlaneInputs *inputs, SlipDraw3DScreenPlaneDispatch *result);

int SlipDraw3D_ClipEdgeList(uint8_t *recordBase, size_t recordBytes, uint32_t headOffset, uint32_t freeHeadOffset,
                            uint32_t clipMask, size_t maxVisits, SlipDraw3DClipEdge *result);

int SlipDraw3D_LineClipExecute(uint8_t *recordBase, size_t recordBytes, uint32_t inputActiveHeadOffset,
                               uint32_t inputAnyClipFlags, uint32_t renderFlags, uint32_t projectionMode,
                               int32_t limitZMin, int32_t limitZMax, int32_t limitXMin, int32_t limitXMax,
                               int32_t limitYMin, int32_t limitYMax, SlipDraw3DProjectFn projectPrimary,
                               SlipDraw3DProjectFn projectSecondary, void *userData, SlipDraw3DLineClipExecute *result);

int SlipDraw3D_ProjectFlags(uint8_t *recordBase, size_t recordBytes, uint32_t recordOffset, uint32_t renderFlags,
                            int32_t projectedScreenX, int32_t projectedScreenY, int32_t limitXMin, int32_t limitXMax,
                            int32_t limitYMin, int32_t limitYMax, SlipDraw3DProjectFlags *result);

int SlipDraw3D_ProjectFlagsWithCallback(uint8_t *recordBase, size_t recordBytes, uint32_t recordOffset,
                                        uint32_t renderFlags, SlipDraw3DProjectFn projectPrimary,
                                        SlipDraw3DProjectFn projectSecondary, void *userData, int32_t limitXMin,
                                        int32_t limitXMax, int32_t limitYMin, int32_t limitYMax,
                                        SlipDraw3DProjectFlags *result);

int SlipDraw3D_ScreenPointFlags(SlipDraw3DDrawRecord *record, int32_t screenX, int32_t screenY, int32_t minX,
                                int32_t maxX, int32_t minY, int32_t maxY, SlipDraw3DScreenPointFlags *result);

int SlipDraw3D_SplitDepthRecord(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset, uint32_t otherOffset,
                                uint32_t renderFlags, uint32_t projectionMode, int32_t clipPlaneZ,
                                int32_t projectedScreenX, int32_t projectedScreenY, int32_t limitXMin,
                                int32_t limitXMax, int32_t limitYMin, int32_t limitYMax, SlipDraw3DSplitDepth *result);

int SlipDraw3D_SplitDepthRecordWithCallback(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset,
                                            uint32_t otherOffset, uint32_t renderFlags, uint32_t projectionMode,
                                            int32_t clipPlaneZ, SlipDraw3DProjectFn projectPrimary,
                                            SlipDraw3DProjectFn projectSecondary, void *userData, int32_t limitXMin,
                                            int32_t limitXMax, int32_t limitYMin, int32_t limitYMax,
                                            SlipDraw3DSplitDepth *result);

int SlipDraw3D_SplitDepthMathRecord(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset,
                                    uint32_t otherOffset, uint32_t renderFlags, uint32_t projectionMode,
                                    int32_t projectedScreenX, int32_t projectedScreenY, int32_t limitZMin,
                                    int32_t limitZMax, int32_t limitXMin, int32_t limitXMax, int32_t limitYMin,
                                    int32_t limitYMax, SlipDraw3DSplitDepthMath *result);

int SlipDraw3D_SplitDepthMathRecordWithCallback(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset,
                                                uint32_t otherOffset, uint32_t renderFlags, uint32_t projectionMode,
                                                SlipDraw3DProjectFn projectPrimary,
                                                SlipDraw3DProjectFn projectSecondary, void *userData, int32_t limitZMin,
                                                int32_t limitZMax, int32_t limitXMin, int32_t limitXMax,
                                                int32_t limitYMin, int32_t limitYMax, SlipDraw3DSplitDepthMath *result);

int SlipDraw3D_InterpolateExtraFields16(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset,
                                        uint32_t otherOffset, uint32_t projectionMode, uint16_t textureRatio,
                                        SlipDraw3DExtraFieldInterpolation *result);

int SlipDraw3D_InterpolateExtraFields32(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset,
                                        uint32_t otherOffset, uint32_t projectionMode, uint32_t interpolationRatio,
                                        SlipDraw3DExtraFieldInterpolation *result);

int SlipDraw3D_InterpolateDepthAndExtra16(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset,
                                          uint32_t otherOffset, uint32_t projectionMode, uint32_t interpolationRatio,
                                          SlipDraw3DDepthExtraInterpolation *result);

int SlipDraw3D_InterpolateDepthAndExtra32(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset,
                                          uint32_t otherOffset, uint32_t projectionMode, uint32_t interpolationRatio,
                                          SlipDraw3DDepthExtraInterpolation *result);

int SlipDraw3D_SplitScreenXRecord(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset, uint32_t otherOffset,
                                  uint32_t projectionMode, int32_t clipPlaneX, int32_t limitYMin, int32_t limitYMax,
                                  SlipDraw3DSplitScreenX *result);

int SlipDraw3D_SplitScreenYRecord(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset, uint32_t otherOffset,
                                  uint32_t projectionMode, int32_t clipPlaneY, SlipDraw3DSplitScreenY *result);

int SlipDraw3D_PostPlaneBounds(const uint8_t *recordBase, size_t recordBytes, uint32_t headOffset, int32_t limitXMin,
                               int32_t limitXMax, int32_t limitYMin, int32_t limitYMax,
                               SlipDraw3DPostPlaneBoundsVisit *visits, size_t visitCapacity,
                               SlipDraw3DPostPlaneBounds *result);

int SlipDraw3D_SplitPostPlaneRecord(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset,
                                    uint32_t otherOffset, SlipDraw3DSplitPostPlane *result);

int SlipDraw3D_PostPlaneClip(uint8_t *recordBase, size_t recordBytes, uint32_t inputActiveHeadOffset,
                             uint32_t freeHeadOffset, const uint8_t *planeBase, size_t planeBytes,
                             uint32_t planeHeadOffset, size_t maxRecordVisitsPerPlane, size_t maxClipEdgeVisits,
                             SlipDraw3DPostPlaneClipRecordVisit *recordVisits, size_t recordVisitCapacity,
                             SlipDraw3DPostPlaneClipPlaneVisit *planeVisits, size_t planeVisitCapacity,
                             SlipDraw3DPostPlaneClip *result);

int SlipDraw3D_ScreenPlaneExecute(uint8_t *recordBase, size_t recordBytes, uint32_t inputActiveHeadOffset,
                                  uint32_t freeHeadOffset, uint32_t anyFlagsEntry, uint32_t projectionMode,
                                  int32_t limitXMin, int32_t limitXMax, int32_t limitYMin, int32_t limitYMax,
                                  int hasPostPlanes, const uint8_t *planeBase, size_t planeBytes,
                                  uint32_t planeHeadOffset, int32_t postLimitXMin, int32_t postLimitXMax,
                                  int32_t postLimitYMin, int32_t postLimitYMax, size_t maxClipEdgeVisits,
                                  SlipDraw3DClipFlagVisit *clipFlagVisits, size_t clipFlagVisitCapacity,
                                  SlipDraw3DPostPlaneBoundsVisit *postBoundsVisits, size_t postBoundsVisitCapacity,
                                  SlipDraw3DPostPlaneClipRecordVisit *postClipRecordVisits,
                                  size_t postClipRecordVisitCapacity,
                                  SlipDraw3DPostPlaneClipPlaneVisit *postClipPlaneVisits,
                                  size_t postClipPlaneVisitCapacity, SlipDraw3DScreenPlaneExecute *result);

int SlipDraw3D_ClipDispatchExecute(
    uint8_t *recordBase, size_t recordBytes, uint32_t inputActiveHeadOffset, uint32_t freeHeadOffset, uint32_t anyFlags,
    uint32_t allFlags, uint32_t renderFlags, uint32_t projectionMode, int32_t limitZMin, int32_t limitZMax,
    int32_t limitXMin, int32_t limitXMax, int32_t limitYMin, int32_t limitYMax, SlipDraw3DProjectFn projectPrimary,
    SlipDraw3DProjectFn projectSecondary, void *userData, int hasPostPlanes, const uint8_t *planeBase,
    size_t planeBytes, uint32_t planeHeadOffset, int32_t postLimitXMin, int32_t postLimitXMax, int32_t postLimitYMin,
    int32_t postLimitYMax, size_t maxClipEdgeVisits, SlipDraw3DClipFlagVisit *clipFlagVisits,
    size_t clipFlagVisitCapacity, SlipDraw3DPostPlaneBoundsVisit *postBoundsVisits, size_t postBoundsVisitCapacity,
    SlipDraw3DPostPlaneClipRecordVisit *postClipRecordVisits, size_t postClipRecordVisitCapacity,
    SlipDraw3DPostPlaneClipPlaneVisit *postClipPlaneVisits, size_t postClipPlaneVisitCapacity,
    SlipDraw3DClipDispatchExecute *result);

int SlipDraw3D_LoadStateRecord(const SlipDraw3DStateRecord *recordBase, size_t recordCount, uint16_t recordIndex,
                               SlipDraw3DStateLoad *result);

int SlipDraw3D_SetOrigin(SlipDraw3DStateRecord *drawStateRecord, const SlipView3DMatrix *destinationMatrix,
                         const SlipView3DMatrix *originMatrix, SlipDraw3DVec32 cameraOrigin,
                         SlipDraw3DVec32 drawPosition, bool lightEnabled, SlipDraw3DVec32 lightInput,
                         SlipDraw3DOriginSetup *result);

int SlipDraw3D_RestoreVertexBufferCursor(uint32_t vertexBufferCursor, const SlipDraw3DStateRecord *drawStateRecord,
                                         SlipDraw3DRestoreVertexBufferCursor *result);

uint32_t SlipDraw3D_CurrentStateRecordIndex(uint32_t recordIndex);

int SlipDraw3D_BackgroundSetup(const uint8_t *materialTable, size_t materialTableBytes,
                               const SlipView3DMatrix *viewMatrix, uint32_t backgroundDistance, uint32_t backgroundSpan,
                               uint16_t backgroundMaterialIndex, uint8_t materialStripCount, uint8_t fixedStripCount,
                               uint16_t stripMaterialIndex, uint16_t stripCurvature, uint32_t projectionScale,
                               uint32_t cachedProjectionScale, uint16_t projectionRevision,
                               uint8_t cachedFixedStripCount, uint8_t cachedEndValueByte, uint16_t cachedStripCurvature,
                               uint16_t materialFillValue, SlipDraw3DBackgroundSetup *result);

int SlipDraw3D_BackgroundStripBuild(const SlipView3DMaths *maths, uint8_t *fixedStripTable, size_t stripTableBytes,
                                    uint8_t fixedStripCount, uint8_t startValueByte, uint8_t endValueByte,
                                    uint16_t stripCurvature, uint32_t scale, SlipDraw3DBackgroundStripBuild *result);

int SlipDraw3D_BackgroundSetupExecute(
    const SlipView3DMaths *maths, uint8_t *materialStripTable, size_t materialStripTableBytes, uint8_t *fixedStripTable,
    size_t fixedStripTableBytes, const uint8_t *materialTable, size_t materialTableBytes,
    const SlipView3DMatrix *viewMatrix, uint32_t backgroundDistance, uint32_t backgroundSpan,
    uint16_t backgroundMaterialIndex, uint8_t materialStripCount, uint8_t fixedStripCount, uint16_t stripMaterialIndex,
    uint16_t stripCurvature, uint32_t projectionScale, uint32_t cachedProjectionScale, uint16_t projectionRevision,
    uint8_t cachedFixedStripCount, uint8_t cachedEndValueByte, uint16_t cachedStripCurvature, uint32_t viewportX,
    uint32_t viewportY, uint32_t detailScale, const SlipDraw3DStateRecord *stateRecord, uint32_t fadeStart,
    uint32_t fadeEnd, uint32_t fadeRange, uint32_t ambientLight, uint32_t fadeColour, uint32_t limitEnabled,
    uint32_t limitStart, uint32_t limitEnd, SlipDraw3DBackgroundStripTableVisitFixed *fixedStripVisits,
    size_t fixedStripVisitCapacity, SlipDraw3DBackgroundStripTableVisitMaterial *materialStripVisits,
    size_t materialStripVisitCapacity, SlipDraw3DBackgroundSetupExecute *result);

int SlipDraw3D_BackgroundPassMaterial(uint16_t materialFillFlag, uint16_t fixedFillFlag, uint16_t materialFillTag,
                                      SlipDraw3DBackgroundPassMaterial *result);

int SlipDraw3D_BackgroundPassFixed(uint16_t materialFillFlag, uint16_t fixedFillFlag, uint16_t fixedFillTag,
                                   SlipDraw3DBackgroundPassFixed *result);

int SlipDraw3D_BackgroundPassExecuteMaterial(
    SlipDraw3DRecordPool *pool, uint16_t materialFillFlag, uint16_t fixedFillFlag, uint16_t materialFillTag,
    const uint8_t *materialStripTable, size_t stripTableBytes, uint32_t renderFlags, uint32_t initialMaterialColor,
    int32_t minX, int32_t maxX, int32_t minY, int32_t maxY, int depthClipRejected, int screenClipRejected,
    SlipDraw3DReturnActiveVisit *returnVisits, size_t returnVisitCapacity,
    SlipDraw3DPointPointerRingVisit *pointRingVisits, size_t pointRingVisitCapacity,
    SlipDraw3DStripDispatchVisit *stripVisits, size_t stripVisitCapacity,
    SlipDraw3DBackgroundPassExecuteMaterial *result);

int SlipDraw3D_BackgroundPassExecuteFixed(SlipDraw3DRecordPool *pool, uint16_t materialFillFlag, uint16_t fixedFillFlag,
                                          uint16_t fixedFillTag, const uint8_t *fixedStripTable, size_t stripTableBytes,
                                          uint32_t renderFlags, uint32_t initialMaterialColor, int32_t minX,
                                          int32_t maxX, int32_t minY, int32_t maxY, int depthClipRejected,
                                          int screenClipRejected, SlipDraw3DReturnActiveVisit *returnVisits,
                                          size_t returnVisitCapacity, SlipDraw3DPointPointerRingVisit *pointRingVisits,
                                          size_t pointRingVisitCapacity, SlipDraw3DStripDispatchVisit *stripVisits,
                                          size_t stripVisitCapacity, SlipDraw3DBackgroundPassExecuteFixed *result);

int SlipDraw3D_BackgroundDispatch(uint16_t materialFillFlag, uint16_t fixedFillFlag, uint16_t materialFillTag,
                                  uint16_t fixedFillTag, SlipDraw3DBackgroundDispatch *result);

int SlipDraw3D_BackgroundCenter(uint32_t horizontalOffset, uint16_t tiltComponent, uint16_t complementComponent,
                                uint32_t backgroundDistance, uint32_t scale, uint32_t viewportX, uint32_t viewportY,
                                uint16_t scaleX, uint16_t scaleY, SlipDraw3DBackgroundCenter *result);

int SlipDraw3D_BackgroundMaterial(const uint8_t *materialTable, size_t materialTableBytes, uint16_t materialIndex,
                                  uint32_t detailScale, const SlipDraw3DStateRecord *stateRecord,
                                  SlipDraw3DBackgroundMaterial *result);

int SlipDraw3D_BackgroundValue(const uint8_t *materialRecord, size_t materialRecordBytes, uint16_t depthBlend,
                               uint16_t lightValue, uint32_t limitEnabled, uint32_t limitStart, uint32_t limitEnd,
                               SlipDraw3DBackgroundValue *result);

int SlipDraw3D_BackgroundValueExecute(const uint8_t *materialRecord, size_t materialRecordBytes, uint32_t depth,
                                      uint16_t lightValue, uint32_t fadeStart, uint32_t fadeEnd, uint32_t fadeRange,
                                      uint32_t directLight, uint32_t ambientLight, uint32_t fadeColour,
                                      uint32_t limitEnabled, uint32_t limitStart, uint32_t limitEnd,
                                      SlipDraw3DBackgroundValueExecute *result);

int SlipDraw3D_BackgroundStripEntry(uint8_t *stripRecord, size_t stripRecordBytes, uint16_t centerOffset,
                                    uint16_t centerX, uint16_t centerY, uint16_t scaleX, uint16_t scaleY,
                                    uint16_t halfWidth, uint16_t halfHeight, SlipDraw3DBackgroundStripEntry *result);

int SlipDraw3D_BackgroundStripTableFixed(uint8_t *fixedStripTable, size_t stripTableBytes, uint32_t backgroundSpan,
                                         uint16_t tiltComponent, uint16_t complementComponent,
                                         uint32_t backgroundDistance, uint32_t scale, uint32_t viewportX,
                                         uint32_t viewportY, uint16_t scaleX, uint16_t scaleY, uint16_t halfWidth,
                                         uint16_t halfHeight, SlipDraw3DBackgroundStripTableVisitFixed *visits,
                                         size_t visitCapacity, SlipDraw3DBackgroundStripTableFixed *result);

int SlipDraw3D_BackgroundStripTableMaterial(
    uint8_t *materialStripTable, size_t stripTableBytes, const uint8_t *materialTable, size_t materialTableBytes,
    uint16_t materialIndex, uint32_t detailScale, const SlipDraw3DStateRecord *stateRecord, uint16_t count,
    uint32_t fadeStart, uint32_t fadeEnd, uint32_t fadeRange, uint32_t ambientLight, uint32_t fadeColour,
    uint32_t backgroundSpan, uint32_t backgroundDistance, uint16_t tiltComponent, uint16_t complementComponent,
    uint32_t scale, uint32_t viewportX, uint32_t viewportY, uint16_t scaleX, uint16_t scaleY, uint16_t halfWidth,
    uint16_t halfHeight, uint32_t limitEnabled, uint32_t limitStart, uint32_t limitEnd,
    SlipDraw3DBackgroundStripTableVisitMaterial *visits, size_t visitCapacity,
    SlipDraw3DBackgroundStripTableMaterial *result);

int SlipDraw3D_BackgroundDispatchExecute(SlipDraw3DRecordPool *pool, uint16_t materialFillFlag, uint16_t fixedFillFlag,
                                         uint16_t materialFillTag, uint16_t fixedFillTag,
                                         const uint8_t *materialStripTable, size_t materialStripTableBytes,
                                         const uint8_t *fixedStripTable, size_t fixedStripTableBytes,
                                         uint32_t renderFlags, uint32_t initialMaterialColor, int32_t minX,
                                         int32_t maxX, int32_t minY, int32_t maxY, int depthClipRejected,
                                         int screenClipRejected, SlipDraw3DReturnActiveVisit *returnVisits,
                                         size_t returnVisitCapacity, SlipDraw3DPointPointerRingVisit *pointRingVisits,
                                         size_t pointRingVisitCapacity, SlipDraw3DStripDispatchVisit *stripVisits,
                                         size_t stripVisitCapacity, SlipDraw3DBackgroundDispatchExecute *result);

int SlipDraw3D_BackgroundDispatchSetupExecute(
    const SlipView3DMaths *maths, SlipDraw3DRecordPool *pool, uint8_t *materialStripTable,
    size_t materialStripTableBytes, uint8_t *fixedStripTable, size_t fixedStripTableBytes, const uint8_t *materialTable,
    size_t materialTableBytes, const SlipView3DMatrix *viewMatrix, uint32_t backgroundDistance, uint32_t backgroundSpan,
    uint16_t backgroundMaterialIndex, uint8_t materialStripCount, uint8_t fixedStripCount, uint16_t stripMaterialIndex,
    uint16_t stripCurvature, uint32_t projectionScale, uint32_t cachedProjectionScale, uint16_t projectionRevision,
    uint8_t cachedFixedStripCount, uint8_t cachedEndValueByte, uint16_t cachedStripCurvature, uint32_t viewportX,
    uint32_t viewportY, uint32_t detailScale, const SlipDraw3DStateRecord *stateRecord, uint32_t fadeStart,
    uint32_t fadeEnd, uint32_t fadeRange, uint32_t ambientLight, uint32_t fadeColour, uint32_t limitEnabled,
    uint32_t limitStart, uint32_t limitEnd, uint32_t renderFlags, uint32_t initialMaterialColor, int32_t minX,
    int32_t maxX, int32_t minY, int32_t maxY, int depthClipRejected, int screenClipRejected,
    SlipDraw3DReturnActiveVisit *returnVisits, size_t returnVisitCapacity,
    SlipDraw3DPointPointerRingVisit *pointRingVisits, size_t pointRingVisitCapacity,
    SlipDraw3DStripDispatchVisit *stripVisits, size_t stripVisitCapacity,
    SlipDraw3DBackgroundStripTableVisitFixed *fixedStripVisits, size_t fixedStripVisitCapacity,
    SlipDraw3DBackgroundStripTableVisitMaterial *materialStripVisits, size_t materialStripVisitCapacity,
    SlipDraw3DBackgroundDispatchSetupExecute *result);

int SlipDraw3D_StripDispatch(const uint8_t *stripTable, size_t stripTableBytes, uint32_t stripTableBaseAddress,
                             uint32_t renderFlags, const bool *carryFromByVisit, size_t carryCount,
                             SlipDraw3DStripDispatchVisit *visits, size_t visitCapacity,
                             SlipDraw3DStripDispatch *result);

int SlipDraw3D_StripDispatchWithPointPointerRing(
    SlipDraw3DRecordPool *pool, const uint8_t *stripTable, size_t stripTableBytes, uint32_t stripTableBaseAddress,
    uint32_t renderFlags, uint32_t initialMaterialColor, int32_t minX, int32_t maxX, int32_t minY, int32_t maxY,
    int depthClipRejected, int screenClipRejected, SlipDraw3DReturnActiveVisit *returnVisits,
    size_t returnVisitCapacity, SlipDraw3DPointPointerRingVisit *pointRingVisits, size_t pointRingVisitCapacity,
    SlipDraw3DStripDispatchVisit *visits, size_t visitCapacity, SlipDraw3DStripDispatch *result);

#endif
