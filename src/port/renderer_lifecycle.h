#ifndef SLIPSTREAM5000_RENDERER_LIFECYCLE_H
#define SLIPSTREAM5000_RENDERER_LIFECYCLE_H
#include "draw3d.h"
#include "material_residency.h"
#include "raster/raster.h"
#include "renderer_flags.h"
#include "resource.h"
#include "view3d.h"

/* DOS allocation sizes are independent of native pointers and structure padding. */
enum {
	SLIP_RENDERER_DRAW_STATE_DOS_BYTES = 56,
	SLIP_RENDERER_DRAW_STATE_CAPACITY = 32,
	SLIP_RENDERER_RESERVED_WORKSPACE_BYTES = 0x380,
	SLIP_RENDERER_POINT_BUFFER_BYTES = 0x402
};

typedef struct SlipRendererPolygonPoint {
	SlipDraw3DVec32 world;
	int32_t screenX, screenY;
	uint32_t flags;
	int32_t depth;
	int32_t screenPlaneDistance;
	uint16_t shade, reservedShadePadding;
	uint32_t textureU, textureV;
	int32_t planeNormalX, planeNormalY;
} SlipRendererPolygonPoint;

typedef char SlipRendererPolygonPointSizeCheck[sizeof(SlipRendererPolygonPoint) == 52 ? 1 : -1];

typedef struct SlipRendererPolygon {
	SlipRendererPolygonPoint point;
	struct SlipRendererPolygon *next, *previous; /* +34,+38 */
} SlipRendererPolygon;

typedef struct SlipRendererDrawState {
	SlipDraw3DVertexRecord *vertices; /* +00 */
	SlipDraw3DTransformFn transform;  /* +04 */
	SlipDraw3DSourcePointFn source;   /* +08 */
	SlipDraw3DVec32 origin, light;    /* +0c,+18 */
	SlipView3DMatrix matrix;          /* +24 */
} SlipRendererDrawState;

typedef struct SlipRendererState {
	SlipDraw3DProjectState projection;
	int32_t boundingBox[SLIP_VIEW_BOX_BOUNDS_AND_CORNERS_COUNT];
	uint32_t boxAnyClipMask, boxAllClipMask;
	uint32_t interpolationFlags;
	uint32_t polygonDrawMode;
	uint32_t polygonTexture;
	uint32_t reverseRasterTraversal;
	uint32_t polygonColour, polygonColourAuxiliary;
	uint32_t polygonAllClipMask, polygonAnyClipMask;
	SlipDraw3DMaterialRecord *activeMaterial;
	SlipRendererPolygon *borrowedClipPoint;
	SlipRendererPolygon *postClipPlanes;
	int32_t postClipMinX, postClipMinY, postClipMaxX, postClipMaxY;
	uint16_t shapeFlags;
	uint16_t polygonCount;
	SlipRendererPolygon *freePolygons, *activePolygons;
	uint32_t initialized;
	SlipMaterialResidency *materials;
	uint16_t backgroundColour;
	uint16_t vertexResource, polygonResource;
	uint32_t vertexCapacity;
	SlipDraw3DVertexRecord *vertexBase, *vertexCursor;
	SlipDraw3DVertexRecord *vertexLimit;
	SlipRendererDrawState *states;
	SlipRendererDrawState *currentState;
	uint32_t stateIndex;
	SlipDraw3DVertexRecord *activeVertices;
	SlipDraw3DTransformFn activeTransform;
	SlipDraw3DSourcePointFn activeSource;
	SlipDraw3DVec32 origin;
	SlipView3DVec32 camera;
	SlipView3DVec32 cameraLight;
	uint16_t stateResource, stateCount;
	uint16_t reservedResource;

	SlipResourceBlock *reservedWorkspace;
	uint16_t pointResource;
	RasterTexturedPoint *points;
	uint32_t specularThreshold;
	uint16_t *specularTable;
	uint16_t specularResource;
	uint32_t overrideRamp, rampStart, rampEnd;
} SlipRendererState;

typedef struct SlipRendererLifecycleCalls SlipRendererLifecycleCalls;
typedef void (*SlipRendererCleanup)(SlipRendererState *, const SlipRendererLifecycleCalls *);

struct SlipRendererLifecycleCalls {
	void *context;
	void (*initializeVertices)(void *, uint32_t capacity);
	bool (*initializePolygons)(void *);
	void (*initializeSpecular)(void *);
	bool (*allocate)(void *, uint32_t bytes, uint32_t flags, uint16_t *resource);
	SlipRendererDrawState *(*lockStates)(void *, uint16_t resource);
	SlipResourceBlock *(*lockReserved)(void *, uint16_t resource);
	RasterTexturedPoint *(*lockPoints)(void *, uint16_t resource);
	void (*initializeProjection)(void *);
	void (*advanceBackground)(void *);
	void (*resetDrawState)(void *);
	void (*resetLighting)(void *);
	void (*registerExit)(void *, SlipRendererCleanup);
	void (*freeSpecular)(void *);
	void (*unlock)(void *, uint16_t resource);
	void (*release)(void *, uint16_t resource);
	void (*freeMaterials)(void *);
};

void SlipRenderer_Initialize(SlipRendererState *, uint16_t vertices, const SlipRendererLifecycleCalls *);
void SlipRenderer_Shutdown(SlipRendererState *, const SlipRendererLifecycleCalls *);
void SlipRenderer_Begin(SlipRendererState *, const SlipRendererLifecycleCalls *);
#endif
