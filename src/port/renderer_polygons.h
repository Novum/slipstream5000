#ifndef SLIPSTREAM5000_RENDERER_POLYGONS_H
#define SLIPSTREAM5000_RENDERER_POLYGONS_H
#include "renderer_lifecycle.h"

typedef struct SlipRendererPolygonMasks {
	uint32_t all, any;
} SlipRendererPolygonMasks;

typedef struct SlipRendererClipEdges {
	SlipRendererPolygon *firstTarget, *firstInside;
	SlipRendererPolygon *secondTarget, *secondInside;
} SlipRendererClipEdges;

/* Requires both inside and outside vertices. Outputs are valid on carry clear. */
bool SlipRenderer_PrepareClipEdges(SlipRendererState *, uint32_t planeMask, SlipRendererClipEdges *);

typedef struct SlipRendererDepthClipCalls {
	void *context;
	bool (*edges)(void *, uint32_t planeMask, SlipRendererClipEdges *);
	void (*auxiliary)(void *, SlipRendererPolygon *target, SlipRendererPolygon *inside);
	void (*depth)(void *, SlipRendererPolygon *target, SlipRendererPolygon *inside, int32_t limit);
} SlipRendererDepthClipCalls;

/* True means carry set. The output mask is consumed only on carry clear. */
bool SlipRenderer_ClipDepth(SlipRendererState *, uint32_t allClipMask, uint32_t *anyClipMask,
                            const SlipRendererDepthClipCalls *);

typedef struct SlipRendererScreenClipCalls {
	void *context;
	bool (*edges)(void *, uint32_t planeMask, SlipRendererClipEdges *);
	void (*horizontal)(void *, SlipRendererPolygon *target, SlipRendererPolygon *inside, int32_t limit);
	void (*vertical)(void *, SlipRendererPolygon *target, SlipRendererPolygon *inside, int32_t limit);
	bool (*postPlanes)(void *);
} SlipRendererScreenClipCalls;

bool SlipRenderer_RejectPostBounds(const SlipRendererState *);
bool SlipRenderer_ClipScreen(SlipRendererState *, uint32_t anyClipMask, const SlipRendererScreenClipCalls *);
void SlipRenderer_ReturnActivePolygons(SlipRendererState *);
SlipRendererPolygonMasks SlipRenderer_ActivePolygonMasks(const SlipRendererState *);

typedef struct SlipRendererSolidRingCalls {
	void *context;
	uint32_t (*projectVertex)(void *, SlipDraw3DVertexRecord *);
	bool (*clip)(void *, uint32_t allClipMask, uint32_t *anyClipMask);
	bool (*finish)(void *, uint32_t anyClipMask);
	uint32_t (*shade)(void *, SlipDraw3DMaterialRecord *, SlipDraw3DVertexRecord *, int16_t normalX, int16_t normalY,
	                  int16_t normalZ);
} SlipRendererSolidRingCalls;

bool SlipRenderer_BuildSolidRing(SlipRendererState *, SlipRendererPolygon *first, const uint8_t *serializedIndices,
                                 uint32_t count, const SlipRendererSolidRingCalls *);

bool SlipRenderer_BuildShadedRing(SlipRendererState *, SlipRendererPolygon *first, const uint8_t *serializedIndices,
                                  const uint8_t *serializedNormals, uint32_t count, const SlipRendererSolidRingCalls *);

typedef struct SlipRendererTexturedRingCalls {
	SlipRendererSolidRingCalls ring;

	bool (*reject)(void *, uint16_t countAndFlags, const uint8_t *serializedIndices);
} SlipRendererTexturedRingCalls;

bool SlipRenderer_BuildTexturedRing(SlipRendererState *, uint16_t countAndFlags, uint32_t texture,
                                    const uint8_t *serializedIndices, const SlipRendererTexturedRingCalls *);

typedef struct SlipRendererSubmitCalls {
	void *context;
	bool (*buildSolid)(void *, uint32_t countAndFlags, int16_t normalX, int16_t normalY, int16_t normalZ,
	                   uint16_t material, const uint8_t *serializedStream);
	void (*draw)(void *);
	void (*drawUnclipped)(void *, uint16_t countAndFlags, int16_t normalX, int16_t normalY, int16_t normalZ,
	                      uint16_t material, const uint8_t *serializedStream);
} SlipRendererSubmitCalls;

/* True is the original carry-set return. */
bool SlipRenderer_SubmitSolid(SlipRendererState *, uint32_t countAndFlags, int16_t normalX, int16_t normalY,
                              int16_t normalZ, uint16_t material, const uint8_t *serializedStream,
                              const SlipRendererSubmitCalls *);
bool SlipRenderer_ClipPostPlanes(SlipRendererState *);

typedef struct SlipRendererTexturedSubmitCalls {
	void *context;
	void (*selectRaster)(void *, int16_t normalX, int16_t normalY, int16_t normalZ);
	bool (*buildTextured)(void *, uint32_t countAndFlags, int16_t normalX, int16_t normalY, int16_t normalZ,
	                      uint32_t texture, const uint8_t *serializedStream);
	bool (*buildSolid)(void *, uint32_t countAndFlags, int16_t normalX, int16_t normalY, int16_t normalZ,
	                   uint16_t material, const uint8_t *serializedStream);
	void (*draw)(void *);
} SlipRendererTexturedSubmitCalls;

void SlipRenderer_SelectTextureRaster(SlipRendererState *, int16_t normalX, int16_t normalY, int16_t normalZ);

bool SlipRenderer_SubmitTextured(SlipRendererState *, uint32_t countAndFlags, int16_t normalX, int16_t normalY,
                                 int16_t normalZ, uint16_t material, const uint8_t *serializedStream,
                                 const SlipRendererTexturedSubmitCalls *);

typedef struct SlipRendererPolygonColour {
	uint32_t colour, shade;
} SlipRendererPolygonColour;

typedef struct SlipRendererSolidBuilderCalls {
	SlipRendererSolidRingCalls ring;
	SlipRendererPolygonColour (*colour)(void *, SlipDraw3DMaterialRecord *, int16_t normalX, int16_t normalY,
	                                    uint32_t vertexCount, uint32_t countAndFlags, const uint8_t *serializedIndices);
} SlipRendererSolidBuilderCalls;

bool SlipRenderer_BuildSolidPolygon(SlipRendererState *, uint32_t countAndFlags, int16_t normalX, int16_t normalY,
                                    uint16_t materialIndex, const uint8_t *serializedIndices,
                                    const SlipRendererSolidBuilderCalls *);
bool SlipRenderer_BuildMaterialPolygon(SlipRendererState *, SlipDraw3DMaterialRecord *, uint32_t countAndFlags,
                                       int16_t normalX, int16_t normalY, const uint8_t *serializedIndices,
                                       const SlipRendererSolidBuilderCalls *);

typedef struct SlipRendererFlatColourCalls {
	void *context;
	uint32_t (*polygonDepth)(void *, uint16_t countAndFlags, const uint8_t *serializedIndices);
	uint32_t (*depthFadeBlend)(void *, uint32_t depth);
	uint32_t (*lighting)(void *, SlipDraw3DMaterialRecord *, uint32_t blend, uint16_t diffuse, uint16_t specular);
} SlipRendererFlatColourCalls;

SlipRendererPolygonColour SlipRenderer_FlatColour(SlipDraw3DMaterialRecord *, int16_t normalX, int16_t normalY,
                                                  int16_t normalZ, uint32_t countAndFlags,
                                                  const uint8_t *serializedIndices, const SlipDraw3DVertexLighting *,
                                                  const SlipRendererFlatColourCalls *);

typedef struct SlipRendererRasterCalls {
	void *context;
	void (*line)(void *, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t colour);
	void (*rectangle)(void *, uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY, uint32_t colour);
	void (*flat)(void *, RasterTexturedPoint *, uint32_t count, uint32_t colour);
	void (*dithered)(void *, RasterTexturedPoint *, uint32_t count, uint32_t colour, uint32_t dither);
	void (*shaded)(void *, RasterTexturedPoint *, uint32_t count);
	void (*opaqueAffine)(void *, RasterTexturedPoint *, uint32_t count, uint32_t texture);
	void (*maskedAffine)(void *, RasterTexturedPoint *, uint32_t count, uint32_t texture);
	void (*opaquePerspective)(void *, RasterTexturedPoint *, uint32_t count, uint32_t texture);
	void (*maskedPerspective)(void *, RasterTexturedPoint *, uint32_t count, uint32_t texture);
} SlipRendererRasterCalls;

void SlipRenderer_DrawPolygon(SlipRendererState *, const SlipRendererRasterCalls *);

typedef struct SlipRendererUnclippedCalls {
	void *context;
	RasterPoint (*project)(void *, SlipDraw3DVertexRecord *);
	uint32_t (*shade)(void *, SlipDraw3DMaterialRecord *, SlipDraw3DVertexRecord *, int16_t, int16_t, int16_t);
	SlipRendererPolygonColour (*colour)(void *, SlipDraw3DMaterialRecord *, int16_t, int16_t, uint32_t vertexCount,
	                                    uint32_t countAndFlags, const uint8_t *);
	SlipRendererRasterCalls raster;
} SlipRendererUnclippedCalls;

void SlipRenderer_DrawUnclipped(SlipRendererState *, uint32_t countAndFlags, int16_t normalX, int16_t normalY,
                                uint16_t materialIndex, const uint8_t *serializedIndices,
                                const SlipRendererUnclippedCalls *);

void SlipRenderer_DrawUnclippedMaterial(SlipRendererState *, SlipDraw3DMaterialRecord *, uint32_t countAndFlags,
                                        int16_t normalX, int16_t normalY, const uint8_t *serializedIndices,
                                        const SlipRendererUnclippedCalls *);

typedef struct SlipRendererLineCalls {
	void *context;
	uint32_t (*project)(void *, SlipRendererPolygonPoint *, SlipDraw3DVec32);
	bool (*clip)(void *, uint32_t allClipMask, uint32_t anyClipMask);
} SlipRendererLineCalls;

bool SlipRenderer_BuildLine(SlipRendererState *, SlipDraw3DVec32 first, SlipDraw3DVec32 second, uint32_t colour,
                            const SlipRendererLineCalls *);

typedef struct SlipRendererLineEdge {
	SlipRendererPolygon *target, *inside;
} SlipRendererLineEdge;

SlipRendererLineEdge SlipRenderer_LineEdge(SlipRendererState *, uint16_t mask);
bool SlipRenderer_ClipLine(SlipRendererState *, uint32_t anyClipMask);
uint32_t SlipRenderer_ProjectPoint(SlipRendererState *, SlipRendererPolygonPoint *, SlipDraw3DVec32);
bool SlipRenderer_DrawLine(SlipRendererState *, SlipDraw3DVec32 first, SlipDraw3DVec32 second, uint32_t colour,
                           const SlipRendererLineCalls *, const SlipRendererRasterCalls *);
#endif
