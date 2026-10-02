#include "map.h"
#include "byte_order.h"
#include "renderer.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
	uint32_t a, b;
	bool used;
} MapEdge;

static SDL_FPoint Project(const SlipView3DMatrix *matrix, const SlipView3DVec32 *camera,
                          const SlipDraw3DProjectState *project, SlipView3DVec32 world) {
	/* Keep fractions through matrix multiplication and orthographic projection. */
	double dx = (double)world.x - camera->x;
	double dz = (double)world.z - camera->z;
	double x = (matrix->m[0] * dx + matrix->m[2] * dz) / 16384.;
	double y = (matrix->m[3] * dx + matrix->m[5] * dz) / 16384.;
	return (SDL_FPoint){(float)(project->centerX + x / project->modeOneScale),
	                    (float)(project->centerY - y / project->modeOneScale)};
}

typedef struct {
	uint32_t first, length;
	bool closed;
} MapRibbon;

typedef struct {
	const uint8_t *source;
	size_t bytes, base;
	uint16_t count;
	uint32_t pointCount, ribbonCount;
	MapRibbon *ribbons;
	SlipView3DVec32 *world;
	SDL_FPoint *points;
} MapRibbonCache;

static MapRibbonCache ribbonCache;

void SlipRaceGpu_ResetMap(void) {
	free(ribbonCache.ribbons);
	free(ribbonCache.world);
	free(ribbonCache.points);
	memset(&ribbonCache, 0, sizeof(ribbonCache));
}

void SlipRaceGpu_DrawMapRoute(const SlipView3DMatrix *matrix, const SlipView3DVec32 *camera,
                              const SlipDraw3DProjectState *project, const uint8_t *data, size_t bytes, size_t base,
                              uint16_t count, uint8_t color) {
	if (!count)
		return;
	if (ribbonCache.source != data || ribbonCache.bytes != bytes || ribbonCache.base != base ||
	    ribbonCache.count != count) {
		SlipRaceGpu_ResetMap();
		uint32_t capacity = (uint32_t)count * 3;
		uint16_t *offsets = malloc(capacity * sizeof(*offsets));
		SlipView3DVec32 *world = malloc(capacity * sizeof(*world));
		MapEdge *edges = calloc(count * 2u, sizeof(*edges));
		/* Each path consumes at least one edge and adds one starting vertex. */
		ribbonCache.ribbons = malloc(count * 2u * sizeof(*ribbonCache.ribbons));
		ribbonCache.world = malloc(count * 4u * sizeof(*ribbonCache.world));
		ribbonCache.points = malloc(count * 4u * sizeof(*ribbonCache.points));
		if (!offsets || !world || !edges || !ribbonCache.ribbons || !ribbonCache.world || !ribbonCache.points) {
			free(offsets);
			free(world);
			free(edges);
			SlipRaceGpu_ResetMap();
			return;
		}
		uint32_t nodes = 0, edgeCount = 0;
		for (uint32_t i = 0; i < count && base + i * 0x32u + 0x32u <= bytes; i++) {
			uint16_t offset = (uint16_t)(base + i * 0x32u);
			for (unsigned link = 0; link < 2; link++) {
				uint16_t target = SlipBytes_ReadLE16(data + offset + link * 4);
				if (!target || (size_t)target + 0x18u > bytes)
					continue;
				uint16_t ends[] = {offset, target};
				uint32_t ids[2];
				for (unsigned end = 0; end < 2; end++) {
					uint32_t id = 0;
					while (id < nodes && offsets[id] != ends[end])
						++id;
					if (id == nodes) {
						offsets[nodes] = ends[end];
						const uint8_t *r = data + ends[end];
						world[nodes++] = (SlipView3DVec32){SlipBytes_ReadLEI32(r + 12), SlipBytes_ReadLEI32(r + 16),
						                                   SlipBytes_ReadLEI32(r + 20)};
					}
					ids[end] = id;
				}
				if (ids[0] == ids[1])
					continue;
				uint32_t e = 0;
				while (e < edgeCount && !((edges[e].a == ids[0] && edges[e].b == ids[1]) ||
				                          (edges[e].a == ids[1] && edges[e].b == ids[0])))
					++e;
				if (e == edgeCount)
					edges[edgeCount++] = (MapEdge){ids[0], ids[1], false};
			}
		}
		uint32_t pathNodeCount = 0;
		for (uint32_t first = 0; first < edgeCount; first++) {
			if (edges[first].used)
				continue;
			uint32_t start = edges[first].a, current = start;
			const uint32_t pathStart = pathNodeCount;
			ribbonCache.world[pathNodeCount++] = world[start];
			for (uint32_t e = first;;) {
				edges[e].used = true;
				current = edges[e].a == current ? edges[e].b : edges[e].a;
				ribbonCache.world[pathNodeCount++] = world[current];
				if (current == start)
					break;
				for (e = 0; e < edgeCount; e++)
					if (!edges[e].used && (edges[e].a == current || edges[e].b == current))
						break;
				if (e == edgeCount)
					break;
			}
			ribbonCache.ribbons[ribbonCache.ribbonCount++] =
			    (MapRibbon){pathStart, pathNodeCount - pathStart, current == start};
		}
		free(offsets);
		free(world);
		free(edges);
		ribbonCache.source = data;
		ribbonCache.bytes = bytes;
		ribbonCache.base = base;
		ribbonCache.count = count;
		ribbonCache.pointCount = pathNodeCount;
	}
	for (uint32_t i = 0; i < ribbonCache.pointCount; i++)
		ribbonCache.points[i] = Project(matrix, camera, project, ribbonCache.world[i]);
	for (uint32_t i = 0; i < ribbonCache.ribbonCount; i++) {
		const MapRibbon *ribbon = ribbonCache.ribbons + i;
		SlipRaceGpu_MapRibbon(ribbonCache.points + ribbon->first, ribbon->length, ribbon->closed, color);
	}
}

void SlipRaceGpu_DrawMapFinish(const SlipView3DMatrix *matrix, const SlipView3DVec32 *camera,
                               const SlipDraw3DProjectState *project, SlipView3DVec32 world, uint8_t color) {
	SlipRaceGpu_MapMarker(Project(matrix, camera, project, world), color);
}
