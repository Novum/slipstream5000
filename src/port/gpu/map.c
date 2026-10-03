#include "map.h"
#include "byte_order.h"
#include "fixed_point.h"
#include "renderer.h"
#include "track_format.h"

#include <stdlib.h>
#include <string.h>

enum {
	/* One source node plus both possible linked route nodes. */
	SLIP_MAP_NODES_PER_ROUTE = 1 + SLIP_TRD_ROUTE_LINK_COUNT,
	SLIP_MAP_EDGES_PER_ROUTE = SLIP_TRD_ROUTE_LINK_COUNT,
	/* Each path has one more vertex than edges, at most two per edge. */
	SLIP_MAP_PATH_VERTICES_PER_ROUTE = 2 * SLIP_MAP_EDGES_PER_ROUTE
};

typedef struct {
	uint32_t a, b;
	bool used;
} MapEdge;

static SDL_FPoint Project(const SlipView3DMatrix *matrix, const SlipView3DVec32 *camera,
                          const SlipDraw3DProjectState *project, SlipView3DVec32 world) {
	/* Keep fractions through matrix multiplication and orthographic projection. */
	double dx = (double)world.x - camera->x;
	double dz = (double)world.z - camera->z;
	double x = (matrix->m[0] * dx + matrix->m[2] * dz) / SLIP_Q14_ONE;
	double y = (matrix->m[3] * dx + matrix->m[5] * dz) / SLIP_Q14_ONE;
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
		uint32_t capacity = (uint32_t)count * SLIP_MAP_NODES_PER_ROUTE;
		uint16_t *offsets = malloc(capacity * sizeof(*offsets));
		SlipView3DVec32 *world = malloc(capacity * sizeof(*world));
		MapEdge *edges = calloc(count * SLIP_MAP_EDGES_PER_ROUTE, sizeof(*edges));
		/* Each path consumes at least one edge and adds one starting vertex. */
		ribbonCache.ribbons = malloc(count * SLIP_MAP_EDGES_PER_ROUTE * sizeof(*ribbonCache.ribbons));
		ribbonCache.world = malloc(count * SLIP_MAP_PATH_VERTICES_PER_ROUTE * sizeof(*ribbonCache.world));
		ribbonCache.points = malloc(count * SLIP_MAP_PATH_VERTICES_PER_ROUTE * sizeof(*ribbonCache.points));
		if (!offsets || !world || !edges || !ribbonCache.ribbons || !ribbonCache.world || !ribbonCache.points) {
			free(offsets);
			free(world);
			free(edges);
			SlipRaceGpu_ResetMap();
			return;
		}
		uint32_t nodes = 0, edgeCount = 0;
		for (uint32_t i = 0; i < count && base + i * SLIP_TRD_ROUTE_RECORD_BYTES + SLIP_TRD_ROUTE_RECORD_BYTES <= bytes;
		     i++) {
			uint16_t offset = (uint16_t)(base + i * SLIP_TRD_ROUTE_RECORD_BYTES);
			for (unsigned link = 0; link < SLIP_TRD_ROUTE_LINK_COUNT; link++) {
				uint16_t target = SlipBytes_ReadLE16(data + offset + SLIP_TRD_ROUTE_FIRST_LINK_OFFSET +
				                                     link * SLIP_TRD_ROUTE_LINK_STRIDE);
				if (!target || (size_t)target + SLIP_TRD_POSITION_RECORD_BYTES > bytes)
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
						world[nodes++] = (SlipView3DVec32){SlipBytes_ReadLEI32(r + SLIP_TRD_POSITION_X_OFFSET),
						                                   SlipBytes_ReadLEI32(r + SLIP_TRD_POSITION_Y_OFFSET),
						                                   SlipBytes_ReadLEI32(r + SLIP_TRD_POSITION_Z_OFFSET)};
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
