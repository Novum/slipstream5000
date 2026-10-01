#ifndef SLIPSTREAM5000_SHAPE_DISPATCH_H
#define SLIPSTREAM5000_SHAPE_DISPATCH_H
#include <stdint.h>

typedef struct SlipShapeDispatchCalls {
	void *context;
	int32_t (*polygonDepth)(void *, uint16_t countAndFlags, const uint8_t *indices);
	uint32_t (*renderFlags)(void *);
	void (*setRenderFlags)(void *, uint16_t);
	void (*solid)(void *, uint16_t countAndFlags, int16_t normalX, int16_t normalY, int16_t normalZ, uint16_t material,
	              const uint8_t *stream);
	void (*textured)(void *, uint16_t countAndFlags, int16_t normalX, int16_t normalY, int16_t normalZ,
	                 uint16_t material, const uint8_t *stream);
} SlipShapeDispatchCalls;

extern int32_t SlipShape_textureDispatchThreshold;

void SlipShape_DispatchPrimitive(const uint8_t *, uint32_t traversalValue, const SlipShapeDispatchCalls *);
#endif
