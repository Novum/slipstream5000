#include "track_view_render.h"
#include "actor_format.h"
#include "actor_tags.h"
#include "byte_order.h"
#include "config_settings.h"
#include "fixed_point.h"
#include "gpu/renderer.h"
#include "material_format.h"
#include "material_frames.h"
#include "material_host.h"
#include "menu_resources.h"
#include "raster/raster.h"
#include "renderer_allocation.h"
#include "renderer_bounds.h"
#include "renderer_flags.h"
#include "renderer_host.h"
#include "renderer_state.h"
#include "renderer_vertices.h"
#include "shape_format.h"
#include "sprite_format.h"
#include "track_format.h"

#include "actor_render.h"
#include "artic_slot.h"
#include "draw3d.h"
#include "font.h"
#include "frame_timer.h"
#include "game_errors.h"
#include "port_app_bridge.h"
#include "race.h"
#include "race_display.h"
#include "race_player.h"
#include "refuel_beams.h"
#include "renderer_culling.h"
#include "resource.h"
#include "resource_host.h"
#include "resource_storage.h"
#include "shape3d.h"
#include "shape_dispatch.h"
#include "shape_primitives.h"
#include "sprite.h"
#include "sprite_resource_host.h"
#include "startup_intro.h"
#include "text_layout.h"
#include "timed_values.h"
#include "track_assets.h"
#include "track_world.h"
#include "vehicle_viewer.h"
#include "view3d.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void TrackView_WriteLE32(uint8_t *p, uint32_t value);

enum { SLIP_TRACK_EFFECT_RANDOM_INITIAL_SEED = 0x5a4a };

static uint16_t TrackView_effectRandomState = SLIP_TRACK_EFFECT_RANDOM_INITIAL_SEED;
static SlipRefuelBeamState TrackView_refuelBeams;

enum {
	kDriverCount = SLIPSTREAM_DRIVER_COUNT,
	TRACK_VIEW_DIAGNOSTIC_POINT_BUFFER_BASE = 0x00018880u,
	SLIP_CALLBACK_PAYLOAD_UPPER_WORD_MASK = 0xffff0000u,
	SLIP_IMPACT_SPRITE_SELECTION_ATTEMPTS = 4,
	SLIP_IMPACT_SPRITE_RADIUS = 0xa00,
	SLIP_CLOUD_FULL_DETAIL_MINIMUM_WIDTH = 400,
	/* SLIP_RENDER_MASKED_TEXTURE and SLIP_RENDER_ALTERNATE_TEXTURE_RASTER. */
	SLIP_CLOUD_RENDER_FLAGS = 0x30u,
	SLIP_VEHICLE_DIAGNOSTIC_PAYLOAD_DUMP_BYTES = 160,
	SLIP_PRIMITIVE_DIAGNOSTIC_PAYLOAD_DUMP_BYTES = 64,
	SLIP_TEXTURED_RASTER_VISIT_CAPACITY = 512,
	SLIP_GPU_WORLD_TEXTURE_VERTEX_CAPACITY = 128,
	SLIP_MATERIAL_SETUP_SEED_CAPACITY = 16,
	SLIP_MATERIAL_SETUP_DIAGNOSTIC_RECORD_LIMIT = 16,
	SLIP_COMPONENT_TAIL_VISIT_CAPACITY = 128,
	SLIP_PRIMITIVE_WALKER_VISIT_CAPACITY = 128,
	SLIP_DIRECT_CALLBACK_VISIT_CAPACITY = 256,
	SLIP_POST_PLANE_DIAGNOSTIC_VISIT_LIMIT = 16,
	SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY = 256,
	SLIP_RENDER_FULL_DETAIL_LEVEL = 3,

	SLIP_TRACK_PRIMITIVE_REPLAY_PLANE = SLIP_TRC_PRIMITIVE_REPLAY_PLANE,
	SLIP_TRACK_PRIMITIVE_REPLAY_LIGHT_ANGLE = 0x3f00,
	SLIP_TRACK_PRIMITIVE_REPLAY_MAXIMUM_NORMAL_Y_Q14 = 0x3000,
	SLIP_TRACK_TEXTURE_SCROLL_ACCUMULATOR_MASK = 0x7fff,
	SLIP_TRACK_TEXTURE_SCROLL_FRACTION_MASK = SLIP_Q14_ONE - 1,
	SLIP_TRACK_TEXTURE_SCROLL_TOKEN_MASK = 0x3ff,
	SLIP_TRACK_TEXTURE_SCROLL_REVERSE_TOKEN_START = 0x200,
	SLIP_VEHICLE_PREVIEW_DIRECT_LIGHT_Q14 = SLIP_Q14_ONE,
	SLIP_VEHICLE_PREVIEW_LIGHT_DIAGONAL_Q14 = -11594,
	SLIP_VEHICLE_PREVIEW_ORIGINAL_PART_CAPACITY = 16,
	SLIP_VEHICLE_PREVIEW_VIEWPORT_LEFT = 31,
	SLIP_VEHICLE_PREVIEW_VIEWPORT_TOP = 14,
	SLIP_VEHICLE_PREVIEW_VIEWPORT_RIGHT = 287,
	SLIP_VEHICLE_PREVIEW_VIEWPORT_BOTTOM = 183,
	SLIP_VEHICLE_PREVIEW_VIEWPORT_CENTER_X = 159,
	SLIP_VEHICLE_PREVIEW_VIEWPORT_CENTER_Y = 98,
	SLIP_TRACK_RESOURCE_SECONDARY_PATH_BYTES = SLIP_MENU_ARCHIVE_PATH_BYTES,
	SLIP_VEHICLE_PREVIEW_RESOURCE_NAME_BYTES = 32,
	SLIP_TRACK_MATERIAL_NAME_BUFFER_BYTES = 16,
	SLIP_MATERIAL_ANIMATED_POLYGON_COUNT = 8,
	SLIP_MATERIAL_ANIMATED_QUAD_BYTES =
	    sizeof(uint16_t) + SLIP_POLYGON_RECTANGLE_VERTICES * SLIP_SERIALIZED_INDEX_BYTES,
	SLIP_VEHICLE_DESCRIPTION_LEFT = 33,
	SLIP_VEHICLE_DESCRIPTION_RIGHT = 285,
	SLIP_VEHICLE_DESCRIPTION_TOP = 130,
	SLIP_VEHICLE_DESCRIPTION_BOTTOM = 179,
	SLIP_MASKED_RASTER_DIAGNOSTIC_VISIT_LIMIT = 80,
	SLIP_CROSS_EFFECT_MAXIMUM_DETAIL = 100,
	SLIP_CROSS_EFFECT_POINT_MAXIMUM_DETAIL = 2,
	/* Q14 sine/cosine scaled to one quarter of the projected detail size. */
	SLIP_CROSS_EFFECT_ARM_SCALE_SHIFT = SLIP_Q14_FRACTION_BITS + 2,
	SLIP_DIAGNOSTIC_VEHICLE_BOX_LEFT = 151,
	SLIP_DIAGNOSTIC_VEHICLE_BOX_RIGHT = 169,
	SLIP_DIAGNOSTIC_VEHICLE_BOX_TOP = 107,
	SLIP_DIAGNOSTIC_VEHICLE_BOX_BOTTOM = 115,
	SLIP_VEHICLE_PREVIEW_AMBIENT_LIGHT_Q14 = 0x0c00,
	SLIP_MATERIAL_ANIMATION_PHASE_MASK = SLIP_Q14_ONE - 1,
	SLIP_MATERIAL_ANIMATION_AMPLITUDE_SHIFT = 1,
	SLIP_MATERIAL_PERSPECTIVE_DEPTH_THRESHOLD = 683200,
	SLIP_MATERIAL_LINES_MAXIMUM_DEPTH = 19520000,
	SLIP_MATERIAL_TWO_COLOUR_LINES_MAXIMUM_DEPTH = 1561600,
	SLIP_MATERIAL_DARK_LINES_MAXIMUM_DEPTH = 3904000,
	SLIP_MATERIAL_DARK_LINES_DETAIL_DEPTH = 1952,
	SLIP_MATERIAL_ANIMATED_POLYGONS_MAXIMUM_DEPTH = 2147200,
	SLIP_MATERIAL_FLOOR_LIGHTS_MAXIMUM_DEPTH = 1708000,
	SLIP_MATERIAL_POLYGONS_MAXIMUM_DEPTH = 2732800,
	SLIP_MATERIAL_ROAD_LINES_MAXIMUM_DEPTH = 1464000,
	SLIP_MATERIAL_NO_POLYGON_COLOUR = UINT16_MAX,
	SLIP_MATERIAL_DASH_PHASE_SHIFT = 11,
	SLIP_MATERIAL_DASH_PHASE_COUNT = 4,
	SLIP_MATERIAL_DASH_PHASE_MASK = SLIP_MATERIAL_DASH_PHASE_COUNT - 1,
	SLIP_MATERIAL_DASH_ACCUMULATOR_MASK = (SLIP_MATERIAL_DASH_PHASE_COUNT << SLIP_MATERIAL_DASH_PHASE_SHIFT) - 1,
	SLIP_MATERIAL_DASH_DETAIL_PAIR_COUNT = 8,
	SLIP_MATERIAL_DASH_LISTS_PER_PAIR = 2,
	SLIP_MATERIAL_DASH_LIST_ADDRESS_BYTES = sizeof(uint32_t),
	SLIP_MATERIAL_DASH_PAIR_BYTES = SLIP_MATERIAL_DASH_LISTS_PER_PAIR * SLIP_MATERIAL_DASH_LIST_ADDRESS_BYTES,
	SLIP_MATERIAL_POLYGON_LIST_HEADER_BYTES = sizeof(uint16_t),
	/* Bits recorded in the primitive callback diagnostics. */
	SLIP_TRACK_CALLBACK_SKIPPED = 1u,
	SLIP_TRACK_CALLBACK_TEXTURED = 2u,
	SLIP_TRACK_CALLBACK_SPECIAL_PLANE = 4u,
	SLIP_TRACK_CALLBACK_PLANE_REJECTED = 8u,
	SLIP_TRACK_CALLBACK_GLOBAL_GATE_REJECTED = 16u,
	SLIP_TRACK_CALLBACK_HIGH_TEXTURED_PATH = 32u,
	SLIP_TRACK_SPRITE_MINIMUM_SOLID_RADIUS = 16384,
	SLIP_VEHICLE_PREVIEW_ANIMATED_PART_COUNT = 4,
	SLIP_VEHICLE_PREVIEW_INITIAL_TIMER_SAMPLES = 2,
	SLIP_VEHICLE_PREVIEW_ACTOR_ANGLE_STEP = SLIP_ANGLE_QUARTER_TURN,
	SLIP_VEHICLE_PREVIEW_FAN_ANGLE_STEP = SLIP_ANGLE_HALF_TURN - 1,
	SLIP_VEHICLE_PREVIEW_JET_RATE_SHIFT = 1,
	SLIP_REFUEL_COLOUR_RAMP_START = 0x40,
	SLIP_REFUEL_COLOUR_RAMP_END = 0x4f,
};

/* Original routine addresses retained as identifiers in diagnostic traces. */
enum {
	TRACK_VIEW_DIAGNOSTIC_OBJECT_VISIBILITY_COUNT = 0x00039e7cu,
	TRACK_VIEW_DIAGNOSTIC_OBJECT_LIST_ENTRY = 0x00039e67u,
	TRACK_VIEW_DIAGNOSTIC_OBJECT_TRANSFORM = 0x00039f27u,
	TRACK_VIEW_DIAGNOSTIC_OBJECT_VIEW_MATRIX = 0x00039f54u,
	TRACK_VIEW_DIAGNOSTIC_OBJECT_COMPONENT_LIST = 0x00039f7eu,
	TRACK_VIEW_DIAGNOSTIC_OBJECT_CHILD_LIST_DISPATCH = 0x0003a468u,
	TRACK_VIEW_DIAGNOSTIC_OBJECT_PRIMITIVE_LIST = 0x00039fafu,
	TRACK_VIEW_DIAGNOSTIC_OBJECT_BOUNDS = 0x00039fb6u,
	TRACK_VIEW_DIAGNOSTIC_PRIMITIVE_PRE_GATE = 0x0003a065u,
	TRACK_VIEW_DIAGNOSTIC_PRIMITIVE_DRAW_GATE = 0x0003a128u,
	TRACK_VIEW_DIAGNOSTIC_PRIMITIVE_RELATED_SCAN = 0x0003a166u,
	TRACK_VIEW_DIAGNOSTIC_RELATED_CHILD_LIST_DISPATCH = 0x0003a204u,
	TRACK_VIEW_DIAGNOSTIC_PRIMITIVE_RANGE = 0x0003a218u,
	TRACK_VIEW_DIAGNOSTIC_NESTED_PRIMITIVE_SETUP = 0x0003a34cu,
	TRACK_VIEW_DIAGNOSTIC_RELATED_OBJECT_RECORD = 0x0003a39cu,
	TRACK_VIEW_DIAGNOSTIC_STORED_COMPONENT_CHILD_LIST_DISPATCH = 0x0003a3e3u,
	TRACK_VIEW_DIAGNOSTIC_PRIMITIVE_DRAW_ADVANCE = 0x0003a40du,
	TRACK_VIEW_DIAGNOSTIC_DIRECT_PRIMITIVE_HEADER = 0x00038754u,
	TRACK_VIEW_DIAGNOSTIC_CLASSIFY_SOURCE_PLANE = 0x000193ffu,
	TRACK_VIEW_DIAGNOSTIC_PRIMITIVE_CALLBACK_DISPATCH = 0x0003872cu,
	TRACK_VIEW_DIAGNOSTIC_HIGH_TEXTURED_CALLBACK = 0x00038795u,
	TRACK_VIEW_DIAGNOSTIC_TEXTURED_PERSPECTIVE_DEPTH = 0x00019354u,
	TRACK_VIEW_DIAGNOSTIC_TEXTURED_EMIT_GATE = 0x00019e0du,
	TRACK_VIEW_DIAGNOSTIC_TEXTURED_DISPATCH_FALLBACK = 0x00019eafu,
	TRACK_VIEW_DIAGNOSTIC_LOCK_TEXTURE_PAYLOAD = 0x000248a5u,
	TRACK_VIEW_DIAGNOSTIC_TEXTURE_ROW_TABLE = 0x0002d91au,
	TRACK_VIEW_DIAGNOSTIC_AFFINE_TEXTURED_ENTRY = 0x00030600u,
	TRACK_VIEW_DIAGNOSTIC_AFFINE_HORIZONTAL_SPAN = 0x0003072eu,
	TRACK_VIEW_DIAGNOSTIC_AFFINE_EDGE_STEPS = 0x000316d3u,
	TRACK_VIEW_DIAGNOSTIC_AFFINE_SCANLINE_LOOP = 0x00030676u,
	TRACK_VIEW_DIAGNOSTIC_PERSPECTIVE_TEXTURED_ENTRY = 0x000308e2u,
	TRACK_VIEW_DIAGNOSTIC_OPAQUE_PERSPECTIVE_POLYGON = 0x00030978u,
	TRACK_VIEW_DIAGNOSTIC_MASKED_PERSPECTIVE_POLYGON = 0x0002dc2fu,
	TRACK_VIEW_DIAGNOSTIC_OPAQUE_AFFINE_POLYGON = 0x00030e48u,
	TRACK_VIEW_DIAGNOSTIC_ACTIVE_MATERIAL_EMIT = 0x0001c0a1u,
	TRACK_VIEW_DIAGNOSTIC_COMPONENT_TRAVERSAL_CALLBACK = 0x00039a6eu,
	TRACK_VIEW_DIAGNOSTIC_LOAD_DRAW_STATE = 0x0001dbbeu,
	TRACK_VIEW_DIAGNOSTIC_DIRECT_PRIMITIVE_LOOP = 0x000396dau,
	TRACK_VIEW_DIAGNOSTIC_DRAW_COMPONENT_ACTORS = 0x00037b46u,
	TRACK_VIEW_DIAGNOSTIC_CHUNK_PROJECT_INDEX = 0x0001cfa8u,
	TRACK_VIEW_DIAGNOSTIC_DEFERRED_CALLBACK_GATE = 0x0003a5b6u,
	TRACK_VIEW_DIAGNOSTIC_ACTOR_SHADED_RING = 0x0001b01au,
	TRACK_VIEW_DIAGNOSTIC_CHILD_BUILD_VERTEX_RECORDS = 0x00037878u,
	TRACK_VIEW_DIAGNOSTIC_CHILD_RESTORE_VERTEX_CURSOR = 0x000378a7u,
	TRACK_VIEW_DIAGNOSTIC_COMPONENT_BUILD_VERTEX_RECORDS = 0x00039b7fu,
	TRACK_VIEW_DIAGNOSTIC_COMPONENT_RESTORE_VERTEX_CURSOR = 0x00039c44u,
	TRACK_VIEW_DIAGNOSTIC_OBJECT_RESTORE_VERTEX_CURSOR = 0x0003a43eu,
	TRACK_VIEW_DIAGNOSTIC_UNCLIPPED_POLYGON = 0x0001dfa8u,
	TRACK_VIEW_DIAGNOSTIC_MATERIAL_GATE = 0x0001ba7du,
	TRACK_VIEW_DIAGNOSTIC_MATERIAL_NORMAL_COLOUR = 0x0001cce9u,
	TRACK_VIEW_DIAGNOSTIC_REGULAR_MATERIAL_SETUP = 0x0001bbb1u,
	TRACK_VIEW_DIAGNOSTIC_SOLID_RING = 0x0001bc0cu,
	TRACK_VIEW_DIAGNOSTIC_FLAT_RING = 0x0001ae47u,
	TRACK_VIEW_DIAGNOSTIC_MATERIAL_COLOUR_TRIPLET = 0x0001a1c8u,
	TRACK_VIEW_DIAGNOSTIC_MATERIAL_VERTEX_SETUP = 0x0003f32au,
	TRACK_VIEW_DIAGNOSTIC_MATERIAL_SCREEN_VERTEX_SETUP = 0x0003f3c4u,
	TRACK_VIEW_DIAGNOSTIC_BUILD_VERTEX_RECORDS = 0x0001dcf7u,
	TRACK_VIEW_DIAGNOSTIC_RESTORE_VERTEX_CURSOR = 0x0001de44u,
	TRACK_VIEW_DIAGNOSTIC_MATERIAL_STATE_POP = 0x0003f4e0u,
	TRACK_VIEW_DIAGNOSTIC_STORE_CLIP_BOUNDS = 0x00018bd1u,
	TRACK_VIEW_DIAGNOSTIC_PROJECT_INDEX = 0x00019746u,
	TRACK_VIEW_DIAGNOSTIC_CAPTURE_POST_PLANE_RING = 0x0001ab34u,
	TRACK_VIEW_DIAGNOSTIC_RELEASE_POST_PLANE_RING = 0x0001abb5u,
	TRACK_VIEW_DIAGNOSTIC_REPLAY_LIST = 0x0003977au,
	TRACK_VIEW_DIAGNOSTIC_REPLAY_OBJECT_DRAW = 0x00011eecu,
	TRACK_VIEW_DIAGNOSTIC_FAR_MATERIAL_COLOUR_TRIPLET = 0x0001a290u,
	TRACK_VIEW_DIAGNOSTIC_SHADED_TEN_LINES_LIST = 0x00040554u,
	TRACK_VIEW_DIAGNOSTIC_ORANGE_DASH_MAIN_LIST = 0x0003f630u,
	TRACK_VIEW_DIAGNOSTIC_ORANGE_DASH_DETAIL_LIST = 0x0003f697u,
	TRACK_VIEW_DIAGNOSTIC_FLOOR_LIGHT_DASH_LIST = 0x0003fac9u,
	TRACK_VIEW_DIAGNOSTIC_SECONDARY_COLOUR_LINE_LIST = 0x000411f6u,
	TRACK_VIEW_DIAGNOSTIC_EXTENDED_MATERIAL_DISPATCH = 0x0003f2c8u,
	TRACK_VIEW_DIAGNOSTIC_PREVIEW_BUILD_VERTEX_RECORDS = 0x00026242u,
	TRACK_VIEW_DIAGNOSTIC_ACTOR_PREVIEW_RESTORE_VERTEX_CURSOR = 0x00025d80u,
	TRACK_VIEW_DIAGNOSTIC_VEHICLE_PREVIEW_RESTORE_VERTEX_CURSOR = 0x00025efdu,
};

/* Original identifiers for embedded material lists and their draw routines. */
enum {
	TRACK_VIEW_INDEPENDENT_SETUP_TRIANGLE_QUADS_DATA_TOKEN = 0x0004109eu,
	TRACK_VIEW_ROAD_LINE_POLYGONS_DATA_TOKEN = 0x00041584u,
	TRACK_VIEW_ROAD_SURFACE_POLYGON_TOKEN = 0x0004159cu,
	TRACK_VIEW_ROAD_DARK_SURFACE_POLYGON_TOKEN = 0x000415a4u,
	TRACK_VIEW_ROAD_FIRST_LINE_POLYGON_TOKEN = TRACK_VIEW_ROAD_LINE_POLYGONS_DATA_TOKEN,
	TRACK_VIEW_ROAD_SECOND_LINE_POLYGON_TOKEN = 0x0004158cu,
	TRACK_VIEW_ROAD_THIRD_LINE_POLYGON_TOKEN = 0x00041594u,
	TRACK_VIEW_SHADED_TRIANGLE_QUADS_DATA_TOKEN = 0x00040dc2u,
	TRACK_VIEW_SHARED_SETUP_TRIANGLE_QUADS_DATA_TOKEN = 0x00040f6cu,
	TRACK_VIEW_SECONDARY_COLOR_QUADS_DATA_TOKEN = 0x0004133eu,
	TRACK_VIEW_ANIMATED_ORANGE_DASHES_DATA_TOKEN = 0x0003f6c0u,
	TRACK_VIEW_ORANGE_DASH_DETAIL_PAIRS_DATA_TOKEN = 0x0003f6e0u,
	TRACK_VIEW_ANIMATED_FLOOR_LIGHT_DASHES_DATA_TOKEN = 0x0003faf8u,
	TRACK_VIEW_THREE_LINE_DIAGONAL_DATA_TOKEN = 0x0004027cu,
	TRACK_VIEW_SEVEN_LINE_FRAME_ALTERNATE_DATA_TOKEN = 0x0004034cu,
	TRACK_VIEW_FIVE_LINE_FRAME_DATA_TOKEN = 0x000403d8u,
	TRACK_VIEW_CAGE_FIVE_LINES_DATA_TOKEN = 0x00040464u,
	TRACK_VIEW_CAGE_SIXTEEN_LINES_DATA_TOKEN = 0x000404e4u,
	TRACK_VIEW_SHADED_TEN_LINES_DATA_TOKEN = 0x00040580u,
	TRACK_VIEW_QUAD_OUTLINE_DATA_TOKEN = 0x000416d4u,
	TRACK_VIEW_TWO_CONNECTED_LINES_DATA_TOKEN = 0x0004173cu,
	TRACK_VIEW_TWO_CORNER_LINES_DATA_TOKEN = 0x00041798u,
	TRACK_VIEW_CAGE_SEVEN_LINES_DATA_TOKEN = 0x0004017cu,
	TRACK_VIEW_SEVEN_LINE_FRAME_DATA_TOKEN = 0x000402e4u,
	TRACK_VIEW_THREE_LINE_STRIP_DATA_TOKEN = 0x00040208u,
	TRACK_VIEW_SHADED_QUADS_DATA_TOKEN = 0x00040ba4u,
	TRACK_VIEW_SECONDARY_COLOR_POLYGONS_WITH_LINES_DATA_TOKEN = 0x0004124au,
	TRACK_VIEW_THREE_LINE_STRIP_SETUP_TOKEN = 0x0004021eu,
	TRACK_VIEW_THREE_LINE_STRIP_DIAGNOSTIC_DRAW = 0x000401ebu,
	TRACK_VIEW_THREE_LINE_DIAGONAL_SETUP_TOKEN = 0x0004028au,
	TRACK_VIEW_THREE_LINE_DIAGONAL_DIAGNOSTIC_DRAW = 0x0004025fu,
	TRACK_VIEW_SEVEN_LINE_FRAME_ALTERNATE_SETUP_TOKEN = 0x0004036au,
	TRACK_VIEW_SEVEN_LINE_FRAME_ALTERNATE_DIAGNOSTIC_DRAW = 0x0004032fu,
	TRACK_VIEW_FIVE_LINE_FRAME_SETUP_TOKEN = 0x000403f6u,
	TRACK_VIEW_FIVE_LINE_FRAME_DIAGNOSTIC_DRAW = 0x000403bbu,
	TRACK_VIEW_CAGE_FIVE_LINES_SETUP_TOKEN = 0x0004047au,
	TRACK_VIEW_CAGE_FIVE_LINES_DIAGNOSTIC_DRAW = 0x00040447u,
	TRACK_VIEW_CAGE_SIXTEEN_LINES_SETUP_TOKEN = 0x000404fau,
	TRACK_VIEW_CAGE_SIXTEEN_LINES_DIAGNOSTIC_DRAW = 0x000404c7u,
	TRACK_VIEW_QUAD_OUTLINE_SETUP_TOKEN = 0x000416e6u,
	TRACK_VIEW_QUAD_OUTLINE_DIAGNOSTIC_DRAW = 0x000416b5u,
	TRACK_VIEW_TWO_CONNECTED_LINES_SETUP_TOKEN = 0x00041746u,
	TRACK_VIEW_TWO_CONNECTED_LINES_DIAGNOSTIC_DRAW = 0x0004171fu,
	TRACK_VIEW_TWO_CORNER_LINES_SETUP_TOKEN = 0x000417a2u,
	TRACK_VIEW_TWO_CORNER_LINES_DIAGNOSTIC_DRAW = 0x0004177bu,
	TRACK_VIEW_SEVEN_LINE_FRAME_SETUP_TOKEN = 0x000402f2u,
	TRACK_VIEW_SEVEN_LINE_FRAME_DIAGNOSTIC_DRAW = 0x000402c7u,
	TRACK_VIEW_CAGE_SEVEN_LINES_SETUP_TOKEN = 0x0004019au,
	TRACK_VIEW_CAGE_SEVEN_LINES_DIAGNOSTIC_DRAW = 0x0004015du,
	TRACK_VIEW_DEPTH_DETAILED_DARK_LINES_DATA_TOKEN = 0x000406f8u,
	TRACK_VIEW_ANIMATED_EIGHT_POLYGONS_DATA_TOKEN = 0x0004092cu,
};

enum {
	TRACK_VIEW_TWO_COLOUR_SIXTEEN_LINE_FRAME_LINES_TOKEN = 0x0003fd90u,
	TRACK_VIEW_TWO_COLOUR_SIXTEEN_LINE_FRAME_SETUP_TOKEN = 0x0003fdf2u,
	TRACK_VIEW_TWO_COLOUR_SHARED_FRAME_LINES_TOKEN = 0x0003ff0cu,
	TRACK_VIEW_TWO_COLOUR_SHARED_FRAME_ALTERNATE_LINES_TOKEN = 0x0003ff90u,
	TRACK_VIEW_TWO_COLOUR_SHARED_FRAME_SETUP_TOKEN = 0x0003ffb0u,
	TRACK_VIEW_TWO_COLOUR_CROSS_FRAME_LINES_TOKEN = 0x00040054u,
	TRACK_VIEW_TWO_COLOUR_CROSS_FRAME_INVERSE_LINES_TOKEN = 0x000400d8u,
	TRACK_VIEW_TWO_COLOUR_CROSS_FRAME_SETUP_TOKEN = 0x000400f8u,
	TRACK_VIEW_DIAGNOSTIC_TWO_COLOUR_LINE_LIST = 0x0003fd7du,
	TRACK_VIEW_DIAGNOSTIC_LINE_PAIR = 0x0001bcaau,
	SLIP_MATERIAL_LINE_LIST_HEADER_BYTES = sizeof(uint16_t),
	SLIP_MATERIAL_LINE_PAIR_BYTES = 2 * sizeof(uint16_t),
	SLIP_MATERIAL_COLOURED_LINE_SELECTOR_OFFSET = SLIP_MATERIAL_LINE_PAIR_BYTES,
	SLIP_MATERIAL_COLOURED_LINE_BYTES = SLIP_MATERIAL_LINE_PAIR_BYTES + sizeof(uint16_t)
};

enum {
	TRACK_VIEW_INDEPENDENT_SETUP_TRIANGLE_QUADS_SETUP_TOKEN = 0x000410deu,
	TRACK_VIEW_INDEPENDENT_SETUP_TRIANGLE_QUADS_SECONDARY_BASE_TOKEN = 0x000410a0u,
	TRACK_VIEW_INDEPENDENT_SETUP_TRIANGLE_QUADS_SECONDARY_DETAIL_TOKEN = 0x000410a8u,
	TRACK_VIEW_INDEPENDENT_SETUP_TRIANGLE_QUADS_PRIMARY_TOKEN = 0x000410aeu,
	TRACK_VIEW_INDEPENDENT_SETUP_TRIANGLE_QUADS_LINES_TOKEN = 0x000410b8u,
	TRACK_VIEW_INDEPENDENT_SETUP_TRIANGLE_QUADS_DIAGNOSTIC_FAR_PATH = 0x00041080u,
	TRACK_VIEW_INDEPENDENT_SETUP_TRIANGLE_QUADS_DIAGNOSTIC_LINES = 0x0004104au,
	TRACK_VIEW_SHADED_QUADS_SETUP_TOKEN = 0x00040c18u,
	TRACK_VIEW_SHADED_QUADS_SECONDARY_DETAIL_TOKEN = 0x00040bacu,
	TRACK_VIEW_SHADED_QUADS_PRIMARY_TOKEN = 0x00040bb4u,
	TRACK_VIEW_SHADED_QUADS_LINES_TOKEN = 0x00040bbcu,
	TRACK_VIEW_SHADED_QUADS_DIAGNOSTIC_FAR_PATH = 0x00040b84u,
	TRACK_VIEW_SHADED_QUADS_DIAGNOSTIC_LINES = 0x00040b4eu,
	TRACK_VIEW_SHADED_TRIANGLE_QUADS_SETUP_TOKEN = 0x00040dfeu,
	TRACK_VIEW_SHADED_TRIANGLE_QUADS_SECONDARY_DETAIL_TOKEN = 0x00040dcau,
	TRACK_VIEW_SHADED_TRIANGLE_QUADS_PRIMARY_TOKEN = 0x00040dd0u,
	TRACK_VIEW_SHADED_TRIANGLE_QUADS_LINES_TOKEN = 0x00040dd8u,
	TRACK_VIEW_SHADED_TRIANGLE_QUADS_DIAGNOSTIC_FAR_PATH = 0x00040da2u,
	TRACK_VIEW_SHADED_TRIANGLE_QUADS_DIAGNOSTIC_LINES = 0x00040d6cu,
	TRACK_VIEW_SHARED_SETUP_TRIANGLE_QUADS_SECONDARY_DETAIL_TOKEN = 0x00040f74u,
	TRACK_VIEW_SHARED_SETUP_TRIANGLE_QUADS_PRIMARY_TOKEN = 0x00040f7au,
	TRACK_VIEW_SHARED_SETUP_TRIANGLE_QUADS_LINES_TOKEN = 0x00040f84u,
	TRACK_VIEW_SHARED_SETUP_TRIANGLE_QUADS_DIAGNOSTIC_FAR_PATH = 0x00040f4cu,
	TRACK_VIEW_SHARED_SETUP_TRIANGLE_QUADS_DIAGNOSTIC_LINES = 0x00040f16u,
	TRACK_VIEW_SHADED_TEN_LINES_SETUP_LIST_TOKEN = 0x000405e2u,
	TRACK_VIEW_DEPTH_DETAILED_DARK_LINES_SETUP_LIST_TOKEN = 0x00040754u,
	TRACK_VIEW_DEPTH_DETAILED_DARK_LINES_NEAR_DETAIL_LINE_LIST_TOKEN = 0x00040722u,
	TRACK_VIEW_ANIMATED_EIGHT_POLYGONS_SETUP_LIST_TOKEN = 0x0004097cu,
	TRACK_VIEW_ANIMATED_ORANGE_DASHES_SETUP_LIST_TOKEN = 0x0003f810u,
	TRACK_VIEW_ANIMATED_FLOOR_LIGHT_DASHES_ENTRY_TABLE_TOKEN = 0x0003fb00u,
	TRACK_VIEW_ANIMATED_FLOOR_LIGHT_DASHES_SETUP_LIST_TOKEN = 0x0003fbe0u,
	TRACK_VIEW_ROAD_LINE_POLYGONS_SETUP_LIST_TOKEN = 0x000415acu,
	TRACK_VIEW_SECONDARY_COLOR_QUADS_SETUP_LIST_TOKEN = 0x00041356u,
	TRACK_VIEW_SECONDARY_COLOR_QUADS_SECONDARY_DETAIL_POLYGON_TOKEN = 0x00041346u,
	TRACK_VIEW_SECONDARY_COLOR_QUADS_PRIMARY_POLYGON_TOKEN = 0x0004134eu,
	TRACK_VIEW_SECONDARY_COLOR_POLYGONS_WITH_LINES_SECONDARY_BASE_POLYGON_TOKEN = 0x0004124cu,
	TRACK_VIEW_SECONDARY_COLOR_POLYGONS_WITH_LINES_SECONDARY_DETAIL_POLYGON_TOKEN = 0x00041254u,
	TRACK_VIEW_SECONDARY_COLOR_POLYGONS_WITH_LINES_PRIMARY_POLYGON_TOKEN = 0x0004125au,
	TRACK_VIEW_SECONDARY_COLOR_POLYGONS_WITH_LINES_LINE_LIST_TOKEN = 0x00041264u,
};

enum {
	SLIP_MATERIAL_VERTEX_SETUP_HEADER_BYTES = 2 * sizeof(uint16_t),
	SLIP_MATERIAL_VERTEX_SETUP_DERIVED_COUNT_OFFSET = sizeof(uint16_t),
	SLIP_MATERIAL_VERTEX_INDEX_BYTES = sizeof(uint16_t),
	SLIP_MATERIAL_MIDPOINT_RECORD_BYTES = 2 * SLIP_MATERIAL_VERTEX_INDEX_BYTES,
	/* Preserve the original setup check for one word beyond the midpoint records. */
	SLIP_MATERIAL_VERTEX_SETUP_TRAILING_BYTES = sizeof(uint16_t),
	SLIP_MATERIAL_RAMP_UPPER_WORD_MASK = 0xffff0000u,
	TRACK_VIEW_DIAGNOSTIC_BUILD_MATERIAL_MIDPOINTS = 0x0003f37du,
	TRACK_VIEW_DIAGNOSTIC_BUILD_MATERIAL_SCREEN_MIDPOINTS = 0x0003f408u
};

const SlipRaceTrackFrameCallback g_trackViewFrameCallbacks[kDriverCount] = {
    SlipRaceTrack_DrawArizonaNorwayBackground, SlipRaceTrack_DrawChicagoBackground,
    SlipRaceTrack_DrawAmazonBackground,        SlipRaceTrack_DrawLondonBackground,
    SlipRaceTrack_DrawArizonaNorwayBackground, SlipRaceTrack_DrawEgyptBackground,
    SlipRaceTrack_DrawFranceBackground,        SlipRaceTrack_DrawHawaiiBackground,
    SlipRaceTrack_DrawTokyoBackground,         SlipRaceTrack_DrawNewYorkBackground};

static SlipView3DMaths g_maths;
static bool g_mathsLoaded;

static bool TrackView_LoadMaths(SlipView3DMaths *maths, const char *const *archives, size_t archiveCount) {
	if (g_mathsLoaded) {
		*maths = g_maths;
		return true;
	}
	if (archiveCount == 0 || !SlipView3D_LoadMathsFromArchives(maths, archives, archiveCount)) {
		return false;
	}
	g_maths = *maths;
	g_mathsLoaded = true;
	return true;
}

static bool TrackView_DrawShape(const SlipResourcePayload *residentShape, TrackViewRawBspContext *context,
                                const char *shapeName, const SlipView3DMatrix *objectMatrix,
                                const SlipView3DMatrix *viewMatrix, SlipView3DVec32 viewPosition,
                                SlipView3DVec32 worldPosition);

bool TrackView_LoadResourceHandlePayload(const TrackViewResourceHandleRegistry *registry, uint32_t resourceHandle,
                                         SlipResourcePayload *payload);
static bool TrackView_LockResourceHandlePayload(const TrackViewResourceHandleRegistry *registry, uint32_t handle,
                                                SlipResourcePayload *payload);
static void TrackView_UnlockResourceHandlePayload(const TrackViewResourceHandleRegistry *registry, uint32_t handle);
static const char *TrackView_ResourceNameFromHandle(const TrackViewResourceHandleRegistry *registry, uint16_t handle);
bool TrackViewDrawSceneryShape(TrackViewRawBspContext *context, const uint8_t *record, size_t recordBytes,
                               const SlipView3DMatrix *objectMatrix, const SlipView3DMatrix *viewMatrix,
                               SlipView3DVec32 viewPosition, SlipView3DVec32 worldPosition);

static bool TrackView_WriteTexturedPointBuffer(const SlipDraw3DTexturedDispatchPoint *points, size_t pointCount,
                                               uint8_t *pointBuffer, size_t pointBufferBytes);

static void TrackView_DumpTexturedDispatch(size_t recordOffset, uint32_t inputActiveHeadOffset,
                                           const SlipDraw3DTexturedDispatch *dispatch,
                                           const SlipDraw3DTexturedDispatchVisit *visits, size_t visitCapacity);

static void TrackView_DumpAffineEntry(size_t recordOffset, const RasterAffineTexturedEntrySetup *affineEntry);

static void TrackView_DumpAffineTexturePayload(size_t recordOffset, const TrackViewResourceHandleRegistry *registry,
                                               uint32_t resourceHandle, const SlipResourcePayload *payload,
                                               uint32_t rowScroll, const RasterTextureRowTable *rowTable);

static void TrackView_CopyScreenSnapshot(uint8_t snapshot[SLIPSTREAM_SCREEN_WIDTH * SLIPSTREAM_SCREEN_HEIGHT]);

typedef struct TrackViewDiagnosticProbePixel {
	int x, y;
} TrackViewDiagnosticProbePixel;

static void TrackView_DumpRecordDamage(size_t recordOffset, uint32_t dispatchFlags,
                                       const uint8_t before[SLIPSTREAM_SCREEN_WIDTH * SLIPSTREAM_SCREEN_HEIGHT]);

static void TrackView_DumpAffineLoop(size_t recordOffset, const RasterAffineScanlineLoop *loop,
                                     const RasterAffineScanlineLoopVisit *visits);

static void TrackView_DumpOpaquePerspective(size_t recordOffset, const RasterOpaquePerspectiveTexturedPolygon *opaque,
                                            const RasterOpaquePerspectiveTexturedVisit *visits, size_t visitCapacity);

static size_t TrackView_VertexBufferCapacityBytes(const TrackViewRawBspContext *context);

static bool TrackView_SetActiveVertexBuffer(TrackViewRawBspContext *context, uint32_t cursor);

static const uint8_t kTrackViewIndependentSetupTriangleQuadsData[] = {
    0x53, 0x00, 0x0c, 0x00, 0x02, 0x00, 0x00, 0x00, 0x07, 0x00, 0x01, 0x00, 0x0e, 0x00, 0x05, 0x00, 0x05, 0x00, 0x0e,
    0x00, 0x0c, 0x00, 0x07, 0x00, 0x87, 0xdb, 0x06, 0x00, 0x16, 0x00, 0x05, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x04, 0x00,
    0x01, 0x00, 0x18, 0x00, 0x09, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x03, 0x00, 0x01, 0x00, 0x19, 0x00, 0x08, 0x00, 0x00,
    0x00, 0x1c, 0x00, 0x06, 0x00, 0x01, 0x00, 0x03, 0x00, 0x1d, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x06, 0x00, 0x03, 0x00, 0x06, 0x00, 0x03, 0x00, 0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x0a, 0x00,
    0x02, 0x00, 0x02, 0x00, 0x0b, 0x00, 0x0a, 0x00, 0x01, 0x00, 0x01, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
    0x00, 0x0f, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x02, 0x00, 0x12, 0x00, 0x0f, 0x00,
    0x02, 0x00, 0x13, 0x00, 0x13, 0x00, 0x0b, 0x00, 0x0a, 0x00, 0x0f, 0x00, 0x17, 0x00, 0x0f, 0x00, 0x07, 0x00, 0x18,
    0x00, 0x16, 0x00, 0x18, 0x00, 0x18, 0x00, 0x19, 0x00, 0x07, 0x00, 0x19, 0x00, 0x3d, 0x00};

static const uint8_t kTrackViewCageSevenLinesData[] = {
    0x07, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0x06,
    0x00, 0x09, 0x00, 0x05, 0x00, 0x08, 0x00, 0x04, 0x00, 0x07, 0x00, 0x04, 0x00, 0x0a, 0x00, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x01, 0x00, 0x05, 0x00, 0x03, 0x00, 0x08, 0x00, 0x02, 0x00, 0x03, 0x00, 0x02, 0x00, 0x08, 0x00, 0x3d, 0x00};

static const uint8_t kTrackViewSevenLineFrameData[] = {0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x02, 0x00,
                                                       0x03, 0x00, 0x04, 0x00, 0x03, 0x00, 0x05, 0x00, 0xff, 0xff,
                                                       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                                       0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x3d, 0x00};

static const char kTrackViewCageMaterialName[] = "SDCage";

static const char kTrackViewFloorLightMaterialName[] = "SDFloorLight";
static const char kTrackViewOrangeLightMaterialName[] = "SDOrangeLight";
static const char kTrackViewBlueLightMaterialName[] = "SDBlueLight";
static const char kTrackViewWhiteLightMaterialName[] = "SDWhiteLight";
static const char kTrackViewYellowMaterialName[] = "SDYellow";

static const char kTrackViewRoadLineMaterialName[] = "SDRoadLine";

static uint32_t g_materialAnimAccumulator;
static uint16_t g_materialAnimWave;
static uint32_t g_materialAnimLerp;
static uint32_t g_materialAnimRangeLow;
static uint32_t g_materialAnimRangeHigh;

static const uint8_t kTrackViewThreeLineStripData[] = {
    0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x05, 0x00, 0x04, 0x00, 0x06, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x3d, 0x00};

static const uint8_t kTrackViewShadedQuadsData[] = {
    0x00, 0x00, 0x01, 0x00, 0x0b, 0x00, 0x06, 0x00, 0x08, 0x00, 0x0d, 0x00, 0x02, 0x00, 0x03, 0x00, 0x06, 0x00, 0x0b,
    0x00, 0x0d, 0x00, 0x08, 0x00, 0x0f, 0x00, 0x1c, 0x00, 0x24, 0x00, 0x01, 0x00, 0x16, 0x00, 0x17, 0x00, 0x00, 0x00,
    0x1d, 0x00, 0x25, 0x00, 0x01, 0x00, 0x13, 0x00, 0x10, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x26, 0x00, 0x01, 0x00, 0x15,
    0x00, 0x18, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x27, 0x00, 0x01, 0x00, 0x0e, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x20, 0x00,
    0x28, 0x00, 0x01, 0x00, 0x14, 0x00, 0x19, 0x00, 0x00, 0x00, 0x21, 0x00, 0x29, 0x00, 0x01, 0x00, 0x12, 0x00, 0x11,
    0x00, 0x00, 0x00, 0x22, 0x00, 0x2a, 0x00, 0x01, 0x00, 0x1b, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x23, 0x00, 0x2b, 0x00,
    0x01, 0x00, 0x04, 0x00, 0x2c, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x05, 0x00, 0x03, 0x00, 0x04, 0x00,
    0x03, 0x00, 0x07, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x09, 0x00, 0x01, 0x00, 0x0a, 0x00, 0x02, 0x00, 0x09,
    0x00, 0x02, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x0b, 0x00, 0x08, 0x00, 0x0d, 0x00, 0x08, 0x00, 0x0f, 0x00, 0x0d, 0x00,
    0x0f, 0x00, 0x0b, 0x00, 0x0e, 0x00, 0x06, 0x00, 0x0e, 0x00, 0x12, 0x00, 0x0e, 0x00, 0x0e, 0x00, 0x13, 0x00, 0x06,
    0x00, 0x13, 0x00, 0x08, 0x00, 0x10, 0x00, 0x10, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x11, 0x00, 0x11, 0x00, 0x0d, 0x00,
    0x12, 0x00, 0x0b, 0x00, 0x06, 0x00, 0x16, 0x00, 0x16, 0x00, 0x13, 0x00, 0x13, 0x00, 0x15, 0x00, 0x15, 0x00, 0x0e,
    0x00, 0x0e, 0x00, 0x14, 0x00, 0x14, 0x00, 0x12, 0x00, 0x12, 0x00, 0x1b, 0x00, 0x1b, 0x00, 0x0b, 0x00, 0x08, 0x00,
    0x17, 0x00, 0x17, 0x00, 0x10, 0x00, 0x10, 0x00, 0x18, 0x00, 0x18, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x19, 0x00, 0x19,
    0x00, 0x11, 0x00, 0x11, 0x00, 0x1a, 0x00, 0x1a, 0x00, 0x0d, 0x00, 0x3d, 0x00};

static const uint8_t kTrackViewRoadLinePolygonsData[] = {
    0x13, 0x00, 0x1e, 0x00, 0x23, 0x00, 0x18, 0x00, 0x28, 0x00, 0x2d, 0x00, 0x1a, 0x00, 0x0f, 0x00, 0x14, 0x00,
    0x1f, 0x00, 0x37, 0x00, 0x32, 0x00, 0x00, 0x00, 0x04, 0x00, 0x09, 0x00, 0x03, 0x00, 0x04, 0x00, 0x01, 0x00,
    0x02, 0x00, 0x09, 0x00, 0x04, 0x00, 0x38, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x05, 0x00,
    0x01, 0x00, 0x04, 0x00, 0x01, 0x00, 0x07, 0x00, 0x02, 0x00, 0x03, 0x00, 0x03, 0x00, 0x09, 0x00, 0x03, 0x00,
    0x0a, 0x00, 0x02, 0x00, 0x09, 0x00, 0x02, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x0b, 0x00, 0x06, 0x00, 0x0e, 0x00,
    0x0f, 0x00, 0x0e, 0x00, 0x10, 0x00, 0x0e, 0x00, 0x11, 0x00, 0x0e, 0x00, 0x12, 0x00, 0x0e, 0x00, 0x0e, 0x00,
    0x0b, 0x00, 0x14, 0x00, 0x0e, 0x00, 0x15, 0x00, 0x0e, 0x00, 0x16, 0x00, 0x0e, 0x00, 0x17, 0x00, 0x0e, 0x00,
    0x08, 0x00, 0x0d, 0x00, 0x08, 0x00, 0x19, 0x00, 0x1a, 0x00, 0x19, 0x00, 0x1b, 0x00, 0x19, 0x00, 0x1c, 0x00,
    0x19, 0x00, 0x1d, 0x00, 0x19, 0x00, 0x0d, 0x00, 0x19, 0x00, 0x1f, 0x00, 0x19, 0x00, 0x20, 0x00, 0x19, 0x00,
    0x21, 0x00, 0x19, 0x00, 0x22, 0x00, 0x19, 0x00, 0x06, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x24, 0x00, 0x0f, 0x00,
    0x25, 0x00, 0x0f, 0x00, 0x26, 0x00, 0x0f, 0x00, 0x27, 0x00, 0x08, 0x00, 0x1a, 0x00, 0x1a, 0x00, 0x29, 0x00,
    0x1a, 0x00, 0x2a, 0x00, 0x1a, 0x00, 0x2b, 0x00, 0x1a, 0x00, 0x2c, 0x00, 0x0b, 0x00, 0x14, 0x00, 0x14, 0x00,
    0x2e, 0x00, 0x14, 0x00, 0x2f, 0x00, 0x14, 0x00, 0x30, 0x00, 0x14, 0x00, 0x31, 0x00, 0x0d, 0x00, 0x1f, 0x00,
    0x1f, 0x00, 0x33, 0x00, 0x1f, 0x00, 0x34, 0x00, 0x1f, 0x00, 0x35, 0x00, 0x1f, 0x00, 0x36, 0x00, 0x3d, 0x00};

static const uint8_t kTrackViewShadedTriangleQuadsData[] = {
    0x01, 0x00, 0x0c, 0x00, 0x07, 0x00, 0x00, 0x00, 0x02, 0x00, 0x05, 0x00, 0x0e, 0x00, 0x07, 0x00, 0x0c, 0x00, 0x0e,
    0x00, 0x05, 0x00, 0x06, 0x00, 0x16, 0x00, 0x05, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x04, 0x00, 0x01, 0x00, 0x18, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x03, 0x00, 0x01, 0x00, 0x19, 0x00, 0x08, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x06,
    0x00, 0x01, 0x00, 0x03, 0x00, 0x1d, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x06,
    0x00, 0x03, 0x00, 0x06, 0x00, 0x03, 0x00, 0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x0a, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x0b, 0x00, 0x0a, 0x00, 0x02, 0x00, 0x02, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x10, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x01, 0x00, 0x12, 0x00, 0x0f, 0x00, 0x01, 0x00, 0x13, 0x00,
    0x13, 0x00, 0x0b, 0x00, 0x0a, 0x00, 0x0f, 0x00, 0x17, 0x00, 0x0f, 0x00, 0x07, 0x00, 0x18, 0x00, 0x16, 0x00, 0x18,
    0x00, 0x18, 0x00, 0x19, 0x00, 0x07, 0x00, 0x19, 0x00, 0x3d, 0x00};

static const uint8_t kTrackViewSharedSetupTriangleQuadsData[] = {
    0x01, 0x00, 0x0c, 0x00, 0x07, 0x00, 0x00, 0x00, 0x02, 0x00, 0x05, 0x00, 0x0e, 0x00, 0x07, 0x00,
    0x0c, 0x00, 0x0e, 0x00, 0x05, 0x00, 0x87, 0xdb, 0x06, 0x00, 0x16, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x1a, 0x00, 0x04, 0x00, 0x01, 0x00, 0x18, 0x00, 0x09, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x03, 0x00,
    0x01, 0x00, 0x19, 0x00, 0x08, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x06, 0x00, 0x01, 0x00};

static const uint8_t kTrackViewSecondaryColorQuadsData[] = {
    0x00, 0x00, 0x01, 0x00, 0x0b, 0x00, 0x06, 0x00, 0x08, 0x00, 0x0d, 0x00, 0x02, 0x00, 0x03, 0x00, 0x06, 0x00,
    0x0b, 0x00, 0x0d, 0x00, 0x08, 0x00, 0x04, 0x00, 0x0e, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x03, 0x00, 0x04, 0x00, 0x03, 0x00, 0x07, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x09, 0x00,
    0x01, 0x00, 0x0a, 0x00, 0x02, 0x00, 0x09, 0x00, 0x02, 0x00, 0x0c, 0x00, 0xc3, 0xc3};

static const uint8_t kTrackViewAnimatedFloorLightDashesData[] = {
    0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x40, 0xfb, 0x03, 0x00, 0x90, 0xfb, 0x03, 0x00, 0x4a, 0xfb, 0x03,
    0x00, 0x9a, 0xfb, 0x03, 0x00, 0x54, 0xfb, 0x03, 0x00, 0xa4, 0xfb, 0x03, 0x00, 0x5e, 0xfb, 0x03, 0x00, 0xae, 0xfb,
    0x03, 0x00, 0x68, 0xfb, 0x03, 0x00, 0xb8, 0xfb, 0x03, 0x00, 0x72, 0xfb, 0x03, 0x00, 0xc2, 0xfb, 0x03, 0x00, 0x7c,
    0xfb, 0x03, 0x00, 0xcc, 0xfb, 0x03, 0x00, 0x86, 0xfb, 0x03, 0x00, 0xd6, 0xfb, 0x03, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x11, 0x00, 0x20, 0x00, 0x08, 0x00, 0x04, 0x00, 0x10, 0x00, 0x12, 0x00, 0x21, 0x00, 0x1f, 0x00, 0x04, 0x00, 0x0f,
    0x00, 0x14, 0x00, 0x23, 0x00, 0x1e, 0x00, 0x04, 0x00, 0x13, 0x00, 0x15, 0x00, 0x24, 0x00, 0x22, 0x00, 0x04, 0x00,
    0x0e, 0x00, 0x18, 0x00, 0x27, 0x00, 0x1d, 0x00, 0x04, 0x00, 0x17, 0x00, 0x19, 0x00, 0x28, 0x00, 0x26, 0x00, 0x04,
    0x00, 0x16, 0x00, 0x1b, 0x00, 0x2a, 0x00, 0x25, 0x00, 0x04, 0x00, 0x1a, 0x00, 0x1c, 0x00, 0x2b, 0x00, 0x29, 0x00,
    0x04, 0x00, 0x2f, 0x00, 0x51, 0x00, 0x37, 0x00, 0x03, 0x00, 0x04, 0x00, 0x45, 0x00, 0x47, 0x00, 0x38, 0x00, 0x36,
    0x00, 0x04, 0x00, 0x44, 0x00, 0x48, 0x00, 0x3a, 0x00, 0x35, 0x00, 0x04, 0x00, 0x46, 0x00, 0x49, 0x00, 0x3b, 0x00,
    0x39, 0x00, 0x04, 0x00, 0x43, 0x00, 0x4c, 0x00, 0x3e, 0x00, 0x34, 0x00, 0x04, 0x00, 0x4b, 0x00, 0x4d, 0x00, 0x3f,
    0x00, 0x3d, 0x00, 0x04, 0x00, 0x4a, 0x00, 0x4f, 0x00, 0x41, 0x00, 0x3c, 0x00, 0x04, 0x00, 0x4e, 0x00, 0x50, 0x00,
    0x42, 0x00, 0x40, 0x00, 0x04, 0x00, 0x52, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x07, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x09, 0x00, 0x01, 0x00, 0x0a, 0x00, 0x01,
    0x00, 0x0b, 0x00, 0x01, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x0f, 0x00,
    0x00, 0x00, 0x10, 0x00, 0x0f, 0x00, 0x10, 0x00, 0x0e, 0x00, 0x0f, 0x00, 0x13, 0x00, 0x0f, 0x00, 0x0e, 0x00, 0x13,
    0x00, 0x01, 0x00, 0x0e, 0x00, 0x16, 0x00, 0x0e, 0x00, 0x0e, 0x00, 0x17, 0x00, 0x16, 0x00, 0x17, 0x00, 0x01, 0x00,
    0x16, 0x00, 0x1a, 0x00, 0x16, 0x00, 0x01, 0x00, 0x1a, 0x00, 0x08, 0x00, 0x0d, 0x00, 0x08, 0x00, 0x1d, 0x00, 0x08,
    0x00, 0x1e, 0x00, 0x08, 0x00, 0x1f, 0x00, 0x1e, 0x00, 0x1f, 0x00, 0x1d, 0x00, 0x1e, 0x00, 0x1e, 0x00, 0x22, 0x00,
    0x22, 0x00, 0x1d, 0x00, 0x0d, 0x00, 0x1d, 0x00, 0x1d, 0x00, 0x25, 0x00, 0x1d, 0x00, 0x26, 0x00, 0x25, 0x00, 0x26,
    0x00, 0x0d, 0x00, 0x25, 0x00, 0x25, 0x00, 0x29, 0x00, 0x29, 0x00, 0x0d, 0x00, 0x03, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x2c, 0x00, 0x03, 0x00, 0x2d, 0x00, 0x03, 0x00, 0x2e, 0x00, 0x09, 0x00, 0x02, 0x00, 0x02, 0x00, 0x30, 0x00, 0x02,
    0x00, 0x31, 0x00, 0x02, 0x00, 0x32, 0x00, 0x02, 0x00, 0x03, 0x00, 0x03, 0x00, 0x34, 0x00, 0x03, 0x00, 0x35, 0x00,
    0x03, 0x00, 0x36, 0x00, 0x36, 0x00, 0x35, 0x00, 0x34, 0x00, 0x35, 0x00, 0x39, 0x00, 0x35, 0x00, 0x34, 0x00, 0x39,
    0x00, 0x02, 0x00, 0x34, 0x00, 0x34, 0x00, 0x3c, 0x00, 0x3d, 0x00, 0x34, 0x00, 0x3c, 0x00, 0x3d, 0x00, 0x02, 0x00,
    0x3c, 0x00, 0x3c, 0x00, 0x40, 0x00, 0x02, 0x00, 0x40, 0x00, 0x2f, 0x00, 0x33, 0x00, 0x2f, 0x00, 0x43, 0x00, 0x2f,
    0x00, 0x44, 0x00, 0x43, 0x00, 0x44, 0x00, 0x44, 0x00, 0x45, 0x00, 0x44, 0x00, 0x46, 0x00, 0x43, 0x00, 0x46, 0x00,
    0x33, 0x00, 0x43, 0x00, 0x4a, 0x00, 0x43, 0x00, 0x43, 0x00, 0x4b, 0x00, 0x4a, 0x00, 0x4b, 0x00, 0x33, 0x00, 0x4a,
    0x00, 0x4e, 0x00, 0x4a, 0x00, 0x33, 0x00, 0x4e, 0x00, 0x2f, 0x00, 0x45, 0x00, 0x3d, 0x00};

static const uint8_t kTrackViewThreeLineDiagonalData[] = {0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00,
                                                          0x03, 0x00, 0x04, 0x00, 0x03, 0x00, 0x05, 0x00, 0xff, 0xff,
                                                          0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                                          0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x3d, 0x00};

static const uint8_t kTrackViewSevenLineFrameAlternateData[] = {
    0x07, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0x06,
    0x00, 0x09, 0x00, 0x05, 0x00, 0x08, 0x00, 0x04, 0x00, 0x07, 0x00, 0x04, 0x00, 0x0a, 0x00, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x01, 0x00, 0x05, 0x00, 0x03, 0x00, 0x08, 0x00, 0x02, 0x00, 0x03, 0x00, 0x02, 0x00, 0x08, 0x00, 0x3d, 0x00};

static const uint8_t kTrackViewFiveLineFrameData[] = {
    0x07, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0x06,
    0x00, 0x09, 0x00, 0x05, 0x00, 0x08, 0x00, 0x04, 0x00, 0x07, 0x00, 0x04, 0x00, 0x0a, 0x00, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x01, 0x00, 0x05, 0x00, 0x03, 0x00, 0x08, 0x00, 0x02, 0x00, 0x03, 0x00, 0x02, 0x00, 0x08, 0x00, 0x3d, 0x00};

static const uint8_t kTrackViewCageFiveLinesData[] = {
    0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x06, 0x00, 0x05, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x07, 0x00, 0x03, 0x00, 0x09, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x02, 0x00, 0x07, 0x00, 0x02, 0x00, 0x3d, 0x00};

static const uint8_t kTrackViewCageSixteenLinesData[] = {
    0x05, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x06, 0x00, 0x05, 0x00,
    0x08, 0x00, 0x04, 0x00, 0x07, 0x00, 0x03, 0x00, 0x09, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x02, 0x00, 0x07, 0x00, 0x02, 0x00, 0xc3, 0xbb};

static const uint8_t kTrackViewShadedTenLinesData[] = {
    0x10, 0x00, 0x18, 0x00, 0x17, 0x00, 0x00, 0x00, 0x17, 0x00, 0x2a, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x2b, 0x00, 0x01,
    0x00, 0x2b, 0x00, 0x18, 0x00, 0x01, 0x00, 0x15, 0x00, 0x14, 0x00, 0x00, 0x00, 0x14, 0x00, 0x27, 0x00, 0x00, 0x00,
    0x27, 0x00, 0x28, 0x00, 0x01, 0x00, 0x28, 0x00, 0x15, 0x00, 0x01, 0x00, 0x11, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10,
    0x00, 0x23, 0x00, 0x00, 0x00, 0x23, 0x00, 0x24, 0x00, 0x01, 0x00, 0x24, 0x00, 0x11, 0x00, 0x01, 0x00, 0x0e, 0x00,
    0x0d, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x00, 0x21, 0x00, 0x01, 0x00, 0x21, 0x00, 0x0e,
    0x00, 0x01, 0x00, 0x04, 0x00, 0x2c, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x05, 0x00, 0x01, 0x00, 0x02,
    0x00, 0x01, 0x00, 0x07, 0x00, 0x01, 0x00, 0x08, 0x00, 0x09, 0x00, 0x06, 0x00, 0x09, 0x00, 0x0a, 0x00, 0x09, 0x00,
    0x0b, 0x00, 0x09, 0x00, 0x0c, 0x00, 0x0c, 0x00, 0x0b, 0x00, 0x0a, 0x00, 0x0b, 0x00, 0x0b, 0x00, 0x0f, 0x00, 0x0a,
    0x00, 0x0f, 0x00, 0x06, 0x00, 0x0a, 0x00, 0x0a, 0x00, 0x12, 0x00, 0x0a, 0x00, 0x13, 0x00, 0x12, 0x00, 0x13, 0x00,
    0x06, 0x00, 0x12, 0x00, 0x16, 0x00, 0x12, 0x00, 0x06, 0x00, 0x16, 0x00, 0x03, 0x00, 0x04, 0x00, 0x03, 0x00, 0x19,
    0x00, 0x02, 0x00, 0x07, 0x00, 0x02, 0x00, 0x1b, 0x00, 0x1c, 0x00, 0x1a, 0x00, 0x1c, 0x00, 0x1d, 0x00, 0x1c, 0x00,
    0x1e, 0x00, 0x1c, 0x00, 0x1f, 0x00, 0x1e, 0x00, 0x1f, 0x00, 0x1e, 0x00, 0x1d, 0x00, 0x1e, 0x00, 0x22, 0x00, 0x22,
    0x00, 0x1d, 0x00, 0x1a, 0x00, 0x1d, 0x00, 0x1d, 0x00, 0x25, 0x00, 0x1d, 0x00, 0x26, 0x00, 0x25, 0x00, 0x26, 0x00,
    0x25, 0x00, 0x1a, 0x00, 0x25, 0x00, 0x29, 0x00, 0x1a, 0x00, 0x29, 0x00, 0xc3, 0xc3, 0xc3};

static const uint8_t kTrackViewDepthDetailedDarkLinesData[] = {
    0x0a, 0x00, 0x0e, 0x00, 0x15, 0x00, 0x0d, 0x00, 0x14, 0x00, 0x11, 0x00, 0x16, 0x00, 0x1d, 0x00, 0x2c, 0x00, 0x1c,
    0x00, 0x2b, 0x00, 0x20, 0x00, 0x2f, 0x00, 0x1b, 0x00, 0x2a, 0x00, 0x24, 0x00, 0x33, 0x00, 0x23, 0x00, 0x32, 0x00,
    0x25, 0x00, 0x34, 0x00, 0x0c, 0x00, 0x10, 0x00, 0x18, 0x00, 0x12, 0x00, 0x19, 0x00, 0x13, 0x00, 0x1a, 0x00, 0x0f,
    0x00, 0x17, 0x00, 0x1e, 0x00, 0x2d, 0x00, 0x1f, 0x00, 0x2e, 0x00, 0x21, 0x00, 0x30, 0x00, 0x22, 0x00, 0x31, 0x00,
    0x26, 0x00, 0x35, 0x00, 0x27, 0x00, 0x36, 0x00, 0x28, 0x00, 0x37, 0x00, 0x29, 0x00, 0x38, 0x00, 0x04, 0x00, 0x41,
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x3a, 0x00, 0x04, 0x00, 0x01, 0x00, 0x01, 0x00, 0x3c, 0x00, 0x02,
    0x00, 0x04, 0x00, 0x02, 0x00, 0x3e, 0x00, 0x03, 0x00, 0x04, 0x00, 0x03, 0x00, 0x40, 0x00, 0x0a, 0x00, 0x08, 0x00,
    0x08, 0x00, 0x0d, 0x00, 0x0e, 0x00, 0x08, 0x00, 0x0e, 0x00, 0x0d, 0x00, 0x0a, 0x00, 0x0d, 0x00, 0x11, 0x00, 0x0d,
    0x00, 0x0a, 0x00, 0x11, 0x00, 0x06, 0x00, 0x0c, 0x00, 0x06, 0x00, 0x14, 0x00, 0x14, 0x00, 0x0c, 0x00, 0x06, 0x00,
    0x15, 0x00, 0x14, 0x00, 0x15, 0x00, 0x16, 0x00, 0x14, 0x00, 0x0c, 0x00, 0x16, 0x00, 0x06, 0x00, 0x08, 0x00, 0x08,
    0x00, 0x1b, 0x00, 0x08, 0x00, 0x1c, 0x00, 0x08, 0x00, 0x1d, 0x00, 0x1c, 0x00, 0x1d, 0x00, 0x1c, 0x00, 0x1b, 0x00,
    0x1c, 0x00, 0x20, 0x00, 0x1b, 0x00, 0x20, 0x00, 0x1b, 0x00, 0x06, 0x00, 0x23, 0x00, 0x1b, 0x00, 0x06, 0x00, 0x23,
    0x00, 0x1b, 0x00, 0x24, 0x00, 0x23, 0x00, 0x24, 0x00, 0x23, 0x00, 0x25, 0x00, 0x06, 0x00, 0x25, 0x00, 0x0a, 0x00,
    0x0c, 0x00, 0x0a, 0x00, 0x2a, 0x00, 0x0a, 0x00, 0x2b, 0x00, 0x0a, 0x00, 0x2c, 0x00, 0x2c, 0x00, 0x2b, 0x00, 0x2a,
    0x00, 0x2b, 0x00, 0x2b, 0x00, 0x2f, 0x00, 0x2f, 0x00, 0x2a, 0x00, 0x0c, 0x00, 0x2a, 0x00, 0x32, 0x00, 0x2a, 0x00,
    0x0c, 0x00, 0x32, 0x00, 0x33, 0x00, 0x2a, 0x00, 0x32, 0x00, 0x33, 0x00, 0x34, 0x00, 0x32, 0x00, 0x0c, 0x00, 0x34,
    0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x39, 0x00, 0x01, 0x00, 0x07, 0x00, 0x01, 0x00, 0x3b, 0x00, 0x02, 0x00,
    0x09, 0x00, 0x02, 0x00, 0x3d, 0x00, 0x03, 0x00, 0x0b, 0x00, 0x03, 0x00, 0x3f, 0x00, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3};

static const uint8_t kTrackViewAnimatedEightPolygonsData[] = {
    0x04, 0x00, 0x14, 0x00, 0x16, 0x00, 0x17, 0x00, 0x15, 0x00, 0x04, 0x00, 0x36, 0x00, 0x38, 0x00, 0x39, 0x00, 0x37,
    0x00, 0x04, 0x00, 0x1a, 0x00, 0x1c, 0x00, 0x1d, 0x00, 0x1b, 0x00, 0x04, 0x00, 0x3c, 0x00, 0x3e, 0x00, 0x3f, 0x00,
    0x3d, 0x00, 0x04, 0x00, 0x20, 0x00, 0x22, 0x00, 0x23, 0x00, 0x21, 0x00, 0x04, 0x00, 0x46, 0x00, 0x48, 0x00, 0x49,
    0x00, 0x47, 0x00, 0x04, 0x00, 0x26, 0x00, 0x28, 0x00, 0x29, 0x00, 0x27, 0x00, 0x04, 0x00, 0x4a, 0x00, 0x4c, 0x00,
    0x4d, 0x00, 0x4b, 0x00, 0x04, 0x00, 0x4e, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x04, 0x00, 0x06, 0x00, 0x01, 0x00, 0x05, 0x00, 0x01, 0x00, 0x09, 0x00, 0x05, 0x00, 0x09, 0x00, 0x07,
    0x00, 0x0a, 0x00, 0x08, 0x00, 0x0b, 0x00, 0x0a, 0x00, 0x0c, 0x00, 0x0d, 0x00, 0x0b, 0x00, 0x0c, 0x00, 0x07, 0x00,
    0x0d, 0x00, 0x08, 0x00, 0x10, 0x00, 0x07, 0x00, 0x11, 0x00, 0x08, 0x00, 0x07, 0x00, 0x12, 0x00, 0x08, 0x00, 0x13,
    0x00, 0x10, 0x00, 0x12, 0x00, 0x13, 0x00, 0x11, 0x00, 0x0c, 0x00, 0x10, 0x00, 0x0d, 0x00, 0x11, 0x00, 0x10, 0x00,
    0x18, 0x00, 0x11, 0x00, 0x19, 0x00, 0x0c, 0x00, 0x18, 0x00, 0x0d, 0x00, 0x19, 0x00, 0x0c, 0x00, 0x0e, 0x00, 0x0d,
    0x00, 0x0f, 0x00, 0x0c, 0x00, 0x1e, 0x00, 0x0d, 0x00, 0x1f, 0x00, 0x0e, 0x00, 0x1e, 0x00, 0x0f, 0x00, 0x1f, 0x00,
    0x0e, 0x00, 0x0a, 0x00, 0x0f, 0x00, 0x0b, 0x00, 0x0e, 0x00, 0x24, 0x00, 0x0f, 0x00, 0x25, 0x00, 0x0a, 0x00, 0x24,
    0x00, 0x0b, 0x00, 0x25, 0x00, 0x03, 0x00, 0x04, 0x00, 0x02, 0x00, 0x05, 0x00, 0x04, 0x00, 0x2a, 0x00, 0x03, 0x00,
    0x2a, 0x00, 0x05, 0x00, 0x2b, 0x00, 0x02, 0x00, 0x2b, 0x00, 0x2c, 0x00, 0x2e, 0x00, 0x2d, 0x00, 0x2f, 0x00, 0x2c,
    0x00, 0x30, 0x00, 0x2d, 0x00, 0x31, 0x00, 0x2c, 0x00, 0x32, 0x00, 0x2d, 0x00, 0x33, 0x00, 0x2c, 0x00, 0x34, 0x00,
    0x2d, 0x00, 0x35, 0x00, 0x32, 0x00, 0x34, 0x00, 0x33, 0x00, 0x35, 0x00, 0x30, 0x00, 0x32, 0x00, 0x31, 0x00, 0x33,
    0x00, 0x3a, 0x00, 0x32, 0x00, 0x33, 0x00, 0x3b, 0x00, 0x30, 0x00, 0x3a, 0x00, 0x31, 0x00, 0x3b, 0x00, 0x30, 0x00,
    0x2e, 0x00, 0x31, 0x00, 0x2f, 0x00, 0x30, 0x00, 0x40, 0x00, 0x31, 0x00, 0x41, 0x00, 0x2e, 0x00, 0x40, 0x00, 0x2f,
    0x00, 0x41, 0x00, 0x30, 0x00, 0x42, 0x00, 0x31, 0x00, 0x43, 0x00, 0x40, 0x00, 0x42, 0x00, 0x41, 0x00, 0x43, 0x00,
    0x40, 0x00, 0x44, 0x00, 0x41, 0x00, 0x45, 0x00, 0x2e, 0x00, 0x44, 0x00, 0x2f, 0x00, 0x45, 0x00, 0x3d, 0x00};

static const uint8_t kTrackViewQuadOutlineData[] = {0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00,
                                                    0x02, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00,
                                                    0x04, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                                    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3d, 0x00};

static const uint8_t kTrackViewTwoConnectedLinesData[] = {0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00,
                                                          0x03, 0x00, 0x03, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                                          0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3d, 0x00};

static const uint8_t kTrackViewTwoCornerLinesData[] = {0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x02, 0x00,
                                                       0x03, 0x00, 0x03, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                                       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00};
static const uint8_t kTrackViewSecondaryColorPolygonsWithLinesData[] = {
    0x53, 0x00, 0x0c, 0x00, 0x02, 0x00, 0x00, 0x00, 0x07, 0x00, 0x01, 0x00, 0x0e, 0x00, 0x05, 0x00, 0x05,
    0x00, 0x0e, 0x00, 0x0c, 0x00, 0x07, 0x00, 0x87, 0xdb, 0x06, 0x00, 0x16, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x1a, 0x00, 0x04, 0x00, 0x01, 0x00, 0x18, 0x00, 0x09, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x03, 0x00, 0x01,
    0x00, 0x19, 0x00, 0x08, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x06, 0x00, 0x01, 0x00, 0x3d, 0x00};

typedef struct TrackViewPrimitiveCallbackContext {
	SlipView3DMatrix viewMatrix;
	SlipView3DVec32 objectPosition;
	SlipView3DVec32 transformedObjectOffset;
	const SlipDraw3DProjectState *projectState;
	const SlipTrackWorldProjectFrustum *frustum;
	uint16_t shapeScaleShift;
	uint16_t shapeScaleRightShift;
	bool shapePath;
} TrackViewPrimitiveCallbackContext;

typedef struct TrackViewMaterialMidpointContext {
	SlipDraw3DVertexRecord *vertexRecords;
	size_t vertexRecordCount;
	TrackViewPrimitiveCallbackContext *primitiveContext;
} TrackViewMaterialMidpointContext;

static SlipDraw3DVec32 TrackView_TransformVertex(uint32_t sourceX, uint32_t sourceY, uint32_t sourceZ,
                                                 SlipDraw3DVertexRecord *record, void *userData);
static SlipView3DVec32 TrackView_SourcePoint(int16_t sourceX, int16_t sourceY, int16_t sourceZ, void *userData);

static void TrackView_ProjectScreenPrimary(SlipDraw3DVec32 world, int32_t *screenX, int32_t *screenY, void *userData);

static void TrackView_ProjectScreenSecondary(SlipDraw3DVec32 world, int32_t *screenX, int32_t *screenY, void *userData);

static SlipDraw3DVec32 TrackView_MaterialMidpoint(uint32_t sourceX, uint32_t sourceY, uint32_t sourceZ,
                                                  SlipDraw3DVertexRecord *record, void *userData);

static SlipView3DVec32 TrackView_MaterialSourcePoint(int16_t sourceX, int16_t sourceY, int16_t sourceZ, void *userData);

static SlipDraw3DVec32 TrackView_MaterialScreenMidpoint(uint32_t sourceX, uint32_t sourceY, uint32_t sourceZ,
                                                        SlipDraw3DVertexRecord *record, void *userData);

static void TrackView_StoreMaterialScreenVertex(SlipDraw3DVertexRecord *record, const SlipDraw3DProjectState *state,
                                                int32_t screenX, int32_t screenY);

static void TrackView_MaterialProjectScreenPrimary(SlipDraw3DVec32 world, int32_t *screenX, int32_t *screenY,
                                                   void *userData);

static void TrackView_MaterialProjectScreenSecondary(SlipDraw3DVec32 world, int32_t *screenX, int32_t *screenY,
                                                     void *userData);

typedef bool (*TrackViewMaterialHandler)(TrackViewRawBspContext *context,
                                         TrackViewPrimitiveCallbackContext *primitiveContext,
                                         const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                         uint16_t materialIndex, uint32_t depth, bool *carryOut);

static bool TrackView_DrawNoOpShadedLinesMaterial(TrackViewRawBspContext *context,
                                                  TrackViewPrimitiveCallbackContext *primitiveContext,
                                                  const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                  uint16_t materialIndex, uint32_t depth, bool *carryOut) {
	(void)context;
	(void)primitiveContext;
	(void)recordIndexStream;
	(void)recordIndexStreamBytes;
	(void)materialIndex;
	(void)depth;
	(void)carryOut;
	return true;
}

static bool TrackView_DrawNoOpDepthLinesMaterial(TrackViewRawBspContext *context,
                                                 TrackViewPrimitiveCallbackContext *primitiveContext,
                                                 const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                 uint16_t materialIndex, uint32_t depth, bool *carryOut) {
	(void)context;
	(void)primitiveContext;
	(void)recordIndexStream;
	(void)recordIndexStreamBytes;
	(void)materialIndex;
	(void)depth;
	(void)carryOut;
	return true;
}

static bool TrackView_DrawNoOpAnimatedPolygonsMaterial(TrackViewRawBspContext *context,
                                                       TrackViewPrimitiveCallbackContext *primitiveContext,
                                                       const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                       uint16_t materialIndex, uint32_t depth, bool *carryOut) {
	(void)context;
	(void)primitiveContext;
	(void)recordIndexStream;
	(void)recordIndexStreamBytes;
	(void)materialIndex;
	(void)depth;
	(void)carryOut;
	return true;
}

static bool TrackView_ExecuteExtendedMaterialDispatch(TrackViewRawBspContext *context,
                                                      TrackViewPrimitiveCallbackContext *primitiveContext,
                                                      const uint8_t *primitiveRecord, size_t recordBytesRemaining,
                                                      uint32_t materialDispatchValue, uint16_t countAndFlags,
                                                      uint16_t materialIndex, uint8_t materialFlags, bool *carryOut);

static bool TrackView_ExecuteHighTexturedCallback(TrackViewRawBspContext *context,
                                                  const TrackViewPrimitiveCallbackContext *primitiveContext,
                                                  const uint8_t *primitiveRecord, size_t recordBytesRemaining,
                                                  size_t recordOffset, uint16_t countAndFlags, uint16_t normalX,
                                                  uint16_t normalY, uint16_t normalZ, uint16_t materialIndex,
                                                  bool *handled, bool *fallbackLow, uint32_t *callbackResult,
                                                  bool *carryOut);

static bool TrackView_PrepareMaterialState(TrackViewRawBspContext *context,
                                           TrackViewPrimitiveCallbackContext *primitiveContext,
                                           const uint8_t *primitiveRecord, size_t recordBytesRemaining,
                                           uint16_t countAndFlags, bool *skipDispatch);

static SlipDraw3DVec32 TrackView_SourceVertex(uint32_t x, uint32_t y, uint32_t z, SlipDraw3DVertexRecord *record,
                                              void *context) {
	(void)record;
	SlipView3DVec32 point = TrackView_SourcePoint((int16_t)x, (int16_t)y, (int16_t)z, context);
	return (SlipDraw3DVec32){point.x, point.y, point.z};
}

static bool TrackView_ExecuteLowEmitPath(TrackViewRawBspContext *context,
                                         const TrackViewPrimitiveCallbackContext *primitiveContext,
                                         const uint8_t *indexStream, size_t indexStreamBytes,
                                         uint16_t polygonCountAndFlags, uint16_t normalX, uint16_t normalY,
                                         uint16_t normalZ, uint16_t materialIndex, bool *carryOut);

TrackViewReplayCallback TrackView_replayCallback;

void TrackView_SetReplayCallback(TrackViewReplayCallback callback) { TrackView_replayCallback = callback; }

bool TrackView_ExecuteReplay(TrackViewRawBspContext *context);

static bool TrackView_ExecuteInlineReplayTail(TrackViewRawBspContext *context,
                                              TrackViewPrimitiveCallbackContext *primitiveContext,
                                              const uint8_t *primitiveRecord, size_t recordBytesRemaining,
                                              uint16_t materialIndex, bool *skippedByCapture);

static bool TrackView_ExecuteObjectEntry(TrackViewRawBspContext *context, const uint8_t *objectRecord,
                                         size_t objectBytesRemaining, uint32_t objectAddress, uint32_t counterBefore);

static bool g_vehicleViewDumpDiagnostics;
static SlipView3DMatrix g_vehicleViewActorMatrix;
static bool g_vehicleViewActorMatrixValid;
static uint64_t g_vehicleViewLastTickMs;
static int g_vehicleViewTimerSuppressedSamples;
static SlipView3DMatrix g_trackSelectGlobeMatrix;
static bool g_trackSelectGlobeMatrixValid;
static int16_t g_vehicleViewFanAngles[SLIP_VEHICLE_PREVIEW_ANIMATED_PART_COUNT];
static int16_t g_vehicleViewJetAngle;
static int16_t g_vehicleViewJetDirection;

static void TrackView_DumpTexturedDispatch(size_t recordOffset, uint32_t inputActiveHeadOffset,
                                           const SlipDraw3DTexturedDispatch *dispatch,
                                           const SlipDraw3DTexturedDispatchVisit *visits, size_t visitCapacity) {
	size_t visitIndex;

	if (!g_vehicleViewDumpDiagnostics || dispatch == NULL || visits == NULL) {
		return;
	}
	fprintf(
	    stderr,
	    "track_view_textured_dispatch_0001ae47 offset=0x%zx active=0x%08x link=0x%08x mode=%u render=0x%08x "
	    "texture=0x%08x target=%u pointbuf=0x%08x points=%zu call30600=%u call308e2=%u call2dc2f=%u call30e48=%u\n",
	    recordOffset, inputActiveHeadOffset, dispatch->linkOffset, dispatch->drawMode, dispatch->renderFlags,
	    dispatch->textureHandle, (unsigned)dispatch->rasterizerCall, dispatch->pointBufferBase, dispatch->pointCount,
	    dispatch->calledOpaqueAffineRasterizer ? 1u : 0u, dispatch->calledTransparentPerspectiveRasterizer ? 1u : 0u,
	    dispatch->calledOpaquePerspectiveRasterizer ? 1u : 0u, dispatch->calledTransparentAffineRasterizer ? 1u : 0u);
	for (visitIndex = 0; visitIndex < dispatch->pointCount && visitIndex < visitCapacity; ++visitIndex) {
		const SlipDraw3DTexturedDispatchVisit *const visit = &visits[visitIndex];

		fprintf(stderr,
		        "  dispatch_visit=%zu record=0x%08x pointbuf=0x%08x screen=%d,%d tex=%08x,%08x depth=%d next=0x%08x "
		        "count=%u loop=%u\n",
		        visitIndex, visit->recordOffset, visit->pointBufferOffset, visit->point.screenX, visit->point.screenY,
		        visit->point.textureU, visit->point.textureV, visit->point.depth, visit->nextRecordOffset,
		        visit->pointCountAfterInc, visit->loop ? 1u : 0u);
	}
}

/* Offsets of captured primitive records selected for vehicle-view diagnostics.
 * These identify resource data records, rather than fields within each record. */
enum { TRACK_VIEW_OPAQUE_DIAGNOSTIC_RECORD = 0x649b };

static bool TrackView_DiagnosticRecordSelected(size_t recordOffset, const uint16_t *records, size_t count) {
	for (size_t index = 0; index < count; ++index) {
		if (recordOffset == records[index])
			return true;
	}
	return false;
}

static bool TrackView_ShouldDumpAffineRecord(size_t recordOffset) {
	static const uint16_t records[] = {
	    0x52b8u, 0x52dcu, 0x5300u, 0x552eu, 0x58fau, 0x591eu, 0x5c0eu, 0x5c2cu, TRACK_VIEW_OPAQUE_DIAGNOSTIC_RECORD,
	    0x64d7u, 0x650du, 0x6619u, 0x6637u};
	return TrackView_DiagnosticRecordSelected(recordOffset, records, sizeof(records) / sizeof(records[0]));
}

static bool TrackView_ShouldDumpMaskedRecord(size_t recordOffset) {
	static const uint16_t records[] = {
	    0x65c9u, 0x65e7u, 0x6619u, 0x6637u, 0x6655u, 0x6673u, 0x6245u,
	    0x6269u, 0x630du, 0x6355u, 0x63a1u, 0x63c5u, 0x6411u, TRACK_VIEW_OPAQUE_DIAGNOSTIC_RECORD,
	    0x64d7u, 0x650du};
	return TrackView_DiagnosticRecordSelected(recordOffset, records, sizeof(records) / sizeof(records[0]));
}

static bool TrackView_ShouldDumpCallbackRecord(size_t recordOffset) {
	static const uint16_t records[] = {0x65c9u, 0x65e7u, 0x6619u, 0x6637u, 0x6655u, 0x6673u, 0x6245u};
	return TrackView_DiagnosticRecordSelected(recordOffset, records, sizeof(records) / sizeof(records[0]));
}

static void TrackView_DumpAffineEntry(size_t recordOffset, const RasterAffineTexturedEntrySetup *affineEntry) {
	if (!g_vehicleViewDumpDiagnostics || affineEntry == NULL || !TrackView_ShouldDumpAffineRecord(recordOffset)) {
		return;
	}
	fprintf(stderr,
	        "track_view_affine_entry_00030600 offset=0x%zx points=%u top=%d bottom=%d left=0x%08x right=0x%08x "
	        "end=0x%08x horizontal=%u call316d3=%u call31764=%u spanloop=%u\n",
	        recordOffset, affineEntry->inputPointCount, affineEntry->topY, affineEntry->bottomY,
	        affineEntry->topLeftPointOffset, affineEntry->topRightPointOffset, affineEntry->endPointOffset,
	        affineEntry->horizontalBranch ? 1u : 0u, affineEntry->calledStepLeftEdgeAffine ? 1u : 0u,
	        affineEntry->calledStepRightEdgeAffine ? 1u : 0u, affineEntry->spanLoopEntry ? 1u : 0u);
}

static const char *TrackView_FindResourceNameForHandle(const TrackViewResourceHandleRegistry *registry,
                                                       uint32_t resourceHandle) {
	size_t i;

	if (registry == NULL || resourceHandle == 0u) {
		return NULL;
	}
	for (i = 0; i < registry->entryCount; ++i) {
		if (registry->entries[i].resourceHandle == resourceHandle) {
			return registry->entries[i].name;
		}
	}
	return NULL;
}

static void TrackView_DumpPayloadBytes(const uint8_t *data, size_t size, size_t byteCount) {
	size_t i;

	if (data == NULL) {
		fprintf(stderr, " <null>");
		return;
	}
	if (byteCount > size) {
		byteCount = size;
	}
	for (i = 0; i < byteCount; ++i) {
		fprintf(stderr, "%s%02x", i == 0u ? "" : " ", data[i]);
	}
}

static void TrackView_CopyScreenSnapshot(uint8_t snapshot[SLIPSTREAM_SCREEN_WIDTH * SLIPSTREAM_SCREEN_HEIGHT]) {
	int y;

	if (snapshot == NULL) {
		return;
	}
	for (y = 0; y < SLIPSTREAM_SCREEN_HEIGHT; ++y) {
		if (g_screenRowPtrs[y] != NULL) {
			memcpy(snapshot + (size_t)y * SLIPSTREAM_SCREEN_WIDTH, g_screenRowPtrs[y], SLIPSTREAM_SCREEN_WIDTH);
		} else {
			memset(snapshot + (size_t)y * SLIPSTREAM_SCREEN_WIDTH, 0, SLIPSTREAM_SCREEN_WIDTH);
		}
	}
}

static void TrackView_DumpRecordDamage(size_t recordOffset, uint32_t dispatchFlags,
                                       const uint8_t before[SLIPSTREAM_SCREEN_WIDTH * SLIPSTREAM_SCREEN_HEIGHT]) {
	static const TrackViewDiagnosticProbePixel probePixels[] = {
	    {59, 68},   {100, 68}, {150, 68}, {59, 80},   {100, 80}, {150, 80},
	    {220, 130}, {239, 94}, {250, 94}, {240, 103}, {280, 71},
	};

	size_t changed = 0;
	unsigned colorCounts[UINT8_MAX + 1] = {0};
	int minX = SLIPSTREAM_SCREEN_WIDTH;
	int minY = SLIPSTREAM_SCREEN_HEIGHT;
	int maxX = -1;
	int maxY = -1;
	unsigned dominantCount = 0;
	unsigned dominantColor = 0;
	size_t vehicleBoxChanged = 0;
	size_t probeIndex;
	int x;
	int y;

	if (!g_vehicleViewDumpDiagnostics || before == NULL) {
		return;
	}
	for (y = 0; y < SLIPSTREAM_SCREEN_HEIGHT; ++y) {
		const uint8_t *const row = g_screenRowPtrs[y];
		if (row == NULL) {
			continue;
		}
		for (x = 0; x < SLIPSTREAM_SCREEN_WIDTH; ++x) {
			const uint8_t afterPixel = row[x];
			if (before[(size_t)y * SLIPSTREAM_SCREEN_WIDTH + (size_t)x] != afterPixel) {
				++changed;
				++colorCounts[afterPixel];
				if (x < minX) {
					minX = x;
				}
				if (x > maxX) {
					maxX = x;
				}
				if (y < minY) {
					minY = y;
				}
				if (y > maxY) {
					maxY = y;
				}
				if (x >= SLIP_DIAGNOSTIC_VEHICLE_BOX_LEFT && x <= SLIP_DIAGNOSTIC_VEHICLE_BOX_RIGHT &&
				    y >= SLIP_DIAGNOSTIC_VEHICLE_BOX_TOP && y <= SLIP_DIAGNOSTIC_VEHICLE_BOX_BOTTOM) {
					++vehicleBoxChanged;
				}
			}
		}
	}
	for (x = 0; x <= UINT8_MAX; ++x) {
		if (colorCounts[x] > dominantCount) {
			dominantCount = colorCounts[x];
			dominantColor = (unsigned)x;
		}
	}
	fprintf(stderr,
	        "track_view_record_damage_00038795 offset=0x%zx dispatch=0x%08x changed=%zu vehicle_box=%zu "
	        "bbox=%d,%d..%d,%d dominant=0x%02x:%u\n",
	        recordOffset, dispatchFlags, changed, vehicleBoxChanged, changed != 0u ? minX : -1,
	        changed != 0u ? minY : -1, changed != 0u ? maxX : -1, changed != 0u ? maxY : -1, dominantColor,
	        dominantCount);
	for (probeIndex = 0; probeIndex < sizeof(probePixels) / sizeof(probePixels[0]); ++probeIndex) {
		const int probeX = probePixels[probeIndex].x;
		const int probeY = probePixels[probeIndex].y;
		const uint8_t beforePixel = before[(size_t)probeY * SLIPSTREAM_SCREEN_WIDTH + (size_t)probeX];
		const uint8_t afterPixel = g_screenRowPtrs[probeY] != NULL ? g_screenRowPtrs[probeY][probeX] : 0u;
		if (beforePixel == afterPixel) {
			continue;
		}
		fprintf(stderr,
		        "track_view_probe_pixel_00038795 offset=0x%zx dispatch=0x%08x xy=%d,%d before=0x%02x after=0x%02x\n",
		        recordOffset, dispatchFlags, probeX, probeY, beforePixel, afterPixel);
	}
}

static void TrackView_DumpAffineTexturePayload(size_t recordOffset, const TrackViewResourceHandleRegistry *registry,
                                               uint32_t resourceHandle, const SlipResourcePayload *payload,
                                               uint32_t rowScroll, const RasterTextureRowTable *rowTable) {
	const char *resourceName;

	if (!g_vehicleViewDumpDiagnostics || payload == NULL || !TrackView_ShouldDumpAffineRecord(recordOffset)) {
		return;
	}
	resourceName = TrackView_FindResourceNameForHandle(registry, resourceHandle);
	fprintf(stderr,
	        "track_view_affine_texture_000248a5 offset=0x%zx handle=0x%08x name=%s size=%zu row_scroll=0x%08x width=%u "
	        "height=%u rotated=%u rows=%zu direct=%u rotated_path=%u bytes=",
	        recordOffset, resourceHandle, resourceName != NULL ? resourceName : "<missing>", payload->size, rowScroll,
	        rowTable != NULL ? rowTable->textureWidth : 0u, rowTable != NULL ? rowTable->textureHeight : 0u,
	        rowTable != NULL ? rowTable->rotationRowOffset : 0u, rowTable != NULL ? rowTable->rowsWritten : 0u,
	        rowTable != NULL && rowTable->directRows ? 1u : 0u,
	        rowTable != NULL && rowTable->usedRotatedRows ? 1u : 0u);
	TrackView_DumpPayloadBytes(payload->data, payload->size, SLIP_VEHICLE_DIAGNOSTIC_PAYLOAD_DUMP_BYTES);
	fprintf(stderr, "\n");
}

static void TrackView_DumpAffineLoop(size_t recordOffset, const RasterAffineScanlineLoop *loop,
                                     const RasterAffineScanlineLoopVisit *visits) {
	if (!g_vehicleViewDumpDiagnostics || loop == NULL || !TrackView_ShouldDumpAffineRecord(recordOffset)) {
		return;
	}
	fprintf(stderr,
	        "track_view_affine_loop_00030676 offset=0x%zx start_y=%d bottom=%d left=0x%08x right=0x%08x left_x=%d "
	        "right_x=%d visits=%zu final=%u left_carry=%u right_carry=%u\n",
	        recordOffset, loop->in.scanline, loop->in.bottomY, loop->in.leftPointOffset, loop->in.rightPointOffset,
	        loop->in.leftX, loop->in.rightX, loop->visitCount, loop->calledFinalTexturedSpanCore ? 1u : 0u,
	        loop->jumpFromLeftCarry ? 1u : 0u, loop->jumpFromRightCarry ? 1u : 0u);
	if (visits != NULL && loop->visitCount != 0u) {
		const RasterAffineScanlineLoopVisit *const firstVisit = &visits[0];
		const RasterAffineScanlineLoopVisit *const lastVisit = &visits[loop->visitCount - 1u];

		fprintf(stderr,
		        "  affine_loop_first y=%d left=%d right=%d pixels=%u tested=%zu written=%zu tex_l=%08x,%08x "
		        "tex_r=%08x,%08x\n",
		        firstVisit->scanline, firstVisit->spanCoreLeftX, firstVisit->spanCoreRightX,
		        firstVisit->spanCore.pixelCount, firstVisit->spanCore.pixelsTested, firstVisit->spanCore.pixelsWritten,
		        firstVisit->spanLeftTexUBefore, firstVisit->spanLeftTexVBefore, firstVisit->spanRightTexUBefore,
		        firstVisit->spanRightTexVBefore);
		fprintf(stderr,
		        "  affine_loop_last y=%d left=%d right=%d pixels=%u tested=%zu written=%zu tex_l=%08x,%08x "
		        "tex_r=%08x,%08x final_pixels=%zu\n",
		        lastVisit->scanline, lastVisit->spanCoreLeftX, lastVisit->spanCoreRightX,
		        lastVisit->spanCore.pixelCount, lastVisit->spanCore.pixelsTested, lastVisit->spanCore.pixelsWritten,
		        lastVisit->spanLeftTexUBefore, lastVisit->spanLeftTexVBefore, lastVisit->spanRightTexUBefore,
		        lastVisit->spanRightTexVBefore, loop->finalSpanCore.pixelsWritten);
	}
}

static void TrackView_DumpMaskedRaster(size_t recordOffset, const RasterMaskedPerspectiveTexturedPolygon *masked,
                                       const RasterMaskedPerspectiveTexturedPolygonVisit *visits,
                                       size_t visitCapacity) {
	size_t visitIndex;
	size_t visitLimit;

	if (!g_vehicleViewDumpDiagnostics || masked == NULL || visits == NULL) {
		return;
	}
	if (!TrackView_ShouldDumpMaskedRecord(recordOffset)) {
		return;
	}
	fprintf(stderr,
	        "track_view_masked_0002dc2f offset=0x%zx points=%u top=%d bottom=%d left=0x%08x right=0x%08x end=0x%08x "
	        "visits=%zu spans=%zu short=%zu long=%zu pixels=%zu initial_left=%u initial_right=%u\n",
	        recordOffset, masked->inputPointCount, masked->topY, masked->bottomY, masked->topLeftPointOffset,
	        masked->topRightPointOffset, masked->pointBufferEnd, masked->visitCount, masked->spanDispatchCount,
	        masked->shortSpanCount, masked->longSpanCount, masked->pixelsWritten,
	        masked->initialCalledStepLeftEdgeTexturedPerspective ? 1u : 0u,
	        masked->initialCalledStepRightEdgeTexturedPerspective ? 1u : 0u);
	if (masked->initialCalledStepLeftEdgeTexturedPerspective) {
		const RasterTexturedLeftEdgeStep *const left = &masked->initialLeftEdge;

		fprintf(stderr,
		        "  initial_left point_in=0x%08x current=%d,%d candidate=0x%08x,%d,%d height=%u rem=%u xstep=%d frac=%u "
		        "accum=%08x,%08x step=%08x,%08x out=0x%08x carry=%u\n",
		        left->pointOffsetIn, left->currentX, left->currentY, left->candidateOffset, left->candidateX,
		        left->candidateY, left->edgeHeight, left->remaining, left->xStep, left->xFraction,
		        left->accumulatedBaseDepthPerRow, left->accumulatedNextPointDepthPerRow, left->baseDepthPerRow,
		        left->nextPointDepthPerRow, left->pointOffsetOut, left->carryOut ? 1u : 0u);
	}
	if (masked->initialCalledStepRightEdgeTexturedPerspective) {
		const RasterTexturedRightEdgeStep *const right = &masked->initialRightEdge;

		fprintf(stderr,
		        "  initial_right point_in=0x%08x current=%d,%d candidate=0x%08x,%d,%d height=%u rem=%u xstep=%d "
		        "frac=%u accum=%08x,%08x step=%08x,%08x out=0x%08x carry=%u\n",
		        right->pointOffsetIn, right->currentX, right->currentY, right->candidateOffset, right->candidateX,
		        right->candidateY, right->edgeHeight, right->remaining, right->xStep, right->xFraction,
		        right->accumulatedBaseDepthPerRow, right->accumulatedNextPointDepthPerRow, right->baseDepthPerRow,
		        right->nextPointDepthPerRow, right->pointOffsetOut, right->carryOut ? 1u : 0u);
	}
	visitLimit = masked->visitCount;
	if (visitLimit > visitCapacity) {
		visitLimit = visitCapacity;
	}
	if (visitLimit > SLIP_MASKED_RASTER_DIAGNOSTIC_VISIT_LIMIT) {
		visitLimit = SLIP_MASKED_RASTER_DIAGNOSTIC_VISIT_LIMIT;
	}
	for (visitIndex = 0; visitIndex < visitLimit; ++visitIndex) {
		const RasterMaskedPerspectiveTexturedPolygonVisit *const visit = &visits[visitIndex];

		fprintf(stderr,
		        "  masked_visit=%zu y=%d span=%d..%d left=0x%08x right=0x%08x setup=%u call_span=%u neg=%d width=%u "
		        "short=%u long=%u b_ea0f=%u b_df15=%u b_ffe7=%u tex=%08x,%08x..%08x,%08x depth=%08x..%08x fail=%u "
		        "fail_run=%u fail_pix=%u fail_tex=%08x,%08x fail_xy=%u,%u pixels=%zu adv=%u adv_out_y=%d adv_left=%d "
		        "adv_right=%d left_step=%u left_x=%d left_out=0x%08x left_carry=%u right_step=%u right_x=%d "
		        "right_out=0x%08x right_carry=%u\n",
		        visitIndex, visit->scanline, visit->spanLeftX, visit->spanRightX, visit->leftPointOffset,
		        visit->rightPointOffset, visit->calledSetupPerspectiveTexturedSpan ? 1u : 0u,
		        visit->calledDispatchTexturedSpan ? 1u : 0u, visit->spanDispatch.spanNegatedDelta,
		        visit->spanDispatch.inclusiveWidth, visit->spanDispatch.jumpShortSpan ? 1u : 0u,
		        visit->spanDispatch.longSpanBranch ? 1u : 0u, visit->spanDispatch.lowDepthRatioBranch ? 1u : 0u,
		        visit->spanDispatch.fourSegmentBranch ? 1u : 0u, visit->spanDispatch.twoSegmentBranch ? 1u : 0u,
		        visit->spanDispatch.in.startTexU, visit->spanDispatch.in.startTexV, visit->spanDispatch.in.endTexU,
		        visit->spanDispatch.in.endTexV, visit->spanDispatch.in.startDepth, visit->spanDispatch.in.endDepth,
		        visit->spanDispatch.longSpanFailStage, visit->spanDispatch.longSpanFailRun,
		        visit->spanDispatch.longSpanFailPixel, visit->spanDispatch.longSpanFailTexU,
		        visit->spanDispatch.longSpanFailTexV, visit->spanDispatch.longSpanFailTexX,
		        visit->spanDispatch.longSpanFailTexY, visit->pixelsWritten,
		        visit->calledAdvancePerspectiveTexturedEdges ? 1u : 0u, visit->advance.out.scanY,
		        visit->advance.out.leftX, visit->advance.out.rightX,
		        visit->calledStepLeftEdgeTexturedPerspective ? 1u : 0u, visit->leftEdge.currentX,
		        visit->leftEdge.pointOffsetOut, visit->leftEdge.carryOut ? 1u : 0u,
		        visit->calledStepRightEdgeTexturedPerspective ? 1u : 0u, visit->rightEdge.currentX,
		        visit->rightEdge.pointOffsetOut, visit->rightEdge.carryOut ? 1u : 0u);
	}
}

static void TrackView_DumpSpanDispatch(const char *label, size_t index, const RasterTexturedSpanDispatch *dispatch) {
	if (dispatch == NULL) {
		return;
	}
	fprintf(stderr,
	        "  %s=%zu y=%u span=%d..%d width=%u short=%u long=%u b_ea0f=%u b_df15=%u b_ffe7=%u pixels=%zu "
	        "tex=%08x,%08x..%08x,%08x depth=%08x..%08x recip=0x%08x\n",
	        label, index, dispatch->in.scanlineIndex, dispatch->in.spanLeftX, dispatch->in.spanRightX,
	        dispatch->inclusiveWidth, dispatch->jumpShortSpan ? 1u : 0u, dispatch->longSpanBranch ? 1u : 0u,
	        dispatch->lowDepthRatioBranch ? 1u : 0u, dispatch->fourSegmentBranch ? 1u : 0u,
	        dispatch->twoSegmentBranch ? 1u : 0u,
	        dispatch->jumpShortSpan ? dispatch->shortSpanCore.pixelsWritten : dispatch->longSpanPixelsWritten,
	        dispatch->in.startTexU, dispatch->in.startTexV, dispatch->in.endTexU, dispatch->in.endTexV,
	        dispatch->in.startDepth, dispatch->in.endDepth, dispatch->reciprocalBucket);
}

static void TrackView_DumpOpaquePerspective(size_t recordOffset, const RasterOpaquePerspectiveTexturedPolygon *opaque,
                                            const RasterOpaquePerspectiveTexturedVisit *visits, size_t visitCapacity) {
	size_t visitIndex;
	size_t visitLimit;

	if (!g_vehicleViewDumpDiagnostics || opaque == NULL || visits == NULL ||
	    recordOffset != TRACK_VIEW_OPAQUE_DIAGNOSTIC_RECORD) {
		return;
	}
	fprintf(stderr,
	        "track_view_opaque_00030978 offset=0x%zx points=%u top=%d bottom=%d left=0x%08x right=0x%08x end=0x%08x "
	        "visits=%zu spans=%zu rows=%zu bytes=%zu pixels=%zu initial_left=%u initial_right=%u\n",
	        recordOffset, opaque->in.pointCount, opaque->in.topY, opaque->in.bottomY, opaque->in.topLeftPointOffset,
	        opaque->in.topRightPointOffset, opaque->in.pointBufferEnd, opaque->visitCount, opaque->spanDispatchCount,
	        opaque->rowCopyCount, opaque->rowCopyBytes, opaque->pixelsWritten,
	        opaque->initialCalledStepLeftEdgeTexturedPerspective ? 1u : 0u,
	        opaque->initialCalledStepRightEdgeTexturedPerspective ? 1u : 0u);
	TrackView_DumpSpanDispatch("opaque_initial_span", 0, &opaque->initialSpanDispatch);
	visitLimit = opaque->visitCount;
	if (visitLimit > visitCapacity) {
		visitLimit = visitCapacity;
	}
	for (visitIndex = 0; visitIndex < visitLimit; ++visitIndex) {
		const RasterOpaquePerspectiveTexturedVisit *const visit = &visits[visitIndex];

		if (visit->spanDispatch.in.screenRow != NULL) {
			TrackView_DumpSpanDispatch("opaque_visit_span", visitIndex, &visit->spanDispatch);
		}
		if (visit->terminalDispatch) {
			TrackView_DumpSpanDispatch("opaque_terminal_span", visitIndex, &visit->terminalSpanDispatch);
		}
	}
}

static bool TrackView_ApplyLoadedDrawState(TrackViewRawBspContext *context, uint32_t recordIndex) {
	SlipDraw3DStateLoad load;
	const uint16_t stateIndex = (uint16_t)recordIndex;

	if (context == NULL) {
		return false;
	}
	if (context->vertexBufferBase == NULL || context->drawStateRecords == NULL) {
		context->recordIndex = (uint32_t)stateIndex;
		return true;
	}
	if ((size_t)stateIndex >= context->drawStateRecordCount ||
	    !SlipDraw3D_LoadStateRecord(context->drawStateRecords, context->drawStateRecordCount, stateIndex, &load)) {
		return false;
	}
	context->recordIndex = load.recordIndex;
	context->drawStateRecord = (SlipDraw3DStateRecord *)load.recordPointer;
	if (!TrackView_SetActiveVertexBuffer(context, load.recordPointer->vertexBufferCursor)) {
		return false;
	}

	context->transform = load.recordPointer->transform;
	context->sourcePoint = load.recordPointer->sourcePoint;
	if (context->hostRenderer != NULL) {
		SlipRendererDrawState *const state = &context->hostRenderer->states[stateIndex];
		state->vertices =
		    context->hostRenderer->vertexBase + load.recordPointer->vertexBufferCursor / SLIP_DRAW3D_VERTEX_RECORD_SIZE;
		state->transform = context->transform;
		state->source = context->sourcePoint;
		state->origin = load.recordPointer->origin;
		state->light = load.recordPointer->lightVector;
		state->matrix = load.recordPointer->matrix;
		SlipRenderer_SelectState(context->hostRenderer, stateIndex);
	}

	context->origin.x = load.recordPointer->origin.x;
	context->origin.y = load.recordPointer->origin.y;
	context->origin.z = load.recordPointer->origin.z;
	return true;
}

bool TrackView_LoadDrawState(uint32_t recordIndex, void *userData) {
	return TrackView_ApplyLoadedDrawState((TrackViewRawBspContext *)userData, recordIndex);
}

static size_t TrackView_VertexBufferCapacityBytes(const TrackViewRawBspContext *context) {
	if (context == NULL || context->vertexBufferBase == NULL || context->vertexBufferRecordCapacity == 0) {
		return 0;
	}
	return context->vertexBufferRecordCapacity * sizeof(context->vertexBufferBase[0]);
}

static bool TrackView_SetActiveVertexBuffer(TrackViewRawBspContext *context, uint32_t cursor) {
	size_t capacityBytes;

	if (context == NULL || context->vertexBufferBase == NULL || cursor % SLIP_DRAW3D_VERTEX_RECORD_SIZE != 0) {
		return false;
	}
	capacityBytes = TrackView_VertexBufferCapacityBytes(context);
	if ((size_t)cursor > capacityBytes) {
		return false;
	}
	context->vertexRecords = context->vertexBufferBase + (size_t)(cursor / SLIP_DRAW3D_VERTEX_RECORD_SIZE);
	context->vertexRecordCount =
	    context->vertexBufferRecordCapacity - (size_t)(cursor / SLIP_DRAW3D_VERTEX_RECORD_SIZE);
	return true;
}

static bool TrackView_RestoreVertexBufferCursor(TrackViewRawBspContext *context, uint32_t callerAddress) {
	SlipDraw3DRestoreVertexBufferCursor restore;
	uint32_t stateCursor = 0;

	if (context == NULL ||
	    !SlipDraw3D_RestoreVertexBufferCursor(context->vertexBufferCursor, context->drawStateRecord, &restore)) {
		return false;
	}
	if (context->drawStateRecord != NULL) {
		stateCursor = context->drawStateRecord->vertexBufferCursor;
	}
	if (g_vehicleViewDumpDiagnostics) {
		fprintf(stderr,
		        "track_view_restore_vertex_scratch_cursor_0001de44 caller=0x%08x bp=%u cursor_before=0x%08x "
		        "state_cursor=0x%08x discarded=0x%08x\n",
		        callerAddress, context->recordIndex, context->vertexBufferCursor, stateCursor,
		        restore.discardedVertexBufferBytes);
	}
	context->vertexBufferCursor = restore.vertexBufferCursorAfter;
	if (context->hostRenderer != NULL)
		context->hostRenderer->vertexCursor =
		    context->hostRenderer->vertexBase + context->vertexBufferCursor / SLIP_DRAW3D_VERTEX_RECORD_SIZE;

	return true;
}

static bool TrackView_MaterialStatePop(TrackViewRawBspContext *context) {
	uint32_t nextRecordIndex;

	if (context == NULL) {
		return false;
	}
	if (g_vehicleViewDumpDiagnostics) {
		fprintf(stderr,
		        "track_view_material_state_pop_0003f4e0 enter bp=%u cursor=0x%08x state_ptr=%p "
		        "frames=%p frame_count=%zu\n",
		        context->recordIndex, context->vertexBufferCursor, (void *)context->drawStateRecord,
		        (void *)context->vertexBufferBase, context->drawStateRecordCount);
	}
	if (!TrackView_RestoreVertexBufferCursor(context, TRACK_VIEW_DIAGNOSTIC_MATERIAL_STATE_POP)) {
		if (g_vehicleViewDumpDiagnostics) {
			fprintf(stderr,
			        "track_view_material_state_pop_0003f4e0 restore_failed bp=%u cursor=0x%08x state_ptr=%p "
			        "\n",
			        context->recordIndex, context->vertexBufferCursor, (void *)context->drawStateRecord);
		}
		return false;
	}
	nextRecordIndex = SlipDraw3D_CurrentStateRecordIndex(context->recordIndex);
	if (nextRecordIndex != 0u) {
		--nextRecordIndex;
	}
	if (g_vehicleViewDumpDiagnostics) {
		fprintf(stderr, "track_view_material_state_pop_0003f4e0 after_restore cursor=0x%08x next_bp=%u\n",
		        context->vertexBufferCursor, nextRecordIndex);
	}
	if (!TrackView_ApplyLoadedDrawState(context, nextRecordIndex)) {
		if (g_vehicleViewDumpDiagnostics) {
			fprintf(stderr, "track_view_material_state_pop_0003f4e0 load_failed next_bp=%u frame_count=%zu\n",
			        nextRecordIndex, context->drawStateRecordCount);
		}
		return false;
	}
	if (g_vehicleViewDumpDiagnostics) {
		fprintf(stderr, "track_view_material_state_pop_0003f4e0 done bp=%u cursor=0x%08x\n", context->recordIndex,
		        context->vertexBufferCursor);
	}
	return true;
}

typedef struct TrackViewPostPlaneArgs {
	int hasPostPlanes;
	const uint8_t *planeBase;
	size_t planeBytes;
	uint32_t planeHeadOffset;
	int32_t limitXMin;
	int32_t limitXMax;
	int32_t limitYMin;
	int32_t limitYMax;
} TrackViewPostPlaneArgs;

static TrackViewPostPlaneArgs TrackView_PostPlaneArgs(const TrackViewRawBspContext *context) {
	TrackViewPostPlaneArgs args = {0};

	if (context == NULL || context->postPlaneHead == 0 || context->drawRecordPool == NULL) {
		return args;
	}
	args.planeBase = SlipDraw3D_RecordPoolConstBytes(context->drawRecordPool);
	args.planeBytes = SlipDraw3D_RecordPoolByteSize();
	if (args.planeBase == NULL) {
		return (TrackViewPostPlaneArgs){0};
	}
	args.hasPostPlanes = 1;
	args.planeHeadOffset = context->postPlaneHead;
	args.limitXMin = context->postLimitXMin;
	args.limitXMax = context->postLimitXMax;
	args.limitYMin = context->postLimitYMin;
	args.limitYMax = context->postLimitYMax;
	return args;
}

static bool TrackView_StoreClipBounds(TrackViewRawBspContext *context, uint32_t minX, uint32_t minY, uint32_t maxX,
                                      uint32_t maxY) {
	SlipDraw3DRefreshMode0Projection refresh;

	if (context == NULL || context->projectState == NULL) {
		return false;
	}
	SlipDraw3D_StoreClipBounds(context->projectState, minX, minY, maxX, maxY);
	if (context->mode != SLIP_DRAW3D_PROJECTION_PERSPECTIVE) {
		return true;
	}
	if (!SlipDraw3D_RefreshProjectFrustum(context->projectState, context->mode, &refresh)) {
		return false;
	}
	context->frustum = (SlipTrackWorldProjectFrustum){refresh.maxXStep,
	                                                  refresh.minXStep,
	                                                  refresh.minYStep,
	                                                  refresh.maxYStep,
	                                                  refresh.minXPlaneDepthQ,
	                                                  refresh.minXPlaneNegXQ,
	                                                  refresh.maxXPlaneNegDepthQ,
	                                                  refresh.maxXPlaneXQ,
	                                                  refresh.maxYPlaneDepthQ,
	                                                  refresh.maxYPlaneYQ,
	                                                  refresh.minYPlaneNegDepthQ,
	                                                  refresh.minYPlaneNegYQ,
	                                                  context->projectState->minZ,
	                                                  context->projectState->maxZ};
	return true;
}

bool TrackView_StoreClipBoundsCallback(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY, void *userData) {
	return TrackView_StoreClipBounds((TrackViewRawBspContext *)userData, minX, minY, maxX, maxY);
}

static bool TrackView_BuildVertexRecords(TrackViewRawBspContext *context, uint32_t callerAddress,
                                         const uint8_t *vertexSource, size_t sourceBytes, uint16_t vertexCount,
                                         int16_t sourceStride, SlipDraw3DTransformFn transform,
                                         SlipDraw3DSourcePointFn sourcePoint, SlipDraw3DBuildVertexRecords *result) {
	size_t capacityBytes;
	size_t recordOffset;
	size_t recordCapacity;
	uint32_t cursorBefore;

	if (context == NULL || vertexSource == NULL || context->vertexBufferBase == NULL ||
	    context->drawStateRecord == NULL) {
		return false;
	}
	if (context->hostRenderer != NULL) {
		SlipRendererState *const renderer = context->hostRenderer;

		SlipRendererDrawState *const state = &renderer->states[context->recordIndex];
		state->vertices =
		    renderer->vertexBase + context->drawStateRecord->vertexBufferCursor / SLIP_DRAW3D_VERTEX_RECORD_SIZE;
		state->transform = context->drawStateRecord->transform;
		state->source = context->drawStateRecord->sourcePoint;
		state->origin = context->drawStateRecord->origin;
		state->light = context->drawStateRecord->lightVector;
		state->matrix = context->drawStateRecord->matrix;
		renderer->vertexCursor = renderer->vertexBase + context->vertexBufferCursor / SLIP_DRAW3D_VERTEX_RECORD_SIZE;
		SlipRenderer_SelectState(renderer, context->recordIndex);
		SlipRenderer_BuildVertices(renderer, vertexSource, vertexCount, sourceStride, transform, sourcePoint,
		                           &SlipRendererHost_allocationCalls);
		context->vertexBufferBase = renderer->vertexBase;
		context->vertexBufferRecordCapacity = renderer->vertexCapacity;
		context->vertexBufferLimit = renderer->vertexCapacity * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
		context->vertexBufferCursor =
		    (uint32_t)(renderer->vertexCursor - renderer->vertexBase) * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
		context->vertexRecords = renderer->activeVertices;
		context->vertexRecordCount = (size_t)(renderer->vertexLimit - renderer->activeVertices);
		context->drawStateRecord->vertexBufferCursor =
		    (uint32_t)(renderer->activeVertices - renderer->vertexBase) * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
		context->drawStateRecord->transform = transform;
		context->drawStateRecord->sourcePoint = sourcePoint;
		context->transform = transform;
		context->sourcePoint = sourcePoint;
		if (result != NULL) {
			memset(result, 0, sizeof(*result));
			result->vertexBufferCursorAfter = context->vertexBufferCursor;
		}
		return true;
	}
	capacityBytes = TrackView_VertexBufferCapacityBytes(context);
	cursorBefore = context->vertexBufferCursor;
	if (context->vertexBufferLimit == 0 || (size_t)context->vertexBufferLimit > capacityBytes) {
		context->vertexBufferLimit = (uint32_t)capacityBytes;
	}
	if (!TrackView_SetActiveVertexBuffer(context, context->vertexBufferCursor)) {
		return false;
	}
	recordOffset = (size_t)(context->vertexBufferCursor / SLIP_DRAW3D_VERTEX_RECORD_SIZE);
	recordCapacity = capacityBytes / sizeof(context->vertexBufferBase[0]) - recordOffset;
	if (!SlipDraw3D_BuildVertexRecords(context->vertexRecords, recordCapacity, vertexSource, sourceBytes, vertexCount,
	                                   sourceStride, transform, sourcePoint, context->drawStateRecord,
	                                   context->vertexBufferCursor, context->vertexBufferLimit, result)) {
		return false;
	}

	context->transform = transform;
	context->sourcePoint = sourcePoint;
	context->vertexBufferCursor =
	    result != NULL ? result->vertexBufferCursorAfter
	                   : context->vertexBufferCursor + (uint32_t)vertexCount * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
	if (g_vehicleViewDumpDiagnostics) {
		fprintf(stderr,
		        "track_view_build_vertex_records_0001dcf7 caller=0x%08x bp=%u cursor_before=0x%08x cursor_after=0x%08x "
		        "active=0x%08zx count=%u stride=%d transform=%p source_callback=%p\n",
		        callerAddress, context->recordIndex, cursorBefore, context->vertexBufferCursor, recordOffset,
		        (unsigned)vertexCount, (int)sourceStride, (void *)transform, (void *)sourcePoint);
	}
	return true;
}

bool TrackView_BuildChunkVertexRecords(const uint8_t *vertexSource, size_t sourceBytes, uint16_t vertexCount,
                                       int16_t sourceStride, void *userData) {
	return TrackView_BuildVertexRecords(
	    (TrackViewRawBspContext *)userData, TRACK_VIEW_DIAGNOSTIC_CHILD_BUILD_VERTEX_RECORDS, vertexSource, sourceBytes,
	    vertexCount, sourceStride, TrackView_ChunkTransformPoint, TrackView_ChunkSourcePoint, NULL);
}

bool TrackView_RestoreChunkVertexBuffer(void *userData) {
	return TrackView_RestoreVertexBufferCursor((TrackViewRawBspContext *)userData,
	                                           TRACK_VIEW_DIAGNOSTIC_CHILD_RESTORE_VERTEX_CURSOR);
}

bool TrackView_BuildComponentVertexRecords(const uint8_t *vertexSource, size_t sourceBytes, uint16_t vertexCount,
                                           int16_t sourceStride, void *userData) {
	return TrackView_BuildVertexRecords(
	    (TrackViewRawBspContext *)userData, TRACK_VIEW_DIAGNOSTIC_COMPONENT_BUILD_VERTEX_RECORDS, vertexSource,
	    sourceBytes, vertexCount, sourceStride, TrackView_TransformVertex, TrackView_SourcePoint, NULL);
}

bool TrackView_RestoreComponentVertexBuffer(void *userData) {
	return TrackView_RestoreVertexBufferCursor((TrackViewRawBspContext *)userData,
	                                           TRACK_VIEW_DIAGNOSTIC_COMPONENT_RESTORE_VERTEX_CURSOR);
}

bool TrackView_ApplyComponentLight(void *userData, const SlipTrackWorldComponentTail *tail) {
	TrackViewRawBspContext *const context = userData;
	SlipTrackWorldScaledCallSetup scaledCall;

	if (context == NULL) {
		return false;
	}
	if (tail == NULL || !tail->calledScaleLight) {
		return true;
	}
	if (!SlipTrackWorld_ScaledCallSetup(tail->shade, context->ambientLightScaleQ14, context->directLightScaleQ14,
	                                    context->scaledLightX, context->scaledLightY, context->scaledLightZ,
	                                    &scaledCall)) {
		return false;
	}

	SlipDraw3D_SetAmbientLight((uint16_t)scaledCall.ambientLightTo);
	context->directLight = SlipDraw3D_directLight;
	context->ambientLight = SlipDraw3D_ambientLight;

	context->lightInput = (SlipView3DVec32){(int32_t)scaledCall.lightDirectionX, (int32_t)scaledCall.lightDirectionY,
	                                        (int32_t)scaledCall.lightDirectionZ};

	SlipDraw3D_SetLightVector((int32_t)scaledCall.lightDirectionX, (int32_t)scaledCall.lightDirectionY,
	                          (int32_t)scaledCall.lightDirectionZ, scaledCall.directLightScaleTo);
	context->directLight = SlipDraw3D_directLight;
	context->ambientLight = SlipDraw3D_ambientLight;
	if (g_vehicleViewDumpDiagnostics) {
		fprintf(stderr,
		        "track_view_scaled_light_00039427 cx=0x%04x ambient=0x%08x direct=0x%08x g33e48=0x%08x "
		        "g33e4c=0x%08x\n",
		        (unsigned)tail->shade, context->ambientLight, context->directLight, context->ambientLightScaleQ14,
		        context->directLightScaleQ14);
	}
	return true;
}

bool TrackView_RestoreComponentLight(void *userData, const SlipTrackWorldComponentTail *tail) {
	TrackViewRawBspContext *const context = userData;
	if (context == NULL) {
		return false;
	}
	if (tail == NULL || !tail->callDraw3DSetAmbientLight) {
		return true;
	}

	SlipDraw3D_SetAmbientLight((uint16_t)tail->ambientLightScaleQ14);
	context->directLight = SlipDraw3D_directLight;
	context->ambientLight = SlipDraw3D_ambientLight;
	if (!tail->callDraw3DSetLightVector) {
		return true;
	}

	context->lightInput =
	    (SlipView3DVec32){(int32_t)tail->scaledLightX, (int32_t)tail->scaledLightY, (int32_t)tail->scaledLightZ};
	SlipDraw3D_SetLightVector((int32_t)tail->scaledLightX, (int32_t)tail->scaledLightY, (int32_t)tail->scaledLightZ,
	                          (uint16_t)tail->directLightScaleQ14);
	context->directLight = SlipDraw3D_directLight;
	context->ambientLight = SlipDraw3D_ambientLight;
	return true;
}

static bool TrackView_SlotDrawRecord(TrackViewRawBspContext *context, uint32_t recordAddress, uint8_t **record) {
	uint32_t recordOffset;

	if (context == NULL || record == NULL || context->slotDrawBase == NULL ||
	    recordAddress < context->slotDrawBaseAddress) {
		return false;
	}
	recordOffset = recordAddress - context->slotDrawBaseAddress;
	if ((size_t)recordOffset + SLIP_TRACK_DRAW_RECORD_BYTES > context->slotDrawBytes ||
	    recordOffset % SLIP_TRACK_DRAW_RECORD_BYTES != 0) {
		return false;
	}
	*record = context->slotDrawBase + recordOffset;
	return true;
}

static bool TrackView_ScheduleSlotDraw(TrackViewRawBspContext *context, SlipDraw3DListState *drawList,
                                       SlipDraw3DListNode *nodePool, size_t nodePoolBytes, uint8_t *drawRecord,
                                       uint32_t drawRecordAddress) {
	SlipTrackWorldDrawSchedule schedule;
	SlipView3DVec32 viewPosition;
	uint16_t objectOffset;
	SlipObject *object;

	objectOffset = (uint16_t)SlipBytes_ReadLE32(drawRecord + offsetof(SlipTrackDrawRecord, objectOffset));
	if ((size_t)objectOffset + SLIP_OBJECT_DOS_STRIDE > context->objectTableBytes ||
	    objectOffset % SLIP_OBJECT_DOS_STRIDE != 0) {
		return false;
	}
	object = &context->objectTable[objectOffset / SLIP_OBJECT_DOS_STRIDE];

	if ((object->flags & SLIP_OBJECT_RENDER_HIDDEN) != 0) {
		return true;
	}
	if (!SlipObject_ViewPosition(context->objectTable, context->objectTableBytes, objectOffset, &viewPosition)) {
		return false;
	}
	return SlipTrackWorld_ScheduleDrawCallback(drawList, nodePool, nodePoolBytes, (uint32_t)viewPosition.z,
	                                           drawRecordAddress, context->slotDrawBaseAddress, &schedule);
}

bool TrackView_DrawSlotRecord(TrackViewRawBspContext *context, uint32_t drawPayload) {
	SlipTrackWorldDrawCallbackHeader header;
	SlipTrackWorldInvokeDrawCallbackExecute execute;
	SlipObjectDrawCallback callback;
	SlipTrackWorldDrawFlags drawFlags;
	SlipView3DVec32 viewPosition;
	uint8_t *drawRecord;
	uint32_t drawRecordAddress;
	uint32_t drawRecordOffset;
	size_t drawRecordIndex;
	uint16_t objectOffset;
	uint32_t savedRecordIndex;
	bool attachmentEnabled = false;
	bool callbackResult;
	bool restoreResult;

	drawRecordAddress = context->slotDrawBaseAddress + (uint16_t)drawPayload;
	if (!TrackView_SlotDrawRecord(context, drawRecordAddress, &drawRecord)) {
		return false;
	}
	drawRecordOffset = drawRecordAddress - context->slotDrawBaseAddress;
	drawRecordIndex = drawRecordOffset / SLIP_TRACK_DRAW_RECORD_BYTES;
	if (context->slotDrawCallbacks == NULL || drawRecordIndex >= context->slotDrawCallbackCount) {
		return false;
	}
	const SlipTrackDrawRecord *const draw = (const SlipTrackDrawRecord *)(const void *)drawRecord;
	objectOffset = (uint16_t)draw->objectOffset;
	if ((size_t)objectOffset + SLIP_OBJECT_DOS_STRIDE > context->objectTableBytes ||
	    objectOffset % SLIP_OBJECT_DOS_STRIDE != 0) {
		return false;
	}
	callback = context->slotDrawCallbacks[drawRecordIndex];
	if (!SlipTrackWorld_DrawCallbackHeader(context->slotDrawBase, context->slotDrawBytes, (uint16_t)drawPayload,
	                                       callback, context->componentBaseToken, &header)) {
		return false;
	}
	if (callback == NULL) {
		return true;
	}
	if (header.branch == SLIP_TRACK_WORLD_OBJECT_EDGE_DEFAULT_VALUE) {
		SlipTrackWorldBuildAttachment builtAttachment;
		SlipDraw3DProjectIndex project;
		const uint32_t attachmentOffset = draw->edgeReference;
		const uint32_t childAddress = draw->pairedDrawAddress;
		uint8_t *childRecord;
		const uint8_t *attachmentRecord;

		if (attachmentOffset + SLIP_TRC_PRIMITIVE_FIRST_INDEX_DWORD_END > context->componentBaseBytes ||
		    context->cameraMatrix == NULL || !TrackView_SlotDrawRecord(context, childAddress, &childRecord)) {
			return false;
		}
		attachmentRecord = context->componentBase + attachmentOffset;

		TrackViewPrimitiveCallbackContext attachmentContext = {.viewMatrix = *context->cameraMatrix,
		                                                       .transformedObjectOffset = context->componentViewOrigin};

		if (!SlipDraw3D_ProjectIndex(
		        context->vertexRecords, context->vertexRecordCount,
		        (uint16_t)SlipBytes_ReadLE32(attachmentRecord + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET),
		        context->transform, &attachmentContext, &project)) {
			return false;
		}

		if (!SlipTrackWorld_BuildAttachmentTransform(
		        drawRecord, context->slotDrawBytes - drawRecordOffset, childRecord,
		        context->slotDrawBytes - (size_t)(childRecord - context->slotDrawBase), attachmentRecord,
		        context->componentBaseBytes - attachmentOffset, context->cameraMatrix,
		        (SlipView3DVec32){project.world.x, project.world.y, project.world.z}, &builtAttachment)) {
			return false;
		}
		SlipDraw3D_SetAuxiliaryClipPlane(context->projectState,
		                                 (SlipDraw3DVec32){builtAttachment.pairedAttachmentOrigin.x,
		                                                   builtAttachment.pairedAttachmentOrigin.y,
		                                                   builtAttachment.pairedAttachmentOrigin.z},
		                                 (int16_t)builtAttachment.transformed.x, (int16_t)builtAttachment.transformed.y,
		                                 (int16_t)builtAttachment.transformed.z);
		attachmentEnabled = true;
	} else if (header.branch == SLIP_TRACK_WORLD_OBJECT_EDGE_EXPLICIT_VALUE) {
		SlipTrackWorldUseAttachment storedAttachment;

		if (!SlipTrackWorld_UseAttachmentTransform(drawRecord, context->slotDrawBytes - drawRecordOffset,
		                                           &storedAttachment)) {
			return false;
		}
		SlipDraw3D_SetAuxiliaryClipPlane(
		    context->projectState,
		    (SlipDraw3DVec32){(int32_t)storedAttachment.originX, (int32_t)storedAttachment.originY,
		                      (int32_t)storedAttachment.originZ},
		    (int16_t)storedAttachment.normalX, (int16_t)storedAttachment.normalY, (int16_t)storedAttachment.normalZ);
		attachmentEnabled = true;
	}
	if (!SlipObject_ViewPosition(context->objectTable, context->objectTableBytes, objectOffset, &viewPosition) ||
	    !SlipTrackWorld_UpdateDrawFlags(context->rendererFlags, viewPosition.z, context->textureMode, context->shading,
	                                    context->componentDistance, context->shadingSecondary, context->componentRadius,
	                                    &drawFlags)) {
		return false;
	}
	context->rendererFlags = drawFlags.rendererFlags;
	savedRecordIndex = SlipDraw3D_CurrentStateRecordIndex(context->recordIndex);
	if (!TrackView_ApplyLoadedDrawState(context, savedRecordIndex + 1u) ||
	    !SlipTrackWorld_InvokeDrawCallbackExecute(
	        drawRecord, context->slotDrawBytes - drawRecordOffset, drawRecordAddress, (uint32_t)viewPosition.x,
	        savedRecordIndex, context->detailLevel, context->frameRenderFlags, attachmentEnabled, context->slotListBase,
	        context->slotListBytes, context->slotListBaseAddress, context->objectTable, context->objectTableBytes,
	        &execute)) {
		return false;
	}
	context->articMinimumLod = execute.block.actorMode;
	callbackResult = callback(context, objectOffset);
	context->rendererFlags = context->frameRenderFlags;
	restoreResult = TrackView_ApplyLoadedDrawState(context, savedRecordIndex);
	if (attachmentEnabled) {
		SlipDraw3D_ClearAuxiliaryClipPlane(context->projectState);
	}
	if (!callbackResult || !restoreResult) {
		return false;
	}
	return true;
}

static bool TrackView_QueueSectionBeams(TrackViewRawBspContext *context) {
	for (uint32_t index = 0; index < SlipTrackWorld_beams.recordCount; ++index) {
		const SlipTrackBeamRecord *const beam =
		    &(SlipTrackWorld_beams.resourceRecords ? SlipTrackWorld_beams.resourceRecords
		                                           : SlipTrackWorld_beams.records)[index];
		if (beam->section != context->beamSection)
			continue;
		SlipView3DVec32 relative = {(int32_t)((uint32_t)beam->midpoint.x - context->cameraWorldX),
		                            (int32_t)((uint32_t)beam->midpoint.y - context->cameraWorldY),
		                            (int32_t)((uint32_t)beam->midpoint.z - context->cameraWorldZ)};
		SlipView3DMatrix camera;
		SlipObjectMatrixCopy copied;
		if (!SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, 0, &camera, &copied))
			return false;
		SlipView3DVec32 view = SlipView3D_TransformPositionByRows(&camera, relative);
		SlipDraw3D_ListInsert(&SlipDraw3D_listState, SlipDraw3D_listPool,
		                      (size_t)SlipDraw3D_listState.capacity * sizeof(*SlipDraw3D_listPool), (uint32_t)view.z,
		                      TrackView_DrawBeam, index);
	}
	return true;
}

bool TrackView_DrawComponentActors(uint32_t objectListOffset, const uint8_t *sectionRecord, uint32_t sectionRenderFlags,
                                   SlipView3DVec32 componentViewOrigin, void *userData) {
	TrackViewRawBspContext *const context = (TrackViewRawBspContext *)userData;
	SlipDraw3DListNode *const nodePool = SlipDraw3D_listPool;
	SlipDraw3DListState *const drawList = &SlipDraw3D_listState;
	const size_t nodePoolBytes = (size_t)drawList->capacity * sizeof(*nodePool);
	uint32_t firstRecordAddress;
	uint32_t recordAddress;

	if (context == NULL) {
		return true;
	}
	context->componentViewOrigin = componentViewOrigin;

	context->rendererFlags = sectionRenderFlags;

	context->beamSection = context->chunkBaseToken + (uint32_t)(sectionRecord - context->chunkBase);
	if (nodePool == NULL) {
		return false;
	}

	if (objectListOffset == 0 && SlipTrackWorld_beams.recordCount == 0)
		return true;
	firstRecordAddress = context->slotDrawBaseAddress + objectListOffset;
	if (objectListOffset != 0) {
		recordAddress = firstRecordAddress;
		do {
			SlipTrackWorldObjectAttachmentDrawExecution attachmentDrawResult;
			uint8_t *attachmentDrawRecord;
			size_t attachmentDrawRecordBytes;

			if (!TrackView_SlotDrawRecord(context, recordAddress, &attachmentDrawRecord)) {
				return false;
			}
			attachmentDrawRecordBytes = context->slotDrawBytes - (size_t)(attachmentDrawRecord - context->slotDrawBase);
			if (!SlipTrackWorld_ExecuteObjectAttachmentDraw(
			        attachmentDrawRecord, attachmentDrawRecordBytes, recordAddress, context->slotListBase,
			        context->slotListBytes, context->slotListBaseAddress, context->objectTable,
			        context->objectTableBytes, &attachmentDrawResult)) {
				return false;
			}
			if (attachmentDrawResult.block.slotAttachmentReference != 0) {
				if (!SlipDraw3D_ListPushFrame(drawList, nodePool, nodePoolBytes) ||
				    !TrackView_ScheduleSlotDraw(context, drawList, nodePool, nodePoolBytes, attachmentDrawRecord,
				                                recordAddress) ||
				    !SlipDraw3D_ListTraverse(drawList, nodePool, nodePoolBytes, context) ||
				    !SlipDraw3D_ListPopFrame(drawList)) {
					return false;
				}
			}
			const SlipTrackDrawRecord *const drawRecord = (const void *)attachmentDrawRecord;
			recordAddress = drawRecord->nextAddress;
		} while (recordAddress != firstRecordAddress);
	}
	if (!SlipDraw3D_ListPushFrame(drawList, nodePool, nodePoolBytes)) {
		return false;
	}
	if (objectListOffset != 0) {
		recordAddress = firstRecordAddress;
		do {
			SlipTrackWorldObjectCallbackDrawExecution callbackDrawResult;
			uint8_t *callbackDrawRecord;
			size_t callbackDrawRecordBytes;

			if (!TrackView_SlotDrawRecord(context, recordAddress, &callbackDrawRecord)) {
				return false;
			}
			callbackDrawRecordBytes = context->slotDrawBytes - (size_t)(callbackDrawRecord - context->slotDrawBase);
			if (!SlipTrackWorld_ExecuteObjectCallbackDraw(callbackDrawRecord, callbackDrawRecordBytes, recordAddress, 0,
			                                              context->slotListBase, context->slotListBytes,
			                                              context->slotListBaseAddress, context->objectTable,
			                                              context->objectTableBytes, &callbackDrawResult)) {
				return false;
			}
			if (callbackDrawResult.block.slotAttachmentReference == 0 &&
			    !TrackView_ScheduleSlotDraw(context, drawList, nodePool, nodePoolBytes, callbackDrawRecord,
			                                recordAddress)) {
				return false;
			}
			const SlipTrackDrawRecord *const drawRecord = (const void *)callbackDrawRecord;
			recordAddress = drawRecord->nextAddress;
		} while (recordAddress != firstRecordAddress);
	}
	if (!TrackView_QueueSectionBeams(context))
		return false;

	if (sectionRecord == context->specialRecord) {
		context->limitStart = SLIP_REFUEL_COLOUR_RAMP_START;
		context->limitEnd = SLIP_REFUEL_COLOUR_RAMP_END;
		context->limitEnabled = UINT32_MAX;
	}
	if (!SlipDraw3D_ListTraverse(drawList, nodePool, nodePoolBytes, context) || !SlipDraw3D_ListPopFrame(drawList)) {
		return false;
	}
	context->limitEnabled = 0;
	context->rendererFlags = sectionRenderFlags;
	return true;
}

static uint32_t TrackView_TextureRowScroll(const uint8_t *materialTable, size_t materialTableBytes, uint16_t materialId,
                                           uint32_t primitiveRecordToken, uint32_t animationAccumulator) {
	uint32_t materialCount;
	uint32_t materialIndex;
	size_t materialOffset;
	const uint8_t *materialRecord;
	uint32_t rowScroll;
	uint32_t recordPhase;

	if (materialTable == NULL || materialTableBytes < SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES) {
		return 0;
	}
	materialCount = SlipBytes_ReadLE32(materialTable);
	materialIndex = materialId & SLIP_DRAW3D_MATERIAL_INDEX_MASK;
	materialOffset = SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES;
	if (materialIndex < materialCount) {
		materialOffset += (size_t)materialIndex * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE;
	}
	if (materialOffset > materialTableBytes || materialTableBytes - materialOffset < sizeof("TRNCSIN") - 1) {
		return 0;
	}
	materialRecord = materialTable + materialOffset;
	if (memcmp(materialRecord, "TRNCSIN", sizeof("TRNCSIN") - 1) != 0) {
		return 0;
	}
	rowScroll = animationAccumulator;
	rowScroll &= SLIP_TRACK_TEXTURE_SCROLL_ACCUMULATOR_MASK;
	rowScroll >>= 1u;
	rowScroll &= SLIP_TRACK_TEXTURE_SCROLL_FRACTION_MASK;
	recordPhase = primitiveRecordToken & SLIP_TRACK_TEXTURE_SCROLL_TOKEN_MASK;
	if (recordPhase >= SLIP_TRACK_TEXTURE_SCROLL_REVERSE_TOKEN_START) {
		rowScroll = SLIP_Q14_ONE - rowScroll;
	}
	return rowScroll & UINT16_MAX;
}

static SlipView3DVec32 TrackView_DrawStateLightVector(const TrackViewRawBspContext *context) {
	const SlipDraw3DVec32 *const light = &context->drawStateRecord->lightVector;
	return (SlipView3DVec32){light->x, light->y, light->z};
}

static bool TrackView_MaterialColorNormal(const SlipDraw3DMaterialRecord *material,
                                          const TrackViewRawBspContext *context, SlipView3DVec32 light,
                                          uint16_t normalX, uint16_t normalY, uint16_t normalZ,
                                          SlipDraw3DVertexRecord *vertices, size_t vertexCount, const uint8_t *indices,
                                          size_t indexBytes, uint16_t count, const SlipDraw3DProjectState *projection,
                                          SlipDraw3DTransformFn transform, void *transformContext, uint8_t *colorOut) {
	SlipDraw3DVertexLighting state = {.light = {light.x, light.y, light.z},
	                                  .direct = context->directLight,
	                                  .ambient = context->ambientLight,
	                                  .fadeStart = SlipDraw3D_fadeStart,
	                                  .fadeEnd = SlipDraw3D_fadeEnd,
	                                  .fadeRange = SlipDraw3D_fadeRange,
	                                  .fadeShade = SlipDraw3D_fadeColour,
	                                  .overrideRamp = context->limitEnabled,
	                                  .rampStart = context->limitStart,
	                                  .rampEnd = context->limitEnd,
	                                  .transform = transform,
	                                  .transformContext = transformContext};
	uint32_t color;
	if (!SlipDraw3D_PolygonColor(material, (int16_t)normalX, (int16_t)normalY, (int16_t)normalZ, &state, vertices,
	                             vertexCount, indices, indexBytes, count, projection->inverseProjectionScale, &color))
		return false;
	*colorOut = (uint8_t)color;
	return true;
}

static int TrackView_ActorCollectRingPoints(const SlipDraw3DRecordPool *pool, uint32_t inputActiveHeadOffset,
                                            SlipDraw3DRasterPoint *points, size_t pointCapacity,
                                            size_t *pointCountOut) {
	const uint8_t *recordPoolBytes;
	size_t recordPoolBytesCount;
	uint32_t currentOffset;
	size_t pointCount = 0;

	if (pool == NULL || points == NULL || pointCountOut == NULL || pointCapacity == 0) {
		return 0;
	}
	recordPoolBytes = SlipDraw3D_RecordPoolConstBytes(pool);
	recordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();
	if (recordPoolBytes == NULL || inputActiveHeadOffset >= recordPoolBytesCount ||
	    inputActiveHeadOffset % SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE != 0) {
		return 0;
	}
	currentOffset = inputActiveHeadOffset;
	do {
		const uint8_t *record;

		if (pointCount >= pointCapacity || currentOffset >= recordPoolBytesCount ||
		    recordPoolBytesCount - currentOffset < SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE ||
		    currentOffset % SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE != 0) {
			return 0;
		}
		record = recordPoolBytes + currentOffset;
		points[pointCount].x = (int32_t)SlipBytes_ReadLE32(record + SLIP_DRAW3D_VERTEX_RECORD_SCREEN_OFFSET);
		points[pointCount].y = (int32_t)SlipBytes_ReadLE32(record + offsetof(SlipDraw3DVertexRecord, screenY));
		++pointCount;
		currentOffset = SlipBytes_ReadLE32(record + SLIP_DRAW3D_RECORD_NEXT_OFFSET);
	} while (currentOffset != inputActiveHeadOffset);
	*pointCountOut = pointCount;
	return 1;
}

static int TrackView_ActorCollectShadedRing(const SlipDraw3DRecordPool *pool, uint32_t inputActiveHeadOffset,
                                            RasterShadedPoint *points, size_t pointCapacity, size_t *pointCountOut,
                                            uint8_t *flatColorOut, bool *flatColorOutIsUniform) {
	const uint8_t *recordPoolBytes;
	size_t recordPoolBytesCount;
	uint32_t currentOffset;
	size_t pointCount = 0;
	uint8_t firstColor = 0;
	bool uniform = true;

	if (pool == NULL || points == NULL || pointCountOut == NULL || flatColorOut == NULL ||
	    flatColorOutIsUniform == NULL || pointCapacity == 0) {
		return 0;
	}
	recordPoolBytes = SlipDraw3D_RecordPoolConstBytes(pool);
	recordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();
	if (recordPoolBytes == NULL || inputActiveHeadOffset >= recordPoolBytesCount ||
	    inputActiveHeadOffset % SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE != 0) {
		return 0;
	}
	currentOffset = inputActiveHeadOffset;
	do {
		const uint8_t *record;
		uint16_t shade;
		uint8_t color;

		if (pointCount >= pointCapacity || currentOffset >= recordPoolBytesCount ||
		    recordPoolBytesCount - currentOffset < SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE ||
		    currentOffset % SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE != 0) {
			return 0;
		}
		record = recordPoolBytes + currentOffset;
		shade = SlipBytes_ReadLE16(record + offsetof(SlipDraw3DDrawRecord, shade));
		color = (uint8_t)(shade >> SLIP_SHADE_COLOUR_SHIFT);
		if (pointCount == 0) {
			firstColor = color;
		} else if (color != firstColor) {
			uniform = false;
		}
		uint8_t *const rasterPoint = SlipDraw3D_PointBuffer() + pointCount * sizeof(RasterTexturedPoint);
		TrackView_WriteLE32(rasterPoint, SlipBytes_ReadLE32(record + SLIP_DRAW3D_VERTEX_RECORD_SCREEN_OFFSET));
		TrackView_WriteLE32(rasterPoint + offsetof(RasterTexturedPoint, y),
		                    SlipBytes_ReadLE32(record + offsetof(SlipDraw3DVertexRecord, screenY)));
		TrackView_WriteLE32(rasterPoint + offsetof(RasterTexturedPoint, reserved08), shade);
		points[pointCount].x = SlipBytes_ReadLEI32(rasterPoint);
		points[pointCount].y = SlipBytes_ReadLEI32(rasterPoint + offsetof(RasterTexturedPoint, y));
		points[pointCount].shade = SlipBytes_ReadLE16(rasterPoint + offsetof(RasterTexturedPoint, reserved08));
		++pointCount;
		currentOffset = SlipBytes_ReadLE32(record + SLIP_DRAW3D_RECORD_NEXT_OFFSET);
	} while (currentOffset != inputActiveHeadOffset);
	*pointCountOut = pointCount;
	*flatColorOut = firstColor;
	*flatColorOutIsUniform = uniform;
	return 1;
}

static void TrackView_WriteLE16(uint8_t *p, uint16_t v) {
	p[0] = (uint8_t)(v & UINT8_MAX);
	p[1] = (uint8_t)((v >> 8) & UINT8_MAX);
}

static void TrackView_WriteLE32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static uint8_t *TrackView_FindComponentMaterial(uint8_t *componentBase, size_t componentBaseBytes,
                                                uint16_t materialId) {
	uint16_t materialTableOffset;
	uint16_t materialCount;
	size_t materialOffset;
	uint16_t materialIndex;

	if (componentBase == NULL || componentBaseBytes < SLIP_TRC_FILE_MATERIAL_TABLE_END) {
		return NULL;
	}
	materialTableOffset = SlipBytes_ReadLE16(componentBase + SLIP_TRC_FILE_MATERIAL_TABLE_OFFSET);
	if (materialTableOffset == 0u || (size_t)materialTableOffset + SLIP_TRC_TABLE_COUNT_BYTES > componentBaseBytes) {
		return NULL;
	}
	materialCount = SlipBytes_ReadLE16(componentBase + materialTableOffset);
	materialOffset = (size_t)materialTableOffset + SLIP_TRC_TABLE_COUNT_BYTES;
	for (materialIndex = 0; materialIndex < materialCount; ++materialIndex) {
		uint8_t *materialRecord;

		if (materialOffset > componentBaseBytes ||
		    componentBaseBytes - materialOffset < SLIP_SHAPE_MATERIAL_ENTRY_BYTES) {
			return NULL;
		}
		materialRecord = componentBase + materialOffset;
		if (SlipBytes_ReadLE16(materialRecord + SLIP_SHAPE_MATERIAL_ID_OFFSET) == materialId) {
			return materialRecord;
		}
		materialOffset += SLIP_SHAPE_MATERIAL_ENTRY_BYTES;
	}
	return NULL;
}

static int TrackView_ResolvePrimitiveMaterials(uint8_t *componentBase, size_t componentBaseBytes,
                                               uint16_t primitiveListOffset, const uint8_t *materialTable,
                                               size_t materialTableBytes, uint16_t materialGlobal) {
	uint16_t primitiveCount;
	size_t primitiveOffset;
	uint16_t primitiveIndex;

	if (primitiveListOffset == 0u) {
		return 1;
	}
	if ((size_t)primitiveListOffset + SLIP_TRC_TABLE_COUNT_BYTES > componentBaseBytes) {
		return 0;
	}
	primitiveCount = SlipBytes_ReadLE16(componentBase + primitiveListOffset);
	primitiveOffset = (size_t)primitiveListOffset + SLIP_TRC_TABLE_COUNT_BYTES;
	for (primitiveIndex = 0; primitiveIndex < primitiveCount; ++primitiveIndex) {
		uint8_t *primitive;
		uint16_t localMaterial;
		uint16_t resolvedMaterial;
		uint16_t descriptor;
		uint32_t primitiveBytes;
		uint8_t *componentMaterial;
		SlipDraw3DMaterialNumber lookup;

		if (primitiveOffset > componentBaseBytes ||
		    componentBaseBytes - primitiveOffset < SLIP_TRC_PRIMITIVE_HEADER_BYTES) {
			return 0;
		}
		primitive = componentBase + primitiveOffset;
		localMaterial = (uint16_t)(SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_MATERIAL_OFFSET) &
		                           SLIP_DRAW3D_MATERIAL_INDEX_MASK);
		resolvedMaterial = (uint16_t)(localMaterial | SLIP_TRC_PRIMITIVE_MATERIAL_UNRESOLVED);
		componentMaterial = TrackView_FindComponentMaterial(componentBase, componentBaseBytes, localMaterial);
		if (componentMaterial != NULL &&
		    SlipDraw3D_GetMaterialNumber(materialTable, materialTableBytes, materialGlobal, componentMaterial,
		                                 SLIP_SHAPE_MATERIAL_NAME_BYTES, &lookup) &&
		    !lookup.carryOut) {
			resolvedMaterial = lookup.materialIndex;
		}
		TrackView_WriteLE16(primitive + SLIP_TRC_PRIMITIVE_MATERIAL_OFFSET, resolvedMaterial);

		descriptor = SlipBytes_ReadLE16(primitive);
		if ((descriptor & SLIP_TRC_PRIMITIVE_TEXTURED) == 0u) {
			primitiveBytes = (uint32_t)descriptor * SLIP_TRC_VERTEX_INDEX_BYTES + SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		} else {
			primitiveBytes =
			    (uint32_t)(descriptor & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK) * SLIP_TRC_TEXTURED_VERTEX_BYTES +
			    SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		}
		primitiveOffset += primitiveBytes;
	}
	return 1;
}

int TrackView_ResolveComponentMaterials(uint8_t *componentBase, size_t componentBaseBytes, const uint8_t *materialTable,
                                        size_t materialTableBytes, uint16_t materialGlobal,
                                        TrackViewMaterialInit *result) {
	uint16_t componentMaterialTableOffset;

	if (componentBase == NULL || componentBaseBytes < SLIP_TRC_FILE_MATERIAL_TABLE_END || result == NULL) {
		return 0;
	}
	componentMaterialTableOffset = SlipBytes_ReadLE16(componentBase + SLIP_TRC_FILE_MATERIAL_TABLE_OFFSET);
	if (componentMaterialTableOffset != 0u) {
		const uint16_t siblingListOffset = SlipBytes_ReadLE16(componentBase + SLIP_TRC_FILE_COMPONENT_LIST_OFFSET);
		uint16_t siblingCount;
		size_t siblingOffset;
		uint16_t siblingIndex;
		uint16_t componentMaterialCount;
		size_t componentMaterialOffset;
		uint16_t componentMaterialIndex;

		if ((size_t)siblingListOffset + SLIP_TRC_TABLE_COUNT_BYTES > componentBaseBytes) {
			return 0;
		}
		siblingCount = SlipBytes_ReadLE16(componentBase + siblingListOffset);
		siblingOffset = (size_t)siblingListOffset + SLIP_TRC_TABLE_COUNT_BYTES;
		for (siblingIndex = 0; siblingIndex < siblingCount; ++siblingIndex) {
			uint8_t *sibling;
			uint16_t siblingBytes;

			if (siblingOffset > componentBaseBytes ||
			    componentBaseBytes - siblingOffset < SLIP_TRC_COMPONENT_LINK_HEADER_BYTES) {
				return 0;
			}
			sibling = componentBase + siblingOffset;
			if (!TrackView_ResolvePrimitiveMaterials(
			        componentBase, componentBaseBytes,
			        SlipBytes_ReadLE16(sibling + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET), materialTable,
			        materialTableBytes, materialGlobal) ||
			    !TrackView_ResolvePrimitiveMaterials(
			        componentBase, componentBaseBytes,
			        SlipBytes_ReadLE16(sibling + SLIP_TRC_COMPONENT_SECOND_LIST_OFFSET), materialTable,
			        materialTableBytes, materialGlobal)) {
				return 0;
			}
			siblingBytes = SlipBytes_ReadLE16(sibling);
			if (siblingBytes == 0u) {
				return 0;
			}
			siblingOffset += siblingBytes;
		}

		if ((size_t)componentMaterialTableOffset + SLIP_TRC_TABLE_COUNT_BYTES > componentBaseBytes) {
			return 0;
		}
		componentMaterialCount = SlipBytes_ReadLE16(componentBase + componentMaterialTableOffset);
		componentMaterialOffset = (size_t)componentMaterialTableOffset + SLIP_TRC_TABLE_COUNT_BYTES;
		for (componentMaterialIndex = 0; componentMaterialIndex < componentMaterialCount; ++componentMaterialIndex) {
			uint8_t *componentMaterial;
			SlipDraw3DMaterialNumber lookup;

			if (componentMaterialOffset > componentBaseBytes ||
			    componentBaseBytes - componentMaterialOffset < SLIP_SHAPE_MATERIAL_ENTRY_BYTES) {
				return 0;
			}
			componentMaterial = componentBase + componentMaterialOffset;
			if (SlipDraw3D_GetMaterialNumber(materialTable, materialTableBytes, materialGlobal, componentMaterial,
			                                 SLIP_SHAPE_MATERIAL_NAME_BYTES, &lookup) &&
			    !lookup.carryOut) {
				TrackView_WriteLE16(componentMaterial + SLIP_SHAPE_MATERIAL_ID_OFFSET, lookup.materialIndex);
			}
			componentMaterialOffset += SLIP_SHAPE_MATERIAL_ENTRY_BYTES;
		}
	}

	TrackView_MaterialInit(materialTable, materialTableBytes, materialGlobal, result);
	return 1;
}

static const uint8_t *TrackView_MaterialRecord(const uint8_t *materialTable, size_t materialTableBytes,
                                               uint16_t materialIndex) {
	uint16_t count;
	size_t offset = SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES;

	if (materialTable == NULL || materialTableBytes < SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES) {
		return NULL;
	}

	count = SlipBytes_ReadLE16(materialTable);
	materialIndex = (uint16_t)(materialIndex & SLIP_DRAW3D_MATERIAL_INDEX_MASK);
	if (materialIndex < count) {
		offset = SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES +
		         (size_t)(uint16_t)(SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE * materialIndex);
	}
	if (offset > materialTableBytes || materialTableBytes - offset < SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE) {
		return NULL;
	}
	return materialTable + offset;
}

void TrackViewNormalizePrimitiveFlags(uint8_t *componentBase, size_t componentBaseBytes, const uint8_t *materialTable,
                                      size_t materialTableBytes, uint16_t materialGlobal, uint32_t materialFrameIndex,
                                      const TrackViewResourceHandleRegistry *resourceRegistry,
                                      const SlipView3DMaths *maths, SlipView3DVec32 light) {
	static const uint8_t kMaterialSetBit[] = {
	    SLIP_TRC_MATERIAL_CAGE_SEVEN_LINES,    SLIP_TRC_MATERIAL_CAGE_FIVE_LINES,
	    SLIP_TRC_MATERIAL_CAGE_SIXTEEN_LINES,  SLIP_TRC_MATERIAL_FLOOR_LIGHT_DASHES,
	    SLIP_TRC_MATERIAL_THREE_LINE_STRIP,    SLIP_TRC_MATERIAL_QUAD_OUTLINE,
	    SLIP_TRC_MATERIAL_THREE_LINE_DIAGONAL, SLIP_TRC_MATERIAL_SEVEN_LINE_FRAME,
	    SLIP_TRC_MATERIAL_TWO_CONNECTED_LINES, SLIP_TRC_MATERIAL_TWO_CORNER_LINES};
	uint16_t rootListOffset;
	uint16_t siblingCount;
	size_t siblingOffset;
	uint16_t siblingIndex;
	bool haveFrameState;
	int16_t threshold = 0;

	if (componentBase == NULL || componentBaseBytes < SLIP_TRC_FILE_COMPONENT_LIST_END) {
		return;
	}
	haveFrameState =
	    materialTable != NULL && materialTableBytes >= SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES && maths != NULL;
	if (haveFrameState) {

		threshold = SlipView3D_CosQ14(maths, SLIP_TRACK_PRIMITIVE_REPLAY_LIGHT_ANGLE);
	}
	rootListOffset = SlipBytes_ReadLE16(componentBase + SLIP_TRC_FILE_COMPONENT_LIST_OFFSET);
	if (rootListOffset == 0u || (size_t)rootListOffset + SLIP_TRC_TABLE_COUNT_BYTES > componentBaseBytes) {
		return;
	}
	siblingCount = SlipBytes_ReadLE16(componentBase + rootListOffset);
	siblingOffset = (size_t)rootListOffset + SLIP_TRC_TABLE_COUNT_BYTES;

	for (siblingIndex = 0; siblingIndex < siblingCount; ++siblingIndex) {
		uint8_t *sibling;
		uint16_t listOffset;
		uint16_t stride;

		if (siblingOffset + SLIP_TRC_COMPONENT_LINK_HEADER_BYTES > componentBaseBytes) {
			break;
		}
		sibling = componentBase + siblingOffset;
		listOffset = SlipBytes_ReadLE16(sibling + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET);

		if (listOffset != 0u && (size_t)listOffset + SLIP_TRC_TABLE_COUNT_BYTES <= componentBaseBytes) {
			const uint16_t primitiveCount = SlipBytes_ReadLE16(componentBase + listOffset);
			size_t primitiveOffset = (size_t)listOffset + SLIP_TRC_TABLE_COUNT_BYTES;
			uint16_t primitiveIndex;

			for (primitiveIndex = 0; primitiveIndex < primitiveCount; ++primitiveIndex) {
				uint8_t *primitive;
				const uint8_t *primaryMaterialRecord;
				uint8_t flags;
				uint8_t material;
				bool setBit;
				uint16_t descriptor;
				uint32_t advance;
				size_t k;

				if (primitiveOffset + SLIP_TRC_PRIMITIVE_FLAGS_END > componentBaseBytes) {
					break;
				}
				primitive = componentBase + primitiveOffset;

				primaryMaterialRecord =
				    materialGlobal != 0
				        ? TrackView_MaterialRecord(materialTable, materialTableBytes,
				                                   SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_MATERIAL_OFFSET))
				        : NULL;
				flags = primitive[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET];

				flags = (uint8_t)(flags & ~SLIP_TRC_PRIMITIVE_SPECIAL_PLANE);

				if (primaryMaterialRecord != NULL && memcmp(primaryMaterialRecord, "WATE", 4u) == 0) {
					flags = (uint8_t)(flags | SLIP_TRC_PRIMITIVE_SPECIAL_PLANE);
				}

				flags = (uint8_t)(flags & ~(SLIP_TRC_PRIMITIVE_TRANSPARENT | SLIP_TRC_PRIMITIVE_TRENCH));

				material = primitive[SLIP_TRC_PRIMITIVE_MATERIAL_FLAGS_OFFSET];
				setBit = false;
				if ((flags & SLIP_TRC_PRIMITIVE_RANGE_PLANE) == 0u) {
					if ((flags & SLIP_TRC_PRIMITIVE_FORCE_TRANSPARENT) != 0u) {
						setBit = true;
					} else {
						for (k = 0; k < sizeof(kMaterialSetBit); ++k) {
							if (material == kMaterialSetBit[k]) {
								setBit = true;
								break;
							}
						}
					}
				}

				if (setBit) {
					flags = (uint8_t)(flags | SLIP_TRC_PRIMITIVE_TRANSPARENT);
				}

				flags = (uint8_t)(flags & ~SLIP_TRACK_PRIMITIVE_REPLAY_PLANE);

				if (haveFrameState) {
					const uint8_t *const siblingMaterialRecord = primaryMaterialRecord;
					bool skipAll = false;
					bool skipToTrnc = false;

					if (material != SLIP_TRC_MATERIAL_FLOOR_LIGHT_DASHES) {
						if ((flags & SLIP_TRC_PRIMITIVE_RANGE_PLANE) != 0 ||
						    (flags & SLIP_TRC_PRIMITIVE_FORCE_TRANSPARENT) != 0) {

							skipAll = true;
						} else if ((flags & SLIP_TRC_PRIMITIVE_TRANSPARENT) != 0) {

							skipToTrnc = true;
						} else if (siblingMaterialRecord != NULL &&
						           memcmp(siblingMaterialRecord, "TRNCHIDD", 8u) == 0) {

							skipToTrnc = true;
						}
					}
					if (!skipAll && !skipToTrnc) {

						SlipView3DDotProductQ14 dot;
						int16_t facingDot;

						SlipView3D_DotProductQ14(
						    (uint16_t)(int16_t)-(int32_t)light.x, (uint16_t)(int16_t)-(int32_t)light.y,
						    (uint16_t)(int16_t)-(int32_t)light.z,
						    (uint16_t)SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET),
						    (uint16_t)SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET),
						    (uint16_t)SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET), &dot);
						facingDot = (int16_t)dot.dotProductQ14;
						if (facingDot >= threshold) {
							flags = (uint8_t)(flags | SLIP_TRACK_PRIMITIVE_REPLAY_PLANE);
						}

						if ((int16_t)SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET) >
						    SLIP_TRACK_PRIMITIVE_REPLAY_MAXIMUM_NORMAL_Y_Q14) {
							skipToTrnc = true;
						}
					}
					if (!skipAll && !skipToTrnc) {

						uint32_t materialIndex = SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_MATERIAL_OFFSET);
						const SlipDraw3DMaterialTable *const frameTable = (const void *)materialTable;
						const uint32_t textureHandle =
						    SlipMaterial_GetFrame(frameTable, materialGlobal, &materialIndex, materialFrameIndex);
						if ((uint16_t)textureHandle != 0 && siblingMaterialRecord != NULL &&

						    SlipBytes_ReadLE16(siblingMaterialRecord +
						                       offsetof(SlipDraw3DMaterialRecord, skipFlatPolygon)) == 0 &&
						    resourceRegistry != NULL) {
							SlipResourcePayload texturePayload = {0};

							if (TrackView_LockResourceHandlePayload(resourceRegistry, textureHandle, &texturePayload)) {
								TrackView_UnlockResourceHandlePayload(resourceRegistry, textureHandle);
								if (texturePayload.size >= SLIP_SPRITE_TRANSPARENT_COLOUR_END &&
								    SlipBytes_ReadLE16(texturePayload.data + SLIP_SPRITE_TRANSPARENT_COLOUR_OFFSET) !=
								        SLIP_SPRITE_NO_TRANSPARENT_COLOUR) {
									flags = (uint8_t)(flags | SLIP_TRC_PRIMITIVE_TRANSPARENT);
								}
							}
						}
					}
					if (!skipAll) {

						if (siblingMaterialRecord != NULL && memcmp(siblingMaterialRecord, "TRNC", 4u) == 0) {
							flags = (uint8_t)(flags | SLIP_TRC_PRIMITIVE_TRENCH);
						}
					}
				}
				primitive[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET] = flags;

				descriptor = SlipBytes_ReadLE16(primitive);
				if ((descriptor & SLIP_TRC_PRIMITIVE_TEXTURED) != 0u) {
					advance =
					    (uint32_t)(descriptor & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK) * SLIP_TRC_TEXTURED_VERTEX_BYTES +
					    SLIP_TRC_PRIMITIVE_HEADER_BYTES;
				} else {
					advance = (uint32_t)descriptor * SLIP_TRC_VERTEX_INDEX_BYTES + SLIP_TRC_PRIMITIVE_HEADER_BYTES;
				}
				primitiveOffset += advance;
			}
		}

		stride = SlipBytes_ReadLE16(sibling);
		if (stride == 0u) {
			break;
		}
		siblingOffset += stride;
	}
}

static uint32_t TrackView_ProjectMaskCallback(SlipView3DVec32 point, void *userData) {
	const SlipTrackWorldProjectFrustum *const frustum = (const SlipTrackWorldProjectFrustum *)userData;
	uint32_t mask = 0;

	if (!SlipTrackWorld_ProjectMask(point, frustum, &mask)) {
		return 0;
	}
	return mask;
}

static bool TrackView_SphereCullCallback(SlipView3DVec32 center, int32_t radius, void *userData) {
	const SlipTrackWorldProjectFrustum *const frustum = (const SlipTrackWorldProjectFrustum *)userData;
	bool carry;

	if (!SlipTrackWorld_SphereCull(center, radius, frustum, &carry)) {
		return true;
	}
	return carry;
}

static uint32_t TrackView_PrimitiveProjectMask(SlipDraw3DVec32 point, void *userData) {
	const TrackViewPrimitiveCallbackContext *const context = (const TrackViewPrimitiveCallbackContext *)userData;

	if (context == NULL) {
		return 0;
	}
	return TrackView_ProjectMaskCallback((SlipView3DVec32){point.x, point.y, point.z}, (void *)context->frustum);
}

static SlipView3DVec32 TrackView_SourcePoint(int16_t sourceX, int16_t sourceY, int16_t sourceZ, void *userData) {
	const TrackViewPrimitiveCallbackContext *const context = (const TrackViewPrimitiveCallbackContext *)userData;

	if (context == NULL) {
		return (SlipView3DVec32){0, 0, 0};
	}
	return SlipView3D_LocalVertex(context->objectPosition, (SlipView3DVec16){sourceX, sourceY, sourceZ});
}

static bool TrackView_ExecuteLowEmitPath(TrackViewRawBspContext *context,
                                         const TrackViewPrimitiveCallbackContext *primitiveContext,
                                         const uint8_t *indexStream, size_t indexStreamBytes,
                                         uint16_t polygonCountAndFlags, uint16_t normalX, uint16_t normalY,
                                         uint16_t normalZ, uint16_t materialIndex, bool *carryOut) {
	SlipDraw3DReturnActiveVisit returnVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DClipFlagVisit clipFlagVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneBoundsVisit postBoundsVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneClipRecordVisit postClipRecordVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneClipPlaneVisit postClipPlaneVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DReturnActiveRing returnActive;
	SlipDraw3DMaterialGate materialGate;

	if (context == NULL || primitiveContext == NULL || indexStream == NULL || carryOut == NULL) {
		return false;
	}
	*carryOut = false;
	++context->emitPathCount;
	if (context->postPlaneHead == 0 && (context->shapeProjectionFlags & SLIP_SHAPE_INSIDE_VIEW) != 0) {
		SlipView3DVec32 light = TrackView_DrawStateLightVector(context);
		SlipDraw3DVertexLighting lighting = {.light = {light.x, light.y, light.z},
		                                     .origin = {context->origin.x, context->origin.y, context->origin.z},
		                                     .flags = context->rendererFlags,
		                                     .direct = context->directLight,
		                                     .ambient = context->ambientLight,
		                                     .fadeStart = SlipDraw3D_fadeStart,
		                                     .fadeEnd = SlipDraw3D_fadeEnd,
		                                     .fadeRange = SlipDraw3D_fadeRange,
		                                     .fadeShade = SlipDraw3D_fadeColour,
		                                     .overrideRamp = context->limitEnabled,
		                                     .rampStart = context->limitStart,
		                                     .rampEnd = context->limitEnd,
		                                     .transform = TrackView_SourceVertex,
		                                     .transformContext = (void *)primitiveContext};
		if (!SlipDraw3D_DrawUnclippedPolygon((const void *)context->materialTable, materialIndex, polygonCountAndFlags,
		                                     (int16_t)normalX, (int16_t)normalY, (int16_t)normalZ,
		                                     context->vertexRecords, context->vertexRecordCount, indexStream,
		                                     indexStreamBytes, context->projectState, TrackView_TransformVertex,
		                                     TrackView_ProjectScreenSecondary, &lighting, &context->materialColor)) {
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_UNCLIPPED_POLYGON;
			context->failed = true;
			return false;
		}
		return true;
	}

	if (context->drawRecordPool == NULL || context->projectState == NULL ||
	    !SlipDraw3D_ReturnActiveRing(context->drawRecordPool, returnVisits,
	                                 sizeof(returnVisits) / sizeof(returnVisits[0]), &returnActive) ||
	    !SlipDraw3D_MaterialGate(context->materialTable, context->materialTableBytes, materialIndex,
	                             context->rendererFlags, polygonCountAndFlags, indexStream, indexStreamBytes,
	                             &materialGate)) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_GATE;
		context->failed = true;
		return false;
	}
	(void)returnActive;
	++context->emitPathMaterialGateCount;
	if (materialGate.branch == SLIP_DRAW3D_MATERIAL_GATE_BRANCH_REJECT) {
		*carryOut = true;
		return true;
	}

	context->materialRecord = materialGate.materialRecord;
	context->materialRecordBytes =
	    context->materialTableBytes - (size_t)(materialGate.materialRecord - context->materialTable);
	if (materialGate.branch == SLIP_DRAW3D_MATERIAL_GATE_BRANCH_REGULAR) {
		uint8_t color;
		SlipDraw3DRegularSetup regularSetup;
		SlipDraw3DSolidRingExecute solidRing;
		SlipDraw3DFlatRingDispatch flatRing;
		TrackViewPostPlaneArgs postPlaneArgs = TrackView_PostPlaneArgs(context);
		SlipDraw3DRasterPoint flatPoints[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];

		const uint16_t polygonVertexCount = polygonCountAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
		if (!TrackView_MaterialColorNormal((const void *)materialGate.materialRecord, context,
		                                   TrackView_DrawStateLightVector(context), normalX, normalY,
		                                   polygonVertexCount, context->vertexRecords, context->vertexRecordCount,
		                                   indexStream, indexStreamBytes, polygonCountAndFlags, context->projectState,
		                                   TrackView_TransformVertex, (void *)primitiveContext, &color) ||
		    !SlipDraw3D_RegularSetup(materialGate.materialRecord,
		                             context->materialTableBytes -
		                                 (size_t)(materialGate.materialRecord - context->materialTable),
		                             polygonCountAndFlags, color, &regularSetup) ||
		    (context->materialColor = regularSetup.storedMaterialColor, false) ||
		    !SlipDraw3D_BuildSolidRingExecute(
		        context->drawRecordPool, context->vertexRecords, context->vertexRecordCount, indexStream,
		        indexStreamBytes, (uint16_t)regularSetup.maskedIndex, regularSetup.materialDitherBits,
		        context->projectState, TrackView_TransformVertex, TrackView_ProjectScreenPrimary,
		        TrackView_ProjectScreenSecondary, (void *)primitiveContext, postPlaneArgs.hasPostPlanes,
		        postPlaneArgs.planeBase, postPlaneArgs.planeBytes, postPlaneArgs.planeHeadOffset,
		        postPlaneArgs.limitXMin, postPlaneArgs.limitXMax, postPlaneArgs.limitYMin, postPlaneArgs.limitYMax,
		        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, clipFlagVisits,
		        sizeof(clipFlagVisits) / sizeof(clipFlagVisits[0]), postBoundsVisits,
		        sizeof(postBoundsVisits) / sizeof(postBoundsVisits[0]), postClipRecordVisits,
		        sizeof(postClipRecordVisits) / sizeof(postClipRecordVisits[0]), postClipPlaneVisits,
		        sizeof(postClipPlaneVisits) / sizeof(postClipPlaneVisits[0]), &solidRing)) {
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_SOLID_RING;
			context->failed = true;
			return false;
		}
		++context->emitPathSolidRingCount;
		*carryOut = solidRing.carryOut;
		if (!*carryOut) {
			uint8_t screenBeforeFlat[SLIPSTREAM_SCREEN_WIDTH * SLIPSTREAM_SCREEN_HEIGHT];
			bool haveScreenBeforeFlat = false;

			if (g_vehicleViewDumpDiagnostics) {
				TrackView_CopyScreenSnapshot(screenBeforeFlat);
				haveScreenBeforeFlat = true;
			}
			if (!SlipDraw3D_RasterizeFlatRing(context->drawRecordPool, solidRing.activeHeadOffsetOut,
			                                  regularSetup.drawMode, context->rendererFlags,
			                                  regularSetup.storedMaterialColor, regularSetup.materialDitherBits,
			                                  SLIP_DRAW3D_RECORD_NEXT_OFFSET, flatPoints,
			                                  sizeof(flatPoints) / sizeof(flatPoints[0]), &flatRing)) {
				context->failureAddress = TRACK_VIEW_DIAGNOSTIC_FLAT_RING;
				context->failed = true;
				return false;
			}
			if (g_vehicleViewDumpDiagnostics) {
				size_t flatPointIndex;

				fprintf(stderr,
				        "track_view_flat_ring_0001ae47 offset=0x%08x active=0x%08x any=0x%08x all=0x%08x depth_call=%u "
				        "screen_call=%u carry=%u mode=%u render=0x%08x color=0x%08x kind=%u points=%u rasterized=%u\n",
				        context->directCallbackActiveRecordOffset, solidRing.activeHeadOffsetOut,
				        solidRing.build.anyClipFlags, solidRing.build.allClipFlags,
				        solidRing.dispatch.dispatch.calledClipDepth ? 1u : 0u,
				        solidRing.dispatch.dispatch.calledClipScreen ? 1u : 0u, solidRing.carryOut ? 1u : 0u,
				        regularSetup.drawMode, context->rendererFlags, regularSetup.storedMaterialColor,
				        (unsigned)flatRing.dispatch.kind, (unsigned)flatRing.pointCount, flatRing.rasterized ? 1u : 0u);
				for (flatPointIndex = 0; flatPointIndex < flatRing.pointCount &&
				                         flatPointIndex < sizeof(flatPoints) / sizeof(flatPoints[0]);
				     ++flatPointIndex) {
					fprintf(stderr, "  flat_point=%zu x=%d y=%d\n", flatPointIndex, flatPoints[flatPointIndex].x,
					        flatPoints[flatPointIndex].y);
				}
				if (haveScreenBeforeFlat) {
					TrackView_DumpRecordDamage(context->directCallbackActiveRecordOffset, context->rendererFlags,
					                           screenBeforeFlat);
				}
			}
			++context->emitPathFlatDispatchCount;
			if (flatRing.rasterized) {
				++context->emitPathRasterizedCount;
			}
		}
		return true;
	}
	context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_GATE;
	context->failed = true;
	return false;
}

static bool TrackView_ExecuteObjectEntry(TrackViewRawBspContext *context, const uint8_t *objectRecord,
                                         size_t objectBytesRemaining, uint32_t objectAddress, uint32_t counterBefore) {
	SlipTrackWorldObjectListEntryResult objectListEntry;
	SlipTrackWorldObjectTransform objectTransform;
	SlipView3DMatrix viewMatrix;
	SlipView3DVec32 relativeObject;
	SlipView3DVec32 transformedObject;
	uint32_t objectCount;
	uint32_t objectListCursorAddress;

	if (context == NULL || objectRecord == NULL || context->objectList == NULL ||
	    context->objectListBytes < SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES ||
	    context->chunkCallbacks.viewMatrix == NULL) {
		return false;
	}
	objectCount = *(const uint32_t *)(const void *)context->objectList;
	if (objectCount > SLIP_TRACK_VISIBILITY_ENTRY_CAPACITY ||
	    (size_t)objectCount >
	        (context->objectListBytes - SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES) / SLIP_TRACK_VISIBILITY_ENTRY_BYTES) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_OBJECT_VISIBILITY_COUNT;
		context->failed = true;
		return false;
	}
	objectListCursorAddress = context->objectListBaseToken + SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES +
	                          objectCount * SLIP_TRACK_VISIBILITY_ENTRY_BYTES;
	if (!SlipTrackWorld_ObjectListEntry(
	        context->objectList, context->objectListBytes, context->objectListBaseToken, &objectListCursorAddress,
	        objectRecord, objectBytesRemaining, objectAddress, counterBefore, context->defaultTraversalGate,
	        context->nestedClipBoundsSuppressed, context->useFullObjectViewport, (uint32_t)context->projectState->minX,
	        (uint32_t)context->projectState->minY, (uint32_t)context->projectState->maxX,
	        (uint32_t)context->projectState->maxY, context->viewportMinX, context->viewportMinY, context->viewportMaxX,
	        context->viewportMaxY, &objectListEntry)) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_OBJECT_LIST_ENTRY;
		context->failed = true;
		return false;
	}
	if (objectListEntry.branch == SLIP_TRACK_WORLD_OBJECT_LIST_ENTRY_BRANCH_CAPACITY_REACHED) {
		return true;
	}
	viewMatrix = *context->chunkCallbacks.viewMatrix;
	relativeObject =
	    (SlipView3DVec32){(int32_t)(uint32_t)(SlipBytes_ReadLE32(objectRecord + SLIP_TRD_SECTION_ORIGIN_X_OFFSET) -
	                                          context->cameraWorldX),
	                      (int32_t)(uint32_t)(SlipBytes_ReadLE32(objectRecord + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET) -
	                                          context->cameraWorldY),
	                      (int32_t)(uint32_t)(SlipBytes_ReadLE32(objectRecord + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET) -
	                                          context->cameraWorldZ)};
	transformedObject = SlipView3D_TransformPositionByColumns(&viewMatrix, relativeObject);
	if (!SlipTrackWorld_ObjectTransform(
	        objectRecord, objectBytesRemaining, objectListEntry.entry,
	        context->objectListBytes - (size_t)(objectListEntry.entry - context->objectList), context->cameraWorldX,
	        context->cameraWorldY, context->cameraWorldZ, (uint32_t)transformedObject.x, (uint32_t)transformedObject.y,
	        (uint32_t)transformedObject.z, &objectTransform)) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_OBJECT_TRANSFORM;
		context->failed = true;
		return false;
	}
	return TrackView_ObjectContinuation(
	    objectRecord, objectBytesRemaining, objectAddress, objectListEntry.counter, objectListEntry.entry,
	    context->objectListBytes - (size_t)(objectListEntry.entry - context->objectList),
	    (SlipView3DVec32){(int32_t)objectTransform.storedObjectWorldX, (int32_t)objectTransform.storedObjectWorldY,
	                      (int32_t)objectTransform.storedObjectWorldZ},
	    (SlipView3DVec32){(int32_t)objectTransform.storedViewX, (int32_t)objectTransform.storedViewY,
	                      (int32_t)objectTransform.storedViewZ},
	    context);
}

enum { SLIP_TRACK_OBJECT_BOUNDS_VISIT_CAPACITY = 512, SLIP_ARTIC_PART_POINTER_BYTES = sizeof(uint32_t) };

bool TrackView_ObjectContinuation(const uint8_t *objectRecord, size_t objectBytesRemaining, uint32_t objectAddress,
                                  uint32_t counter, uint8_t *objectListEntry, size_t objectListEntryBytes,
                                  SlipView3DVec32 objectPosition, SlipView3DVec32 transformedObjectOffset,
                                  void *userData) {
	TrackViewRawBspContext *const context = (TrackViewRawBspContext *)userData;
	TrackViewPrimitiveCallbackContext primitiveContext;
	SlipTrackWorldComponentList componentList;
	SlipTrackWorldChildListDispatchExecute childDispatch;
	SlipTrackWorldPrimitiveBoundsCall *boundsCalls = NULL;
	SlipTrackWorldPrimitiveBoundsVisit *boundsVisits = NULL;
	SlipTrackWorldPrimitiveBoundsEvaluatedExecuteVisit *evaluatedVisits = NULL;
	SlipDraw3DReturnActiveVisit *returnVisits = NULL;
	SlipDraw3DClipFlagVisit *clipFlagVisits = NULL;
	SlipDraw3DPostPlaneBoundsVisit *postBoundsVisits = NULL;
	SlipDraw3DPostPlaneClipRecordVisit *postClipRecordVisits = NULL;
	SlipDraw3DPostPlaneClipPlaneVisit *postClipPlaneVisits = NULL;
	SlipDraw3DActiveRingVisit *activeVisits = NULL;
	SlipDraw3DActiveBoundsVisit *activeBoundsVisits = NULL;
	SlipDraw3DPolygonStatusVisit *drawStatusVisits = NULL;
	SlipTrackWorldPrimitiveRelatedScanVisit *relatedVisits = NULL;
	SlipTrackWorldPrimitiveBoundsEvaluatedExecute bounds;
	size_t primitiveVisitCount = 0;
	size_t componentListOffset;

#define TRACK_VIEW_FREE_OBJECT_CONTINUATION()                                                                          \
	do {                                                                                                               \
		free(boundsCalls);                                                                                             \
		free(boundsVisits);                                                                                            \
		free(evaluatedVisits);                                                                                         \
		free(returnVisits);                                                                                            \
		free(clipFlagVisits);                                                                                          \
		free(postBoundsVisits);                                                                                        \
		free(postClipRecordVisits);                                                                                    \
		free(postClipPlaneVisits);                                                                                     \
		free(activeVisits);                                                                                            \
		free(activeBoundsVisits);                                                                                      \
		free(drawStatusVisits);                                                                                        \
		free(relatedVisits);                                                                                           \
	} while (0)

	(void)objectAddress;
	if (context == NULL || objectRecord == NULL || objectListEntry == NULL ||
	    objectListEntryBytes < SLIP_TRACK_VISIBILITY_POSITION_END || context->componentBase == NULL ||
	    context->drawRecordPool == NULL || context->projectState == NULL || context->vertexRecords == NULL) {
		return false;
	}
	memset(&primitiveContext, 0, sizeof(primitiveContext));
	if (context->chunkCallbacks.viewMatrix == NULL) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_OBJECT_VIEW_MATRIX;
		context->failed = true;
		return false;
	}
	primitiveContext.viewMatrix = *context->chunkCallbacks.viewMatrix;
	primitiveContext.objectPosition = objectPosition;
	primitiveContext.transformedObjectOffset = transformedObjectOffset;
	primitiveContext.projectState = context->projectState;
	primitiveContext.frustum = &context->frustum;
	if (!SlipTrackWorld_ComponentList(objectRecord, objectBytesRemaining, context->componentBase,
	                                  context->componentBaseBytes, &componentList)) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_OBJECT_COMPONENT_LIST;
		context->failed = true;
		return false;
	}
	++context->objectContinuationCount;
	if (componentList.branch == SLIP_TRACK_WORLD_COMPONENT_LIST_BRANCH_EMPTY) {
		return true;
	}
	if (componentList.callTrackWorldChildListDispatch) {
		size_t sourceSize;

		memset(&childDispatch, 0, sizeof(childDispatch));
		if (!SlipTrackWorld_ChildListDispatch(
		        componentList.componentRecord,
		        context->componentBaseBytes - (size_t)(componentList.componentRecord - context->componentBase),
		        context->componentBase, context->componentBaseBytes, &childDispatch.dispatch)) {
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_OBJECT_CHILD_LIST_DISPATCH;
			context->failed = true;
			return false;
		}
		if (childDispatch.dispatch.vertexSource < context->componentBase ||
		    childDispatch.dispatch.vertexSource > context->componentBase + context->componentBaseBytes) {
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_OBJECT_CHILD_LIST_DISPATCH;
			context->failed = true;
			return false;
		}
		sourceSize =
		    context->componentBaseBytes - (size_t)(childDispatch.dispatch.vertexSource - context->componentBase);
		childDispatch.callBuildVertexRecords = true;
		if (!TrackView_BuildVertexRecords(
		        context, TRACK_VIEW_DIAGNOSTIC_OBJECT_CHILD_LIST_DISPATCH, childDispatch.dispatch.vertexSource,
		        sourceSize, childDispatch.dispatch.vertexCount, (int16_t)childDispatch.dispatch.vertexSourceStride,
		        TrackView_TransformVertex, TrackView_SourcePoint, NULL)) {
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_BUILD_VERTEX_RECORDS;
			context->failed = true;
			return false;
		}
		childDispatch.returned = true;
	}
	if (componentList.callTrackWorldChildListDispatch) {
		++context->objectChildListDispatchCount;
	}
	if (componentList.primitiveList < context->componentBase ||
	    componentList.primitiveList > context->componentBase + context->componentBaseBytes) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_OBJECT_PRIMITIVE_LIST;
		context->failed = true;
		return false;
	}
	componentListOffset = (size_t)(componentList.primitiveList - context->componentBase);
	boundsCalls =
	    (SlipTrackWorldPrimitiveBoundsCall *)calloc(SLIP_TRACK_OBJECT_BOUNDS_VISIT_CAPACITY, sizeof(*boundsCalls));
	boundsVisits =
	    (SlipTrackWorldPrimitiveBoundsVisit *)calloc(SLIP_TRACK_OBJECT_BOUNDS_VISIT_CAPACITY, sizeof(*boundsVisits));
	evaluatedVisits = (SlipTrackWorldPrimitiveBoundsEvaluatedExecuteVisit *)calloc(
	    SLIP_TRACK_OBJECT_BOUNDS_VISIT_CAPACITY, sizeof(*evaluatedVisits));
	returnVisits = (SlipDraw3DReturnActiveVisit *)calloc(SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, sizeof(*returnVisits));
	clipFlagVisits = (SlipDraw3DClipFlagVisit *)calloc(SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, sizeof(*clipFlagVisits));
	postBoundsVisits =
	    (SlipDraw3DPostPlaneBoundsVisit *)calloc(SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, sizeof(*postBoundsVisits));
	postClipRecordVisits = (SlipDraw3DPostPlaneClipRecordVisit *)calloc(SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT,
	                                                                    sizeof(*postClipRecordVisits));
	postClipPlaneVisits =
	    (SlipDraw3DPostPlaneClipPlaneVisit *)calloc(SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, sizeof(*postClipPlaneVisits));
	activeVisits = (SlipDraw3DActiveRingVisit *)calloc(SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, sizeof(*activeVisits));
	activeBoundsVisits =
	    (SlipDraw3DActiveBoundsVisit *)calloc(SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, sizeof(*activeBoundsVisits));
	drawStatusVisits =
	    (SlipDraw3DPolygonStatusVisit *)calloc(SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, sizeof(*drawStatusVisits));
	relatedVisits =
	    (SlipTrackWorldPrimitiveRelatedScanVisit *)calloc(SLIP_TRC_RELATED_OBJECT_COUNT, sizeof(*relatedVisits));
	if (boundsCalls == NULL || boundsVisits == NULL || evaluatedVisits == NULL || returnVisits == NULL ||
	    clipFlagVisits == NULL || postBoundsVisits == NULL || postClipRecordVisits == NULL ||
	    postClipPlaneVisits == NULL || activeVisits == NULL || activeBoundsVisits == NULL || drawStatusVisits == NULL ||
	    relatedVisits == NULL) {
		TRACK_VIEW_FREE_OBJECT_CONTINUATION();
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_OBJECT_BOUNDS;
		context->failed = true;
		return false;
	}
	{
		TrackViewPostPlaneArgs postPlaneArgs = TrackView_PostPlaneArgs(context);

		if (!SlipTrackWorld_PrimitiveBoundsEvaluatedExecute(
		        componentList.primitiveList, context->componentBaseBytes - componentListOffset,
		        context->renderContextCount, context->primaryLeft, context->primaryTop, context->primaryRight,
		        context->primaryBottom, context->mode, (const uint8_t *)context->vertexRecords,
		        context->vertexRecordCount * sizeof(context->vertexRecords[0]), context->origin, TrackView_SourcePoint,
		        context->drawRecordPool, context->vertexRecords, context->vertexRecordCount, context->projectState,
		        TrackView_TransformVertex, TrackView_ProjectScreenPrimary, TrackView_ProjectScreenSecondary,
		        &primitiveContext, postPlaneArgs.hasPostPlanes, postPlaneArgs.planeBase, postPlaneArgs.planeBytes,
		        postPlaneArgs.planeHeadOffset, postPlaneArgs.limitXMin, postPlaneArgs.limitXMax,
		        postPlaneArgs.limitYMin, postPlaneArgs.limitYMax, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, boundsCalls,
		        SLIP_TRACK_OBJECT_BOUNDS_VISIT_CAPACITY, boundsVisits, SLIP_TRACK_OBJECT_BOUNDS_VISIT_CAPACITY,
		        &primitiveVisitCount, evaluatedVisits, SLIP_TRACK_OBJECT_BOUNDS_VISIT_CAPACITY, returnVisits,
		        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, clipFlagVisits, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT,
		        postBoundsVisits, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, postClipRecordVisits,
		        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, postClipPlaneVisits, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT,
		        activeVisits, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, activeBoundsVisits,
		        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT,
		        context->drawStateRecord != NULL ? &context->drawStateRecord->matrix : NULL, &bounds)) {
			TRACK_VIEW_FREE_OBJECT_CONTINUATION();
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_OBJECT_BOUNDS;
			context->failed = true;
			return false;
		}
	}
	(void)primitiveVisitCount;
	context->renderContextCount = bounds.bounds.renderContextCount;
	context->primaryLeft = bounds.bounds.primaryLeft;
	context->primaryTop = bounds.bounds.primaryTop;
	context->primaryRight = bounds.bounds.primaryRight;
	context->primaryBottom = bounds.bounds.primaryBottom;
	++context->objectPrimitiveBoundsCount;
	if (componentList.primitiveList != NULL &&
	    componentListOffset + SLIP_TRC_TABLE_COUNT_BYTES <= context->componentBaseBytes) {
		const uint16_t drawCount = SlipBytes_ReadLE16(componentList.primitiveList);
		uint16_t drawRemaining = drawCount;
		size_t recordOffset = componentListOffset + SLIP_TRC_TABLE_COUNT_BYTES;
		uint32_t rangeFlag = 0;
		uint32_t rangeMinX = context->rangeMinX;
		uint32_t rangeMinY = context->rangeMinY;
		uint32_t rangeMaxX = context->rangeMaxX;
		uint32_t rangeMaxY = context->rangeMaxY;

		while (drawRemaining != 0) {
			const uint8_t *projectedRecord;
			size_t recordBytesRemaining;
			SlipTrackWorldPrimitivePreGate preGate;
			SlipTrackWorldPrimitiveDrawAdvance advance;
			bool enterRelatedScan = false;

			if (recordOffset > context->componentBaseBytes ||
			    context->componentBaseBytes - recordOffset < SLIP_TRC_PRIMITIVE_FIRST_INDEX_END) {
				TRACK_VIEW_FREE_OBJECT_CONTINUATION();
				context->failureAddress = TRACK_VIEW_DIAGNOSTIC_PRIMITIVE_PRE_GATE;
				context->failed = true;
				return false;
			}
			projectedRecord = context->componentBase + recordOffset;
			recordBytesRemaining = context->componentBaseBytes - recordOffset;
			++context->objectPrimitiveDrawLoopCount;
			if (!SlipTrackWorld_PrimitivePreGate(
			        projectedRecord, recordBytesRemaining, counter, componentList.storedComponentRecord,
			        context->componentBase, context->componentBaseBytes, 0, 0, 0, objectPosition,
			        (SlipView3DVec32){(int32_t)context->cameraWorldX, (int32_t)context->cameraWorldY,
			                          (int32_t)context->cameraWorldZ},
			        &preGate)) {
				TRACK_VIEW_FREE_OBJECT_CONTINUATION();
				context->failureAddress = TRACK_VIEW_DIAGNOSTIC_PRIMITIVE_PRE_GATE;
				context->failed = true;
				return false;
			}
			if (preGate.storeRangeFlagZero || preGate.storeRangeFlagMinusOne) {
				rangeFlag = preGate.rangeFlag;
			}
			if (preGate.branch == SLIP_TRACK_WORLD_PRIMITIVE_PRE_GATE_BRANCH_RANGE_REJECT) {
				enterRelatedScan = true;
			} else if (preGate.branch == SLIP_TRACK_WORLD_PRIMITIVE_PRE_GATE_BRANCH_DRAW) {
				SlipTrackWorldPrimitiveDrawGateEvaluated drawGate;
				TrackViewPrimitiveDrawGateTrace *drawGateTrace = NULL;

				++context->objectPrimitiveDrawGateCount;
				if (!SlipTrackWorld_PrimitiveDrawGateEvaluated(
				        projectedRecord, recordBytesRemaining, *(const uint32_t *)(const void *)context->objectList,
				        context->mode, context->vertexRecords, context->vertexRecordCount, context->origin,
				        TrackView_SourcePoint, context->projectState, TrackView_TransformVertex,
				        TrackView_PrimitiveProjectMask, &primitiveContext, drawStatusVisits,
				        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT,
				        context->drawStateRecord != NULL ? &context->drawStateRecord->matrix : NULL, &drawGate)) {
					TRACK_VIEW_FREE_OBJECT_CONTINUATION();
					context->failureAddress = TRACK_VIEW_DIAGNOSTIC_PRIMITIVE_DRAW_GATE;
					context->failed = true;
					return false;
				}
				if (context->drawGateTraceCount < sizeof(context->drawGateTrace) / sizeof(context->drawGateTrace[0])) {
					drawGateTrace = &context->drawGateTrace[context->drawGateTraceCount++];
					*drawGateTrace =
					    (TrackViewPrimitiveDrawGateTrace){objectAddress,
					                                      context->componentBaseToken + (uint32_t)recordOffset,
					                                      drawGate.gate.pointIndex,
					                                      drawGate.gate.normalX,
					                                      drawGate.gate.normalY,
					                                      drawGate.gate.normalZ,
					                                      context->origin.x,
					                                      context->origin.y,
					                                      context->origin.z,
					                                      drawGate.plane.sourceX,
					                                      drawGate.plane.sourceY,
					                                      drawGate.plane.sourceZ,
					                                      drawGate.plane.transformedSourcePoint.x,
					                                      drawGate.plane.transformedSourcePoint.y,
					                                      drawGate.plane.transformedSourcePoint.z,
					                                      drawGate.plane.pointMinusOrigin.x,
					                                      drawGate.plane.pointMinusOrigin.y,
					                                      drawGate.plane.pointMinusOrigin.z,
					                                      drawGate.plane.planeX,
					                                      drawGate.plane.planeY,
					                                      drawGate.plane.planeZ,
					                                      drawGate.plane.dotProduct,
					                                      drawGate.polygonStatus.allClipFlags,
					                                      drawGate.polygonStatus.anyClipFlags,
					                                      drawGate.polygonStatus.finalAllMask,
					                                      drawGate.polygonStatus.finalAnyMask,
					                                      drawGate.gate.planeRejected ? 1u : 0u,
					                                      drawGate.gate.polygonRejected ? 1u : 0u,
					                                      (uint32_t)drawGate.gate.branch};
				}
				if (drawGate.gate.branch == SLIP_TRACK_WORLD_PRIMITIVE_DRAW_GATE_BRANCH_DRAW) {
					enterRelatedScan = true;
				} else {
					++context->objectPrimitiveDrawGateSkipCount;
				}
			} else {
				++context->objectPrimitiveDrawPreSkipCount;
			}
			if (enterRelatedScan) {
				SlipTrackWorldPrimitiveRelatedScanExecute related;
				SlipTrackWorldPrimitiveRangeStateEvaluatedExecute range;
				uint32_t relatedObjectAddress = objectAddress;
				size_t relatedVisitCount = 0;
				const uint32_t savedRangeMinX = rangeMinX;
				const uint32_t savedRangeMinY = rangeMinY;
				const uint32_t savedRangeMaxX = rangeMaxX;
				const uint32_t savedRangeMaxY = rangeMaxY;

				memset(relatedVisits, 0, SLIP_TRC_RELATED_OBJECT_COUNT * sizeof(*relatedVisits));
				++context->objectPrimitiveRelatedScanCount;
				if (!SlipTrackWorld_PrimitiveRelatedScanExecute(
				        context->componentBaseToken + (uint32_t)recordOffset, componentList.currentObject,
				        context->componentBaseBytes - (size_t)(componentList.currentObject - context->componentBase),
				        context->componentBaseToken, context->chunkBaseToken, context->excludedRelatedObjectToken,
				        context->objectList, context->objectListBytes, context->viewportMinX, context->viewportMinY,
				        context->viewportMaxX, context->viewportMaxY, rangeMinX, rangeMinY, rangeMaxX, rangeMaxY,
				        componentList.storedComponentRecord, (uint32_t)context->projectState->minX,
				        (uint32_t)context->projectState->minY, (uint32_t)context->projectState->maxX,
				        (uint32_t)context->projectState->maxY, relatedVisits, SLIP_TRC_RELATED_OBJECT_COUNT,
				        &relatedVisitCount, &related)) {
					TRACK_VIEW_FREE_OBJECT_CONTINUATION();
					context->failureAddress = TRACK_VIEW_DIAGNOSTIC_PRIMITIVE_RELATED_SCAN;
					context->failed = true;
					return false;
				}
				if (related.scan.calledRestoreClipBounds) {
					if (!TrackView_StoreClipBounds(context, related.scan.entryMinX, related.scan.entryMinY,
					                               related.scan.entryMaxX, related.scan.entryMaxY)) {
						TRACK_VIEW_FREE_OBJECT_CONTINUATION();
						context->failureAddress = TRACK_VIEW_DIAGNOSTIC_STORE_CLIP_BOUNDS;
						context->failed = true;
						return false;
					}
				}
				if (related.scan.calledRestoreVertexBufferCursor &&
				    !TrackView_RestoreVertexBufferCursor(context, TRACK_VIEW_DIAGNOSTIC_PRIMITIVE_RELATED_SCAN)) {
					TRACK_VIEW_FREE_OBJECT_CONTINUATION();
					context->failureAddress = TRACK_VIEW_DIAGNOSTIC_RESTORE_VERTEX_CURSOR;
					context->failed = true;
					return false;
				}
				if (related.scan.callTrackWorldChildListDispatch) {
					size_t sourceSize;

					memset(&childDispatch, 0, sizeof(childDispatch));
					if (!SlipTrackWorld_ChildListDispatch(
					        related.scan.dispatchComponent,
					        context->componentBaseBytes -
					            (size_t)(related.scan.dispatchComponent - context->componentBase),
					        context->componentBase, context->componentBaseBytes, &childDispatch.dispatch)) {
						TRACK_VIEW_FREE_OBJECT_CONTINUATION();
						context->failureAddress = TRACK_VIEW_DIAGNOSTIC_RELATED_CHILD_LIST_DISPATCH;
						context->failed = true;
						return false;
					}
					if (childDispatch.dispatch.vertexSource < context->componentBase ||
					    childDispatch.dispatch.vertexSource > context->componentBase + context->componentBaseBytes) {
						TRACK_VIEW_FREE_OBJECT_CONTINUATION();
						context->failureAddress = TRACK_VIEW_DIAGNOSTIC_RELATED_CHILD_LIST_DISPATCH;
						context->failed = true;
						return false;
					}
					sourceSize = context->componentBaseBytes -
					             (size_t)(childDispatch.dispatch.vertexSource - context->componentBase);
					childDispatch.callBuildVertexRecords = true;
					if (!TrackView_BuildVertexRecords(context, TRACK_VIEW_DIAGNOSTIC_RELATED_CHILD_LIST_DISPATCH,
					                                  childDispatch.dispatch.vertexSource, sourceSize,
					                                  childDispatch.dispatch.vertexCount,
					                                  (int16_t)childDispatch.dispatch.vertexSourceStride,
					                                  TrackView_TransformVertex, TrackView_SourcePoint, NULL)) {
						TRACK_VIEW_FREE_OBJECT_CONTINUATION();
						context->failureAddress = TRACK_VIEW_DIAGNOSTIC_BUILD_VERTEX_RECORDS;
						context->failed = true;
						return false;
					}
					childDispatch.returned = true;
					++context->objectChildListDispatchCount;
				}
				for (size_t visitIndex = 0; visitIndex < relatedVisitCount; ++visitIndex) {
					if (relatedVisits[visitIndex].matchesComponent && !relatedVisits[visitIndex].matchesCurrentObject) {
						relatedObjectAddress = relatedVisits[visitIndex].objectAddress;
						break;
					}
				}
				if (related.scan.branch == SLIP_TRACK_WORLD_PRIMITIVE_RELATED_SCAN_BRANCH_UNMATCHED) {
					++context->objectPrimitiveRelatedScanToEpilogueCount;
				} else {
					TrackViewPrimitiveRangeTrace *rangeTrace = NULL;

					if (context->rangeTraceCount < sizeof(context->rangeTrace) / sizeof(context->rangeTrace[0])) {
						rangeTrace = &context->rangeTrace[context->rangeTraceCount++];
						*rangeTrace = (TrackViewPrimitiveRangeTrace){
						    .objectAddress = objectAddress,
						    .componentAddress =
						        context->componentBaseToken +
						        (uint32_t)(componentList.storedComponentRecord - context->componentBase),
						    .primitiveAddress = context->componentBaseToken + (uint32_t)recordOffset,
						    .relatedObjectAddress = relatedObjectAddress,
						    .counter = counter,
						    .rangeFlag = rangeFlag,
						    .objectListCountBefore = *(const uint32_t *)(const void *)context->objectList,
						    .objectListCountAfter = *(const uint32_t *)(const void *)context->objectList,
						    .rangeMinXBefore = rangeMinX,
						    .rangeMinYBefore = rangeMinY,
						    .rangeMaxXBefore = rangeMaxX,
						    .rangeMaxYBefore = rangeMaxY,
						    .rangeMinXAfter = rangeMinX,
						    .rangeMinYAfter = rangeMinY,
						    .rangeMaxXAfter = rangeMaxX,
						    .rangeMaxYAfter = rangeMaxY,
						    .relatedBranch = (uint32_t)related.scan.branch,
						    .rangeBranch = SLIP_TRACK_WORLD_PRIMITIVE_RANGE_STATE_BRANCH_CONTINUE};
					}
					++context->objectPrimitiveRangeStateCount;
					{
						TrackViewPostPlaneArgs postPlaneArgs = TrackView_PostPlaneArgs(context);
						if (!SlipTrackWorld_PrimitiveRangeStateEvaluatedExecute(
						        projectedRecord, recordBytesRemaining, relatedObjectAddress, counter, rangeFlag,
						        context->viewportMinX, context->viewportMinY, context->viewportMaxX,
						        context->viewportMaxY, rangeMinX, rangeMinY, rangeMaxX, rangeMaxY,
						        context->drawRecordPool, context->vertexRecords, context->vertexRecordCount,
						        context->projectState, TrackView_TransformVertex, TrackView_ProjectScreenPrimary,
						        TrackView_ProjectScreenSecondary, &primitiveContext, postPlaneArgs.hasPostPlanes,
						        postPlaneArgs.planeBase, postPlaneArgs.planeBytes, postPlaneArgs.planeHeadOffset,
						        postPlaneArgs.limitXMin, postPlaneArgs.limitXMax, postPlaneArgs.limitYMin,
						        postPlaneArgs.limitYMax, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, returnVisits,
						        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, clipFlagVisits,
						        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, postBoundsVisits,
						        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, postClipRecordVisits,
						        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, postClipPlaneVisits,
						        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, activeVisits,
						        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, activeBoundsVisits,
						        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, &range)) {
							TRACK_VIEW_FREE_OBJECT_CONTINUATION();
							context->failureAddress = TRACK_VIEW_DIAGNOSTIC_PRIMITIVE_RANGE;
							context->failed = true;
							return false;
						}
					}
					if (rangeTrace != NULL) {
						rangeTrace->rangeMinXAfter = range.range.rangeMinX;
						rangeTrace->rangeMinYAfter = range.range.rangeMinY;
						rangeTrace->rangeMaxXAfter = range.range.rangeMaxX;
						rangeTrace->rangeMaxYAfter = range.range.rangeMaxY;
						rangeTrace->primitivePathCarry = range.primitive.carryOut ? 1u : 0u;
						rangeTrace->primitivePathBoundsMinX = (uint32_t)range.primitive.minX;
						rangeTrace->primitivePathBoundsMinY = (uint32_t)range.primitive.minY;
						rangeTrace->primitivePathBoundsMaxX = (uint32_t)range.primitive.maxX;
						rangeTrace->primitivePathBoundsMaxY = (uint32_t)range.primitive.maxY;
						rangeTrace->rangeBranch = (uint32_t)range.range.branch;
						rangeTrace->activeRingVisitCount = (uint32_t)range.primitive.activeRing.build.visitCount;
						for (size_t activeVisitIndex = 0;
						     activeVisitIndex < range.primitive.activeRing.build.visitCount &&
						     activeVisitIndex < SLIP_TRACK_DIAGNOSTIC_ACTIVE_VERTEX_LIMIT;
						     ++activeVisitIndex) {
							rangeTrace->activeRingIndex[activeVisitIndex] = activeVisits[activeVisitIndex].indexWord;
							rangeTrace->activeRingFlags[activeVisitIndex] =
							    activeVisits[activeVisitIndex].flagsFromProjectVertex;
							if ((size_t)activeVisits[activeVisitIndex].indexWord < context->vertexRecordCount) {
								const SlipDraw3DVertexRecord *const activeRecord =
								    &context->vertexRecords[activeVisits[activeVisitIndex].indexWord];
								rangeTrace->activeRingSourceX[activeVisitIndex] = (int16_t)SlipBytes_ReadLE16(
								    activeRecord->bytes + SLIP_DRAW3D_VERTEX_RECORD_SOURCE_OFFSET);
								rangeTrace->activeRingSourceY[activeVisitIndex] = (int16_t)SlipBytes_ReadLE16(
								    activeRecord->bytes + offsetof(SlipDraw3DVertexRecord, sourceY));
								rangeTrace->activeRingSourceZ[activeVisitIndex] = (int16_t)SlipBytes_ReadLE16(
								    activeRecord->bytes + offsetof(SlipDraw3DVertexRecord, sourceZ));
								rangeTrace->activeRingWorldX[activeVisitIndex] = (int32_t)SlipBytes_ReadLE32(
								    activeRecord->bytes + SLIP_DRAW3D_VERTEX_RECORD_WORLD_OFFSET);
								rangeTrace->activeRingWorldY[activeVisitIndex] = (int32_t)SlipBytes_ReadLE32(
								    activeRecord->bytes + offsetof(SlipDraw3DVertexRecord, world.y));
								rangeTrace->activeRingWorldZ[activeVisitIndex] = (int32_t)SlipBytes_ReadLE32(
								    activeRecord->bytes + offsetof(SlipDraw3DVertexRecord, world.z));
								rangeTrace->activeRingScreenX[activeVisitIndex] = (int32_t)SlipBytes_ReadLE32(
								    activeRecord->bytes + SLIP_DRAW3D_VERTEX_RECORD_SCREEN_OFFSET);
								rangeTrace->activeRingScreenY[activeVisitIndex] = (int32_t)SlipBytes_ReadLE32(
								    activeRecord->bytes + offsetof(SlipDraw3DVertexRecord, screenY));
							}
						}
					}
					rangeMinX = range.range.rangeMinX;
					rangeMinY = range.range.rangeMinY;
					rangeMaxX = range.range.rangeMaxX;
					rangeMaxY = range.range.rangeMaxY;
					context->rangeMinX = rangeMinX;
					context->rangeMinY = rangeMinY;
					context->rangeMaxX = rangeMaxX;
					context->rangeMaxY = rangeMaxY;
					if (range.range.calledRestoreClipBounds) {
						if (!TrackView_StoreClipBounds(context, range.range.rangeMinX, range.range.rangeMinY,
						                               range.range.rangeMaxX, range.range.rangeMaxY)) {
							TRACK_VIEW_FREE_OBJECT_CONTINUATION();
							context->failureAddress = TRACK_VIEW_DIAGNOSTIC_STORE_CLIP_BOUNDS;
							context->failed = true;
							return false;
						}
					}
					if (range.range.calledRestoreVertexBufferCursor &&
					    !TrackView_RestoreVertexBufferCursor(context, TRACK_VIEW_DIAGNOSTIC_PRIMITIVE_RANGE)) {
						TRACK_VIEW_FREE_OBJECT_CONTINUATION();
						context->failureAddress = TRACK_VIEW_DIAGNOSTIC_RESTORE_VERTEX_CURSOR;
						context->failed = true;
						return false;
					}
					if (range.range.branch == SLIP_TRACK_WORLD_PRIMITIVE_RANGE_STATE_BRANCH_RESTORED) {
						SlipTrackWorldPrimitiveNestedObjectCall nested;
						const uint8_t *nestedObject;
						size_t nestedObjectOffset;
						const uint32_t savedExcludedObjectToken = context->excludedRelatedObjectToken;
						const uint32_t savedClipBoundsSuppression = context->nestedClipBoundsSuppressed;
						const uint32_t currentComponentAddress =
						    context->componentBaseToken +
						    (uint32_t)(componentList.storedComponentRecord - context->componentBase);

						++context->objectPrimitiveRangeStateToNestedCount;
						if (!SlipTrackWorld_PrimitiveNestedObjectCall(
						        (uint32_t)transformedObjectOffset.x, (uint32_t)transformedObjectOffset.y,
						        (uint32_t)transformedObjectOffset.z, (uint32_t)objectPosition.x,
						        (uint32_t)objectPosition.y, (uint32_t)objectPosition.z, objectAddress,
						        currentComponentAddress, context->excludedRelatedObjectToken,
						        context->nestedClipBoundsSuppressed, context->renderContextCount, &nested)) {
							TRACK_VIEW_FREE_OBJECT_CONTINUATION();
							context->failureAddress = TRACK_VIEW_DIAGNOSTIC_NESTED_PRIMITIVE_SETUP;
							context->failed = true;
							return false;
						}
						if (rangeTrace != NULL) {
							rangeTrace->nestedExcludedObjectToken = nested.storedComponentRecord;
							rangeTrace->nestedClipBoundsSuppressed = nested.storedRenderContextCount;
							rangeTrace->nestedCall = nested.callTrackWorldObjectListEntry ? 1u : 0u;
						}
						++context->objectPrimitiveNestedPendingCount;
						if (relatedObjectAddress < context->chunkBaseToken) {
							TRACK_VIEW_FREE_OBJECT_CONTINUATION();
							context->failureAddress = TRACK_VIEW_DIAGNOSTIC_RELATED_OBJECT_RECORD;
							context->failed = true;
							return false;
						}
						nestedObjectOffset = (size_t)(relatedObjectAddress - context->chunkBaseToken);
						if (nestedObjectOffset > context->chunkBaseBytes ||
						    context->chunkBaseBytes - nestedObjectOffset < SLIP_TRD_SECTION_ORIGIN_END) {
							TRACK_VIEW_FREE_OBJECT_CONTINUATION();
							context->failureAddress = TRACK_VIEW_DIAGNOSTIC_RELATED_OBJECT_RECORD;
							context->failed = true;
							return false;
						}
						nestedObject = context->chunkBase + nestedObjectOffset;
						context->nestedClipBoundsSuppressed = nested.storedRenderContextCount;
						context->excludedRelatedObjectToken = nested.storedComponentRecord;
						if (!TrackView_ExecuteObjectEntry(context, nestedObject,
						                                  context->chunkBaseBytes - nestedObjectOffset,
						                                  relatedObjectAddress, counter)) {
							context->nestedClipBoundsSuppressed = savedClipBoundsSuppression;
							context->excludedRelatedObjectToken = savedExcludedObjectToken;
							TRACK_VIEW_FREE_OBJECT_CONTINUATION();
							return false;
						}
						if (rangeTrace != NULL) {
							rangeTrace->objectListCountAfter = *(const uint32_t *)(const void *)context->objectList;
						}
						context->nestedClipBoundsSuppressed = savedClipBoundsSuppression;
						context->excludedRelatedObjectToken = savedExcludedObjectToken;
						if (nested.callTrackWorldChildListDispatch) {
							size_t sourceSize;

							memset(&childDispatch, 0, sizeof(childDispatch));
							if (!SlipTrackWorld_ChildListDispatch(
							        componentList.storedComponentRecord,
							        context->componentBaseBytes -
							            (size_t)(componentList.storedComponentRecord - context->componentBase),
							        context->componentBase, context->componentBaseBytes, &childDispatch.dispatch)) {
								TRACK_VIEW_FREE_OBJECT_CONTINUATION();
								context->failureAddress = TRACK_VIEW_DIAGNOSTIC_STORED_COMPONENT_CHILD_LIST_DISPATCH;
								context->failed = true;
								return false;
							}
							if (childDispatch.dispatch.vertexSource < context->componentBase ||
							    childDispatch.dispatch.vertexSource >
							        context->componentBase + context->componentBaseBytes) {
								TRACK_VIEW_FREE_OBJECT_CONTINUATION();
								context->failureAddress = TRACK_VIEW_DIAGNOSTIC_STORED_COMPONENT_CHILD_LIST_DISPATCH;
								context->failed = true;
								return false;
							}
							sourceSize = context->componentBaseBytes -
							             (size_t)(childDispatch.dispatch.vertexSource - context->componentBase);
							childDispatch.callBuildVertexRecords = true;
							if (!TrackView_BuildVertexRecords(context, TRACK_VIEW_DIAGNOSTIC_OBJECT_CHILD_LIST_DISPATCH,
							                                  childDispatch.dispatch.vertexSource, sourceSize,
							                                  childDispatch.dispatch.vertexCount,
							                                  (int16_t)childDispatch.dispatch.vertexSourceStride,
							                                  TrackView_TransformVertex, TrackView_SourcePoint, NULL)) {
								TRACK_VIEW_FREE_OBJECT_CONTINUATION();
								context->failureAddress = TRACK_VIEW_DIAGNOSTIC_BUILD_VERTEX_RECORDS;
								context->failed = true;
								return false;
							}
							childDispatch.returned = true;
						}
						if (nested.callTrackWorldChildListDispatch) {
							++context->objectChildListDispatchCount;
						}
					} else {
						++context->objectPrimitiveRangeStateToEpilogueCount;
						if (rangeTrace != NULL) {
							rangeTrace->objectListCountAfter = *(const uint32_t *)(const void *)context->objectList;
						}
					}
				}
				rangeMinX = savedRangeMinX;
				rangeMinY = savedRangeMinY;
				rangeMaxX = savedRangeMaxX;
				rangeMaxY = savedRangeMaxY;
				context->rangeMinX = savedRangeMinX;
				context->rangeMinY = savedRangeMinY;
				context->rangeMaxX = savedRangeMaxX;
				context->rangeMaxY = savedRangeMaxY;
				if (!TrackView_StoreClipBounds(context, related.load.clipMinX, related.load.clipMinY,
				                               related.load.clipMaxX, related.load.clipMaxY)) {
					TRACK_VIEW_FREE_OBJECT_CONTINUATION();
					context->failureAddress = TRACK_VIEW_DIAGNOSTIC_STORE_CLIP_BOUNDS;
					context->failed = true;
					return false;
				}
			}
			if (!SlipTrackWorld_PrimitiveDrawAdvance(projectedRecord, recordBytesRemaining,
			                                         recordOffset - componentListOffset, drawRemaining, &advance)) {
				TRACK_VIEW_FREE_OBJECT_CONTINUATION();
				context->failureAddress = TRACK_VIEW_DIAGNOSTIC_PRIMITIVE_DRAW_ADVANCE;
				context->failed = true;
				return false;
			}
			++context->objectPrimitiveDrawAdvanceCount;
			drawRemaining = advance.remainingCountAfter;
			recordOffset = componentListOffset + advance.recordOffsetAfterAdvance;
		}
		++context->objectPrimitiveOuterTailCount;
		if (!TrackView_RestoreVertexBufferCursor(context, TRACK_VIEW_DIAGNOSTIC_OBJECT_RESTORE_VERTEX_CURSOR)) {
			TRACK_VIEW_FREE_OBJECT_CONTINUATION();
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_RESTORE_VERTEX_CURSOR;
			context->failed = true;
			return false;
		}
	}
	TRACK_VIEW_FREE_OBJECT_CONTINUATION();
#undef TRACK_VIEW_FREE_OBJECT_CONTINUATION
	return true;
}

bool TrackView_DirectCallback(const uint8_t *primitiveRecord, size_t recordBytesRemaining, size_t recordOffset,
                              const SlipTrackWorldDirectCallbackEnvironment *environment, uint32_t callbackInput,
                              void *userData, uint32_t *callbackResult, bool *carryFromCallback) {
	TrackViewRawBspContext *const context = (TrackViewRawBspContext *)userData;
	TrackViewPrimitiveCallbackContext primitiveContext;
	SlipTrackWorldPrimitiveCallbackDispatch dispatch;
	uint32_t savedContextRenderFlags;
	uint32_t callbackRenderFlags;
	bool planeRejected = false;
	bool trackWorldGlobalCarryGateCarry = false;
	uint8_t flags;
	uint8_t materialFlags;
	uint8_t skipMask;
	uint16_t countAndFlags;
	uint16_t materialIndex;
	uint16_t normalX;
	uint16_t normalY;
	uint16_t normalZ;

	if (context == NULL || environment == NULL || environment->componentSetup == NULL || primitiveRecord == NULL ||
	    recordBytesRemaining < SLIP_TRC_PRIMITIVE_HEADER_BYTES || callbackResult == NULL || carryFromCallback == NULL) {
		return false;
	}
	savedContextRenderFlags = context->rendererFlags;
	callbackRenderFlags = environment->componentSetup->drawFlags.rendererFlags;
	context->rendererFlags = callbackRenderFlags;

	{
		uint16_t replayIndex;
		const uint16_t replayCount = environment->componentSetup->replayCount;

		for (replayIndex = 0; replayIndex < replayCount; ++replayIndex) {
			const uint16_t replayObjectOffset = environment->componentSetup->replayList[replayIndex];

			context->replayListBuffer[replayIndex * SLIP_TRACK_REPLAY_OBJECT_OFFSET_BYTES] =
			    (uint8_t)(replayObjectOffset & UINT8_MAX);
			context->replayListBuffer[replayIndex * SLIP_TRACK_REPLAY_OBJECT_OFFSET_BYTES + 1u] =
			    (uint8_t)(replayObjectOffset >> 8);
		}
		context->replayCount = replayCount;
		context->replayList = context->replayListBuffer;
		context->replayListBytes = sizeof(context->replayListBuffer);
	}
	flags = primitiveRecord[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET];
	materialFlags = primitiveRecord[SLIP_TRC_PRIMITIVE_MATERIAL_FLAGS_OFFSET];
	countAndFlags = SlipBytes_ReadLE16(primitiveRecord);
	normalX = SlipBytes_ReadLE16(primitiveRecord + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET);
	normalY = SlipBytes_ReadLE16(primitiveRecord + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET);
	normalZ = SlipBytes_ReadLE16(primitiveRecord + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET);
	materialIndex = SlipBytes_ReadLE16(primitiveRecord + SLIP_TRC_PRIMITIVE_MATERIAL_OFFSET);

	if ((materialIndex & SLIP_TRC_PRIMITIVE_MATERIAL_UNRESOLVED) != 0) {
		materialIndex = 0;
	}
	context->directCallbackActiveRecordOffset = (uint32_t)recordOffset;
	context->directCallbackActivePolygonCountAndFlags = countAndFlags;
	context->directCallbackActiveMaterialIndex = materialIndex;
	context->directCallbackActivePlaneIndex =
	    recordBytesRemaining >= SLIP_TRC_PRIMITIVE_FIRST_INDEX_END
	        ? SlipBytes_ReadLE16(primitiveRecord + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET)
	        : 0;
	context->directCallbackActiveFlags = flags;
	context->directCallbackActiveMaterialFlags = materialFlags;
	if (g_vehicleViewDumpDiagnostics) {
		fprintf(stderr,
		        "track_view_direct_record_0003872c offset=0x%zx bp=0x%04x ax=0x%04x bx=0x%04x cx=0x%04x flags=0x%02x "
		        "mat=0x%02x dx=0x%04x plane=0x%04x bytes=",
		        recordOffset, countAndFlags, normalX, normalY, normalZ, flags, materialFlags, materialIndex,
		        recordBytesRemaining >= SLIP_TRC_PRIMITIVE_FIRST_INDEX_END
		            ? SlipBytes_ReadLE16(primitiveRecord + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET)
		            : 0);
		TrackView_DumpPayloadBytes(primitiveRecord, recordBytesRemaining, SLIP_PRIMITIVE_DIAGNOSTIC_PAYLOAD_DUMP_BYTES);
		fprintf(stderr, "\n");
	}
	if (g_vehicleViewDumpDiagnostics && TrackView_ShouldDumpCallbackRecord(recordOffset)) {
		const SlipTrackWorldDrawFlags *const drawFlags = &environment->componentSetup->drawFlags;
		const SlipTrackWorldComponentTail *const componentTail = environment->componentTail;

		fprintf(stderr,
		        "track_view_callback_flags_000396eb offset=0x%zx setup_in=0x%08x setup_ecx=%d test_cde=0x%08x "
		        "after_cde=0x%08x after_or2=0x%08x test_cce=0x%08x cmp_cea=%d after_cce=0x%08x after_or4=0x%08x "
		        "test_cd2=0x%08x cmp_cee=%d out=0x%08x tail_in=0x%08x tail_out=0x%08x\n",
		        recordOffset, drawFlags->renderFlagsValue, (int32_t)environment->componentSetup->componentViewZ,
		        drawFlags->textureMode, drawFlags->flagsAfterTextureModeAndClearBit8, drawFlags->flagsWithBit2,
		        drawFlags->shading, (int32_t)drawFlags->componentDistance, drawFlags->flagsAfterShadingGate,
		        drawFlags->flagsWithBit4, drawFlags->secondaryShading, (int32_t)drawFlags->componentRadius,
		        drawFlags->rendererFlags, componentTail != NULL ? componentTail->renderFlagsValue : 0,
		        componentTail != NULL ? componentTail->restoredRenderFlags : 0);
	}

	skipMask = environment->inlineWalkMode ? SLIP_TRC_PRIMITIVE_WALK_SKIP_MASK : SLIP_TRC_PRIMITIVE_CALLBACK_SKIP_MASK;
	context->directCallbackActiveBranchMask =
	    ((flags & skipMask) != 0 ? SLIP_TRACK_CALLBACK_SKIPPED : 0u) |
	    ((countAndFlags & SLIP_TRC_PRIMITIVE_TEXTURED) != 0 ? SLIP_TRACK_CALLBACK_TEXTURED : 0u) |
	    ((flags & SLIP_TRC_PRIMITIVE_SPECIAL_PLANE) != 0 ? SLIP_TRACK_CALLBACK_SPECIAL_PLANE : 0u);
	if ((flags & skipMask) != 0) {
		++context->directCallbackSkipCount;
	} else {
		SlipTrackWorldPlaneClassify plane;
		uint16_t planeVertexIndex;

		if (recordBytesRemaining < SLIP_TRC_PRIMITIVE_FIRST_INDEX_END) {
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_DIRECT_PRIMITIVE_HEADER;
			context->failed = true;
			return false;
		}
		memset(&primitiveContext, 0, sizeof(primitiveContext));
		if (context->chunkCallbacks.viewMatrix != NULL) {
			primitiveContext.viewMatrix = *context->chunkCallbacks.viewMatrix;
		}
		primitiveContext.objectPosition = environment->componentSetup->objectWorldPosition;
		primitiveContext.transformedObjectOffset = environment->transformedObjectOffset;
		if (g_vehicleViewDumpDiagnostics) {
			fprintf(stderr,
			        "track_view_direct_state_0003872c offset=0x%zx origin=%d,%d,%d local=%d,%d,%d transformed=%d,%d,%d "
			        "matrix=%04x,%04x,%04x,%04x,%04x,%04x,%04x,%04x,%04x\n",
			        recordOffset, context->origin.x, context->origin.y, context->origin.z,
			        primitiveContext.objectPosition.x, primitiveContext.objectPosition.y,
			        primitiveContext.objectPosition.z, primitiveContext.transformedObjectOffset.x,
			        primitiveContext.transformedObjectOffset.y, primitiveContext.transformedObjectOffset.z,
			        (uint16_t)primitiveContext.viewMatrix.m[0], (uint16_t)primitiveContext.viewMatrix.m[1],
			        (uint16_t)primitiveContext.viewMatrix.m[2], (uint16_t)primitiveContext.viewMatrix.m[3],
			        (uint16_t)primitiveContext.viewMatrix.m[4], (uint16_t)primitiveContext.viewMatrix.m[5],
			        (uint16_t)primitiveContext.viewMatrix.m[6], (uint16_t)primitiveContext.viewMatrix.m[7],
			        (uint16_t)primitiveContext.viewMatrix.m[8]);
		}
		primitiveContext.projectState = context->projectState;
		primitiveContext.frustum = &context->frustum;
		planeVertexIndex = SlipBytes_ReadLE16(primitiveRecord + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET);
		if (!SlipTrackWorld_ClassifyPlaneFromSource(
		        context->mode, planeVertexIndex, normalX, normalY, normalZ, (const uint8_t *)context->vertexRecords,
		        context->vertexRecordCount * sizeof(context->vertexRecords[0]), context->origin, TrackView_SourcePoint,
		        &primitiveContext, context->drawStateRecord != NULL ? &context->drawStateRecord->matrix : NULL,
		        &plane)) {
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_CLASSIFY_SOURCE_PLANE;
			context->failed = true;
			context->rendererFlags = savedContextRenderFlags;
			return false;
		}
		planeRejected = plane.carry;
		if (planeRejected) {
			context->directCallbackActiveBranchMask |= SLIP_TRACK_CALLBACK_PLANE_REJECTED;
			++context->directCallbackPlaneRejectedCount;
		}
		if (!planeRejected && (flags & SLIP_TRC_PRIMITIVE_SPECIAL_PLANE) != 0) {
			SlipTrackWorldGlobalCarryGate globalCarry;

			if (!SlipTrackWorld_GlobalCarryGate(context->flaggedPrimitiveReject, &globalCarry)) {
				context->rendererFlags = savedContextRenderFlags;
				return false;
			}
			trackWorldGlobalCarryGateCarry = globalCarry.carryOut;
			if (trackWorldGlobalCarryGateCarry) {
				context->directCallbackActiveBranchMask |= SLIP_TRACK_CALLBACK_GLOBAL_GATE_REJECTED;
				++context->directCallbackGlobalGateRejectedCount;
			}
		}
	}
	bool fallbackLowFromHigh = false;

	if ((flags & skipMask) == 0 && !planeRejected && !trackWorldGlobalCarryGateCarry &&
	    (countAndFlags & SLIP_TRC_PRIMITIVE_TEXTURED) != 0) {
		bool handled = false;
		uint32_t texturedCallbackResult = 0;
		bool carryHigh = false;
		const uint32_t farFallbacksBefore = context->directCallbackHighFarFallbackCount;

		if (!TrackView_ExecuteHighTexturedCallback(
		        context, &primitiveContext, primitiveRecord, recordBytesRemaining, recordOffset, countAndFlags, normalX,
		        normalY, normalZ, materialIndex, &handled, &fallbackLowFromHigh, &texturedCallbackResult, &carryHigh)) {
			context->rendererFlags = savedContextRenderFlags;
			return false;
		}
		if (handled) {

			if (environment->inlineWalkMode && (flags & SLIP_TRACK_PRIMITIVE_REPLAY_PLANE) != 0 && !carryHigh) {
				bool skippedByCapture = false;

				if (!TrackView_ExecuteInlineReplayTail(context, &primitiveContext, primitiveRecord,
				                                       recordBytesRemaining, materialIndex, &skippedByCapture)) {
					context->rendererFlags = savedContextRenderFlags;
					return false;
				}
			}
			++context->directCallbackDispatchCount;
			*callbackResult = texturedCallbackResult;

			*carryFromCallback = environment->inlineWalkMode ? carryHigh : false;
			context->rendererFlags = savedContextRenderFlags;
			return true;
		}

		if (environment->inlineWalkMode && fallbackLowFromHigh &&
		    context->directCallbackHighFarFallbackCount > farFallbacksBefore) {
			bool polygonRejected = false;
			const uint16_t polygonCountAndFlags = (uint16_t)(countAndFlags & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK);
			uint16_t drawMaterialIndex = materialIndex;

			if (drawMaterialIndex & SLIP_TRC_PRIMITIVE_MATERIAL_UNRESOLVED) {
				drawMaterialIndex = 0;
			}
			if (!TrackView_ExecuteLowEmitPath(
			        context, &primitiveContext, primitiveRecord + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET,
			        recordBytesRemaining - SLIP_TRC_PRIMITIVE_HEADER_BYTES, polygonCountAndFlags, normalX, normalY,
			        normalZ, drawMaterialIndex, &polygonRejected)) {
				context->rendererFlags = savedContextRenderFlags;
				return false;
			}
			if (!polygonRejected && (flags & SLIP_TRACK_PRIMITIVE_REPLAY_PLANE) != 0) {
				bool skippedByCapture = false;

				if (!TrackView_ExecuteInlineReplayTail(context, &primitiveContext, primitiveRecord,
				                                       recordBytesRemaining, materialIndex, &skippedByCapture)) {
					context->rendererFlags = savedContextRenderFlags;
					return false;
				}
			}
			++context->directCallbackDispatchCount;
			*callbackResult = callbackInput;
			*carryFromCallback = false;
			context->rendererFlags = savedContextRenderFlags;
			return true;
		}
	}

	if (fallbackLowFromHigh) {
		countAndFlags = (uint16_t)(countAndFlags & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK);
	}
	if ((flags & skipMask) == 0 && !planeRejected && !trackWorldGlobalCarryGateCarry &&
	    (countAndFlags & SLIP_TRC_PRIMITIVE_TEXTURED) == 0 &&
	    (materialFlags != 0 && (materialFlags & SLIP_TRC_MATERIAL_EXTENDED) != 0)) {
		bool extendedMaterialRejected = false;
		bool skipDispatch = false;
		uint32_t materialDispatchValue = 0;

		++context->directCallbackOtherMaterialCount;

		if (environment->inlineWalkMode && (flags & SLIP_TRACK_PRIMITIVE_REPLAY_PLANE) != 0) {
			if (!TrackView_PrepareMaterialState(context, &primitiveContext, primitiveRecord, recordBytesRemaining,
			                                    countAndFlags, &skipDispatch)) {
				context->rendererFlags = savedContextRenderFlags;
				return false;
			}
			if (skipDispatch) {
				++context->directCallbackDispatchCount;
				*callbackResult = materialDispatchValue;
				*carryFromCallback = false;
				context->rendererFlags = savedContextRenderFlags;
				return true;
			}
			materialDispatchValue = UINT32_MAX;
		} else {
			SlipDraw3DPolygonStatusVisit statusVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
			SlipDraw3DPolygonStatus status;

			if (!SlipDraw3D_PolygonStatus(context->vertexRecords, context->vertexRecordCount,
			                              primitiveRecord + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET,
			                              recordBytesRemaining - SLIP_TRC_PRIMITIVE_HEADER_BYTES, countAndFlags,
			                              context->projectState, TrackView_TransformVertex,
			                              TrackView_PrimitiveProjectMask, &primitiveContext, statusVisits,
			                              sizeof(statusVisits) / sizeof(statusVisits[0]), &status)) {
				context->failureAddress = SLIP_DRAW3D_TEXTURED_RING_STATUS_FAILURE_DOS_STAGE;
				context->failed = true;
				context->rendererFlags = savedContextRenderFlags;
				return false;
			}
			if (status.signFlagAfterReturn) {
				++context->directCallbackDispatchCount;
				*callbackResult = materialDispatchValue;
				*carryFromCallback = false;
				context->rendererFlags = savedContextRenderFlags;
				return true;
			}
		}
		if (!TrackView_ExecuteExtendedMaterialDispatch(context, &primitiveContext, primitiveRecord,
		                                               recordBytesRemaining, materialDispatchValue, countAndFlags,
		                                               materialIndex, materialFlags, &extendedMaterialRejected)) {
			context->rendererFlags = savedContextRenderFlags;
			return false;
		}
		++context->directCallbackDispatchCount;
		*callbackResult = 0;
		*carryFromCallback = extendedMaterialRejected;
		context->rendererFlags = savedContextRenderFlags;
		return true;
	}
	if ((flags & skipMask) == 0 && !planeRejected && !trackWorldGlobalCarryGateCarry &&
	    (countAndFlags & SLIP_TRC_PRIMITIVE_TEXTURED) == 0 &&
	    (materialFlags == 0 || (materialFlags & SLIP_TRC_MATERIAL_EXTENDED) == 0)) {
		bool polygonRejected = false;
		const uint16_t polygonCountAndFlags = (uint16_t)(countAndFlags & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK);
		uint16_t drawMaterialIndex = materialIndex;

		if (drawMaterialIndex & SLIP_TRC_PRIMITIVE_MATERIAL_UNRESOLVED) {
			drawMaterialIndex = 0;
		}
		if (!TrackView_ExecuteLowEmitPath(context, &primitiveContext,
		                                  primitiveRecord + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET,
		                                  recordBytesRemaining - SLIP_TRC_PRIMITIVE_HEADER_BYTES, polygonCountAndFlags,
		                                  normalX, normalY, normalZ, drawMaterialIndex, &polygonRejected)) {
			context->rendererFlags = savedContextRenderFlags;
			return false;
		}
		++context->directCallbackLowMaterialCount;
		if (environment->inlineWalkMode) {

			bool runReplay = false;

			if (materialFlags == 0) {
				if (!polygonRejected && (flags & SLIP_TRACK_PRIMITIVE_REPLAY_PLANE) != 0) {
					runReplay = true;
				}
			} else if (polygonRejected) {

				runReplay = false;
			} else if ((flags & SLIP_TRACK_PRIMITIVE_REPLAY_PLANE) != 0) {
				runReplay = true;
			}
			if (runReplay) {

				const uint8_t *const shadeRecord =
				    TrackView_MaterialRecord(context->materialTable, context->materialTableBytes, materialIndex);
				const uint32_t shadeValue =
				    shadeRecord != NULL
				        ? SlipBytes_ReadLE32(shadeRecord + offsetof(SlipDraw3DMaterialRecord, importedMaterialByte))
				        : 0;
				SlipView3DVec32 rotatedNormal = SlipView3D_TransformPosition16(
				    &primitiveContext.viewMatrix,
				    (SlipView3DVec32){
				        (int16_t)SlipBytes_ReadLE16(primitiveRecord + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET),
				        (int16_t)SlipBytes_ReadLE16(primitiveRecord + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET),
				        (int16_t)SlipBytes_ReadLE16(primitiveRecord + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET)});
				SlipDraw3DProjectIndex project;
				SlipDraw3DPostPlaneCaptureVisit captureVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
				SlipDraw3DPostPlaneCapture capture;

				if (!SlipDraw3D_ProjectIndex(
				        context->vertexRecords, context->vertexRecordCount,
				        SlipBytes_ReadLE16(primitiveRecord + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET),
				        TrackView_TransformVertex, &primitiveContext, &project)) {
					context->failureAddress = TRACK_VIEW_DIAGNOSTIC_PROJECT_INDEX;
					context->failed = true;
					context->rendererFlags = savedContextRenderFlags;
					return false;
				}
				if (!SlipDraw3D_CapturePostPlaneRing(
				        context->drawRecordPool, context->postPlaneHead, shadeValue, (uint32_t)project.world.x,
				        (uint32_t)project.world.y, (uint32_t)project.world.z, (uint16_t)rotatedNormal.x,
				        (uint16_t)rotatedNormal.y, (uint16_t)rotatedNormal.z, context->cameraLightX,
				        context->cameraLightY, context->cameraLightZ, captureVisits,
				        sizeof(captureVisits) / sizeof(captureVisits[0]), &capture)) {
					context->failureAddress = TRACK_VIEW_DIAGNOSTIC_CAPTURE_POST_PLANE_RING;
					context->failed = true;
					context->rendererFlags = savedContextRenderFlags;
					return false;
				}
				if (!capture.existingPostPlane) {
					context->postPlaneColor = capture.sourceOffset;
					context->postPlaneScale = capture.planeScale;
					context->postPlanePointX = capture.planePointX;
					context->postPlanePointY = capture.planePointY;
					context->postPlanePointZ = capture.planePointZ;
					context->postPlaneNormalX = capture.planeNormalX;
					context->postPlaneNormalY = capture.planeNormalY;
					context->postPlaneNormalZ = capture.planeNormalZ;
					context->postLimitXMin = capture.limitXMin;
					context->postLimitYMin = capture.limitYMin;
					context->postLimitXMax = capture.limitXMax;
					context->postLimitYMax = capture.limitYMax;
				}
				context->postPlaneHead = capture.postPlaneHeadOut;
				if (!capture.carryOut) {
					if (materialFlags != 0) {

						if (!TrackView_ExecuteExtendedMaterialDispatch(
						        context, &primitiveContext, primitiveRecord, recordBytesRemaining, 0, countAndFlags,
						        materialIndex, materialFlags, &polygonRejected)) {
							context->rendererFlags = savedContextRenderFlags;
							return false;
						}
					}

					if (!TrackView_ExecuteReplay(context)) {
						context->rendererFlags = savedContextRenderFlags;
						return false;
					}

					{
						SlipDraw3DPostPlaneReleaseVisit releaseVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
						SlipDraw3DPostPlaneRelease release;

						if (!SlipDraw3D_ReleasePostPlaneRing(
						        context->drawRecordPool, context->postPlaneHead, releaseVisits,
						        sizeof(releaseVisits) / sizeof(releaseVisits[0]), &release)) {
							context->failureAddress = TRACK_VIEW_DIAGNOSTIC_RELEASE_POST_PLANE_RING;
							context->failed = true;
							context->rendererFlags = savedContextRenderFlags;
							return false;
						}
						context->postPlaneHead = release.postPlaneHeadOut;
					}
				} else if (materialFlags != 0) {

					if (!TrackView_ExecuteExtendedMaterialDispatch(context, &primitiveContext, primitiveRecord,
					                                               recordBytesRemaining, 0, countAndFlags,
					                                               materialIndex, materialFlags, &polygonRejected)) {
						context->rendererFlags = savedContextRenderFlags;
						return false;
					}
				}
			} else if (materialFlags != 0 && !polygonRejected) {

				if (!TrackView_ExecuteExtendedMaterialDispatch(context, &primitiveContext, primitiveRecord,
				                                               recordBytesRemaining, 0, countAndFlags, materialIndex,
				                                               materialFlags, &polygonRejected)) {
					context->rendererFlags = savedContextRenderFlags;
					return false;
				}
			}
			++context->directCallbackDispatchCount;
			*callbackResult = callbackInput;
			*carryFromCallback = false;
			context->rendererFlags = savedContextRenderFlags;
			return true;
		}
		if (materialFlags == 0 || polygonRejected) {
			++context->directCallbackDispatchCount;
			*callbackResult = ((callbackInput & (UINT32_MAX ^ UINT8_MAX)) | flags);
			*carryFromCallback = polygonRejected;
			context->rendererFlags = savedContextRenderFlags;
			return true;
		}

		if (!TrackView_ExecuteExtendedMaterialDispatch(context, &primitiveContext, primitiveRecord,
		                                               recordBytesRemaining, 0, countAndFlags, materialIndex,
		                                               materialFlags, &polygonRejected)) {
			context->rendererFlags = savedContextRenderFlags;
			return false;
		}
		++context->directCallbackDispatchCount;
		*callbackResult = 0;
		*carryFromCallback = polygonRejected;
		context->rendererFlags = savedContextRenderFlags;
		return true;
	}
	if (!SlipTrackWorld_PrimitiveCallbackDispatch(primitiveRecord, recordBytesRemaining, recordOffset, callbackInput,
	                                              planeRejected, trackWorldGlobalCarryGateCarry, false, false, 0, 0, 0,
	                                              0, 0, 0, &dispatch) ||
	    !dispatch.returnStateKnown) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_PRIMITIVE_CALLBACK_DISPATCH;
		context->failed = true;
		context->rendererFlags = savedContextRenderFlags;
		return false;
	}
	++context->directCallbackDispatchCount;
	*callbackResult = dispatch.callbackValueResult;
	*carryFromCallback = dispatch.carryOut;
	context->rendererFlags = savedContextRenderFlags;
	return true;
}

static bool TrackView_PrepareMaterialState(TrackViewRawBspContext *context,
                                           TrackViewPrimitiveCallbackContext *primitiveContext,
                                           const uint8_t *primitiveRecord, size_t recordBytesRemaining,
                                           uint16_t countAndFlags, bool *skipDispatch) {
	SlipDraw3DPolygonStatusVisit statusVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPolygonStatus status;
	SlipView3DVec32 transformedOrigin;
	SlipDraw3DProjectIndex project;
	SlipTrackWorldMaterialStateStore materialPlaneState;
	uint32_t pushedDword;

	if (context == NULL || primitiveContext == NULL || primitiveRecord == NULL ||
	    recordBytesRemaining < SLIP_TRC_PRIMITIVE_FIRST_INDEX_DWORD_END || skipDispatch == NULL) {
		return false;
	}
	*skipDispatch = false;
	if (!SlipDraw3D_PolygonStatus(context->vertexRecords, context->vertexRecordCount,
	                              primitiveRecord + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET,
	                              recordBytesRemaining - SLIP_TRC_PRIMITIVE_HEADER_BYTES, countAndFlags,
	                              context->projectState, TrackView_TransformVertex, TrackView_PrimitiveProjectMask,
	                              primitiveContext, statusVisits, sizeof(statusVisits) / sizeof(statusVisits[0]),
	                              &status)) {
		context->failureAddress = SLIP_DRAW3D_TEXTURED_RING_STATUS_FAILURE_DOS_STAGE;
		context->failed = true;
		return false;
	}
	if (status.signFlagAfterReturn) {
		*skipDispatch = true;
		return true;
	}
	pushedDword = SlipBytes_ReadLE32(primitiveRecord + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET);
	transformedOrigin = SlipView3D_TransformPosition16(
	    &primitiveContext->viewMatrix,
	    (SlipView3DVec32){(int16_t)SlipBytes_ReadLE16(primitiveRecord + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET),
	                      (int16_t)SlipBytes_ReadLE16(primitiveRecord + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET),
	                      (int16_t)SlipBytes_ReadLE16(primitiveRecord + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET)});
	if (!SlipDraw3D_ProjectIndex(context->vertexRecords, context->vertexRecordCount, (uint16_t)pushedDword,
	                             TrackView_TransformVertex, primitiveContext, &project) ||
	    !SlipTrackWorld_MaterialStateStore((uint32_t)project.world.x, (uint32_t)project.world.y,
	                                       (uint32_t)project.world.z, (uint32_t)transformedOrigin.x,
	                                       (uint32_t)transformedOrigin.y, (uint32_t)transformedOrigin.z,
	                                       &materialPlaneState)) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_PROJECT_INDEX;
		context->failed = true;
		return false;
	}
	context->materialPlaneNormalX = materialPlaneState.planeNormalX;
	context->materialPlaneNormalY = materialPlaneState.planeNormalY;
	context->materialPlaneNormalZ = materialPlaneState.planeNormalZ;
	context->materialPlanePointX = materialPlaneState.planeOriginX;
	context->materialPlanePointY = materialPlaneState.planeOriginY;
	context->materialPlanePointZ = materialPlaneState.planeOriginZ;
	if (g_vehicleViewDumpDiagnostics) {
		fprintf(stderr, "track_view_material_state_00039646 index=0x%04x projected=%d,%d,%d transformed=%d,%d,%d\n",
		        (uint16_t)pushedDword, project.world.x, project.world.y, project.world.z, transformedOrigin.x,
		        transformedOrigin.y, transformedOrigin.z);
	}
	return true;
}

static int32_t TrackView_ShapeArithmeticShiftRight(int32_t value, uint16_t shift) {
	const uint32_t count = shift & SLIP_DWORD_SHIFT_COUNT_MASK;

	if (count == 0u) {
		return value;
	}
	if (value >= 0) {
		return value >> count;
	}
	return (int32_t)~((uint32_t)(~value) >> count);
}

static SlipDraw3DVec32 TrackView_TransformVertex(uint32_t sourceX, uint32_t sourceY, uint32_t sourceZ,
                                                 SlipDraw3DVertexRecord *record, void *userData) {
	const TrackViewPrimitiveCallbackContext *const context = (const TrackViewPrimitiveCallbackContext *)userData;
	SlipView3DVec32 transformed;
	SlipView3DVec32 source;

	(void)record;
	if (context == NULL) {
		return (SlipDraw3DVec32){0, 0, 0};
	}
	if (context->shapeScaleShift != 0u) {
		const int16_t x = (int16_t)(uint16_t)sourceX;
		const int16_t y = (int16_t)(uint16_t)sourceY;
		const int16_t z = (int16_t)(uint16_t)sourceZ;
		source = (SlipView3DVec32){
		    TrackView_ShapeArithmeticShiftRight((int32_t)((uint32_t)((int32_t)x * context->viewMatrix.m[0]) +
		                                                  (uint32_t)((int32_t)y * context->viewMatrix.m[3]) +
		                                                  (uint32_t)((int32_t)z * context->viewMatrix.m[6])),
		                                        context->shapeScaleRightShift),
		    TrackView_ShapeArithmeticShiftRight((int32_t)((uint32_t)((int32_t)x * context->viewMatrix.m[1]) +
		                                                  (uint32_t)((int32_t)y * context->viewMatrix.m[4]) +
		                                                  (uint32_t)((int32_t)z * context->viewMatrix.m[7])),
		                                        context->shapeScaleRightShift),
		    TrackView_ShapeArithmeticShiftRight((int32_t)((uint32_t)((int32_t)x * context->viewMatrix.m[2]) +
		                                                  (uint32_t)((int32_t)y * context->viewMatrix.m[5]) +
		                                                  (uint32_t)((int32_t)z * context->viewMatrix.m[8])),
		                                        context->shapeScaleRightShift)};
		return (SlipDraw3DVec32){source.x + context->transformedObjectOffset.x,
		                         source.y + context->transformedObjectOffset.y,
		                         source.z + context->transformedObjectOffset.z};
	}
	if (context->shapePath) {
		transformed = SlipView3D_TransformPosition16(
		    &context->viewMatrix,
		    (SlipView3DVec32){(int16_t)(uint16_t)sourceX, (int16_t)(uint16_t)sourceY, (int16_t)(uint16_t)sourceZ});
		transformed.x += context->transformedObjectOffset.x;
		transformed.y += context->transformedObjectOffset.y;
		transformed.z += context->transformedObjectOffset.z;
	} else {
		transformed = SlipView3D_TransformVertex(
		    &context->viewMatrix, context->transformedObjectOffset,
		    (SlipView3DVec16){(int16_t)(uint16_t)sourceX, (int16_t)(uint16_t)sourceY, (int16_t)(uint16_t)sourceZ});
	}
	return (SlipDraw3DVec32){transformed.x, transformed.y, transformed.z};
}

static bool TrackView_DrawGpuWorldTexture(TrackViewRawBspContext *context,
                                          const TrackViewPrimitiveCallbackContext *primitiveContext,
                                          const uint8_t *primitiveRecord, size_t recordBytesRemaining,
                                          uint16_t countAndFlags, uint32_t rowScroll, uint32_t textureHandle,
                                          bool *rejected) {
	SlipDraw3DPolygonStatusVisit statusVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	/* Use immutable model vertices/UVs, not the DOS clipped draw ring. */
	uint32_t count = countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
	uint32_t uvOffset = (countAndFlags & SLIP_PRIMITIVE_VERTEX_NORMALS)
	                        ? count * (SLIP_SERIALIZED_INDEX_BYTES + SLIP_SERIALIZED_NORMAL_BYTES)
	                        : count * SLIP_SERIALIZED_INDEX_BYTES;
	SlipRaceGpuWorldPoint points[SLIP_GPU_WORLD_TEXTURE_VERTEX_CAPACITY];
	SlipDraw3DPolygonStatus status;
	*rejected = false;
	if (count > SLIP_GPU_WORLD_TEXTURE_VERTEX_CAPACITY ||
	    recordBytesRemaining <
	        SLIP_PRIMITIVE_HEADER_BYTES + uvOffset + count * SLIP_SERIALIZED_TEXTURE_COORDINATE_BYTES ||
	    !SlipDraw3D_PolygonStatus(
	        context->vertexRecords, context->vertexRecordCount, primitiveRecord + SLIP_PRIMITIVE_HEADER_BYTES,
	        recordBytesRemaining - SLIP_PRIMITIVE_HEADER_BYTES, countAndFlags, context->projectState,
	        TrackView_TransformVertex, TrackView_PrimitiveProjectMask, (void *)primitiveContext, statusVisits,
	        sizeof(statusVisits) / sizeof(statusVisits[0]), &status))
		return false;
	if (status.clipClassification == -1) {
		*rejected = true;
		return true;
	}
	for (uint32_t i = 0; i < count; i++) {
		uint16_t index =
		    SlipBytes_ReadLE16(primitiveRecord + SLIP_PRIMITIVE_HEADER_BYTES + i * SLIP_SERIALIZED_INDEX_BYTES);
		if (index >= context->vertexRecordCount)
			return false;
		SlipDraw3DVertexRecord *r = &context->vertexRecords[index];
		SlipDraw3D_ProjectVertex(r, context->projectState, TrackView_TransformVertex, TrackView_ProjectScreenPrimary,
		                         TrackView_ProjectScreenSecondary, (void *)primitiveContext);
		points[i] = (SlipRaceGpuWorldPoint){
		    .source = {r->sourceX, r->sourceY, r->sourceZ},
		    .view = r->world,
		    .u = SlipBytes_ReadLE16(primitiveRecord + SLIP_PRIMITIVE_HEADER_BYTES + uvOffset +
		                            i * SLIP_SERIALIZED_TEXTURE_COORDINATE_BYTES),
		    .v = SlipBytes_ReadLE16(primitiveRecord + SLIP_PRIMITIVE_HEADER_BYTES + uvOffset +
		                            i * SLIP_SERIALIZED_TEXTURE_COORDINATE_BYTES + sizeof(uint16_t))};
	}
	SlipResourcePayload texture = {0};
	if (!TrackView_LockResourceHandlePayload(context->resourceRegistry, textureHandle, &texture))
		return false;
	SlipRaceGpu_WorldTexture(texture.data, texture.size, rowScroll, points, count, context->projectState);
	TrackView_UnlockResourceHandlePayload(context->resourceRegistry, textureHandle);
	return true;
}

static bool TrackView_CompleteTexturedCallback(TrackViewRawBspContext *context, uint32_t savedRenderFlags,
                                               bool rejected, bool *handled, uint32_t *callbackResult, bool *carryOut) {
	context->rendererFlags = savedRenderFlags;
	*handled = true;
	*callbackResult = savedRenderFlags & UINT16_MAX;
	*carryOut = rejected;
	return true;
}

static void TrackView_DrawGpuTexturedDispatch(const SlipResourcePayload *texture, uint32_t rowScroll,
                                              const SlipDraw3DTexturedDispatch *dispatch,
                                              const SlipDraw3DTexturedDispatchPoint *points) {
	RasterTexturedPoint gpuPoints[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	for (size_t i = 0; i < dispatch->pointCount; ++i) {
		const SlipDraw3DTexturedDispatchPoint *p = &points[i];
		gpuPoints[i] = (RasterTexturedPoint){
		    .x = p->screenX, .y = p->screenY, .u = p->textureU, .v = p->textureV, .depth = p->depth};
	}
	SlipRaceGpu_Texture(texture->data, texture->size, rowScroll, gpuPoints, (uint32_t)dispatch->pointCount);
}

static bool TrackView_ExecuteHighTexturedCallback(TrackViewRawBspContext *context,
                                                  const TrackViewPrimitiveCallbackContext *primitiveContext,
                                                  const uint8_t *primitiveRecord, size_t recordBytesRemaining,
                                                  size_t recordOffset, uint16_t countAndFlags, uint16_t normalX,
                                                  uint16_t normalY, uint16_t normalZ, uint16_t materialIndex,
                                                  bool *handled, bool *fallbackLow, uint32_t *callbackResult,
                                                  bool *carryOut) {
	SlipDraw3DPerspectiveDepthVisit depthVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPerspectiveDepth depthResult;

	if (context == NULL || primitiveContext == NULL || primitiveRecord == NULL || handled == NULL ||
	    callbackResult == NULL || carryOut == NULL) {
		return false;
	}
	*handled = false;
	if (fallbackLow != NULL) {
		*fallbackLow = false;
	}
	*callbackResult = 0;
	*carryOut = false;
	context->directCallbackActiveBranchMask |= SLIP_TRACK_CALLBACK_HIGH_TEXTURED_PATH;
	context->failureAddress = TRACK_VIEW_DIAGNOSTIC_HIGH_TEXTURED_CALLBACK;
	++context->directCallbackHighTexturedPathCount;
	if (materialIndex & SLIP_TRC_PRIMITIVE_MATERIAL_UNRESOLVED) {
		materialIndex = 0;
	}
	if (!primitiveContext->shapePath) {
		uint32_t resolvedMaterialIndex = materialIndex;
		const SlipDraw3DMaterialTable *const materialTable = (const void *)context->materialTable;
		const uint32_t textureHandle = SlipMaterial_GetFrame(materialTable, context->materialGlobal,
		                                                     &resolvedMaterialIndex, context->materialFrameIndex);
		materialIndex = (uint16_t)resolvedMaterialIndex;

		if ((uint16_t)textureHandle == 0) {
			if (fallbackLow != NULL) {
				*fallbackLow = true;
			}
			return true;
		}
	}
	if (!primitiveContext->shapePath) {

		if (context->projectState == NULL ||
		    !SlipDraw3D_PerspectiveDepth(context->vertexRecords, context->vertexRecordCount,
		                                 primitiveRecord + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET,
		                                 recordBytesRemaining - SLIP_TRC_PRIMITIVE_HEADER_BYTES, countAndFlags,
		                                 context->projectState->inverseProjectionScale, TrackView_TransformVertex,
		                                 (void *)primitiveContext, depthVisits,
		                                 sizeof(depthVisits) / sizeof(depthVisits[0]), &depthResult)) {
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_TEXTURED_PERSPECTIVE_DEPTH;
			context->failed = true;
			return false;
		}
	}

	if (!primitiveContext->shapePath && (int32_t)depthResult.fadeDepth > (int32_t)context->farTextureDepth) {

		++context->directCallbackHighFarFallbackCount;
		if (fallbackLow != NULL) {
			*fallbackLow = true;
		}
		return true;
	}
	{
		SlipDraw3DReturnActiveVisit returnVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DReturnActiveRing returnActive;
		SlipDraw3DTexturedEmitGate texturedEmitGate;
		SlipDraw3DTexturedRingVisit texturedRingVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DTexturedRingExecute texturedRing;
		SlipDraw3DPolygonStatusVisit statusVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DClipFlagVisit texturedClipFlagVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DPostPlaneBoundsVisit texturedPostBoundsVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DPostPlaneClipRecordVisit texturedPostClipRecordVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DPostPlaneClipPlaneVisit texturedPostClipPlaneVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DTexturedDispatchPoint texturedDispatchPoints[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DTexturedDispatchVisit texturedDispatchVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DTexturedDispatch texturedDispatch;
		uint8_t standalonePointBuffer[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT * sizeof(RasterTexturedPoint)];
		uint8_t *texturedPointBuffer = standalonePointBuffer;
		size_t texturedPointBufferBytes = sizeof(standalonePointBuffer);
		if (context->hostRenderer != NULL) {
			SlipResourcePayload points = SlipResourceHost_Payload(context->hostRenderer->pointResource);
			texturedPointBuffer = points.data;
			texturedPointBufferBytes = points.size;
		}
		const uint32_t savedRenderFlags = context->rendererFlags;
		uint32_t dispatchRenderFlags = savedRenderFlags;
		if (!primitiveContext->shapePath)
			dispatchRenderFlags = (int32_t)depthResult.fadeDepth > (int32_t)context->affineDepthThreshold
			                          ? savedRenderFlags | SLIP_RENDER_ALTERNATE_TEXTURE_RASTER
			                          : savedRenderFlags & ~SLIP_RENDER_ALTERNATE_TEXTURE_RASTER;

		uint32_t rowScroll;
		uint8_t screenBefore[SLIPSTREAM_SCREEN_WIDTH * SLIPSTREAM_SCREEN_HEIGHT];
		bool haveScreenBefore = false;

		++context->directCallbackHighFrameCount;
		rowScroll = TrackView_TextureRowScroll(context->materialTable, context->materialTableBytes, materialIndex,
		                                       context->componentBaseToken + (uint32_t)recordOffset,
		                                       context->textureScrollPhase);
		if (context->drawRecordPool == NULL || context->projectState == NULL || context->drawStateRecord == NULL ||
		    !SlipDraw3D_ReturnActiveRing(context->drawRecordPool, returnVisits,
		                                 sizeof(returnVisits) / sizeof(returnVisits[0]), &returnActive) ||
		    !SlipDraw3D_TexturedEmitGate(context->materialTable, context->materialTableBytes, context->drawStateRecord,
		                                 dispatchRenderFlags, countAndFlags, normalX, normalY, normalZ, materialIndex,
		                                 context->materialFrameIndex, &texturedEmitGate)) {
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_TEXTURED_EMIT_GATE;
			context->failed = true;
			return false;
		}
		++context->directCallbackHighTexturedEmitCount;
		if (g_vehicleViewDumpDiagnostics) {
			fprintf(stderr,
			        "track_view_emit_00019e0d offset=0x%zx saved=0x%08x input=0x%08x after=0x%08x dispatch=0x%08x "
			        "cmp=0x%04x transparent=0x%04x material_off=0x%08x texture=0x%08x fallback=%u\n",
			        recordOffset, savedRenderFlags, dispatchRenderFlags, texturedEmitGate.renderFlagsAfter,
			        texturedEmitGate.renderFlagsForDispatch, texturedEmitGate.normalDepth,
			        texturedEmitGate.transparentWord, texturedEmitGate.materialRecordOffset,
			        texturedEmitGate.textureHandle, texturedEmitGate.fallback ? 1u : 0u);
		}
		if (texturedEmitGate.fallback || !texturedEmitGate.calledTexturedDispatch) {
			if (primitiveContext->shapePath) {
				if (fallbackLow != NULL) {
					*fallbackLow = true;
				}
				context->rendererFlags = savedRenderFlags;
				return true;
			}
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_TEXTURED_DISPATCH_FALLBACK;
			context->failed = true;
			return false;
		}
		TrackViewPostPlaneArgs postPlaneArgs = TrackView_PostPlaneArgs(context);
		if (SlipRaceGpu_Active() && !primitiveContext->shapePath && !postPlaneArgs.hasPostPlanes &&
		    !(context->projectState->renderFlags & SLIP_SHAPE_CLIP_AUXILIARY)) {
			bool rejected;
			if (!TrackView_DrawGpuWorldTexture(context, primitiveContext, primitiveRecord, recordBytesRemaining,
			                                   countAndFlags, rowScroll, texturedEmitGate.textureHandle, &rejected))
				return false;
			return TrackView_CompleteTexturedCallback(context, savedRenderFlags, rejected, handled, callbackResult,
			                                          carryOut);
		}
		if (!SlipDraw3D_BuildTexturedRingExecute(
		        context->drawRecordPool, context->vertexRecords, context->vertexRecordCount,
		        primitiveRecord + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET,
		        recordBytesRemaining - SLIP_TRC_PRIMITIVE_HEADER_BYTES, countAndFlags, texturedEmitGate.textureHandle,
		        context->projectState, TrackView_TransformVertex, TrackView_PrimitiveProjectMask,
		        TrackView_ProjectScreenPrimary, TrackView_ProjectScreenSecondary, (void *)primitiveContext,
		        postPlaneArgs.hasPostPlanes, postPlaneArgs.planeBase, postPlaneArgs.planeBytes,
		        postPlaneArgs.planeHeadOffset, postPlaneArgs.limitXMin, postPlaneArgs.limitXMax,
		        postPlaneArgs.limitYMin, postPlaneArgs.limitYMax, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT,
		        texturedClipFlagVisits, sizeof(texturedClipFlagVisits) / sizeof(texturedClipFlagVisits[0]),
		        texturedPostBoundsVisits, sizeof(texturedPostBoundsVisits) / sizeof(texturedPostBoundsVisits[0]),
		        texturedPostClipRecordVisits,
		        sizeof(texturedPostClipRecordVisits) / sizeof(texturedPostClipRecordVisits[0]),
		        texturedPostClipPlaneVisits,
		        sizeof(texturedPostClipPlaneVisits) / sizeof(texturedPostClipPlaneVisits[0]), statusVisits,
		        sizeof(statusVisits) / sizeof(statusVisits[0]), texturedRingVisits,
		        sizeof(texturedRingVisits) / sizeof(texturedRingVisits[0]), &texturedRing)) {
			if (g_vehicleViewDumpDiagnostics) {
				uint8_t *const debugRecordPoolBytes = SlipDraw3D_RecordPoolBytes(context->drawRecordPool);
				const size_t debugRecordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();
				static const uint32_t debugOffsets[] = {
				    0, SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE, 2 * SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE,
				    3 * SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE, 4 * SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE};
				size_t debugOffsetIndex;

				fprintf(
				    stderr,
				    "track_view textured 0001c753 failure: stage=0x%08x active_in=0x%08x active_out=0x%08x free=0x%08x "
				    "call_b098=%u carry_b098=%u call_b14b=%u carry_b14b=%u depth_branch=%u depth_calls 2000=%u 0080=%u "
				    "0100=%u depth_edge2000 carry=%u head=0x%08x target=0x%08x other=0x%08x depth_edge0080 carry=%u "
				    "head=0x%08x target=0x%08x other=0x%08x depth_edge0100 carry=%u head=0x%08x target=0x%08x "
				    "other=0x%08x split0100_first ret=%u call_proj=%u ratio=0x%08x clip_z=%d flags=0x%08x "
				    "split0100_second ret=%u call_proj=%u ratio=0x%08x clip_z=%d flags=0x%08x screen_branch=%u "
				    "screen_calls 0008=%u 0010=%u 0020=%u 0040=%u post_bounds=%u post_clip=%u\n",
				    texturedRing.failureStage, texturedRing.dispatch.activeHeadOffsetIn,
				    texturedRing.dispatch.activeHeadOffsetOut, texturedRing.dispatch.freeHeadOffset,
				    texturedRing.dispatch.dispatch.calledClipDepth ? 1u : 0u,
				    texturedRing.dispatch.dispatch.depthClipRejected ? 1u : 0u,
				    texturedRing.dispatch.dispatch.calledClipScreen ? 1u : 0u,
				    texturedRing.dispatch.dispatch.screenClipRejected ? 1u : 0u,
				    (unsigned)texturedRing.dispatch.clippedDepth.dispatch.branch,
				    texturedRing.dispatch.clippedDepth.dispatch.calledClipAuxiliaryEdge ? 1u : 0u,
				    texturedRing.dispatch.clippedDepth.dispatch.calledClipNearEdge ? 1u : 0u,
				    texturedRing.dispatch.clippedDepth.dispatch.calledClipFarEdge ? 1u : 0u,
				    texturedRing.dispatch.clippedDepth.auxiliaryClip.carryOut ? 1u : 0u,
				    texturedRing.dispatch.clippedDepth.auxiliaryClip.headOffsetOut,
				    texturedRing.dispatch.clippedDepth.auxiliaryClip.targetOffsetOut,
				    texturedRing.dispatch.clippedDepth.auxiliaryClip.otherOffsetOut,
				    texturedRing.dispatch.clippedDepth.nearClip.carryOut ? 1u : 0u,
				    texturedRing.dispatch.clippedDepth.nearClip.headOffsetOut,
				    texturedRing.dispatch.clippedDepth.nearClip.targetOffsetOut,
				    texturedRing.dispatch.clippedDepth.nearClip.otherOffsetOut,
				    texturedRing.dispatch.clippedDepth.farClip.carryOut ? 1u : 0u,
				    texturedRing.dispatch.clippedDepth.farClip.headOffsetOut,
				    texturedRing.dispatch.clippedDepth.farClip.targetOffsetOut,
				    texturedRing.dispatch.clippedDepth.farClip.otherOffsetOut,
				    texturedRing.dispatch.clippedDepth.firstFarSplit.returned ? 1u : 0u,
				    texturedRing.dispatch.clippedDepth.firstFarSplit.calledProjectFlags ? 1u : 0u,
				    texturedRing.dispatch.clippedDepth.firstFarSplit.interpolationRatio,
				    texturedRing.dispatch.clippedDepth.firstFarSplit.clipPlaneZ,
				    texturedRing.dispatch.clippedDepth.firstFarSplit.projectFlags.flagsOut,
				    texturedRing.dispatch.clippedDepth.secondFarSplit.returned ? 1u : 0u,
				    texturedRing.dispatch.clippedDepth.secondFarSplit.calledProjectFlags ? 1u : 0u,
				    texturedRing.dispatch.clippedDepth.secondFarSplit.interpolationRatio,
				    texturedRing.dispatch.clippedDepth.secondFarSplit.clipPlaneZ,
				    texturedRing.dispatch.clippedDepth.secondFarSplit.projectFlags.flagsOut,
				    (unsigned)texturedRing.dispatch.screenPlane.dispatch.branch,
				    texturedRing.dispatch.screenPlane.dispatch.calledClipLeftEdge ? 1u : 0u,
				    texturedRing.dispatch.screenPlane.dispatch.calledClipRightEdge ? 1u : 0u,
				    texturedRing.dispatch.screenPlane.dispatch.calledClipTopEdge ? 1u : 0u,
				    texturedRing.dispatch.screenPlane.dispatch.calledClipBottomEdge ? 1u : 0u,
				    texturedRing.dispatch.screenPlane.dispatch.calledPostPlaneBounds ? 1u : 0u,
				    texturedRing.dispatch.screenPlane.dispatch.calledPostPlaneClip ? 1u : 0u);
				for (debugOffsetIndex = 0;
				     debugRecordPoolBytes != NULL && debugOffsetIndex < sizeof(debugOffsets) / sizeof(debugOffsets[0]);
				     ++debugOffsetIndex) {
					const uint32_t debugOffset = debugOffsets[debugOffsetIndex];
					if ((size_t)debugOffset <= debugRecordPoolBytesCount &&
					    debugRecordPoolBytesCount - (size_t)debugOffset >= SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE) {
						const uint8_t *const debugRecord = debugRecordPoolBytes + debugOffset;
						fprintf(stderr,
						        "  record off=0x%08x xyz=%d,%d,%d screen=%d,%d flags=0x%08x depth=%d next=0x%08x "
						        "prev=0x%08x\n",
						        debugOffset,
						        (int32_t)SlipBytes_ReadLE32(debugRecord + offsetof(SlipDraw3DDrawRecord, world.x)),
						        (int32_t)SlipBytes_ReadLE32(debugRecord + offsetof(SlipDraw3DDrawRecord, world.y)),
						        (int32_t)SlipBytes_ReadLE32(debugRecord + offsetof(SlipDraw3DDrawRecord, world.z)),
						        (int32_t)SlipBytes_ReadLE32(debugRecord + offsetof(SlipDraw3DDrawRecord, screenX)),
						        (int32_t)SlipBytes_ReadLE32(debugRecord + offsetof(SlipDraw3DDrawRecord, screenY)),
						        SlipBytes_ReadLE32(debugRecord + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET),
						        (int32_t)SlipBytes_ReadLE32(debugRecord + SLIP_DRAW3D_VERTEX_RECORD_DEPTH_OFFSET),
						        SlipBytes_ReadLE32(debugRecord + SLIP_DRAW3D_RECORD_NEXT_OFFSET),
						        SlipBytes_ReadLE32(debugRecord + SLIP_DRAW3D_RECORD_PREV_OFFSET));
					}
				}
			}
			context->directCallbackHighTexturedRingLastAnyFlags = texturedRing.build.anyFlagsBeforeDispatch;
			context->directCallbackHighTexturedRingLastAllFlags = texturedRing.build.allFlagsBeforeDispatch;
			context->directCallbackHighTexturedRingLastStatusAny = texturedRing.status.anyClipFlags;
			context->directCallbackHighTexturedRingLastStatusAll = texturedRing.status.allClipFlags;
			context->directCallbackHighTexturedRingFailureStage = texturedRing.failureStage;
			context->failureAddress = texturedRing.failureStage;
			context->failed = true;
			return false;
		}
		++context->directCallbackHighTexturedRingCount;
		context->directCallbackHighTexturedRingLastAnyFlags = texturedRing.build.anyFlagsBeforeDispatch;
		context->directCallbackHighTexturedRingLastAllFlags = texturedRing.build.allFlagsBeforeDispatch;
		context->directCallbackHighTexturedRingLastStatusAny = texturedRing.status.anyClipFlags;
		context->directCallbackHighTexturedRingLastStatusAll = texturedRing.status.allClipFlags;
		if (g_vehicleViewDumpDiagnostics) {
			size_t diagnosticArenaOffset = SIZE_MAX;
			if (context->vertexBufferBase != NULL && context->vertexRecords >= context->vertexBufferBase) {
				diagnosticArenaOffset = (size_t)(context->vertexRecords - context->vertexBufferBase);
			}
			fprintf(stderr,
			        "track_view_textured_ring_0001c753 offset=0x%zx arena_record=%zu record_index=%u bp=0x%04x "
			        "dx=0x%04x status_any=0x%08x status_all=0x%08x final_all=0x%08x final_any=0x%08x eax=%d reject=%u "
			        "build_any=0x%08x build_all=0x%08x all_reject=%u carry=%u carry_b098=%u carry_b14b=%u "
			        "active=0x%08x frustum=%d,%d,%d,%d\n",
			        recordOffset, diagnosticArenaOffset, context->recordIndex, countAndFlags, materialIndex,
			        texturedRing.status.anyClipFlags, texturedRing.status.allClipFlags,
			        texturedRing.status.finalAllMask, texturedRing.status.finalAnyMask,
			        texturedRing.status.clipClassification, texturedRing.build.signReject ? 1u : 0u,
			        texturedRing.build.anyFlagsBeforeDispatch, texturedRing.build.allFlagsBeforeDispatch,
			        texturedRing.build.allMaskedNonzero ? 1u : 0u, texturedRing.carryOut ? 1u : 0u,
			        texturedRing.build.depthClipRejected ? 1u : 0u, texturedRing.build.screenClipRejected ? 1u : 0u,
			        texturedRing.activeHeadOffsetOut, context->frustum.maxXStep, context->frustum.minXStep,
			        context->frustum.minYStep, context->frustum.maxYStep);
			{
				size_t diagnosticVisit;

				for (diagnosticVisit = 0; diagnosticVisit < texturedRing.status.firstPassVisitCount;
				     ++diagnosticVisit) {
					const SlipDraw3DPolygonStatusVisit *const visit = &statusVisits[diagnosticVisit];

					fprintf(
					    stderr,
					    "  status_visit=%zu index=0x%04x already=%u flags_before=0x%08x flags_after=0x%08x "
					    "world=%d,%d,%d src=%04x,%04x,%04x reverse=%u mask=0x%08x all_after=0x%08x any_after=0x%08x\n",
					    diagnosticVisit, visit->vertexIndex, visit->project.alreadyTransformed ? 1u : 0u,
					    visit->flagsBeforeStatus, visit->flagsAfterStatus, visit->project.world.x,
					    visit->project.world.y, visit->project.world.z, (uint16_t)visit->project.vertexRecord->sourceX,
					    (uint16_t)visit->project.vertexRecord->sourceY, (uint16_t)visit->project.vertexRecord->sourceZ,
					    visit->reversePassVisited ? 1u : 0u, visit->projectMask, visit->allMaskAfter,
					    visit->anyMaskAfter);
				}
			}
		}
		if (texturedRing.carryOut) {
			++context->directCallbackHighTexturedRingCarryCount;
			if (texturedRing.build.signReject) {
				++context->directCallbackHighTexturedRingSignRejectCount;
			}
			if (texturedRing.build.allMaskedNonzero) {
				++context->directCallbackHighTexturedRingAllMaskedCount;
			}
			if (texturedRing.build.depthClipRejected) {
				++context->directCallbackHighTexturedRingClipDepthCarryCount;
			}
			if (texturedRing.build.screenClipRejected) {
				++context->directCallbackHighTexturedRingClipScreenCarryCount;
			}
		}
		if (!texturedRing.carryOut &&
		    !SlipDraw3D_PrepareTexturedDispatch(
		        context->drawRecordPool, texturedRing.activeHeadOffsetOut, texturedRing.build.drawMode,
		        texturedEmitGate.renderFlagsForDispatch, texturedRing.build.textureHandle, context->reverseTraversal,
		        TRACK_VIEW_DIAGNOSTIC_POINT_BUFFER_BASE, texturedDispatchPoints,
		        sizeof(texturedDispatchPoints) / sizeof(texturedDispatchPoints[0]), texturedDispatchVisits,
		        sizeof(texturedDispatchVisits) / sizeof(texturedDispatchVisits[0]), &texturedDispatch)) {
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_FLAT_RING;
			context->failed = true;
			return false;
		}
		if (!texturedRing.carryOut) {
			context->directCallbackHighDispatchLastMode = texturedDispatch.drawMode;
			if (texturedDispatch.flatBranch) {
				++context->directCallbackHighFlatDispatchCount;
			} else if (texturedDispatch.shadedBranch) {
				++context->directCallbackHighShadedDispatchCount;
			} else if (texturedDispatch.lineBranch) {
				++context->directCallbackHighLineDispatchCount;
			} else if (texturedDispatch.ditheredBranch) {
				++context->directCallbackHighDitheredDispatchCount;
			} else if (texturedDispatch.branchDefaultTextured) {
				++context->directCallbackHighDispatchDefaultCount;
			}
			TrackView_DumpTexturedDispatch(recordOffset, texturedRing.activeHeadOffsetOut, &texturedDispatch,
			                               texturedDispatchVisits,
			                               sizeof(texturedDispatchVisits) / sizeof(texturedDispatchVisits[0]));
		}
		if (!texturedRing.carryOut && texturedDispatch.branchDefaultTextured) {
			SlipResourcePayload texturePayload = {0};

			++context->directCallbackHighTexturedDispatchCount;
			if (!TrackView_WriteTexturedPointBuffer(texturedDispatchPoints, texturedDispatch.pointCount,
			                                        texturedPointBuffer, texturedPointBufferBytes)) {
				context->failureAddress = TRACK_VIEW_DIAGNOSTIC_FLAT_RING;
				context->failed = true;
				return false;
			}
			if (context->resourceRegistry == NULL ||
			    !TrackView_LockResourceHandlePayload(context->resourceRegistry, texturedDispatch.textureHandle,
			                                         &texturePayload)) {
				context->failureAddress = TRACK_VIEW_DIAGNOSTIC_LOCK_TEXTURE_PAYLOAD;
				context->failed = true;
				return false;
			}
			++context->directCallbackHighTextureLoadCount;
			if (SlipRaceGpu_Active()) {
				TrackView_DrawGpuTexturedDispatch(&texturePayload, rowScroll, &texturedDispatch,
				                                  texturedDispatchPoints);
				TrackView_UnlockResourceHandlePayload(context->resourceRegistry, texturedDispatch.textureHandle);
				return TrackView_CompleteTexturedCallback(context, savedRenderFlags, texturedRing.carryOut, handled,
				                                          callbackResult, carryOut);
			}

			if (texturePayload.size < SLIP_SPRITE_HEIGHT_OFFSET + sizeof(uint16_t)) {
				context->failureAddress = TRACK_VIEW_DIAGNOSTIC_FLAT_RING;
				context->failed = true;
				return false;
			}
			if ((texturedDispatch.calledTransparentPerspectiveRasterizer ||
			     texturedDispatch.calledTransparentAffineRasterizer) &&
			    texturePayload.size >= SLIP_SPRITE_TRANSPARENT_COLOUR_END &&
			    SlipBytes_ReadLE16(texturePayload.data + SLIP_SPRITE_TRANSPARENT_COLOUR_OFFSET) !=
			        SLIP_SPRITE_NO_TRANSPARENT_COLOUR) {

				TrackView_UnlockResourceHandlePayload(context->resourceRegistry, texturedDispatch.textureHandle);
				(void)TrackView_LockResourceHandlePayload(context->resourceRegistry, texturedDispatch.textureHandle,
				                                          &texturePayload);
			}
			{
				const uint16_t textureWidth = SlipBytes_ReadLE16(texturePayload.data);
				const uint16_t textureHeight = SlipBytes_ReadLE16(texturePayload.data + SLIP_SPRITE_HEIGHT_OFFSET);
				const uint8_t **textureRows = NULL;

				if (textureHeight != 0u) {
					textureRows = (const uint8_t **)calloc(textureHeight, sizeof(*textureRows));
				}
				if (textureWidth == 0u || textureHeight == 0u || textureRows == NULL) {
					free(textureRows);
					context->failureAddress = TRACK_VIEW_DIAGNOSTIC_TEXTURE_ROW_TABLE;
					context->failed = true;
					return false;
				}
				{
					RasterTextureRowTable rowTable;

					if (!Raster_BuildTextureRowTable(texturePayload.data, texturePayload.size, rowScroll, textureRows,
					                                 textureHeight, &rowTable)) {
						free(textureRows);
						context->failureAddress = TRACK_VIEW_DIAGNOSTIC_TEXTURE_ROW_TABLE;
						context->failed = true;
						return false;
					}
					TrackView_DumpAffineTexturePayload(recordOffset, context->resourceRegistry,
					                                   texturedDispatch.textureHandle, &texturePayload, rowScroll,
					                                   &rowTable);
				}
				if (g_vehicleViewDumpDiagnostics) {
					TrackView_CopyScreenSnapshot(screenBefore);
					haveScreenBefore = true;
				}
				if (texturedDispatch.calledOpaqueAffineRasterizer ||
				    (texturedDispatch.calledTransparentAffineRasterizer &&
				     texturePayload.size >= SLIP_SPRITE_TRANSPARENT_COLOUR_END &&
				     SlipBytes_ReadLE16(texturePayload.data + SLIP_SPRITE_TRANSPARENT_COLOUR_OFFSET) !=
				         SLIP_SPRITE_NO_TRANSPARENT_COLOUR)) {
					RasterAffineTexturedEntryScanVisit affineEntryVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
					RasterAffineTexturedEntrySetup affineEntry;

					if (!Raster_PrepareAffineTexturedEntry(
					        texturedDispatch.textureHandle, texturePayload.data, texturePayload.size,
					        texturedPointBuffer, TRACK_VIEW_DIAGNOSTIC_POINT_BUFFER_BASE, texturedPointBufferBytes,
					        (uint32_t)texturedDispatch.pointCount, affineEntryVisits,
					        sizeof(affineEntryVisits) / sizeof(affineEntryVisits[0]), &affineEntry)) {
						free(textureRows);
						context->failureAddress = TRACK_VIEW_DIAGNOSTIC_AFFINE_TEXTURED_ENTRY;
						context->failed = true;
						return false;
					}
					TrackView_DumpAffineEntry(recordOffset, &affineEntry);
					++context->directCallbackHighRasterEntryCount;
					if (affineEntry.horizontalBranch) {
						RasterAffineHorizontalSpanState horizontalState;
						RasterAffineHorizontalSpan horizontal;

						if (affineEntry.topY < 0 || (size_t)affineEntry.topY >= SLIPSTREAM_SCREEN_HEIGHT) {
							free(textureRows);
							context->failureAddress = TRACK_VIEW_DIAGNOSTIC_AFFINE_HORIZONTAL_SPAN;
							context->failed = true;
							return false;
						}
						memset(&horizontalState, 0, sizeof(horizontalState));
						horizontalState.screenRow = g_screenRowPtrs[affineEntry.topY];
						horizontalState.screenRowBytes = SLIPSTREAM_SCREEN_WIDTH;
						horizontalState.lockedPayload = texturePayload.data;
						horizontalState.lockedPayloadBytes = texturePayload.size;
						horizontalState.pointBuffer = texturedPointBuffer;
						horizontalState.pointBufferBytes = texturedPointBufferBytes;
						horizontalState.topLeftPointOffset = affineEntry.topLeftPointOffset;
						horizontalState.topRightPointOffset = affineEntry.topRightPointOffset;
						horizontalState.textureRows = textureRows;
						horizontalState.textureRowCount = textureHeight;
						horizontalState.textureRowBytes = textureWidth;
						if (!Raster_DrawAffineHorizontalSpan(&horizontalState, &horizontal)) {
							free(textureRows);
							context->failureAddress = TRACK_VIEW_DIAGNOSTIC_AFFINE_HORIZONTAL_SPAN;
							context->failed = true;
							return false;
						}
						if (horizontal.calledDrawTexturedSpanCore) {
							context->directCallbackHighPixelsWrittenCount +=
							    (uint32_t)horizontal.spanCore.pixelsWritten;
						}
					} else {
						RasterAffineLeftEdgeStep leftStep;
						RasterAffineRightEdgeStep rightStep;
						RasterAffineScanlineLoopVisit *loopVisits;
						RasterAffineScanlineLoopState loopState;
						RasterAffineScanlineLoop loop;
						const uint32_t pointBufferEnd =
						    TRACK_VIEW_DIAGNOSTIC_POINT_BUFFER_BASE + affineEntry.endPointOffset;

						if (!Raster_StepLeftEdgeAffine(texturedPointBuffer, TRACK_VIEW_DIAGNOSTIC_POINT_BUFFER_BASE,
						                               pointBufferEnd, texturedPointBufferBytes,
						                               TRACK_VIEW_DIAGNOSTIC_POINT_BUFFER_BASE +
						                                   affineEntry.topLeftPointOffset,
						                               affineEntry.topY, affineEntry.bottomY, &leftStep) ||
						    !Raster_StepRightEdgeAffine(texturedPointBuffer, TRACK_VIEW_DIAGNOSTIC_POINT_BUFFER_BASE,
						                                pointBufferEnd, texturedPointBufferBytes,
						                                TRACK_VIEW_DIAGNOSTIC_POINT_BUFFER_BASE +
						                                    affineEntry.topRightPointOffset,
						                                affineEntry.topY, affineEntry.bottomY, &rightStep)) {
							free(textureRows);
							context->failureAddress = TRACK_VIEW_DIAGNOSTIC_AFFINE_EDGE_STEPS;
							context->failed = true;
							return false;
						}
						if (!leftStep.carryOut && !rightStep.carryOut) {
							loopVisits = (RasterAffineScanlineLoopVisit *)calloc(SLIP_TEXTURED_RASTER_VISIT_CAPACITY,
							                                                     sizeof(*loopVisits));
							memset(&loopState, 0, sizeof(loopState));
							loopState.screenRows = g_screenRowPtrs;
							loopState.screenRowCount = SLIPSTREAM_SCREEN_HEIGHT;
							loopState.screenRowBytes = SLIPSTREAM_SCREEN_WIDTH;
							loopState.lockedPayload = texturePayload.data;
							loopState.lockedPayloadBytes = texturePayload.size;
							loopState.pointBuffer = texturedPointBuffer;
							loopState.pointBufferBase = TRACK_VIEW_DIAGNOSTIC_POINT_BUFFER_BASE;
							loopState.pointBufferEnd = pointBufferEnd;
							loopState.pointBufferBytes = texturedPointBufferBytes;
							loopState.textureRows = textureRows;
							loopState.textureRowCount = textureHeight;
							loopState.textureRowBytes = textureWidth;
							loopState.leftPointOffset = leftStep.pointOffsetOut;
							loopState.rightPointOffset = rightStep.pointOffsetOut;
							loopState.scanline = affineEntry.topY;
							loopState.bottomY = affineEntry.bottomY;
							loopState.leftX = leftStep.currentX;
							loopState.rightX = rightStep.currentX;
							loopState.spanLeftTexU = leftStep.spanLeftTexU;
							loopState.spanLeftTexV = leftStep.spanLeftTexV;
							loopState.spanRightTexU = rightStep.spanRightTexU;
							loopState.spanRightTexV = rightStep.spanRightTexV;
							loopState.leftTexUStep = leftStep.texUStep;
							loopState.leftTexVStep = leftStep.texVStep;
							loopState.rightTexUStep = rightStep.texUStep;
							loopState.rightTexVStep = rightStep.texVStep;
							loopState.leftXStep = leftStep.xStep;
							loopState.leftXFraction = leftStep.xFraction;
							loopState.leftRemaining = leftStep.remaining;
							loopState.rightXStep = rightStep.xStep;
							loopState.rightXFraction = rightStep.xFraction;
							loopState.rightRemaining = rightStep.remaining;
							if (loopVisits == NULL ||
							    !Raster_DrawAffineScanlineLoop(&loopState, loopVisits,
							                                   SLIP_TEXTURED_RASTER_VISIT_CAPACITY, &loop)) {
								free(loopVisits);
								free(textureRows);
								context->failureAddress = TRACK_VIEW_DIAGNOSTIC_AFFINE_SCANLINE_LOOP;
								context->failed = true;
								return false;
							}
							TrackView_DumpAffineLoop(recordOffset, &loop, loopVisits);
							for (size_t affineVisit = 0; affineVisit < loop.visitCount; ++affineVisit) {
								if (loopVisits[affineVisit].calledDrawTexturedSpanCore) {
									context->directCallbackHighPixelsWrittenCount +=
									    (uint32_t)loopVisits[affineVisit].spanCore.pixelsWritten;
								}
							}
							if (loop.calledFinalTexturedSpanCore) {
								context->directCallbackHighPixelsWrittenCount +=
								    (uint32_t)loop.finalSpanCore.pixelsWritten;
							}
							free(loopVisits);
						}
					}
				} else if (texturedDispatch.calledTransparentPerspectiveRasterizer) {
					RasterTexturedEntryScanVisit perspectiveEntryVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
					RasterTexturedEntrySetup perspectiveEntry;

					if (!Raster_PrepareTexturedEntry(
					        texturedDispatch.textureHandle, texturePayload.data, texturePayload.size,
					        texturedPointBuffer, TRACK_VIEW_DIAGNOSTIC_POINT_BUFFER_BASE, texturedPointBufferBytes,
					        (uint32_t)texturedDispatch.pointCount, perspectiveEntryVisits,
					        sizeof(perspectiveEntryVisits) / sizeof(perspectiveEntryVisits[0]), &perspectiveEntry)) {
						free(textureRows);
						context->failureAddress = TRACK_VIEW_DIAGNOSTIC_PERSPECTIVE_TEXTURED_ENTRY;
						context->failed = true;
						return false;
					}
					++context->directCallbackHighRasterEntryCount;
					if (perspectiveEntry.jumpOpaquePerspectiveScanlineLoop) {
						RasterOpaquePerspectiveTexturedVisit *opaqueVisits;
						RasterOpaquePerspectiveTexturedPolygon opaque;
						RasterOpaquePerspectiveTexturedPolygonState opaqueState;

						opaqueVisits = (RasterOpaquePerspectiveTexturedVisit *)calloc(
						    SLIP_TEXTURED_RASTER_VISIT_CAPACITY, sizeof(*opaqueVisits));
						memset(&opaqueState, 0, sizeof(opaqueState));
						opaqueState.screenRows = g_screenRowPtrs;
						opaqueState.screenRowCount = SLIPSTREAM_SCREEN_HEIGHT;
						opaqueState.screenRowBytes = SLIPSTREAM_SCREEN_WIDTH;
						opaqueState.lockedPayload = texturePayload.data;
						opaqueState.lockedPayloadBytes = texturePayload.size;
						opaqueState.pointBuffer = texturedPointBuffer;
						opaqueState.pointBufferBase = TRACK_VIEW_DIAGNOSTIC_POINT_BUFFER_BASE;
						opaqueState.pointBufferBytes = texturedPointBufferBytes;
						opaqueState.pointCount = (uint32_t)texturedDispatch.pointCount;
						opaqueState.textureRows = textureRows;
						opaqueState.textureRowCount = textureHeight;
						opaqueState.textureRowBytes = textureWidth;
						opaqueState.topY = perspectiveEntry.topY;
						opaqueState.bottomY = perspectiveEntry.bottomY;
						opaqueState.topLeftPointOffset =
						    TRACK_VIEW_DIAGNOSTIC_POINT_BUFFER_BASE + perspectiveEntry.topLeftPointOffset;
						opaqueState.topRightPointOffset =
						    TRACK_VIEW_DIAGNOSTIC_POINT_BUFFER_BASE + perspectiveEntry.topRightPointOffset;
						opaqueState.pointBufferEnd =
						    TRACK_VIEW_DIAGNOSTIC_POINT_BUFFER_BASE + perspectiveEntry.endPointOffset;
						if (opaqueVisits == NULL ||
						    !Raster_DrawOpaquePerspectiveTexturedPolygon(
						        &opaqueState, opaqueVisits, SLIP_TEXTURED_RASTER_VISIT_CAPACITY, &opaque)) {
							free(opaqueVisits);
							free(textureRows);
							context->failureAddress = TRACK_VIEW_DIAGNOSTIC_OPAQUE_PERSPECTIVE_POLYGON;
							context->failed = true;
							return false;
						}
						TrackView_DumpOpaquePerspective(recordOffset, &opaque, opaqueVisits,
						                                SLIP_TEXTURED_RASTER_VISIT_CAPACITY);
						context->directCallbackHighPixelsWrittenCount += (uint32_t)opaque.pixelsWritten;
						free(opaqueVisits);
					} else if (perspectiveEntry.jumpMaskedPerspectivePolygon) {
						RasterMaskedPerspectiveTexturedPolygonVisit *maskedVisits;
						RasterMaskedPerspectiveTexturedPolygon masked;
						RasterMaskedPerspectiveTexturedPolygonState maskedState;

						maskedVisits = (RasterMaskedPerspectiveTexturedPolygonVisit *)calloc(
						    SLIP_TEXTURED_RASTER_VISIT_CAPACITY, sizeof(*maskedVisits));
						memset(&maskedState, 0, sizeof(maskedState));
						maskedState.screenRows = g_screenRowPtrs;
						maskedState.screenRowCount = SLIPSTREAM_SCREEN_HEIGHT;
						maskedState.screenRowBytes = SLIPSTREAM_SCREEN_WIDTH;
						maskedState.lockedPayload = texturePayload.data;
						maskedState.lockedPayloadBytes = texturePayload.size;
						maskedState.pointBuffer = texturedPointBuffer;
						maskedState.pointBufferBase = TRACK_VIEW_DIAGNOSTIC_POINT_BUFFER_BASE;
						maskedState.pointBufferBytes = texturedPointBufferBytes;
						maskedState.pointCount = (uint32_t)texturedDispatch.pointCount;
						maskedState.textureRows = textureRows;
						maskedState.textureRowCount = textureHeight;
						maskedState.textureRowBytes = textureWidth;
						if (maskedVisits == NULL ||
						    !Raster_DrawMaskedPerspectiveTexturedPolygon(
						        &maskedState, maskedVisits, SLIP_TEXTURED_RASTER_VISIT_CAPACITY, &masked)) {
							if (maskedVisits != NULL) {
								TrackView_DumpMaskedRaster(recordOffset, &masked, maskedVisits,
								                           SLIP_TEXTURED_RASTER_VISIT_CAPACITY);
							}
							free(maskedVisits);
							free(textureRows);
							context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MASKED_PERSPECTIVE_POLYGON;
							context->failed = true;
							return false;
						}
						TrackView_DumpMaskedRaster(recordOffset, &masked, maskedVisits,
						                           SLIP_TEXTURED_RASTER_VISIT_CAPACITY);
						context->directCallbackHighPixelsWrittenCount += (uint32_t)masked.pixelsWritten;
						free(maskedVisits);
					}
				} else if (texturedDispatch.calledOpaquePerspectiveRasterizer) {
					RasterMaskedPerspectiveTexturedPolygonVisit *maskedVisits;
					RasterMaskedPerspectiveTexturedPolygon masked;
					RasterMaskedPerspectiveTexturedPolygonState maskedState;

					maskedVisits = (RasterMaskedPerspectiveTexturedPolygonVisit *)calloc(
					    SLIP_TEXTURED_RASTER_VISIT_CAPACITY, sizeof(*maskedVisits));
					memset(&maskedState, 0, sizeof(maskedState));
					maskedState.screenRows = g_screenRowPtrs;
					maskedState.screenRowCount = SLIPSTREAM_SCREEN_HEIGHT;
					maskedState.screenRowBytes = SLIPSTREAM_SCREEN_WIDTH;
					maskedState.lockedPayload = texturePayload.data;
					maskedState.lockedPayloadBytes = texturePayload.size;
					maskedState.pointBuffer = texturedPointBuffer;
					maskedState.pointBufferBase = TRACK_VIEW_DIAGNOSTIC_POINT_BUFFER_BASE;
					maskedState.pointBufferBytes = texturedPointBufferBytes;
					maskedState.pointCount = (uint32_t)texturedDispatch.pointCount;
					maskedState.textureRows = textureRows;
					maskedState.textureRowCount = textureHeight;
					maskedState.textureRowBytes = textureWidth;
					++context->directCallbackHighRasterEntryCount;
					if (maskedVisits == NULL ||
					    !Raster_DrawMaskedPerspectiveTexturedPolygon(&maskedState, maskedVisits,
					                                                 SLIP_TEXTURED_RASTER_VISIT_CAPACITY, &masked)) {
						if (maskedVisits != NULL) {
							TrackView_DumpMaskedRaster(recordOffset, &masked, maskedVisits,
							                           SLIP_TEXTURED_RASTER_VISIT_CAPACITY);
						}
						free(maskedVisits);
						free(textureRows);
						context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MASKED_PERSPECTIVE_POLYGON;
						context->failed = true;
						return false;
					}
					TrackView_DumpMaskedRaster(recordOffset, &masked, maskedVisits,
					                           SLIP_TEXTURED_RASTER_VISIT_CAPACITY);
					context->directCallbackHighPixelsWrittenCount += (uint32_t)masked.pixelsWritten;
					free(maskedVisits);
				} else if (texturedDispatch.calledTransparentAffineRasterizer) {
					RasterOpaqueAffineTexturedVisit *opaqueAffineVisits;
					RasterOpaqueAffineTexturedPolygon opaqueAffine;
					RasterOpaqueAffineTexturedPolygonState opaqueAffineState;

					opaqueAffineVisits = (RasterOpaqueAffineTexturedVisit *)calloc(SLIP_TEXTURED_RASTER_VISIT_CAPACITY,
					                                                               sizeof(*opaqueAffineVisits));
					memset(&opaqueAffineState, 0, sizeof(opaqueAffineState));
					opaqueAffineState.screenRows = g_screenRowPtrs;
					opaqueAffineState.screenRowCount = SLIPSTREAM_SCREEN_HEIGHT;
					opaqueAffineState.screenRowBytes = SLIPSTREAM_SCREEN_WIDTH;
					opaqueAffineState.lockedPayload = texturePayload.data;
					opaqueAffineState.lockedPayloadBytes = texturePayload.size;
					opaqueAffineState.pointBuffer = texturedPointBuffer;
					opaqueAffineState.pointBufferBase = TRACK_VIEW_DIAGNOSTIC_POINT_BUFFER_BASE;
					opaqueAffineState.pointBufferBytes = texturedPointBufferBytes;
					opaqueAffineState.pointCount = (uint32_t)texturedDispatch.pointCount;
					opaqueAffineState.textureRows = textureRows;
					opaqueAffineState.textureRowCount = textureHeight;
					opaqueAffineState.textureRowBytes = textureWidth;
					opaqueAffineState.rowScroll = rowScroll;
					++context->directCallbackHighRasterEntryCount;
					if (opaqueAffineVisits == NULL ||
					    !Raster_DrawOpaqueAffineTexturedPolygon(&opaqueAffineState, opaqueAffineVisits,
					                                            SLIP_TEXTURED_RASTER_VISIT_CAPACITY, &opaqueAffine)) {
						free(opaqueAffineVisits);
						free(textureRows);
						context->failureAddress = TRACK_VIEW_DIAGNOSTIC_OPAQUE_AFFINE_POLYGON;
						context->failed = true;
						return false;
					}
					context->directCallbackHighPixelsWrittenCount += (uint32_t)opaqueAffine.pixelsWritten;
					free(opaqueAffineVisits);
				} else {
					free(textureRows);
					context->failureAddress = TRACK_VIEW_DIAGNOSTIC_FLAT_RING;
					context->failed = true;
					return false;
				}

				TrackView_UnlockResourceHandlePayload(context->resourceRegistry, texturedDispatch.textureHandle);
				if (haveScreenBefore) {
					TrackView_DumpRecordDamage(recordOffset, texturedDispatch.renderFlags, screenBefore);
				}
				free(textureRows);
			}
		}
		return TrackView_CompleteTexturedCallback(context, savedRenderFlags, texturedRing.carryOut, handled,
		                                          callbackResult, carryOut);
	}
}

static SlipDraw3DVec32 TrackView_MaterialMidpoint(uint32_t sourceX, uint32_t sourceY, uint32_t sourceZ,
                                                  SlipDraw3DVertexRecord *record, void *userData) {
	TrackViewMaterialMidpointContext *const context = (TrackViewMaterialMidpointContext *)userData;
	SlipDraw3DProjectIndex first;
	SlipDraw3DProjectIndex second;
	const uint16_t firstIndex = (uint16_t)sourceX;
	const uint16_t secondIndex = (uint16_t)sourceY;

	(void)sourceZ;
	(void)record;
	if (context == NULL ||
	    !SlipDraw3D_ProjectIndex(context->vertexRecords, context->vertexRecordCount, firstIndex,
	                             TrackView_MaterialMidpoint, context, &first) ||
	    !SlipDraw3D_ProjectIndex(context->vertexRecords, context->vertexRecordCount, secondIndex,
	                             TrackView_MaterialMidpoint, context, &second)) {
		return (SlipDraw3DVec32){0, 0, 0};
	}
	return (SlipDraw3DVec32){(first.world.x + second.world.x) >> 1, (first.world.y + second.world.y) >> 1,
	                         (first.world.z + second.world.z) >> 1};
}

static SlipView3DVec32 TrackView_MaterialSourcePoint(int16_t sourceX, int16_t sourceY, int16_t sourceZ,
                                                     void *userData) {
	(void)userData;
	return (SlipView3DVec32){sourceX, sourceY, sourceZ};
}

typedef struct TrackViewMaterialProjectionState {
	uint32_t coordinateX;
	uint32_t coordinateY;
	uint32_t coordinateZ;
	uint32_t vertexFlags;
} TrackViewMaterialProjectionState;

static TrackViewMaterialProjectionState TrackView_MaterialProjectionState(SlipDraw3DVertexRecord *record,
                                                                          const SlipDraw3DProjectState *state,
                                                                          TrackViewMaterialMidpointContext *context,
                                                                          TrackViewMaterialProjectionState in) {
	TrackViewMaterialProjectionState out = in;
	uint32_t flagsBefore;
	uint32_t flagsAfter;

	flagsBefore = (uint32_t)record->flags;
	flagsAfter = SlipDraw3D_ProjectVertex(record, state, TrackView_MaterialScreenMidpoint,
	                                      TrackView_MaterialProjectScreenPrimary,
	                                      TrackView_MaterialProjectScreenSecondary, context);
	out.vertexFlags = flagsAfter;
	if ((flagsBefore & SLIP_VERTEX_PROJECTED) == 0u) {
		out.coordinateX = (uint32_t)record->world.x;
		out.coordinateY = (uint32_t)record->world.y;
		out.coordinateZ = (uint32_t)record->world.z;
	}
	if ((flagsAfter & SLIP_CLIP_BEFORE_PROJECTION) == 0u) {
		out.coordinateX = (uint32_t)record->screenX;
		out.coordinateY = (uint32_t)record->screenY;
	}
	return out;
}

static SlipDraw3DVec32 TrackView_MaterialScreenMidpoint(uint32_t sourceX, uint32_t sourceY, uint32_t sourceZ,
                                                        SlipDraw3DVertexRecord *record, void *userData) {
	TrackViewMaterialMidpointContext *const context = (TrackViewMaterialMidpointContext *)userData;
	SlipDraw3DVertexRecord *first;
	SlipDraw3DVertexRecord *second;
	TrackViewMaterialProjectionState firstProjection;
	TrackViewMaterialProjectionState secondProjection;
	int32_t averageScreenX;
	int32_t averageScreenY;
	const SlipDraw3DProjectState *state;

	if (context == NULL || record == NULL || context->vertexRecords == NULL ||
	    (size_t)(uint16_t)sourceX >= context->vertexRecordCount ||
	    (size_t)(uint16_t)sourceY >= context->vertexRecordCount) {
		return (SlipDraw3DVec32){0, 0, 0};
	}
	state = context->primitiveContext != NULL ? context->primitiveContext->projectState : NULL;
	if (state == NULL) {
		return (SlipDraw3DVec32){0, 0, 0};
	}
	first = &context->vertexRecords[(uint16_t)sourceX];
	second = &context->vertexRecords[(uint16_t)sourceY];

	firstProjection = TrackView_MaterialProjectionState(
	    first, state, context, (TrackViewMaterialProjectionState){sourceX, sourceY, sourceZ, sourceX});
	secondProjection = TrackView_MaterialProjectionState(
	    second, state, context,
	    (TrackViewMaterialProjectionState){firstProjection.coordinateX, firstProjection.coordinateY,
	                                       firstProjection.coordinateZ, sourceY});

	averageScreenX = (int32_t)(firstProjection.coordinateX + secondProjection.coordinateX) >> 1;
	averageScreenY = (int32_t)(firstProjection.coordinateY + secondProjection.coordinateY) >> 1;
	TrackView_StoreMaterialScreenVertex(record, state, averageScreenX, averageScreenY);
	return (SlipDraw3DVec32){averageScreenX, averageScreenY, (int32_t)secondProjection.coordinateZ};
}

static void TrackView_StoreMaterialSeedVertex(SlipDraw3DVertexRecord *record, SlipDraw3DVec32 worldPosition) {
	record->flags = SLIP_VERTEX_TRANSFORMED;
	record->world.x = worldPosition.x;
	record->world.y = worldPosition.y;
	record->world.z = worldPosition.z;
}

static void TrackView_StoreMaterialScreenVertex(SlipDraw3DVertexRecord *record, const SlipDraw3DProjectState *state,
                                                int32_t screenX, int32_t screenY) {
	uint32_t clipFlags = SLIP_VERTEX_PROJECTED;

	record->flags = SLIP_VERTEX_TRANSFORMED_AND_DEPTH_CLASSIFIED;
	record->screenX = screenX;
	record->screenY = screenY;
	if (state == NULL) {
		return;
	}

	if (screenX < state->minX) {
		clipFlags |= SLIP_CLIP_LEFT;
	}

	if (screenX > state->maxX) {
		clipFlags |= SLIP_CLIP_RIGHT;
	}

	if (screenY < state->minY) {
		clipFlags |= SLIP_CLIP_TOP;
	}

	if (screenY > state->maxY) {
		clipFlags |= SLIP_CLIP_BOTTOM;
	}
	if ((clipFlags & SLIP_CLIP_SCREEN) != 0 && screenX < SLIP_SCREEN_CLIP_COORDINATE_LIMIT &&
	    screenY < SLIP_SCREEN_CLIP_COORDINATE_LIMIT && screenX > -SLIP_SCREEN_CLIP_COORDINATE_LIMIT &&
	    screenY > -SLIP_SCREEN_CLIP_COORDINATE_LIMIT) {
		clipFlags |= SLIP_VERTEX_SCREEN_CLIP_IN_RANGE;
	}
	record->flags |= clipFlags;
}

static uint16_t TrackView_MaterialColorStep(const uint8_t *materialRecord, size_t materialRecordBytes,
                                            uint32_t materialColor, int32_t colorDelta) {
	uint32_t steppedColor = materialColor + (uint32_t)colorDelta;

	if (materialRecord == NULL || materialRecordBytes < SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE) {
		return (uint16_t)steppedColor;
	}
	if (steppedColor < SlipBytes_ReadLE32(materialRecord + offsetof(SlipDraw3DMaterialRecord, rampStart))) {
		steppedColor = SlipBytes_ReadLE32(materialRecord + offsetof(SlipDraw3DMaterialRecord, importedMaterialByte));
	}
	return (uint16_t)steppedColor;
}

static uint16_t TrackView_MaterialHandle(const TrackViewRawBspContext *context) {
	SlipDraw3DMaterialNumber lookup;

	if (context == NULL || context->materialTable == NULL ||
	    !SlipDraw3D_GetMaterialNumber(context->materialTable, context->materialTableBytes, context->materialGlobal,
	                                  (const uint8_t *)kTrackViewCageMaterialName, sizeof(kTrackViewCageMaterialName),
	                                  &lookup) ||
	    lookup.carryOut) {
		return 0;
	}
	return lookup.materialIndex;
}

static bool TrackView_MaterialPair(const uint8_t *materialTable, size_t materialTableBytes, uint16_t materialGlobal,
                                   const char *materialName, uint32_t *rampStartOut, uint32_t *rampEndOut) {
	SlipDraw3DMaterialNumber lookup;
	size_t materialRecordOffset;

	if (materialName == NULL || rampStartOut == NULL || rampEndOut == NULL ||
	    !SlipDraw3D_GetMaterialNumber(materialTable, materialTableBytes, materialGlobal, (const uint8_t *)materialName,
	                                  strlen(materialName) + 1u, &lookup) ||
	    lookup.carryOut) {
		return false;
	}
	materialRecordOffset = SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES + (size_t)lookup.materialRecordOffset;
	if (materialRecordOffset > materialTableBytes ||
	    materialTableBytes - materialRecordOffset < offsetof(SlipDraw3DMaterialRecord, rampEnd) + sizeof(uint32_t)) {
		return false;
	}
	*rampStartOut =
	    SlipBytes_ReadLE32(materialTable + materialRecordOffset + offsetof(SlipDraw3DMaterialRecord, rampStart));
	*rampEndOut =
	    SlipBytes_ReadLE32(materialTable + materialRecordOffset + offsetof(SlipDraw3DMaterialRecord, rampEnd));
	return true;
}

static bool TrackView_MaterialValue(const uint8_t *materialTable, size_t materialTableBytes, uint16_t materialGlobal,
                                    const char *materialName, uint32_t *materialValueOut) {
	SlipDraw3DMaterialNumber lookup;
	size_t materialRecordOffset;

	if (materialName == NULL || materialValueOut == NULL ||
	    !SlipDraw3D_GetMaterialNumber(materialTable, materialTableBytes, materialGlobal, (const uint8_t *)materialName,
	                                  strlen(materialName) + 1u, &lookup) ||
	    lookup.carryOut) {
		return false;
	}
	materialRecordOffset = SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES + (size_t)lookup.materialRecordOffset;
	if (materialRecordOffset > materialTableBytes ||
	    materialTableBytes - materialRecordOffset < SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE) {
		return false;
	}
	*materialValueOut = SlipBytes_ReadLE32(materialTable + materialRecordOffset +
	                                       offsetof(SlipDraw3DMaterialRecord, importedMaterialByte));
	return true;
}

void TrackView_MaterialInit(const uint8_t *materialTable, size_t materialTableBytes, uint16_t materialGlobal,
                            TrackViewMaterialInit *result) {
	SlipDraw3DMaterialNumber lookup;

	if (result == NULL) {
		return;
	}
	memset(result, 0, sizeof(*result));

#define TRACK_VIEW_LOOKUP(name_, handle_, body_)                                                                       \
	do {                                                                                                               \
		if (SlipDraw3D_GetMaterialNumber(materialTable, materialTableBytes, materialGlobal, (const uint8_t *)(name_),  \
		                                 sizeof(name_), &lookup) &&                                                    \
		    !lookup.carryOut) {                                                                                        \
			(handle_) = lookup.materialIndex;                                                                          \
			body_                                                                                                      \
		}                                                                                                              \
	} while (0)

	TRACK_VIEW_LOOKUP(kTrackViewCageMaterialName, result->cage, (void)0;);
	TRACK_VIEW_LOOKUP(kTrackViewRoadLineMaterialName, result->roadLine,
	                  (void)TrackView_MaterialValue(materialTable, materialTableBytes, materialGlobal,
	                                                kTrackViewRoadLineMaterialName, &result->roadLineValue););
	TRACK_VIEW_LOOKUP(kTrackViewOrangeLightMaterialName, result->orangeLight,
	                  (void)TrackView_MaterialPair(materialTable, materialTableBytes, materialGlobal,
	                                               kTrackViewOrangeLightMaterialName, &result->orangeLightLow,
	                                               &result->orangeLightHigh););
	TRACK_VIEW_LOOKUP(kTrackViewFloorLightMaterialName, result->floorLight,
	                  (void)TrackView_MaterialPair(materialTable, materialTableBytes, materialGlobal,
	                                               kTrackViewFloorLightMaterialName, &result->floorLightLow,
	                                               &result->floorLightHigh););
	TRACK_VIEW_LOOKUP(kTrackViewBlueLightMaterialName, result->blueLight,
	                  (void)TrackView_MaterialPair(materialTable, materialTableBytes, materialGlobal,
	                                               kTrackViewBlueLightMaterialName, &result->blueLightLow,
	                                               &result->blueLightHigh););
	TRACK_VIEW_LOOKUP(kTrackViewWhiteLightMaterialName, result->whiteLight, (void)0;);
	TRACK_VIEW_LOOKUP(kTrackViewYellowMaterialName, result->yellow,
	                  (void)TrackView_MaterialValue(materialTable, materialTableBytes, materialGlobal,
	                                                kTrackViewYellowMaterialName, &result->yellowValue););
	g_materialAnimRangeLow = result->blueLightLow;
	g_materialAnimRangeHigh = result->blueLightHigh;

#undef TRACK_VIEW_LOOKUP
}

static void TrackView_ProjectScreenPrimary(SlipDraw3DVec32 world, int32_t *screenX, int32_t *screenY, void *userData) {

	const TrackViewPrimitiveCallbackContext *const context = userData;
	SlipDraw3DProjectState *const state = context->projectState;
	state->projectPrimary(world, screenX, screenY, state);
}

static void TrackView_ProjectScreenSecondary(SlipDraw3DVec32 world, int32_t *screenX, int32_t *screenY,
                                             void *userData) {

	const TrackViewPrimitiveCallbackContext *const context = userData;
	SlipDraw3DProjectState *const state = context->projectState;
	state->projectSecondary(world, screenX, screenY, state);
}

static void TrackView_MaterialProjectScreenPrimary(SlipDraw3DVec32 world, int32_t *screenX, int32_t *screenY,
                                                   void *userData) {

	const TrackViewMaterialMidpointContext *const context = userData;
	SlipDraw3DProjectState *const state = context->primitiveContext->projectState;
	state->projectPrimary(world, screenX, screenY, state);
}

static void TrackView_MaterialProjectScreenSecondary(SlipDraw3DVec32 world, int32_t *screenX, int32_t *screenY,
                                                     void *userData) {

	const TrackViewMaterialMidpointContext *const context = userData;
	SlipDraw3DProjectState *const state = context->primitiveContext->projectState;
	state->projectSecondary(world, screenX, screenY, state);
}

static const uint8_t *TrackView_IndependentSetupTriangleQuadsData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_INDEPENDENT_SETUP_TRIANGLE_QUADS_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_INDEPENDENT_SETUP_TRIANGLE_QUADS_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewIndependentSetupTriangleQuadsData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewIndependentSetupTriangleQuadsData) - (size_t)offset;
	}
	return kTrackViewIndependentSetupTriangleQuadsData + offset;
}

static const uint8_t *TrackView_RoadLinePolygonsData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_ROAD_LINE_POLYGONS_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_ROAD_LINE_POLYGONS_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewRoadLinePolygonsData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewRoadLinePolygonsData) - (size_t)offset;
	}
	return kTrackViewRoadLinePolygonsData + offset;
}

static const uint8_t *TrackView_ShadedTriangleQuadsData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_SHADED_TRIANGLE_QUADS_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_SHADED_TRIANGLE_QUADS_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewShadedTriangleQuadsData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewShadedTriangleQuadsData) - (size_t)offset;
	}
	return kTrackViewShadedTriangleQuadsData + offset;
}

static const uint8_t *TrackView_SharedSetupTriangleQuadsData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_SHARED_SETUP_TRIANGLE_QUADS_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_SHARED_SETUP_TRIANGLE_QUADS_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewSharedSetupTriangleQuadsData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewSharedSetupTriangleQuadsData) - (size_t)offset;
	}
	return kTrackViewSharedSetupTriangleQuadsData + offset;
}

static const uint8_t *TrackView_SecondaryColorQuadsData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_SECONDARY_COLOR_QUADS_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_SECONDARY_COLOR_QUADS_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewSecondaryColorQuadsData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewSecondaryColorQuadsData) - (size_t)offset;
	}
	return kTrackViewSecondaryColorQuadsData + offset;
}

static const uint8_t kTrackViewMaterialFamilyData[] = {
    0xc0, 0xf7, 0x03, 0x00, 0xca, 0xf7, 0x03, 0x00, 0xd4, 0xf7, 0x03, 0x00, 0xde, 0xf7, 0x03, 0x00, 0xe8, 0xf7, 0x03,
    0x00, 0xf2, 0xf7, 0x03, 0x00, 0xfc, 0xf7, 0x03, 0x00, 0x06, 0xf8, 0x03, 0x00, 0x20, 0xf7, 0x03, 0x00, 0x70, 0xf7,
    0x03, 0x00, 0x2a, 0xf7, 0x03, 0x00, 0x7a, 0xf7, 0x03, 0x00, 0x34, 0xf7, 0x03, 0x00, 0x84, 0xf7, 0x03, 0x00, 0x3e,
    0xf7, 0x03, 0x00, 0x8e, 0xf7, 0x03, 0x00, 0x48, 0xf7, 0x03, 0x00, 0x98, 0xf7, 0x03, 0x00, 0x52, 0xf7, 0x03, 0x00,
    0xa2, 0xf7, 0x03, 0x00, 0x5c, 0xf7, 0x03, 0x00, 0xac, 0xf7, 0x03, 0x00, 0x66, 0xf7, 0x03, 0x00, 0xb6, 0xf7, 0x03,
    0x00, 0x04, 0x00, 0x8e, 0x00, 0x5f, 0x00, 0x64, 0x00, 0x4e, 0x00, 0x04, 0x00, 0x5b, 0x00, 0x5e, 0x00, 0x65, 0x00,
    0x62, 0x00, 0x04, 0x00, 0x59, 0x00, 0x5d, 0x00, 0x66, 0x00, 0x61, 0x00, 0x04, 0x00, 0x5a, 0x00, 0x5c, 0x00, 0x67,
    0x00, 0x63, 0x00, 0x04, 0x00, 0x51, 0x00, 0x58, 0x00, 0x6e, 0x00, 0x60, 0x00, 0x04, 0x00, 0x56, 0x00, 0x57, 0x00,
    0x6d, 0x00, 0x6c, 0x00, 0x04, 0x00, 0x52, 0x00, 0x54, 0x00, 0x6b, 0x00, 0x68, 0x00, 0x04, 0x00, 0x53, 0x00, 0x55,
    0x00, 0x6a, 0x00, 0x69, 0x00, 0x04, 0x00, 0x50, 0x00, 0x73, 0x00, 0x82, 0x00, 0x91, 0x00, 0x04, 0x00, 0x72, 0x00,
    0x74, 0x00, 0x83, 0x00, 0x81, 0x00, 0x04, 0x00, 0x71, 0x00, 0x76, 0x00, 0x85, 0x00, 0x80, 0x00, 0x04, 0x00, 0x75,
    0x00, 0x77, 0x00, 0x86, 0x00, 0x84, 0x00, 0x04, 0x00, 0x70, 0x00, 0x7a, 0x00, 0x89, 0x00, 0x7f, 0x00, 0x04, 0x00,
    0x79, 0x00, 0x7b, 0x00, 0x8a, 0x00, 0x88, 0x00, 0x04, 0x00, 0x78, 0x00, 0x7d, 0x00, 0x8c, 0x00, 0x87, 0x00, 0x04,
    0x00, 0x7c, 0x00, 0x7e, 0x00, 0x8d, 0x00, 0x8b, 0x00, 0x04, 0x00, 0x14, 0x00, 0x16, 0x00, 0x17, 0x00, 0x15, 0x00,
    0x04, 0x00, 0x36, 0x00, 0x38, 0x00, 0x39, 0x00, 0x37, 0x00, 0x04, 0x00, 0x1a, 0x00, 0x1c, 0x00, 0x1d, 0x00, 0x1b,
    0x00, 0x04, 0x00, 0x3c, 0x00, 0x3e, 0x00, 0x3f, 0x00, 0x3d, 0x00, 0x04, 0x00, 0x20, 0x00, 0x22, 0x00, 0x23, 0x00,
    0x21, 0x00, 0x04, 0x00, 0x46, 0x00, 0x48, 0x00, 0x49, 0x00, 0x47, 0x00, 0x04, 0x00, 0x26, 0x00, 0x28, 0x00, 0x29,
    0x00, 0x27, 0x00, 0x04, 0x00, 0x4a, 0x00, 0x4c, 0x00, 0x4d, 0x00, 0x4b, 0x00, 0x04, 0x00, 0x92, 0x00, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x03, 0x00, 0x01,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x06, 0x00, 0x04, 0x00, 0x06, 0x00, 0x01, 0x00, 0x05, 0x00,
    0x01, 0x00, 0x09, 0x00, 0x05, 0x00, 0x09, 0x00, 0x07, 0x00, 0x0a, 0x00, 0x08, 0x00, 0x0b, 0x00, 0x0a, 0x00, 0x0c,
    0x00, 0x0d, 0x00, 0x0b, 0x00, 0x0c, 0x00, 0x07, 0x00, 0x0d, 0x00, 0x08, 0x00, 0x10, 0x00, 0x07, 0x00, 0x11, 0x00,
    0x08, 0x00, 0x07, 0x00, 0x12, 0x00, 0x08, 0x00, 0x13, 0x00, 0x10, 0x00, 0x12, 0x00, 0x13, 0x00, 0x11, 0x00, 0x0c,
    0x00, 0x10, 0x00, 0x0d, 0x00, 0x11, 0x00, 0x10, 0x00, 0x18, 0x00, 0x11, 0x00, 0x19, 0x00, 0x0c, 0x00, 0x18, 0x00,
    0x0d, 0x00, 0x19, 0x00, 0x0c, 0x00, 0x0e, 0x00, 0x0d, 0x00, 0x0f, 0x00, 0x0c, 0x00, 0x1e, 0x00, 0x0d, 0x00, 0x1f,
    0x00, 0x0e, 0x00, 0x1e, 0x00, 0x0f, 0x00, 0x1f, 0x00, 0x0e, 0x00, 0x0a, 0x00, 0x0f, 0x00, 0x0b, 0x00, 0x0e, 0x00,
    0x24, 0x00, 0x0f, 0x00, 0x25, 0x00, 0x0a, 0x00, 0x24, 0x00, 0x0b, 0x00, 0x25, 0x00, 0x03, 0x00, 0x04, 0x00, 0x02,
    0x00, 0x05, 0x00, 0x04, 0x00, 0x2a, 0x00, 0x03, 0x00, 0x2a, 0x00, 0x05, 0x00, 0x2b, 0x00, 0x02, 0x00, 0x2b, 0x00,
    0x2c, 0x00, 0x2e, 0x00, 0x2d, 0x00, 0x2f, 0x00, 0x2c, 0x00, 0x30, 0x00, 0x2d, 0x00, 0x31, 0x00, 0x2c, 0x00, 0x32,
    0x00, 0x2d, 0x00, 0x33, 0x00, 0x2c, 0x00, 0x34, 0x00, 0x2d, 0x00, 0x35, 0x00, 0x32, 0x00, 0x34, 0x00, 0x33, 0x00,
    0x35, 0x00, 0x30, 0x00, 0x32, 0x00, 0x31, 0x00, 0x33, 0x00, 0x3a, 0x00, 0x32, 0x00, 0x33, 0x00, 0x3b, 0x00, 0x30,
    0x00, 0x3a, 0x00, 0x31, 0x00, 0x3b, 0x00, 0x30, 0x00, 0x2e, 0x00, 0x31, 0x00, 0x2f, 0x00, 0x30, 0x00, 0x40, 0x00,
    0x31, 0x00, 0x41, 0x00, 0x2e, 0x00, 0x40, 0x00, 0x2f, 0x00, 0x41, 0x00, 0x30, 0x00, 0x42, 0x00, 0x31, 0x00, 0x43,
    0x00, 0x40, 0x00, 0x42, 0x00, 0x41, 0x00, 0x43, 0x00, 0x40, 0x00, 0x44, 0x00, 0x41, 0x00, 0x45, 0x00, 0x2e, 0x00,
    0x44, 0x00, 0x2f, 0x00, 0x45, 0x00, 0x00, 0x00, 0x07, 0x00, 0x01, 0x00, 0x0a, 0x00, 0x03, 0x00, 0x2d, 0x00, 0x8e,
    0x00, 0x8f, 0x00, 0x8f, 0x00, 0x51, 0x00, 0x8f, 0x00, 0x52, 0x00, 0x52, 0x00, 0x53, 0x00, 0x8f, 0x00, 0x53, 0x00,
    0x52, 0x00, 0x51, 0x00, 0x52, 0x00, 0x56, 0x00, 0x56, 0x00, 0x51, 0x00, 0x8e, 0x00, 0x51, 0x00, 0x59, 0x00, 0x51,
    0x00, 0x8e, 0x00, 0x59, 0x00, 0x5a, 0x00, 0x51, 0x00, 0x5a, 0x00, 0x59, 0x00, 0x59, 0x00, 0x5b, 0x00, 0x8e, 0x00,
    0x5b, 0x00, 0x4e, 0x00, 0x4f, 0x00, 0x4e, 0x00, 0x60, 0x00, 0x4e, 0x00, 0x61, 0x00, 0x61, 0x00, 0x60, 0x00, 0x4e,
    0x00, 0x62, 0x00, 0x61, 0x00, 0x62, 0x00, 0x61, 0x00, 0x63, 0x00, 0x60, 0x00, 0x63, 0x00, 0x4f, 0x00, 0x60, 0x00,
    0x4f, 0x00, 0x68, 0x00, 0x4f, 0x00, 0x69, 0x00, 0x68, 0x00, 0x69, 0x00, 0x60, 0x00, 0x68, 0x00, 0x68, 0x00, 0x6c,
    0x00, 0x60, 0x00, 0x6c, 0x00, 0x2f, 0x00, 0x02, 0x00, 0x50, 0x00, 0x6f, 0x00, 0x50, 0x00, 0x70, 0x00, 0x50, 0x00,
    0x71, 0x00, 0x50, 0x00, 0x72, 0x00, 0x72, 0x00, 0x71, 0x00, 0x71, 0x00, 0x70, 0x00, 0x71, 0x00, 0x75, 0x00, 0x70,
    0x00, 0x75, 0x00, 0x70, 0x00, 0x6f, 0x00, 0x70, 0x00, 0x78, 0x00, 0x70, 0x00, 0x79, 0x00, 0x79, 0x00, 0x78, 0x00,
    0x78, 0x00, 0x6f, 0x00, 0x78, 0x00, 0x7c, 0x00, 0x7c, 0x00, 0x6f, 0x00, 0x90, 0x00, 0x91, 0x00, 0x7f, 0x00, 0x91,
    0x00, 0x80, 0x00, 0x91, 0x00, 0x81, 0x00, 0x91, 0x00, 0x80, 0x00, 0x81, 0x00, 0x7f, 0x00, 0x80, 0x00, 0x80, 0x00,
    0x84, 0x00, 0x84, 0x00, 0x7f, 0x00, 0x7f, 0x00, 0x90, 0x00, 0x7f, 0x00, 0x87, 0x00, 0x7f, 0x00, 0x88, 0x00, 0x88,
    0x00, 0x87, 0x00, 0x87, 0x00, 0x90, 0x00, 0x8b, 0x00, 0x87, 0x00, 0x90, 0x00, 0x8b, 0x00, 0x00, 0x00, 0x4e, 0x00,
    0x01, 0x00, 0x4f, 0x00, 0x02, 0x00, 0x6f, 0x00, 0x03, 0x00, 0x50, 0x00, 0x3d, 0xe0, 0x0f, 0x1a, 0x00, 0x0f, 0x8f,
    0x90, 0x00, 0x00, 0x00, 0xbf, 0xe0, 0xfb, 0x03, 0x00, 0xe8, 0x53, 0xf9, 0xff, 0xff, 0x0f, 0x82, 0x80, 0x00, 0x00,
    0x00, 0xbe, 0xf8, 0xfa, 0x03, 0x00, 0xbd, 0x04, 0x00, 0x00, 0x00, 0xb8, 0x00, 0x00, 0x00, 0x00, 0xba, 0xff, 0xff,
    0xff, 0xff, 0xe8, 0xab, 0xfa, 0xff, 0xff, 0xe8, 0x12, 0xfb, 0xff, 0xff, 0x8b, 0x1d, 0x78, 0xf0, 0x03, 0x00, 0x81,
    0xe3, 0xff, 0x1f, 0x00, 0x00, 0x81, 0xf3, 0xff, 0x1f, 0x00, 0x00, 0xc1, 0xeb, 0x0b, 0x33, 0xc9, 0xbe, 0x00, 0xfb,
    0x03, 0x00, 0x53, 0x51, 0x8b, 0xc1, 0x83, 0xe1, 0x03, 0x8b, 0x15, 0xf1, 0xef, 0x03, 0x00, 0x3b, 0xd9, 0x75, 0x06,
    0x8b, 0x15, 0xf5, 0xef, 0x03, 0x00, 0x56, 0x8b, 0x36, 0x66, 0x8b, 0x2e, 0x83, 0xc6, 0x02, 0xe8, 0x34, 0xa4, 0xfd,
    0xff, 0x5e, 0x56, 0x8b, 0x76, 0x04, 0x66, 0x8b, 0x2e, 0x83, 0xc6, 0x02, 0xe8, 0x24, 0xa4, 0xfd, 0xff, 0x5e, 0x59,
    0x5b, 0x83, 0xc6, 0x08, 0x41, 0x83, 0xf9, 0x08, 0x75, 0xbf, 0xe8, 0xe9, 0xf9, 0xff, 0xff, 0xc3, 0x00, 0x00, 0x01,
    0x00, 0x02, 0x00, 0x03, 0x00, 0x40, 0xfb, 0x03, 0x00, 0x90, 0xfb, 0x03, 0x00, 0x4a, 0xfb, 0x03, 0x00, 0x9a, 0xfb,
    0x03, 0x00, 0x54, 0xfb, 0x03, 0x00, 0xa4, 0xfb, 0x03, 0x00, 0x5e, 0xfb, 0x03, 0x00, 0xae, 0xfb, 0x03, 0x00, 0x68,
    0xfb, 0x03, 0x00, 0xb8, 0xfb, 0x03, 0x00, 0x72, 0xfb, 0x03, 0x00, 0xc2, 0xfb, 0x03, 0x00, 0x7c, 0xfb, 0x03, 0x00,
    0xcc, 0xfb, 0x03, 0x00, 0x86, 0xfb, 0x03, 0x00, 0xd6, 0xfb, 0x03, 0x00, 0x04, 0x00, 0x00, 0x00, 0x11, 0x00, 0x20,
    0x00, 0x08, 0x00, 0x04, 0x00, 0x10, 0x00, 0x12, 0x00, 0x21, 0x00, 0x1f, 0x00, 0x04, 0x00, 0x0f, 0x00, 0x14, 0x00,
    0x23, 0x00, 0x1e, 0x00, 0x04, 0x00, 0x13, 0x00, 0x15, 0x00, 0x24, 0x00, 0x22, 0x00, 0x04, 0x00, 0x0e, 0x00, 0x18,
    0x00, 0x27, 0x00, 0x1d, 0x00, 0x04, 0x00, 0x17, 0x00, 0x19, 0x00, 0x28, 0x00, 0x26, 0x00, 0x04, 0x00, 0x16, 0x00,
    0x1b, 0x00, 0x2a, 0x00, 0x25, 0x00, 0x04, 0x00, 0x1a, 0x00, 0x1c, 0x00, 0x2b, 0x00, 0x29, 0x00, 0x04, 0x00, 0x2f,
    0x00, 0x51, 0x00, 0x37, 0x00, 0x03, 0x00, 0x04, 0x00, 0x45, 0x00, 0x47, 0x00, 0x38, 0x00, 0x36, 0x00, 0x04, 0x00,
    0x44, 0x00, 0x48, 0x00, 0x3a, 0x00, 0x35, 0x00, 0x04, 0x00, 0x46, 0x00, 0x49, 0x00, 0x3b, 0x00, 0x39, 0x00, 0x04,
    0x00, 0x43, 0x00, 0x4c, 0x00, 0x3e, 0x00, 0x34, 0x00, 0x04, 0x00, 0x4b, 0x00, 0x4d, 0x00, 0x3f, 0x00, 0x3d, 0x00,
    0x04, 0x00, 0x4a, 0x00, 0x4f, 0x00, 0x41, 0x00, 0x3c, 0x00, 0x04, 0x00, 0x4e, 0x00, 0x50, 0x00, 0x42, 0x00, 0x40,
    0x00, 0x04, 0x00, 0x52, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00,
    0x00, 0x07, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x09, 0x00, 0x01, 0x00, 0x0a, 0x00, 0x01, 0x00, 0x0b, 0x00,
    0x01, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x10,
    0x00, 0x0f, 0x00, 0x10, 0x00, 0x0e, 0x00, 0x0f, 0x00, 0x13, 0x00, 0x0f, 0x00, 0x0e, 0x00, 0x13, 0x00, 0x01, 0x00,
    0x0e, 0x00, 0x16, 0x00, 0x0e, 0x00, 0x0e, 0x00, 0x17, 0x00, 0x16, 0x00, 0x17, 0x00, 0x01, 0x00, 0x16, 0x00, 0x1a,
    0x00, 0x16, 0x00, 0x01, 0x00, 0x1a, 0x00, 0x08, 0x00, 0x0d, 0x00, 0x08, 0x00, 0x1d, 0x00, 0x08, 0x00, 0x1e, 0x00,
    0x08, 0x00, 0x1f, 0x00, 0x1e, 0x00, 0x1f, 0x00, 0x1d, 0x00, 0x1e, 0x00, 0x1e, 0x00, 0x22, 0x00, 0x22, 0x00, 0x1d,
    0x00, 0x0d, 0x00, 0x1d, 0x00, 0x1d, 0x00, 0x25, 0x00, 0x1d, 0x00, 0x26, 0x00, 0x25, 0x00, 0x26, 0x00, 0x0d, 0x00,
    0x25, 0x00, 0x25, 0x00, 0x29, 0x00, 0x29, 0x00, 0x0d, 0x00, 0x03, 0x00, 0x04, 0x00, 0x03, 0x00, 0x2c, 0x00, 0x03,
    0x00, 0x2d, 0x00, 0x03, 0x00, 0x2e, 0x00, 0x09, 0x00, 0x02, 0x00, 0x02, 0x00, 0x30, 0x00, 0x02, 0x00, 0x31, 0x00,
    0x02, 0x00, 0x32, 0x00, 0x02, 0x00, 0x03, 0x00, 0x03, 0x00, 0x34, 0x00, 0x03, 0x00, 0x35, 0x00, 0x03, 0x00, 0x36,
    0x00, 0x36, 0x00, 0x35, 0x00, 0x34, 0x00, 0x35, 0x00, 0x39, 0x00, 0x35, 0x00, 0x34, 0x00, 0x39, 0x00, 0x02, 0x00,
    0x34, 0x00, 0x34, 0x00, 0x3c, 0x00, 0x3d, 0x00, 0x34, 0x00, 0x3c, 0x00, 0x3d, 0x00, 0x02, 0x00, 0x3c, 0x00, 0x3c,
    0x00, 0x40, 0x00, 0x02, 0x00, 0x40, 0x00, 0x2f, 0x00, 0x33, 0x00, 0x2f, 0x00, 0x43, 0x00, 0x2f, 0x00, 0x44, 0x00,
    0x43, 0x00, 0x44, 0x00, 0x44, 0x00, 0x45, 0x00, 0x44, 0x00, 0x46, 0x00, 0x43, 0x00, 0x46, 0x00, 0x33, 0x00, 0x43,
    0x00, 0x4a, 0x00, 0x43, 0x00, 0x43, 0x00, 0x4b, 0x00, 0x4a, 0x00, 0x4b, 0x00, 0x33, 0x00, 0x4a, 0x00, 0x4e, 0x00,
    0x4a, 0x00, 0x33, 0x00, 0x4e, 0x00, 0x2f, 0x00, 0x45, 0x00, 0x3d, 0x00, 0xd4, 0x17, 0x00, 0x7f, 0x5b, 0xbb, 0x01,
    0x00, 0x00, 0x00, 0xe8, 0x4f, 0xac, 0xfd, 0xff, 0x66, 0xa3, 0x6c, 0xf0, 0x03, 0x00, 0xbb, 0xff, 0xff, 0xff, 0xff,
    0xe8, 0x3f, 0xac, 0xfd, 0xff, 0x66, 0xa3, 0x68, 0xf0, 0x03, 0x00, 0xbf, 0xf2, 0xfd, 0x03, 0x00, 0xe8, 0xcd, 0xf5,
    0xff, 0xff, 0x72, 0x2f, 0xbe, 0x90, 0xfd, 0x03, 0x00, 0x66, 0x8b, 0x0e, 0x83, 0xc6, 0x02, 0x8b, 0x15, 0x6c, 0xf0,
    0x03, 0x00, 0x66, 0x83, 0x7e, 0x04, 0x00, 0x74, 0x06, 0x8b, 0x15, 0x68, 0xf0, 0x03, 0x00, 0xe8, 0xc6, 0xa1, 0xfd,
    0xff, 0x83, 0xc6, 0x06, 0x66, 0x49, 0x75, 0xe1, 0xe8, 0x52, 0xf7, 0xff, 0xff, 0xc3, 0x90, 0x10, 0x00, 0x18, 0x00,
    0x17, 0x00, 0x00, 0x00, 0x17, 0x00, 0x2a, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x2b, 0x00, 0x01, 0x00, 0x2b, 0x00, 0x18,
    0x00, 0x01, 0x00, 0x15, 0x00, 0x14, 0x00, 0x00, 0x00, 0x14, 0x00, 0x27, 0x00, 0x00, 0x00, 0x27, 0x00, 0x28, 0x00,
    0x01, 0x00, 0x28, 0x00, 0x15, 0x00, 0x01, 0x00, 0x11, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00, 0x23, 0x00, 0x00,
    0x00, 0x23, 0x00, 0x24, 0x00, 0x01, 0x00, 0x24, 0x00, 0x11, 0x00, 0x01, 0x00, 0x0e, 0x00, 0x0d, 0x00, 0x00, 0x00,
    0x0d, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x00, 0x21, 0x00, 0x01, 0x00, 0x21, 0x00, 0x0e, 0x00, 0x01, 0x00, 0x04,
    0x00, 0x2c, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x05, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x07,
    0x00, 0x01, 0x00, 0x08, 0x00, 0x09, 0x00, 0x06, 0x00, 0x09, 0x00, 0x0a, 0x00, 0x09, 0x00, 0x0b, 0x00, 0x09, 0x00,
    0x0c, 0x00, 0x0c, 0x00, 0x0b, 0x00, 0x0a, 0x00, 0x0b, 0x00, 0x0b, 0x00, 0x0f, 0x00, 0x0a, 0x00, 0x0f, 0x00, 0x06,
    0x00, 0x0a, 0x00, 0x0a, 0x00, 0x12, 0x00, 0x0a, 0x00, 0x13, 0x00, 0x12, 0x00, 0x13, 0x00, 0x06, 0x00, 0x12, 0x00,
    0x16, 0x00, 0x12, 0x00, 0x06, 0x00, 0x16, 0x00, 0x03, 0x00, 0x04, 0x00, 0x03, 0x00, 0x19, 0x00, 0x02, 0x00, 0x07,
    0x00, 0x02, 0x00, 0x1b, 0x00, 0x1c, 0x00, 0x1a, 0x00, 0x1c, 0x00, 0x1d, 0x00, 0x1c, 0x00, 0x1e, 0x00, 0x1c, 0x00,
    0x1f, 0x00, 0x1e, 0x00, 0x1f, 0x00, 0x1e, 0x00, 0x1d, 0x00, 0x1e, 0x00, 0x22, 0x00, 0x22, 0x00, 0x1d, 0x00, 0x1a,
    0x00, 0x1d, 0x00, 0x1d, 0x00, 0x25, 0x00, 0x1d, 0x00, 0x26, 0x00, 0x25, 0x00, 0x26, 0x00, 0x25, 0x00, 0x1a, 0x00,
    0x25, 0x00, 0x29, 0x00, 0x1a, 0x00, 0x29, 0x00, 0x3d, 0x00, 0xd4, 0x17, 0x00, 0x7f, 0x5b, 0xbb, 0x01, 0x00, 0x00,
    0x00, 0xe8, 0xd5, 0xaa, 0xfd, 0xff, 0x66, 0xa3, 0x6c, 0xf0, 0x03, 0x00, 0xbb, 0xff, 0xff, 0xff, 0xff, 0xe8, 0xc5,
    0xaa, 0xfd, 0xff, 0x66, 0xa3, 0x68, 0xf0, 0x03, 0x00, 0xbf, 0xb0, 0xff, 0x03, 0x00, 0xe8, 0x53, 0xf4, 0xff, 0xff,
    0x72, 0x2f, 0xbe, 0x0c, 0xff, 0x03, 0x00, 0x66, 0x8b, 0x0e, 0x83, 0xc6, 0x02, 0x8b, 0x15, 0x6c, 0xf0, 0x03, 0x00,
    0x66, 0x83, 0x7e, 0x04, 0x00, 0x74, 0x06, 0x8b, 0x15, 0x68, 0xf0, 0x03, 0x00, 0xe8, 0x4c, 0xa0, 0xfd, 0xff, 0x83,
    0xc6, 0x06, 0x66, 0x49, 0x75, 0xe1, 0xe8, 0xd8, 0xf5, 0xff, 0xff, 0xc3, 0x87, 0xdb, 0x90, 0x05, 0x00, 0x0e, 0x00,
    0x0b, 0x00, 0x01, 0x00, 0x0b, 0x00, 0x05, 0x00, 0x01, 0x00, 0x0e, 0x00, 0x06, 0x00, 0x00, 0x00, 0x09, 0x00, 0x0d,
    0x00, 0x00, 0x00, 0x0d, 0x00, 0x08, 0x00, 0x00, 0x00, 0x3d, 0x00, 0xd4, 0x17, 0x00, 0x7f, 0x5b, 0xbb, 0x01, 0x00,
    0x00, 0x00, 0xe8, 0x4f, 0xaa, 0xfd, 0xff, 0x66, 0xa3, 0x6c, 0xf0, 0x03, 0x00, 0xbb, 0xff, 0xff, 0xff, 0xff, 0xe8,
    0x3f, 0xaa, 0xfd, 0xff, 0x66, 0xa3, 0x68, 0xf0, 0x03, 0x00, 0xbf, 0xb0, 0xff, 0x03, 0x00, 0xe8, 0xcd, 0xf3, 0xff,
    0xff, 0x72, 0x2f, 0xbe, 0x90, 0xff, 0x03, 0x00, 0x66, 0x8b, 0x0e, 0x83, 0xc6, 0x02, 0x8b, 0x15, 0x6c, 0xf0, 0x03,
    0x00, 0x66, 0x83, 0x7e, 0x04, 0x00, 0x74, 0x06, 0x8b, 0x15, 0x68, 0xf0, 0x03, 0x00, 0xe8, 0xc6, 0x9f, 0xfd, 0xff,
    0x83, 0xc6, 0x06, 0x66, 0x49, 0x75, 0xe1, 0xe8, 0x52, 0xf5, 0xff, 0xff, 0xc3, 0x90, 0x05, 0x00, 0x0e, 0x00, 0x0b,
    0x00, 0x01, 0x00, 0x0b, 0x00, 0x05, 0x00, 0x01, 0x00, 0x0e, 0x00, 0x06, 0x00, 0x00, 0x00, 0x09, 0x00, 0x0d, 0x00,
    0x00, 0x00, 0x0d, 0x00, 0x08, 0x00, 0x00, 0x00, 0x03, 0x00, 0x0f, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x02, 0x00, 0x03, 0x00, 0x03, 0x00, 0x07, 0x00, 0x02, 0x00, 0x07, 0x00, 0x03, 0x00, 0x01, 0x00, 0x0a,
    0x00, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x07, 0x00, 0x0c, 0x00, 0x0a, 0x00, 0x0c, 0x00, 0x3d, 0x00, 0xd4, 0x17,
    0x00, 0x7f, 0x5b, 0xbb, 0x01, 0x00, 0x00, 0x00, 0xe8, 0x8b, 0xa9, 0xfd, 0xff, 0x66, 0xa3, 0x6c, 0xf0, 0x03, 0x00,
    0xbb, 0xff, 0xff, 0xff, 0xff, 0xe8, 0x7b, 0xa9, 0xfd, 0xff, 0x66, 0xa3, 0x68, 0xf0, 0x03, 0x00, 0xbf, 0xf8, 0x00,
    0x04, 0x00, 0xe8, 0x09, 0xf3, 0xff, 0xff, 0x72, 0x2f, 0xbe, 0x54, 0x00, 0x04, 0x00, 0x66, 0x8b, 0x0e, 0x83, 0xc6,
    0x02, 0x8b, 0x15, 0x6c, 0xf0, 0x03, 0x00, 0x66, 0x83, 0x7e, 0x04, 0x00, 0x74, 0x06, 0x8b, 0x15, 0x68, 0xf0, 0x03,
    0x00, 0xe8, 0x02, 0x9f, 0xfd, 0xff, 0x83, 0xc6, 0x06, 0x66, 0x49, 0x75, 0xe1, 0xe8, 0x8e, 0xf4, 0xff, 0xff, 0xc3,
    0x90, 0x05, 0x00, 0x0e, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x05, 0x00, 0x00, 0x00, 0x06, 0x00, 0x0e, 0x00,
    0x01, 0x00, 0x09, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x08, 0x00, 0x00, 0x00, 0x3d, 0x00, 0xd4, 0x17, 0x00,
    0x7f, 0x5b, 0xbb, 0x01, 0x00, 0x00, 0x00, 0xe8, 0x07, 0xa9, 0xfd, 0xff, 0x66, 0xa3, 0x6c, 0xf0, 0x03, 0x00, 0xbb,
    0xff, 0xff, 0xff, 0xff, 0xe8, 0xf7, 0xa8, 0xfd, 0xff, 0x66, 0xa3, 0x68, 0xf0, 0x03, 0x00, 0xbf, 0xf8, 0x00, 0x04,
    0x00, 0xe8, 0x85, 0xf2, 0xff, 0xff, 0x72, 0x2f, 0xbe, 0xd8, 0x00, 0x04, 0x00, 0x66, 0x8b, 0x0e, 0x83, 0xc6, 0x02,
    0x8b, 0x15, 0x6c, 0xf0, 0x03, 0x00, 0x66, 0x83, 0x7e, 0x04, 0x00, 0x74, 0x06, 0x8b, 0x15, 0x68, 0xf0, 0x03, 0x00,
    0xe8, 0x7e, 0x9e, 0xfd, 0xff, 0x83, 0xc6, 0x06, 0x66, 0x49, 0x75, 0xe1, 0xe8, 0x0a, 0xf4, 0xff, 0xff, 0xc3, 0x90,
    0x05, 0x00, 0x0e, 0x00, 0x0b, 0x00, 0x01, 0x00, 0x0b, 0x00, 0x05, 0x00, 0x01, 0x00, 0x06, 0x00, 0x0e, 0x00, 0x00,
    0x00, 0x09, 0x00, 0x0d, 0x00, 0x01, 0x00, 0x0d, 0x00, 0x08, 0x00, 0x01, 0x00, 0x03, 0x00, 0x0f, 0x00, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x03, 0x00, 0x04, 0x00, 0x01, 0x00, 0x03, 0x00, 0x03, 0x00, 0x07, 0x00, 0x01, 0x00, 0x07, 0x00,
    0x03, 0x00, 0x02, 0x00, 0x02, 0x00, 0x0a, 0x00, 0x01, 0x00, 0x02, 0x00, 0x07, 0x00, 0x0c, 0x00, 0x0a, 0x00, 0x0c,
    0x00, 0x3d, 0x00, 0xda, 0x29, 0x01, 0x7f, 0x3a, 0xbf};

static const uint8_t *TrackView_AnimatedOrangeDashesData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_ANIMATED_ORANGE_DASHES_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_ANIMATED_ORANGE_DASHES_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewMaterialFamilyData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewMaterialFamilyData) - (size_t)offset;
	}
	return kTrackViewMaterialFamilyData + offset;
}

static const uint8_t *TrackView_AnimatedFloorLightDashesData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_ANIMATED_FLOOR_LIGHT_DASHES_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_ANIMATED_FLOOR_LIGHT_DASHES_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewAnimatedFloorLightDashesData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewAnimatedFloorLightDashesData) - (size_t)offset;
	}
	return kTrackViewAnimatedFloorLightDashesData + offset;
}

static const uint8_t *TrackView_ThreeLineDiagonalData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_THREE_LINE_DIAGONAL_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_THREE_LINE_DIAGONAL_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewThreeLineDiagonalData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewThreeLineDiagonalData) - (size_t)offset;
	}
	return kTrackViewThreeLineDiagonalData + offset;
}

static const uint8_t *TrackView_SevenLineFrameAlternateData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_SEVEN_LINE_FRAME_ALTERNATE_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_SEVEN_LINE_FRAME_ALTERNATE_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewSevenLineFrameAlternateData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewSevenLineFrameAlternateData) - (size_t)offset;
	}
	return kTrackViewSevenLineFrameAlternateData + offset;
}

static const uint8_t *TrackView_FiveLineFrameData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_FIVE_LINE_FRAME_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_FIVE_LINE_FRAME_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewFiveLineFrameData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewFiveLineFrameData) - (size_t)offset;
	}
	return kTrackViewFiveLineFrameData + offset;
}

static const uint8_t *TrackView_CageFiveLinesData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_CAGE_FIVE_LINES_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_CAGE_FIVE_LINES_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewCageFiveLinesData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewCageFiveLinesData) - (size_t)offset;
	}
	return kTrackViewCageFiveLinesData + offset;
}

static const uint8_t *TrackView_CageSixteenLinesData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_CAGE_SIXTEEN_LINES_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_CAGE_SIXTEEN_LINES_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewCageSixteenLinesData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewCageSixteenLinesData) - (size_t)offset;
	}
	return kTrackViewCageSixteenLinesData + offset;
}

static const uint8_t *TrackView_ShadedTenLinesData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_SHADED_TEN_LINES_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_SHADED_TEN_LINES_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewShadedTenLinesData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewShadedTenLinesData) - (size_t)offset;
	}
	return kTrackViewShadedTenLinesData + offset;
}

static const uint8_t *TrackView_DepthDetailedDarkLinesData(uint32_t dosAddress, size_t *bytesRemaining) {
	size_t offset;

	if (dosAddress < TRACK_VIEW_DEPTH_DETAILED_DARK_LINES_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_DEPTH_DETAILED_DARK_LINES_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewDepthDetailedDarkLinesData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewDepthDetailedDarkLinesData) - offset;
	}
	return kTrackViewDepthDetailedDarkLinesData + offset;
}

static const uint8_t *TrackView_AnimatedEightPolygonsData(uint32_t dosAddress, size_t *bytesRemaining) {
	size_t offset;

	if (dosAddress < TRACK_VIEW_ANIMATED_EIGHT_POLYGONS_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_ANIMATED_EIGHT_POLYGONS_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewAnimatedEightPolygonsData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewAnimatedEightPolygonsData) - offset;
	}
	return kTrackViewAnimatedEightPolygonsData + offset;
}

static const uint8_t *TrackView_QuadOutlineData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_QUAD_OUTLINE_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_QUAD_OUTLINE_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewQuadOutlineData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewQuadOutlineData) - (size_t)offset;
	}
	return kTrackViewQuadOutlineData + offset;
}

static const uint8_t *TrackView_TwoConnectedLinesData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_TWO_CONNECTED_LINES_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_TWO_CONNECTED_LINES_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewTwoConnectedLinesData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewTwoConnectedLinesData) - (size_t)offset;
	}
	return kTrackViewTwoConnectedLinesData + offset;
}

static const uint8_t *TrackView_TwoCornerLinesData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_TWO_CORNER_LINES_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_TWO_CORNER_LINES_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewTwoCornerLinesData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewTwoCornerLinesData) - (size_t)offset;
	}
	return kTrackViewTwoCornerLinesData + offset;
}

static const uint8_t *TrackView_CageSevenLinesData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_CAGE_SEVEN_LINES_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_CAGE_SEVEN_LINES_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewCageSevenLinesData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewCageSevenLinesData) - (size_t)offset;
	}
	return kTrackViewCageSevenLinesData + offset;
}

static const uint8_t *TrackView_SevenLineFrameData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_SEVEN_LINE_FRAME_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_SEVEN_LINE_FRAME_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewSevenLineFrameData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewSevenLineFrameData) - (size_t)offset;
	}
	return kTrackViewSevenLineFrameData + offset;
}

static const uint8_t *TrackView_ThreeLineStripData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_THREE_LINE_STRIP_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_THREE_LINE_STRIP_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewThreeLineStripData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewThreeLineStripData) - (size_t)offset;
	}
	return kTrackViewThreeLineStripData + offset;
}

static const uint8_t *TrackView_ShadedQuadsData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_SHADED_QUADS_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_SHADED_QUADS_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewShadedQuadsData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewShadedQuadsData) - (size_t)offset;
	}
	return kTrackViewShadedQuadsData + offset;
}

static const uint8_t *TrackView_SecondaryColorPolygonsWithLinesData(uint32_t dosAddress, size_t *bytesRemaining) {
	uint32_t offset;

	if (dosAddress < TRACK_VIEW_SECONDARY_COLOR_POLYGONS_WITH_LINES_DATA_TOKEN) {
		return NULL;
	}
	offset = dosAddress - TRACK_VIEW_SECONDARY_COLOR_POLYGONS_WITH_LINES_DATA_TOKEN;
	if (offset >= sizeof(kTrackViewSecondaryColorPolygonsWithLinesData)) {
		return NULL;
	}
	if (bytesRemaining != NULL) {
		*bytesRemaining = sizeof(kTrackViewSecondaryColorPolygonsWithLinesData) - (size_t)offset;
	}
	return kTrackViewSecondaryColorPolygonsWithLinesData + offset;
}

static bool TrackView_MaterialColorTriplet(const TrackViewRawBspContext *context, uint32_t materialIdentifier,
                                           uint32_t depth, uint16_t *colorOut, uint16_t *darkerColorOut,
                                           uint16_t *lighterColorOut) {
	const uint8_t *materialRecord;
	size_t materialRecordOffset;
	uint16_t materialIndex;
	uint32_t shadeQ14;
	uint32_t color;
	SlipDraw3DLightDepthBlend depthBlend;
	const uint8_t *materialTable;
	size_t materialTableBytes;

	if (context == NULL || colorOut == NULL || darkerColorOut == NULL || lighterColorOut == NULL) {
		return false;
	}
	materialTable = context->exactMaterialTable != NULL ? context->exactMaterialTable : context->materialTable;
	materialTableBytes =
	    context->exactMaterialTable != NULL ? context->exactMaterialTableBytes : context->materialTableBytes;
	if (materialTable == NULL || materialTableBytes < SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES) {
		return false;
	}
	materialIndex = (uint16_t)(materialIdentifier & SLIP_DRAW3D_MATERIAL_INDEX_MASK);
	if (materialIndex < SlipBytes_ReadLE16(materialTable)) {
		materialRecordOffset =
		    SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES + (size_t)materialIndex * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE;
	} else {
		materialRecordOffset = SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES;
	}
	if (materialRecordOffset > materialTableBytes ||
	    materialTableBytes - materialRecordOffset < SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE) {
		return false;
	}
	materialRecord = materialTable + materialRecordOffset;
	if (!SlipDraw3D_LightDepthBlend(depth, SlipDraw3D_fadeStart, SlipDraw3D_fadeEnd, SlipDraw3D_fadeRange,
	                                &depthBlend)) {
		return false;
	}
	shadeQ14 = context->directLight;
	if (depthBlend.fadeBlendQ14 != 0u && SlipDraw3D_fadeStart != 0u) {
		const uint32_t fadeBlendQ14 = depthBlend.fadeBlendQ14;

		if (fadeBlendQ14 == SLIP_Q14_ONE) {
			shadeQ14 = SlipDraw3D_fadeColour;
		} else {
			const int32_t delta = (int32_t)SlipDraw3D_fadeColour - (int32_t)shadeQ14;
			const int32_t scaled = (delta * (int32_t)(int16_t)(uint16_t)fadeBlendQ14) >> SLIP_Q14_FRACTION_BITS;
			const int32_t blended = (int32_t)shadeQ14 + scaled;

			if (blended < 0) {
				shadeQ14 = 0;
			} else if (blended > SLIP_Q14_ONE) {
				shadeQ14 = SLIP_Q14_ONE;
			} else {
				shadeQ14 = (uint32_t)blended;
			}
		}
	}
	{
		uint32_t rampStart;
		uint32_t rampEnd;
		uint32_t rampDelta;
		uint32_t product;
		uint16_t productLowWord;
		uint16_t productHighWord;

		if (context->limitEnabled == 0u) {
			rampStart = SlipBytes_ReadLE32(materialRecord + offsetof(SlipDraw3DMaterialRecord, rampStart));
			rampEnd = SlipBytes_ReadLE32(materialRecord + offsetof(SlipDraw3DMaterialRecord, rampEnd));
		} else {
			rampStart = context->limitStart;
			rampEnd = context->limitEnd;
		}
		rampDelta = rampEnd - rampStart;
		product = (uint32_t)(uint16_t)rampDelta * (uint32_t)(uint16_t)shadeQ14;
		productLowWord = (uint16_t)product;
		productHighWord = (uint16_t)(product >> SLIP_WORD_BITS);

		color = (rampDelta & SLIP_MATERIAL_RAMP_UPPER_WORD_MASK) |
		        (uint16_t)((productLowWord >> SLIP_Q14_FRACTION_BITS) | (productHighWord << SLIP_Q14_WORD_HIGH_SHIFT));
		color += rampStart;
	}
	*colorOut = (uint16_t)color;
	*darkerColorOut = (uint16_t)(color - 1u);
	*lighterColorOut = (uint16_t)(color + 1u);
	if (color - 1u < SlipBytes_ReadLE32(materialRecord + offsetof(SlipDraw3DMaterialRecord, rampStart))) {
		*darkerColorOut =
		    (uint16_t)SlipBytes_ReadLE32(materialRecord + offsetof(SlipDraw3DMaterialRecord, importedMaterialByte));
	}
	if (color + 1u > SlipBytes_ReadLE32(materialRecord + offsetof(SlipDraw3DMaterialRecord, rampEnd))) {
		*lighterColorOut = (uint16_t)SlipBytes_ReadLE32(materialRecord + offsetof(SlipDraw3DMaterialRecord, rampEnd));
	}
	return true;
}

static bool TrackView_MaterialSetupVertices(TrackViewRawBspContext *context,
                                            TrackViewPrimitiveCallbackContext *primitiveContext,
                                            const uint8_t *setupList, size_t setupListBytes,
                                            const uint8_t *seedIndexStream, size_t seedIndexStreamBytes) {
	SlipDraw3DVec32 seedWorld[SLIP_MATERIAL_SETUP_SEED_CAPACITY];
	SlipDraw3DBuildVertexRecords build;
	uint16_t seedCount;
	uint16_t derivedCount;
	uint32_t currentRecordIndex;
	uint32_t nextRecordIndex;
	uint32_t cursorBeforeLoad;
	uint32_t cursorAfterLoad;
	size_t seedIndex;

	if (context == NULL || primitiveContext == NULL || setupList == NULL || seedIndexStream == NULL ||
	    setupListBytes < SLIP_MATERIAL_VERTEX_SETUP_HEADER_BYTES) {
		return false;
	}
	seedCount = SlipBytes_ReadLE16(setupList);
	derivedCount = SlipBytes_ReadLE16(setupList + SLIP_MATERIAL_VERTEX_SETUP_DERIVED_COUNT_OFFSET);
	if (seedCount == 0 || seedCount > sizeof(seedWorld) / sizeof(seedWorld[0]) ||
	    seedIndexStreamBytes < (size_t)seedCount * SLIP_MATERIAL_VERTEX_INDEX_BYTES ||

	    setupListBytes - SLIP_MATERIAL_VERTEX_SETUP_HEADER_BYTES <
	        (size_t)derivedCount * SLIP_MATERIAL_MIDPOINT_RECORD_BYTES + SLIP_MATERIAL_VERTEX_SETUP_TRAILING_BYTES) {
		if (g_vehicleViewDumpDiagnostics) {
			fprintf(stderr,
			        "track_view_material_setup_fail_0003f32a stage=header seeds=%u derived=%u setup_bytes=%zu "
			        "seed_bytes=%zu\n",
			        (unsigned)seedCount, (unsigned)derivedCount, setupListBytes, seedIndexStreamBytes);
		}
		return false;
	}
	for (seedIndex = 0; seedIndex < (size_t)seedCount; ++seedIndex) {
		SlipDraw3DProjectIndex project;
		const uint16_t seedVertexIndex =
		    SlipBytes_ReadLE16(seedIndexStream + seedIndex * SLIP_MATERIAL_VERTEX_INDEX_BYTES);

		if (!SlipDraw3D_ProjectIndex(context->vertexRecords, context->vertexRecordCount, seedVertexIndex,
		                             TrackView_TransformVertex, primitiveContext, &project)) {
			if (g_vehicleViewDumpDiagnostics) {
				fprintf(stderr,
				        "track_view_material_setup_fail_0003f32a stage=seed_project seed=%zu index=%u records=%zu\n",
				        seedIndex, (unsigned)seedVertexIndex, context->vertexRecordCount);
			}
			return false;
		}
		seedWorld[seedIndex] = project.world;
	}
	currentRecordIndex = SlipDraw3D_CurrentStateRecordIndex(context->recordIndex);
	nextRecordIndex = currentRecordIndex;
	++nextRecordIndex;
	cursorBeforeLoad = context->vertexBufferCursor;
	if (!TrackView_ApplyLoadedDrawState(context, nextRecordIndex)) {
		if (g_vehicleViewDumpDiagnostics) {
			fprintf(stderr,
			        "track_view_material_setup_fail_0003f32a stage=load_state bp=%u record_index=%u cursor=0x%08x\n",
			        (unsigned)nextRecordIndex, (unsigned)context->recordIndex, context->vertexBufferCursor);
		}
		return false;
	}
	cursorAfterLoad = context->vertexBufferCursor;
	if (!TrackView_BuildVertexRecords(context, TRACK_VIEW_DIAGNOSTIC_BUILD_MATERIAL_MIDPOINTS,
	                                  setupList + SLIP_MATERIAL_VERTEX_SETUP_HEADER_BYTES,
	                                  setupListBytes - SLIP_MATERIAL_VERTEX_SETUP_HEADER_BYTES, derivedCount,
	                                  SLIP_MATERIAL_MIDPOINT_RECORD_BYTES, TrackView_MaterialMidpoint,
	                                  TrackView_MaterialSourcePoint, &build)) {
		if (g_vehicleViewDumpDiagnostics) {
			fprintf(
			    stderr,
			    "track_view_material_setup_fail_0003f32a stage=build seeds=%u derived=%u bp=%u cursor_before=0x%08x "
			    "cursor_after_load=0x%08x cursor=0x%08x records=%zu words=%04x,%04x,%04x,%04x\n",
			    (unsigned)seedCount, (unsigned)derivedCount, (unsigned)nextRecordIndex, cursorBeforeLoad,
			    cursorAfterLoad, context->vertexBufferCursor, context->vertexRecordCount,
			    setupListBytes >= SLIP_MATERIAL_VERTEX_SETUP_HEADER_BYTES + 1 * sizeof(uint16_t)
			        ? SlipBytes_ReadLE16(setupList + SLIP_MATERIAL_VERTEX_SETUP_HEADER_BYTES)
			        : 0u,
			    setupListBytes >= SLIP_MATERIAL_VERTEX_SETUP_HEADER_BYTES + 2 * sizeof(uint16_t)
			        ? SlipBytes_ReadLE16(setupList + SLIP_MATERIAL_VERTEX_SETUP_HEADER_BYTES + 1 * sizeof(uint16_t))
			        : 0u,
			    setupListBytes >= SLIP_MATERIAL_VERTEX_SETUP_HEADER_BYTES + 3 * sizeof(uint16_t)
			        ? SlipBytes_ReadLE16(setupList + SLIP_MATERIAL_VERTEX_SETUP_HEADER_BYTES + 2 * sizeof(uint16_t))
			        : 0u,
			    setupListBytes >= SLIP_MATERIAL_VERTEX_SETUP_HEADER_BYTES + 4 * sizeof(uint16_t)
			        ? SlipBytes_ReadLE16(setupList + SLIP_MATERIAL_VERTEX_SETUP_HEADER_BYTES + 3 * sizeof(uint16_t))
			        : 0u);
		}
		return false;
	}

	context->materialSetupSeedY = (uint32_t)seedWorld[0].y;
	for (seedIndex = 0; seedIndex < (size_t)seedCount; ++seedIndex) {
		TrackView_StoreMaterialSeedVertex(&context->vertexRecords[seedIndex], seedWorld[seedIndex]);
	}
	if (g_vehicleViewDumpDiagnostics) {
		uint16_t dumpCount = (uint16_t)(seedCount + derivedCount);

		if (dumpCount > SLIP_MATERIAL_SETUP_DIAGNOSTIC_RECORD_LIMIT) {
			dumpCount = SLIP_MATERIAL_SETUP_DIAGNOSTIC_RECORD_LIMIT;
		}
		fprintf(stderr,
		        "track_view_material_setup_done_0003f32a seeds=%u derived=%u dump=%u bp=%u cursor_before=0x%08x "
		        "cursor_after_load=0x%08x cursor_after_build=0x%08x seed_words=%04x,%04x,%04x\n",
		        (unsigned)seedCount, (unsigned)derivedCount, (unsigned)dumpCount, (unsigned)nextRecordIndex,
		        cursorBeforeLoad, cursorAfterLoad, context->vertexBufferCursor,
		        seedIndexStreamBytes >= SLIP_SERIALIZED_INDEX_BYTES ? SlipBytes_ReadLE16(seedIndexStream) : 0u,
		        seedIndexStreamBytes >= 2 * SLIP_SERIALIZED_INDEX_BYTES
		            ? SlipBytes_ReadLE16(seedIndexStream + SLIP_SERIALIZED_INDEX_BYTES)
		            : 0u,
		        seedIndexStreamBytes >= 3 * SLIP_SERIALIZED_INDEX_BYTES
		            ? SlipBytes_ReadLE16(seedIndexStream + 2 * SLIP_SERIALIZED_INDEX_BYTES)
		            : 0u);
		for (uint16_t vertexIndex = 0; vertexIndex < dumpCount; ++vertexIndex) {
			const SlipDraw3DVertexRecord *const record = &context->vertexRecords[vertexIndex];

			fprintf(stderr,
			        "  material_setup_vertex_0003f32a index=%u flags=0x%08x world=%d,%d,%d screen=%d,%d depth=%d "
			        "source=%04x,%04x,%04x\n",
			        (unsigned)vertexIndex, SlipBytes_ReadLE32(record->bytes + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET),
			        (int32_t)SlipBytes_ReadLE32(record->bytes + SLIP_DRAW3D_VERTEX_RECORD_WORLD_OFFSET),
			        (int32_t)SlipBytes_ReadLE32(record->bytes + offsetof(SlipDraw3DVertexRecord, world.y)),
			        (int32_t)SlipBytes_ReadLE32(record->bytes + offsetof(SlipDraw3DVertexRecord, world.z)),
			        (int32_t)SlipBytes_ReadLE32(record->bytes + SLIP_DRAW3D_VERTEX_RECORD_SCREEN_OFFSET),
			        (int32_t)SlipBytes_ReadLE32(record->bytes + offsetof(SlipDraw3DVertexRecord, screenY)),
			        (int32_t)SlipBytes_ReadLE32(record->bytes + SLIP_DRAW3D_VERTEX_RECORD_DEPTH_OFFSET),
			        SlipBytes_ReadLE16(record->bytes + SLIP_DRAW3D_VERTEX_RECORD_SOURCE_OFFSET),
			        SlipBytes_ReadLE16(record->bytes + offsetof(SlipDraw3DVertexRecord, sourceY)),
			        SlipBytes_ReadLE16(record->bytes + offsetof(SlipDraw3DVertexRecord, sourceZ)));
		}
	}
	return true;
}

static bool TrackView_MaterialSetupScreenVertices(TrackViewRawBspContext *context,
                                                  TrackViewPrimitiveCallbackContext *primitiveContext,
                                                  const uint8_t *setupList, size_t setupListBytes,
                                                  const uint8_t *seedIndexStream, size_t seedIndexStreamBytes,
                                                  uint32_t depth) {
	SlipDraw3DBuildVertexRecords build;
	int32_t screenPairs[SLIP_MATERIAL_SETUP_SEED_CAPACITY][2];
	uint16_t seedCount;
	uint16_t derivedCount;
	uint32_t currentRecordIndex;
	uint32_t nextRecordIndex;
	size_t seedIndex;

	if (context == NULL || primitiveContext == NULL || setupList == NULL || seedIndexStream == NULL ||
	    context->projectState == NULL || setupListBytes < SLIP_MATERIAL_VERTEX_SETUP_HEADER_BYTES) {
		return false;
	}

	if ((int32_t)depth < SLIP_MATERIAL_PERSPECTIVE_DEPTH_THRESHOLD) {
		return TrackView_MaterialSetupVertices(context, primitiveContext, setupList, setupListBytes, seedIndexStream,
		                                       seedIndexStreamBytes);
	}
	seedCount = SlipBytes_ReadLE16(setupList);
	derivedCount = SlipBytes_ReadLE16(setupList + SLIP_MATERIAL_VERTEX_SETUP_DERIVED_COUNT_OFFSET);
	if (seedCount == 0 || seedCount > sizeof(screenPairs) / sizeof(screenPairs[0]) ||
	    seedIndexStreamBytes < (size_t)seedCount * SLIP_MATERIAL_VERTEX_INDEX_BYTES ||
	    setupListBytes - SLIP_MATERIAL_VERTEX_SETUP_HEADER_BYTES <

	        (size_t)derivedCount * SLIP_MATERIAL_MIDPOINT_RECORD_BYTES + SLIP_MATERIAL_VERTEX_SETUP_TRAILING_BYTES) {
		if (g_vehicleViewDumpDiagnostics) {
			fprintf(stderr,
			        "track_view_material_setup_fail_0003f3c4 stage=header seeds=%u derived=%u setup_bytes=%zu "
			        "seed_bytes=%zu\n",
			        (unsigned)seedCount, (unsigned)derivedCount, setupListBytes, seedIndexStreamBytes);
		}
		return false;
	}

	for (seedIndex = 0; seedIndex < (size_t)seedCount; ++seedIndex) {
		const uint16_t seedVertexIndex =
		    SlipBytes_ReadLE16(seedIndexStream + seedIndex * SLIP_MATERIAL_VERTEX_INDEX_BYTES);
		SlipDraw3DVertexRecord *record;
		uint32_t flags;

		if ((size_t)seedVertexIndex >= context->vertexRecordCount) {
			return false;
		}
		record = &context->vertexRecords[seedVertexIndex];
		flags = SlipDraw3D_ProjectVertex(record, context->projectState, TrackView_TransformVertex,
		                                 TrackView_ProjectScreenPrimary, TrackView_ProjectScreenSecondary,
		                                 primitiveContext);

		if ((flags & SLIP_CLIP_BEFORE_PROJECTION) != 0) {
			return TrackView_MaterialSetupVertices(context, primitiveContext, setupList, setupListBytes,
			                                       seedIndexStream, seedIndexStreamBytes);
		}
		screenPairs[seedIndex][0] =
		    (int32_t)SlipBytes_ReadLE32(record->bytes + SLIP_DRAW3D_VERTEX_RECORD_SCREEN_OFFSET);
		screenPairs[seedIndex][1] =
		    (int32_t)SlipBytes_ReadLE32(record->bytes + offsetof(SlipDraw3DVertexRecord, screenY));
	}

	currentRecordIndex = SlipDraw3D_CurrentStateRecordIndex(context->recordIndex);
	nextRecordIndex = currentRecordIndex + 1u;
	if (!TrackView_ApplyLoadedDrawState(context, nextRecordIndex)) {
		if (g_vehicleViewDumpDiagnostics) {
			fprintf(stderr, "track_view_material_setup_fail_0003f3c4 stage=load_state bp=%u record_index=%u\n",
			        (unsigned)nextRecordIndex, (unsigned)context->recordIndex);
		}
		return false;
	}

	if (!TrackView_BuildVertexRecords(context, TRACK_VIEW_DIAGNOSTIC_BUILD_MATERIAL_SCREEN_MIDPOINTS,
	                                  setupList + SLIP_MATERIAL_VERTEX_SETUP_HEADER_BYTES,
	                                  setupListBytes - SLIP_MATERIAL_VERTEX_SETUP_HEADER_BYTES, derivedCount,
	                                  SLIP_MATERIAL_MIDPOINT_RECORD_BYTES, TrackView_MaterialScreenMidpoint,
	                                  TrackView_MaterialSourcePoint, &build)) {
		if (g_vehicleViewDumpDiagnostics) {
			fprintf(
			    stderr, "track_view_material_setup_fail_0003f3c4 stage=build seeds=%u derived=%u bp=%u cursor=0x%08x\n",
			    (unsigned)seedCount, (unsigned)derivedCount, (unsigned)nextRecordIndex, context->vertexBufferCursor);
		}
		return false;
	}

	context->materialSetupSeedY = (uint32_t)screenPairs[seedCount - 1u][1];

	for (seedIndex = 0; seedIndex < (size_t)seedCount; ++seedIndex) {
		if (seedIndex >= context->vertexRecordCount) {
			return false;
		}
		TrackView_StoreMaterialScreenVertex(&context->vertexRecords[seedIndex], context->projectState,
		                                    screenPairs[seedIndex][0], screenPairs[seedIndex][1]);
	}
	if (g_vehicleViewDumpDiagnostics) {
		fprintf(stderr, "track_view_material_setup_done_0003f3c4 seeds=%u derived=%u bp=%u cursor=0x%08x\n",
		        (unsigned)seedCount, (unsigned)derivedCount, (unsigned)nextRecordIndex, context->vertexBufferCursor);
	}
	return true;
}

static bool TrackView_ExecuteActiveMaterialEmitPath(TrackViewRawBspContext *context,
                                                    TrackViewMaterialMidpointContext *materialContext,
                                                    const uint8_t *indexStream, size_t indexStreamBytes,
                                                    uint16_t countAndFlags, uint16_t materialColor, bool *carryOut) {
	SlipDraw3DReturnActiveVisit returnVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DClipFlagVisit clipFlagVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneBoundsVisit postBoundsVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneClipRecordVisit postClipRecordVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneClipPlaneVisit postClipPlaneVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DActiveRingVisit activeVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DReturnActiveRing returnActive;
	SlipDraw3DActiveMaterialRingExecute activeMaterial;
	SlipDraw3DFlatRingDispatch flatRing;
	SlipDraw3DTransformFn transform;
	uint8_t *screenBeforeMaterialFlat = NULL;
	bool haveScreenBeforeMaterialFlat = false;
	TrackViewPostPlaneArgs postPlaneArgs = TrackView_PostPlaneArgs(context);
	SlipDraw3DRasterPoint flatPoints[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];

	if (context == NULL || materialContext == NULL || indexStream == NULL || carryOut == NULL) {
		return false;
	}
	*carryOut = false;
	transform = context->transform;
	if (materialColor == SLIP_MATERIAL_NO_POLYGON_COLOUR) {
		*carryOut = true;
		return true;
	}
	if (!SlipDraw3D_ReturnActiveRing(context->drawRecordPool, returnVisits,
	                                 sizeof(returnVisits) / sizeof(returnVisits[0]), &returnActive) ||
	    !SlipDraw3D_BuildActiveMaterialRingExecute(
	        context->drawRecordPool, context->vertexRecords, context->vertexRecordCount, indexStream, indexStreamBytes,
	        NULL, 0, countAndFlags, materialColor, context->rendererFlags, context->projectState, transform,
	        TrackView_MaterialProjectScreenPrimary, TrackView_MaterialProjectScreenSecondary, materialContext,
	        postPlaneArgs.hasPostPlanes, postPlaneArgs.planeBase, postPlaneArgs.planeBytes,
	        postPlaneArgs.planeHeadOffset, postPlaneArgs.limitXMin, postPlaneArgs.limitXMax, postPlaneArgs.limitYMin,
	        postPlaneArgs.limitYMax, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, clipFlagVisits,
	        sizeof(clipFlagVisits) / sizeof(clipFlagVisits[0]), postBoundsVisits,
	        sizeof(postBoundsVisits) / sizeof(postBoundsVisits[0]), postClipRecordVisits,
	        sizeof(postClipRecordVisits) / sizeof(postClipRecordVisits[0]), postClipPlaneVisits,
	        sizeof(postClipPlaneVisits) / sizeof(postClipPlaneVisits[0]), activeVisits,
	        sizeof(activeVisits) / sizeof(activeVisits[0]), &activeMaterial)) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_ACTIVE_MATERIAL_EMIT;
		context->failed = true;
		return false;
	}
	(void)returnActive;
	*carryOut = activeMaterial.carryOut;
	if (g_vehicleViewDumpDiagnostics) {
		fprintf(stderr,
		        "track_view_material_active_00019f0a bp=%u color=0x%04x active=0x%08x carry=%u mode=%u draw=%u "
		        "mat=0x%08x any=0x%08x all=0x%08x all_mask=%u calls=%u/%u visits=%u words=%04x,%04x,%04x,%04x\n",
		        (unsigned)countAndFlags, (unsigned)materialColor, activeMaterial.activeHeadOffsetOut,
		        activeMaterial.carryOut ? 1u : 0u, activeMaterial.mode, activeMaterial.drawMode,
		        activeMaterial.materialColor, activeMaterial.build.anyFlagsBeforeDispatch,
		        activeMaterial.build.allFlagsBeforeDispatch, activeMaterial.build.allMaskedNonzero ? 1u : 0u,
		        activeMaterial.build.calledClipDepth ? 1u : 0u, activeMaterial.build.calledClipScreen ? 1u : 0u,
		        (unsigned)activeMaterial.build.visitCount,
		        indexStreamBytes >= SLIP_SERIALIZED_INDEX_BYTES ? SlipBytes_ReadLE16(indexStream) : 0u,
		        indexStreamBytes >= 2 * SLIP_SERIALIZED_INDEX_BYTES
		            ? SlipBytes_ReadLE16(indexStream + SLIP_SERIALIZED_INDEX_BYTES)
		            : 0u,
		        indexStreamBytes >= 3 * SLIP_SERIALIZED_INDEX_BYTES
		            ? SlipBytes_ReadLE16(indexStream + 2 * SLIP_SERIALIZED_INDEX_BYTES)
		            : 0u,
		        indexStreamBytes >= 4 * SLIP_SERIALIZED_INDEX_BYTES
		            ? SlipBytes_ReadLE16(indexStream + 3 * SLIP_SERIALIZED_INDEX_BYTES)
		            : 0u);
		for (size_t visitIndex = 0; visitIndex < activeMaterial.build.visitCount &&
		                            visitIndex < sizeof(activeVisits) / sizeof(activeVisits[0]);
		     ++visitIndex) {
			const SlipDraw3DActiveRingVisit *const visit = &activeVisits[visitIndex];
			const SlipDraw3DDrawRecord *const activeRecord =
			    SlipDraw3D_RecordPoolDrawRecord(context->drawRecordPool, visit->drawRecordOffset);
			fprintf(
			    stderr,
			    "  material_active_visit_0001c245 visit=%u index=%u record=0x%08x flags=0x%08x any=0x%08x all=0x%08x "
			    "screen=%d,%d world=%d,%d,%d source=%04x,%04x,%04x\n",
			    (unsigned)visitIndex, (unsigned)visit->indexWord, visit->drawRecordOffset,
			    visit->flagsFromProjectVertex, visit->anyClipFlagsAfter, visit->allClipFlagsAfter,
			    activeRecord != NULL ? activeRecord->screenX : 0, activeRecord != NULL ? activeRecord->screenY : 0,
			    activeRecord != NULL
			        ? (int32_t)SlipBytes_ReadLE32(activeRecord->bytes + SLIP_DRAW3D_VERTEX_RECORD_WORLD_OFFSET)
			        : 0,
			    activeRecord != NULL
			        ? (int32_t)SlipBytes_ReadLE32(activeRecord->bytes + offsetof(SlipDraw3DVertexRecord, world.y))
			        : 0,
			    activeRecord != NULL
			        ? (int32_t)SlipBytes_ReadLE32(activeRecord->bytes + offsetof(SlipDraw3DVertexRecord, world.z))
			        : 0,
			    activeRecord != NULL ? SlipBytes_ReadLE16(activeRecord->bytes + SLIP_DRAW3D_VERTEX_RECORD_SOURCE_OFFSET)
			                         : 0,
			    activeRecord != NULL
			        ? SlipBytes_ReadLE16(activeRecord->bytes + offsetof(SlipDraw3DVertexRecord, sourceY))
			        : 0,
			    activeRecord != NULL
			        ? SlipBytes_ReadLE16(activeRecord->bytes + offsetof(SlipDraw3DVertexRecord, sourceZ))
			        : 0);
		}
	}
	if (*carryOut) {
		return true;
	}
	if (g_vehicleViewDumpDiagnostics) {
		screenBeforeMaterialFlat = (uint8_t *)malloc(SLIPSTREAM_SCREEN_WIDTH * SLIPSTREAM_SCREEN_HEIGHT);
		if (screenBeforeMaterialFlat != NULL) {
			TrackView_CopyScreenSnapshot(screenBeforeMaterialFlat);
			haveScreenBeforeMaterialFlat = true;
		}
	}
	if (!SlipDraw3D_RasterizeFlatRing(context->drawRecordPool, activeMaterial.activeHeadOffsetOut,
	                                  activeMaterial.drawMode, context->rendererFlags, activeMaterial.materialColor, 0,
	                                  SLIP_DRAW3D_RECORD_NEXT_OFFSET, flatPoints,
	                                  sizeof(flatPoints) / sizeof(flatPoints[0]), &flatRing)) {
		free(screenBeforeMaterialFlat);
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_FLAT_RING;
		context->failed = true;
		return false;
	}
	++context->emitPathFlatDispatchCount;
	if (flatRing.rasterized) {
		++context->emitPathRasterizedCount;
	}
	if (g_vehicleViewDumpDiagnostics) {
		fprintf(stderr,
		        "track_view_material_flat_00019f0a bp=%u color=0x%04x active=0x%08x carry=%u kind=%u points=%u "
		        "rasterized=%u\n",
		        countAndFlags, materialColor, activeMaterial.activeHeadOffsetOut, activeMaterial.carryOut ? 1u : 0u,
		        (unsigned)flatRing.dispatch.kind, (unsigned)flatRing.pointCount, flatRing.rasterized ? 1u : 0u);
		for (uint16_t pointIndex = 0;
		     pointIndex < flatRing.pointCount && pointIndex < SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT; ++pointIndex) {
			fprintf(stderr, "  material_flat_point_00019f0a point=%u x=%d y=%d\n", (unsigned)pointIndex,
			        flatPoints[pointIndex].x, flatPoints[pointIndex].y);
		}
		if (haveScreenBeforeMaterialFlat) {
			TrackView_DumpRecordDamage(activeMaterial.activeHeadOffsetOut, activeMaterial.materialColor,
			                           screenBeforeMaterialFlat);
		}
	}
	free(screenBeforeMaterialFlat);
	return true;
}

static bool TrackView_ExecuteLinePairMaterialEmitPath(TrackViewRawBspContext *context,
                                                      TrackViewMaterialMidpointContext *materialContext,
                                                      const uint8_t *indexStream, size_t indexStreamBytes,
                                                      uint16_t materialColor, bool *carryOut) {
	SlipDraw3DReturnActiveVisit returnVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DReturnActiveRing returnActive;
	SlipDraw3DLinePairExecute linePair;
	SlipDraw3DFlatRingDispatch flatRing;
	SlipDraw3DTransformFn transform;
	uint8_t *screenBeforeMaterialLine = NULL;
	bool haveScreenBeforeMaterialLine = false;
	SlipDraw3DRasterPoint flatPoints[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];

	if (context == NULL || materialContext == NULL || indexStream == NULL ||
	    indexStreamBytes < 2 * SLIP_SERIALIZED_INDEX_BYTES || carryOut == NULL) {
		return false;
	}
	*carryOut = false;
	transform = context->transform;
	if (!SlipDraw3D_ReturnActiveRing(context->drawRecordPool, returnVisits,
	                                 sizeof(returnVisits) / sizeof(returnVisits[0]), &returnActive) ||
	    !SlipDraw3D_BuildLinePairExecute(context->drawRecordPool, context->vertexRecords, context->vertexRecordCount,
	                                     indexStream, indexStreamBytes, materialColor, context->projectState, transform,
	                                     TrackView_MaterialProjectScreenPrimary,
	                                     TrackView_MaterialProjectScreenSecondary, materialContext, &linePair)) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_LINE_PAIR;
		context->failed = true;
		return false;
	}
	(void)returnActive;
	*carryOut = linePair.carryOut;
	if (*carryOut) {
		return true;
	}
	if (g_vehicleViewDumpDiagnostics) {
		screenBeforeMaterialLine = (uint8_t *)malloc(SLIPSTREAM_SCREEN_WIDTH * SLIPSTREAM_SCREEN_HEIGHT);
		if (screenBeforeMaterialLine != NULL) {
			TrackView_CopyScreenSnapshot(screenBeforeMaterialLine);
			haveScreenBeforeMaterialLine = true;
		}
	}
	if (!SlipDraw3D_RasterizeFlatRing(context->drawRecordPool, linePair.activeHeadOffsetOut, linePair.build.drawMode,
	                                  context->rendererFlags, linePair.build.materialDitherBits, 0,
	                                  SLIP_DRAW3D_RECORD_NEXT_OFFSET, flatPoints,
	                                  sizeof(flatPoints) / sizeof(flatPoints[0]), &flatRing)) {
		free(screenBeforeMaterialLine);
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_FLAT_RING;
		context->failed = true;
		return false;
	}
	++context->emitPathFlatDispatchCount;
	if (flatRing.rasterized) {
		++context->emitPathRasterizedCount;
	}
	if (g_vehicleViewDumpDiagnostics) {
		fprintf(stderr,
		        "track_view_material_line_00019f48 color=0x%04x active=0x%08x carry=%u kind=%u points=%u rasterized=%u "
		        "indices=%u,%u flag=%u\n",
		        materialColor, linePair.activeHeadOffsetOut, linePair.carryOut ? 1u : 0u,
		        (unsigned)flatRing.dispatch.kind, (unsigned)flatRing.pointCount, flatRing.rasterized ? 1u : 0u,
		        (unsigned)SlipBytes_ReadLE16(indexStream),
		        (unsigned)SlipBytes_ReadLE16(indexStream + SLIP_SERIALIZED_INDEX_BYTES),
		        indexStreamBytes >= 3 * SLIP_SERIALIZED_INDEX_BYTES
		            ? (unsigned)SlipBytes_ReadLE16(indexStream + 2 * SLIP_SERIALIZED_INDEX_BYTES)
		            : 0u);
		if (haveScreenBeforeMaterialLine) {
			TrackView_DumpRecordDamage(linePair.activeHeadOffsetOut, linePair.build.materialDitherBits,
			                           screenBeforeMaterialLine);
		}
	}
	free(screenBeforeMaterialLine);
	return true;
}

static bool TrackView_BuildMaterialCaptureRing(TrackViewRawBspContext *context,
                                               TrackViewMaterialMidpointContext *materialContext,
                                               const uint8_t *indexStream, size_t indexStreamBytes,
                                               uint16_t countAndFlags, uint16_t normalX, uint16_t normalY,
                                               uint16_t materialIndex) {
	SlipDraw3DReturnActiveVisit returnVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DClipFlagVisit clipFlagVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneBoundsVisit postBoundsVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneClipRecordVisit postClipRecordVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneClipPlaneVisit postClipPlaneVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DReturnActiveRing returnActive;
	SlipDraw3DMaterialGate materialGate;

	if (context->drawRecordPool == NULL || context->projectState == NULL ||
	    !SlipDraw3D_ReturnActiveRing(context->drawRecordPool, returnVisits,
	                                 sizeof(returnVisits) / sizeof(returnVisits[0]), &returnActive) ||
	    !SlipDraw3D_MaterialGate(context->materialTable, context->materialTableBytes, materialIndex,
	                             context->rendererFlags, countAndFlags, indexStream, indexStreamBytes, &materialGate)) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_GATE;
		context->failed = true;
		return false;
	}
	(void)returnActive;
	++context->emitPathMaterialGateCount;
	if (materialGate.branch == SLIP_DRAW3D_MATERIAL_GATE_BRANCH_REJECT) {
		return true;
	}

	context->materialRecord = materialGate.materialRecord;
	context->materialRecordBytes =
	    context->materialTableBytes - (size_t)(materialGate.materialRecord - context->materialTable);
	if (materialGate.branch == SLIP_DRAW3D_MATERIAL_GATE_BRANCH_REGULAR) {
		uint8_t color;
		SlipDraw3DRegularSetup regularSetup;
		SlipDraw3DSolidRingExecute solidRing;
		TrackViewPostPlaneArgs postPlaneArgs = TrackView_PostPlaneArgs(context);

		const uint16_t polygonVertexCount = countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
		if (!TrackView_MaterialColorNormal(
		        (const void *)materialGate.materialRecord, context, TrackView_DrawStateLightVector(context), normalX,
		        normalY, polygonVertexCount, context->vertexRecords, context->vertexRecordCount, indexStream,
		        indexStreamBytes, countAndFlags, context->projectState, context->transform, materialContext, &color)) {
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_NORMAL_COLOUR;
			context->failed = true;
			return false;
		}
		if (!SlipDraw3D_RegularSetup(materialGate.materialRecord,
		                             context->materialTableBytes -
		                                 (size_t)(materialGate.materialRecord - context->materialTable),
		                             countAndFlags, color, &regularSetup)) {
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_REGULAR_MATERIAL_SETUP;
			context->failed = true;
			return false;
		}
		context->materialColor = regularSetup.storedMaterialColor;
		if (!SlipDraw3D_BuildSolidRingExecute(
		        context->drawRecordPool, context->vertexRecords, context->vertexRecordCount, indexStream,
		        indexStreamBytes, (uint16_t)regularSetup.maskedIndex, regularSetup.materialDitherBits,
		        context->projectState, context->transform, TrackView_MaterialProjectScreenPrimary,
		        TrackView_MaterialProjectScreenSecondary, materialContext, postPlaneArgs.hasPostPlanes,
		        postPlaneArgs.planeBase, postPlaneArgs.planeBytes, postPlaneArgs.planeHeadOffset,
		        postPlaneArgs.limitXMin, postPlaneArgs.limitXMax, postPlaneArgs.limitYMin, postPlaneArgs.limitYMax,
		        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, clipFlagVisits,
		        sizeof(clipFlagVisits) / sizeof(clipFlagVisits[0]), postBoundsVisits,
		        sizeof(postBoundsVisits) / sizeof(postBoundsVisits[0]), postClipRecordVisits,
		        sizeof(postClipRecordVisits) / sizeof(postClipRecordVisits[0]), postClipPlaneVisits,
		        sizeof(postClipPlaneVisits) / sizeof(postClipPlaneVisits[0]), &solidRing)) {
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_SOLID_RING;
			context->failed = true;
			return false;
		}
		return true;
	}
	context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_GATE;
	context->failed = true;
	return false;
}

static bool TrackView_MaterialBegin(TrackViewRawBspContext *context, TrackViewMaterialMidpointContext *materialContext,
                                    const uint8_t *indexStream, size_t indexStreamBytes, uint32_t postPlaneSourceOffset,
                                    uint16_t countAndFlags, uint16_t materialColor, bool *carryOut) {
	bool polygonRejected = false;

	if (context == NULL || materialContext == NULL || indexStream == NULL || carryOut == NULL) {
		return false;
	}
	context->materialDeferredFlag = 0;
	*carryOut = false;
	if (g_vehicleViewDumpDiagnostics) {
		fprintf(stderr,
		        "track_view_material_begin_entry_0003f53b state=0x%08x eax=0x%08x bp=%u dx=0x%04x "
		        "words=%04x,%04x,%04x,%04x plane=%08x,%08x,%08x,%08x,%08x,%08x light=%08x,%08x,%08x\n",
		        context->materialDispatchState, postPlaneSourceOffset, (unsigned)countAndFlags, (unsigned)materialColor,
		        indexStreamBytes >= SLIP_SERIALIZED_INDEX_BYTES ? SlipBytes_ReadLE16(indexStream) : 0u,
		        indexStreamBytes >= 2 * SLIP_SERIALIZED_INDEX_BYTES
		            ? SlipBytes_ReadLE16(indexStream + SLIP_SERIALIZED_INDEX_BYTES)
		            : 0u,
		        indexStreamBytes >= 3 * SLIP_SERIALIZED_INDEX_BYTES
		            ? SlipBytes_ReadLE16(indexStream + 2 * SLIP_SERIALIZED_INDEX_BYTES)
		            : 0u,
		        indexStreamBytes >= 4 * SLIP_SERIALIZED_INDEX_BYTES
		            ? SlipBytes_ReadLE16(indexStream + 3 * SLIP_SERIALIZED_INDEX_BYTES)
		            : 0u,
		        context->materialPlanePointX, context->materialPlanePointY, context->materialPlanePointZ,
		        context->materialPlaneNormalX, context->materialPlaneNormalY, context->materialPlaneNormalZ,
		        context->cameraLightX, context->cameraLightY, context->cameraLightZ);
	}
	if (context->materialDispatchState == 0) {
		if (materialColor == SLIP_MATERIAL_NO_POLYGON_COLOUR) {
			return true;
		}
		return TrackView_ExecuteActiveMaterialEmitPath(context, materialContext, indexStream, indexStreamBytes,
		                                               countAndFlags, materialColor, carryOut);
	}
	if (materialColor != SLIP_MATERIAL_NO_POLYGON_COLOUR) {
		if (!TrackView_ExecuteActiveMaterialEmitPath(context, materialContext, indexStream, indexStreamBytes,
		                                             countAndFlags, materialColor, &polygonRejected)) {
			return false;
		}
		if (polygonRejected) {
			*carryOut = true;
			return true;
		}
	} else if (!TrackView_BuildMaterialCaptureRing(context, materialContext, indexStream, indexStreamBytes,
	                                               countAndFlags, (uint16_t)postPlaneSourceOffset,
	                                               (uint16_t)context->materialSetupSeedY, materialColor)) {
		return false;
	}
	{
		SlipDraw3DPostPlaneCaptureVisit visits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DPostPlaneCapture capture;

		if (!SlipDraw3D_CapturePostPlaneRing(
		        context->drawRecordPool, context->postPlaneHead, postPlaneSourceOffset, context->materialPlanePointX,
		        context->materialPlanePointY, context->materialPlanePointZ, (uint16_t)context->materialPlaneNormalX,
		        (uint16_t)context->materialPlaneNormalY, (uint16_t)context->materialPlaneNormalZ, context->cameraLightX,
		        context->cameraLightY, context->cameraLightZ, visits, sizeof(visits) / sizeof(visits[0]), &capture)) {
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_CAPTURE_POST_PLANE_RING;
			context->failed = true;
			return false;
		}
		if (!capture.existingPostPlane) {
			context->postPlaneColor = capture.sourceOffset;
			context->postPlaneScale = capture.planeScale;
			context->postPlanePointX = capture.planePointX;
			context->postPlanePointY = capture.planePointY;
			context->postPlanePointZ = capture.planePointZ;
			context->postPlaneNormalX = capture.planeNormalX;
			context->postPlaneNormalY = capture.planeNormalY;
			context->postPlaneNormalZ = capture.planeNormalZ;
			context->postLimitXMin = capture.limitXMin;
			context->postLimitYMin = capture.limitYMin;
			context->postLimitXMax = capture.limitXMax;
			context->postLimitYMax = capture.limitYMax;
		}
		context->postPlaneHead = capture.postPlaneHeadOut;
		*carryOut = capture.carryOut;
		if (g_vehicleViewDumpDiagnostics) {
			fprintf(stderr,
			        "track_view_material_capture_0001ab34 eax=0x%08x carry=%u dot=0x%04x post_in=0x%08x head=0x%08x "
			        "active_in=0x%08x active=0x%08x visits=%u any=0x%08x\n",
			        postPlaneSourceOffset, capture.carryOut ? 1u : 0u, (uint16_t)capture.planeLightDot,
			        capture.postPlaneHeadIn, capture.postPlaneHeadOut, capture.activeHeadIn, capture.activeHeadOut,
			        (unsigned)capture.visitCount, capture.anyEdgeDelta);
			for (size_t diagnosticIndex = 0;
			     diagnosticIndex < capture.visitCount && diagnosticIndex < SLIP_POST_PLANE_DIAGNOSTIC_VISIT_LIMIT;
			     ++diagnosticIndex) {
				const SlipDraw3DPostPlaneCaptureVisit *const visit = &visits[diagnosticIndex];

				fprintf(stderr,
				        "track_view_material_capture_visit_0001ac5d index=%zu current=0x%08x next=0x%08x edge=%d,%d "
				        "normal_len=0x%04x unit=0x%04x,0x%04x removed=%u active_after=0x%08x remaining=%u kept=%u "
				        "stored=%d,%d loop=%u\n",
				        diagnosticIndex, visit->currentOffset, visit->nextOffset, visit->edgeX, visit->edgeY,
				        (uint16_t)visit->normal.length, (uint16_t)visit->normal.unitXQ14,
				        (uint16_t)visit->normal.unitYQ14, visit->removedShortEdge ? 1u : 0u,
				        visit->activeHeadAfterRemove, (unsigned)visit->remainingCount, visit->keptEdge ? 1u : 0u,
				        visit->storedNormalX, visit->storedNormalY, visit->loop ? 1u : 0u);
			}
		}
		if (capture.carryOut) {
			return true;
		}
		context->materialDeferredFlag = UINT32_MAX;
		if (g_vehicleViewDumpDiagnostics) {
			fprintf(stderr, "track_view_material_begin_0003f53b eax=0x%08x head=0x%08x bounds=%d,%d..%d,%d visits=%u\n",
			        postPlaneSourceOffset, context->postPlaneHead, context->postLimitXMin, context->postLimitYMin,
			        context->postLimitXMax, context->postLimitYMax, (unsigned)capture.visitCount);
		}
	}
	return true;
}

static bool TrackView_ExecuteInlineReplayTail(TrackViewRawBspContext *context,
                                              TrackViewPrimitiveCallbackContext *primitiveContext,
                                              const uint8_t *primitiveRecord, size_t recordBytesRemaining,
                                              uint16_t materialIndex, bool *skippedByCapture) {
	const uint8_t *shadeRecord;
	uint32_t shadeValue;
	SlipView3DVec32 rotatedNormal;
	SlipDraw3DProjectIndex project;
	SlipDraw3DPostPlaneCaptureVisit captureVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneCapture capture;

	if (context == NULL || primitiveContext == NULL || primitiveRecord == NULL ||
	    recordBytesRemaining < SLIP_TRC_PRIMITIVE_FIRST_INDEX_END || skippedByCapture == NULL) {
		return false;
	}
	*skippedByCapture = false;
	/*
	 * The port pool uses offset 0 as the empty active ring; an emit that
	 * clipped everything away leaves it so without carrying, and there
	 * is no polygon to capture or sort the cars against. The original
	 * pool's ring is circular and never empty, so this guard is a port
	 * lifecycle detail, not game behaviour.
	 */
	if (context->drawRecordPool == NULL || context->drawRecordPool->inputActiveHeadOffset == 0) {
		*skippedByCapture = true;
		return true;
	}
	shadeRecord = TrackView_MaterialRecord(context->materialTable, context->materialTableBytes, materialIndex);
	shadeValue = shadeRecord != NULL
	                 ? SlipBytes_ReadLE32(shadeRecord + offsetof(SlipDraw3DMaterialRecord, importedMaterialByte))
	                 : 0;
	rotatedNormal = SlipView3D_TransformPosition16(
	    &primitiveContext->viewMatrix,
	    (SlipView3DVec32){(int16_t)SlipBytes_ReadLE16(primitiveRecord + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET),
	                      (int16_t)SlipBytes_ReadLE16(primitiveRecord + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET),
	                      (int16_t)SlipBytes_ReadLE16(primitiveRecord + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET)});
	if (!SlipDraw3D_ProjectIndex(context->vertexRecords, context->vertexRecordCount,
	                             SlipBytes_ReadLE16(primitiveRecord + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET),
	                             TrackView_TransformVertex, primitiveContext, &project)) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_PROJECT_INDEX;
		context->failed = true;
		return false;
	}
	if (!SlipDraw3D_CapturePostPlaneRing(
	        context->drawRecordPool, context->postPlaneHead, shadeValue, (uint32_t)project.world.x,
	        (uint32_t)project.world.y, (uint32_t)project.world.z, (uint16_t)rotatedNormal.x, (uint16_t)rotatedNormal.y,
	        (uint16_t)rotatedNormal.z, context->cameraLightX, context->cameraLightY, context->cameraLightZ,
	        captureVisits, sizeof(captureVisits) / sizeof(captureVisits[0]), &capture)) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_CAPTURE_POST_PLANE_RING;
		context->failed = true;
		return false;
	}
	if (!capture.existingPostPlane) {
		context->postPlaneColor = capture.sourceOffset;
		context->postPlaneScale = capture.planeScale;
		context->postPlanePointX = capture.planePointX;
		context->postPlanePointY = capture.planePointY;
		context->postPlanePointZ = capture.planePointZ;
		context->postPlaneNormalX = capture.planeNormalX;
		context->postPlaneNormalY = capture.planeNormalY;
		context->postPlaneNormalZ = capture.planeNormalZ;
		context->postLimitXMin = capture.limitXMin;
		context->postLimitYMin = capture.limitYMin;
		context->postLimitXMax = capture.limitXMax;
		context->postLimitYMax = capture.limitYMax;
	}
	context->postPlaneHead = capture.postPlaneHeadOut;
	if (capture.carryOut) {

		*skippedByCapture = true;
		return true;
	}
	if (!TrackView_ExecuteReplay(context)) {
		return false;
	}
	{
		SlipDraw3DPostPlaneReleaseVisit releaseVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DPostPlaneRelease release;

		if (!SlipDraw3D_ReleasePostPlaneRing(context->drawRecordPool, context->postPlaneHead, releaseVisits,
		                                     sizeof(releaseVisits) / sizeof(releaseVisits[0]), &release)) {
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_RELEASE_POST_PLANE_RING;
			context->failed = true;
			return false;
		}
		context->postPlaneHead = release.postPlaneHeadOut;
	}
	return true;
}

bool TrackView_ExecuteReplay(TrackViewRawBspContext *context) {
	SlipTrackWorldReplayListVisit replayVisits[SLIP_TRACK_REPLAY_OBJECT_CAPACITY];
	SlipTrackWorldReplayList replay;

	if (context == NULL) {
		return false;
	}
	{
		{
			if (!SlipTrackWorld_ReplayList(context->replayCount, context->replayList, context->replayListBytes, 0, 0,
			                               replayVisits, sizeof(replayVisits) / sizeof(replayVisits[0]), &replay)) {
				context->failureAddress = TRACK_VIEW_DIAGNOSTIC_REPLAY_LIST;
				context->failed = true;
				return false;
			}
			if (g_vehicleViewDumpDiagnostics) {
				fprintf(stderr,
				        "track_view_material_end_0003f5a7 callback_set=%u count=%u visits=%u branch=%u first_si=0x%04x "
				        "head=0x%08x\n",
				        context->replayCallback != NULL ? 1u : 0u, (unsigned)context->replayCount,
				        (unsigned)replay.visitCount, (unsigned)replay.branch,
				        replay.visitCount != 0u ? (unsigned)replayVisits[0].objectOffset : 0u, context->postPlaneHead);
				for (size_t i = 0; i < replay.visitCount; ++i) {
					const uint32_t objectPosition = (uint32_t)replayVisits[i].objectOffset;
					SlipObjectDrawCallback objectDrawResult = NULL;
					uint16_t objectWord = 0;
					uint16_t objectFlags = 0;
					uint32_t objectPosX = 0;
					uint32_t objectPosY = 0;
					uint32_t objectPosZ = 0;
					bool objectRecordInBounds = false;

					if (context->objectTable != NULL && objectPosition <= context->objectTableBytes &&
					    context->objectTableBytes - objectPosition >= SLIP_OBJECT_DOS_STRIDE) {
						const SlipObject *const objectRecord =
						    &context->objectTable[objectPosition / SLIP_OBJECT_DOS_STRIDE];

						objectRecordInBounds = true;
						objectWord = objectRecord->allocated;
						objectFlags = objectRecord->flags;
						objectPosX = (uint32_t)objectRecord->position.x;
						objectPosY = (uint32_t)objectRecord->position.y;
						objectPosZ = (uint32_t)objectRecord->position.z;
						objectDrawResult = objectRecord->drawCallback;
					}
					fprintf(stderr,
					        "track_view_object_dispatch_00026b84 visit=%zu si=0x%04x record=0x%08x in_bounds=%u "
					        "word0=0x%04x flags=0x%04x pos=%d,%d,%d callback_set=%u\n",
					        i, objectPosition, context->objectTableBaseToken + objectPosition,
					        objectRecordInBounds ? 1u : 0u, objectWord, objectFlags, (int32_t)objectPosX,
					        (int32_t)objectPosY, (int32_t)objectPosZ, objectDrawResult != NULL ? 1u : 0u);
				}
			}

			const uint32_t savedState = context->recordIndex;
			if (replay.visitCount != 0 && !TrackView_ApplyLoadedDrawState(context, savedState + 1u))
				return false;
			for (size_t i = 0; i < replay.visitCount; ++i) {
				const uint16_t objectOffset = replayVisits[i].objectOffset;
				const SlipObject *const object = &context->objectTable[objectOffset / SLIP_OBJECT_DOS_STRIDE];

				if (object->drawCallback != NULL && !object->drawCallback(context, objectOffset)) {
					TrackView_ApplyLoadedDrawState(context, savedState);
					context->failureAddress = TRACK_VIEW_DIAGNOSTIC_REPLAY_OBJECT_DRAW;
					context->failed = true;
					return false;
				}
			}

			if (replay.visitCount != 0 && !TrackView_ApplyLoadedDrawState(context, savedState))
				return false;
		}
	}
	return true;
}

static bool TrackView_MaterialEnd(TrackViewRawBspContext *context) {
	SlipDraw3DPostPlaneReleaseVisit visits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneRelease release;

	if (context == NULL) {
		return false;
	}
	if (context->materialDeferredFlag == 0) {
		return true;
	}
	if (context->replayCallback != NULL && !context->replayCallback(context)) {
		return false;
	}
	if (!SlipDraw3D_ReleasePostPlaneRing(context->drawRecordPool, context->postPlaneHead, visits,
	                                     sizeof(visits) / sizeof(visits[0]), &release)) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_RELEASE_POST_PLANE_RING;
		context->failed = true;
		return false;
	}
	context->postPlaneHead = release.postPlaneHeadOut;
	context->materialDeferredFlag = 0;
	return true;
}

typedef const uint8_t *(*TrackViewMaterialHandlerDataFn)(uint32_t dosAddress, size_t *bytesRemaining);

static bool TrackView_ExecuteMaterialLineHandler(TrackViewRawBspContext *context,
                                                 TrackViewPrimitiveCallbackContext *primitiveContext,
                                                 const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                 uint16_t materialIndex, uint32_t depth,
                                                 TrackViewMaterialHandlerDataFn handlerData, uint32_t setupListAddress,
                                                 uint32_t lineListAddress, uint32_t setupFailureAddress,
                                                 uint32_t lineFailureAddress, bool *carryOut) {
	const uint8_t *lineList;
	const uint8_t *setupList;
	size_t lineListBytes;
	size_t setupBytes;
	SlipDraw3DVertexRecord *savedVertexRecords;
	uint32_t savedVertexBufferCursor;
	TrackViewMaterialMidpointContext materialContext;
	uint16_t color;
	uint16_t darker;
	uint16_t lighter;
	bool lineRejected;

	if (context == NULL || primitiveContext == NULL || recordIndexStream == NULL || handlerData == NULL ||
	    carryOut == NULL) {
		return false;
	}
	*carryOut = false;

	if ((int32_t)depth > SLIP_MATERIAL_LINES_MAXIMUM_DEPTH) {
		return true;
	}
	setupList = handlerData(setupListAddress, &setupBytes);
	lineList = handlerData(lineListAddress, &lineListBytes);
	if (setupList == NULL || lineList == NULL) {
		return false;
	}
	savedVertexRecords = context->vertexRecords;
	savedVertexBufferCursor = context->vertexBufferCursor;

	if (!TrackView_MaterialSetupScreenVertices(context, primitiveContext, setupList, setupBytes, recordIndexStream,
	                                           recordIndexStreamBytes, depth)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = setupFailureAddress;
		context->failed = true;
		return false;
	}

	if (!TrackView_MaterialColorTriplet(context, materialIndex, depth, &color, &darker, &lighter)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_COLOUR_TRIPLET;
		context->failed = true;
		return false;
	}
	materialContext =
	    (TrackViewMaterialMidpointContext){context->vertexRecords, context->vertexRecordCount, primitiveContext};
	if (lineListBytes < SLIP_MATERIAL_LINE_LIST_HEADER_BYTES ||
	    lineListBytes - SLIP_MATERIAL_LINE_LIST_HEADER_BYTES <
	        (size_t)SlipBytes_ReadLE16(lineList) * SLIP_MATERIAL_LINE_PAIR_BYTES) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = lineFailureAddress;
		context->failed = true;
		return false;
	}
	for (uint16_t lineIndex = 0; lineIndex < SlipBytes_ReadLE16(lineList); ++lineIndex) {
		const uint8_t *const line =
		    lineList + SLIP_MATERIAL_LINE_LIST_HEADER_BYTES + (size_t)lineIndex * SLIP_MATERIAL_LINE_PAIR_BYTES;

		if (!TrackView_ExecuteLinePairMaterialEmitPath(context, &materialContext, line,
		                                               lineListBytes - SLIP_MATERIAL_LINE_LIST_HEADER_BYTES -
		                                                   (size_t)lineIndex * SLIP_MATERIAL_LINE_PAIR_BYTES,
		                                               color, &lineRejected)) {
			context->vertexRecords = savedVertexRecords;
			context->vertexBufferCursor = savedVertexBufferCursor;
			return false;
		}
	}

	if (!TrackView_MaterialStatePop(context)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		return false;
	}
	(void)darker;
	(void)lighter;
	return true;
}

static bool TrackView_DrawThreeLineStripMaterial(TrackViewRawBspContext *context,
                                                 TrackViewPrimitiveCallbackContext *primitiveContext,
                                                 const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                 uint16_t materialIndex, uint32_t depth, bool *carryOut) {
	return TrackView_ExecuteMaterialLineHandler(
	    context, primitiveContext, recordIndexStream, recordIndexStreamBytes, materialIndex, depth,
	    TrackView_ThreeLineStripData, TRACK_VIEW_THREE_LINE_STRIP_SETUP_TOKEN, TRACK_VIEW_THREE_LINE_STRIP_DATA_TOKEN,
	    TRACK_VIEW_DIAGNOSTIC_MATERIAL_SCREEN_VERTEX_SETUP, TRACK_VIEW_THREE_LINE_STRIP_DIAGNOSTIC_DRAW, carryOut);
}

static bool TrackView_DrawThreeLineDiagonalMaterial(TrackViewRawBspContext *context,
                                                    TrackViewPrimitiveCallbackContext *primitiveContext,
                                                    const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                    uint16_t materialIndex, uint32_t depth, bool *carryOut) {
	return TrackView_ExecuteMaterialLineHandler(
	    context, primitiveContext, recordIndexStream, recordIndexStreamBytes, materialIndex, depth,
	    TrackView_ThreeLineDiagonalData, TRACK_VIEW_THREE_LINE_DIAGONAL_SETUP_TOKEN,
	    TRACK_VIEW_THREE_LINE_DIAGONAL_DATA_TOKEN, TRACK_VIEW_DIAGNOSTIC_MATERIAL_SCREEN_VERTEX_SETUP,
	    TRACK_VIEW_THREE_LINE_DIAGONAL_DIAGNOSTIC_DRAW, carryOut);
}

static bool TrackView_DrawSevenLineFrameAlternateMaterial(TrackViewRawBspContext *context,
                                                          TrackViewPrimitiveCallbackContext *primitiveContext,
                                                          const uint8_t *recordIndexStream,
                                                          size_t recordIndexStreamBytes, uint16_t materialIndex,
                                                          uint32_t depth, bool *carryOut) {
	return TrackView_ExecuteMaterialLineHandler(
	    context, primitiveContext, recordIndexStream, recordIndexStreamBytes, materialIndex, depth,
	    TrackView_SevenLineFrameAlternateData, TRACK_VIEW_SEVEN_LINE_FRAME_ALTERNATE_SETUP_TOKEN,
	    TRACK_VIEW_SEVEN_LINE_FRAME_ALTERNATE_DATA_TOKEN, TRACK_VIEW_DIAGNOSTIC_MATERIAL_SCREEN_VERTEX_SETUP,
	    TRACK_VIEW_SEVEN_LINE_FRAME_ALTERNATE_DIAGNOSTIC_DRAW, carryOut);
}

static bool TrackView_DrawFiveLineFrameMaterial(TrackViewRawBspContext *context,
                                                TrackViewPrimitiveCallbackContext *primitiveContext,
                                                const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                uint16_t materialIndex, uint32_t depth, bool *carryOut) {
	return TrackView_ExecuteMaterialLineHandler(
	    context, primitiveContext, recordIndexStream, recordIndexStreamBytes, materialIndex, depth,
	    TrackView_FiveLineFrameData, TRACK_VIEW_FIVE_LINE_FRAME_SETUP_TOKEN, TRACK_VIEW_FIVE_LINE_FRAME_DATA_TOKEN,
	    TRACK_VIEW_DIAGNOSTIC_MATERIAL_SCREEN_VERTEX_SETUP, TRACK_VIEW_FIVE_LINE_FRAME_DIAGNOSTIC_DRAW, carryOut);
}

static bool TrackView_DrawCageFiveLinesMaterial(TrackViewRawBspContext *context,
                                                TrackViewPrimitiveCallbackContext *primitiveContext,
                                                const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                uint16_t materialIndex, uint32_t depth, bool *carryOut) {

	(void)materialIndex;
	return TrackView_ExecuteMaterialLineHandler(
	    context, primitiveContext, recordIndexStream, recordIndexStreamBytes, TrackView_MaterialHandle(context), depth,
	    TrackView_CageFiveLinesData, TRACK_VIEW_CAGE_FIVE_LINES_SETUP_TOKEN, TRACK_VIEW_CAGE_FIVE_LINES_DATA_TOKEN,
	    TRACK_VIEW_DIAGNOSTIC_MATERIAL_SCREEN_VERTEX_SETUP, TRACK_VIEW_CAGE_FIVE_LINES_DIAGNOSTIC_DRAW, carryOut);
}

static bool TrackView_DrawCageSixteenLinesMaterial(TrackViewRawBspContext *context,
                                                   TrackViewPrimitiveCallbackContext *primitiveContext,
                                                   const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                   uint16_t materialIndex, uint32_t depth, bool *carryOut) {

	(void)materialIndex;
	return TrackView_ExecuteMaterialLineHandler(
	    context, primitiveContext, recordIndexStream, recordIndexStreamBytes, TrackView_MaterialHandle(context), depth,
	    TrackView_CageSixteenLinesData, TRACK_VIEW_CAGE_SIXTEEN_LINES_SETUP_TOKEN,
	    TRACK_VIEW_CAGE_SIXTEEN_LINES_DATA_TOKEN, TRACK_VIEW_DIAGNOSTIC_MATERIAL_SCREEN_VERTEX_SETUP,
	    TRACK_VIEW_CAGE_SIXTEEN_LINES_DIAGNOSTIC_DRAW, carryOut);
}

static bool TrackView_DrawQuadOutlineMaterial(TrackViewRawBspContext *context,
                                              TrackViewPrimitiveCallbackContext *primitiveContext,
                                              const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                              uint16_t materialIndex, uint32_t depth, bool *carryOut) {
	return TrackView_ExecuteMaterialLineHandler(
	    context, primitiveContext, recordIndexStream, recordIndexStreamBytes, materialIndex, depth,
	    TrackView_QuadOutlineData, TRACK_VIEW_QUAD_OUTLINE_SETUP_TOKEN, TRACK_VIEW_QUAD_OUTLINE_DATA_TOKEN,
	    TRACK_VIEW_DIAGNOSTIC_MATERIAL_SCREEN_VERTEX_SETUP, TRACK_VIEW_QUAD_OUTLINE_DIAGNOSTIC_DRAW, carryOut);
}

static bool TrackView_DrawTwoConnectedLinesMaterial(TrackViewRawBspContext *context,
                                                    TrackViewPrimitiveCallbackContext *primitiveContext,
                                                    const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                    uint16_t materialIndex, uint32_t depth, bool *carryOut) {
	return TrackView_ExecuteMaterialLineHandler(
	    context, primitiveContext, recordIndexStream, recordIndexStreamBytes, materialIndex, depth,
	    TrackView_TwoConnectedLinesData, TRACK_VIEW_TWO_CONNECTED_LINES_SETUP_TOKEN,
	    TRACK_VIEW_TWO_CONNECTED_LINES_DATA_TOKEN, TRACK_VIEW_DIAGNOSTIC_MATERIAL_SCREEN_VERTEX_SETUP,
	    TRACK_VIEW_TWO_CONNECTED_LINES_DIAGNOSTIC_DRAW, carryOut);
}

static bool TrackView_DrawTwoCornerLinesMaterial(TrackViewRawBspContext *context,
                                                 TrackViewPrimitiveCallbackContext *primitiveContext,
                                                 const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                 uint16_t materialIndex, uint32_t depth, bool *carryOut) {
	return TrackView_ExecuteMaterialLineHandler(
	    context, primitiveContext, recordIndexStream, recordIndexStreamBytes, materialIndex, depth,
	    TrackView_TwoCornerLinesData, TRACK_VIEW_TWO_CORNER_LINES_SETUP_TOKEN, TRACK_VIEW_TWO_CORNER_LINES_DATA_TOKEN,
	    TRACK_VIEW_DIAGNOSTIC_MATERIAL_SCREEN_VERTEX_SETUP, TRACK_VIEW_TWO_CORNER_LINES_DIAGNOSTIC_DRAW, carryOut);
}

static bool TrackView_ExecuteMaterialTwoColorLineHandler(TrackViewRawBspContext *context,
                                                         TrackViewPrimitiveCallbackContext *primitiveContext,
                                                         const uint8_t *recordIndexStream,
                                                         size_t recordIndexStreamBytes, uint16_t materialIndex,
                                                         uint32_t depth, uint32_t lineListAddress,
                                                         uint32_t setupListAddress, bool *carryOut) {
	const uint8_t *lineList;
	const uint8_t *setupList;
	size_t lineListBytes;
	size_t setupBytes;
	SlipDraw3DVertexRecord *savedVertexRecords;
	uint32_t savedVertexBufferCursor;
	TrackViewMaterialMidpointContext materialContext;
	uint16_t lighter;
	uint16_t darker;
	bool lineRejected;

	(void)materialIndex;
	if (context == NULL || primitiveContext == NULL || recordIndexStream == NULL || carryOut == NULL) {
		return false;
	}
	*carryOut = false;

	if ((int32_t)depth > SLIP_MATERIAL_TWO_COLOUR_LINES_MAXIMUM_DEPTH) {
		return true;
	}

	lighter =
	    TrackView_MaterialColorStep(context->materialRecord, context->materialRecordBytes, context->materialColor, 1);
	darker =
	    TrackView_MaterialColorStep(context->materialRecord, context->materialRecordBytes, context->materialColor, -1);
	lineList = TrackView_AnimatedOrangeDashesData(lineListAddress, &lineListBytes);
	setupList = TrackView_AnimatedOrangeDashesData(setupListAddress, &setupBytes);
	if (lineList == NULL || setupList == NULL) {
		return false;
	}
	savedVertexRecords = context->vertexRecords;
	savedVertexBufferCursor = context->vertexBufferCursor;

	if (!TrackView_MaterialSetupVertices(context, primitiveContext, setupList, setupBytes, recordIndexStream,
	                                     recordIndexStreamBytes)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_VERTEX_SETUP;
		context->failed = true;
		return false;
	}
	materialContext =
	    (TrackViewMaterialMidpointContext){context->vertexRecords, context->vertexRecordCount, primitiveContext};
	if (lineListBytes < SLIP_MATERIAL_LINE_LIST_HEADER_BYTES ||
	    lineListBytes - SLIP_MATERIAL_LINE_LIST_HEADER_BYTES <
	        (size_t)SlipBytes_ReadLE16(lineList) * SLIP_MATERIAL_COLOURED_LINE_BYTES) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_TWO_COLOUR_LINE_LIST;
		context->failed = true;
		return false;
	}
	for (uint16_t lineIndex = 0; lineIndex < SlipBytes_ReadLE16(lineList); ++lineIndex) {
		const uint8_t *const line =
		    lineList + SLIP_MATERIAL_LINE_LIST_HEADER_BYTES + (size_t)lineIndex * SLIP_MATERIAL_COLOURED_LINE_BYTES;

		const uint16_t lineColor =
		    SlipBytes_ReadLE16(line + SLIP_MATERIAL_COLOURED_LINE_SELECTOR_OFFSET) != 0 ? darker : lighter;

		if (!TrackView_ExecuteLinePairMaterialEmitPath(context, &materialContext, line,
		                                               lineListBytes - SLIP_MATERIAL_LINE_LIST_HEADER_BYTES -
		                                                   (size_t)lineIndex * SLIP_MATERIAL_COLOURED_LINE_BYTES,
		                                               lineColor, &lineRejected)) {
			context->vertexRecords = savedVertexRecords;
			context->vertexBufferCursor = savedVertexBufferCursor;
			return false;
		}
	}

	if (!TrackView_MaterialStatePop(context)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		return false;
	}
	return true;
}

static bool TrackView_DrawTwoColorSixteenLineFrameMaterial(TrackViewRawBspContext *context,
                                                           TrackViewPrimitiveCallbackContext *primitiveContext,
                                                           const uint8_t *recordIndexStream,
                                                           size_t recordIndexStreamBytes, uint16_t materialIndex,
                                                           uint32_t depth, bool *carryOut) {
	return TrackView_ExecuteMaterialTwoColorLineHandler(context, primitiveContext, recordIndexStream,
	                                                    recordIndexStreamBytes, materialIndex, depth,
	                                                    TRACK_VIEW_TWO_COLOUR_SIXTEEN_LINE_FRAME_LINES_TOKEN,
	                                                    TRACK_VIEW_TWO_COLOUR_SIXTEEN_LINE_FRAME_SETUP_TOKEN, carryOut);
}

static bool TrackView_DrawTwoColorFiveLineSharedFrameMaterial(TrackViewRawBspContext *context,
                                                              TrackViewPrimitiveCallbackContext *primitiveContext,
                                                              const uint8_t *recordIndexStream,
                                                              size_t recordIndexStreamBytes, uint16_t materialIndex,
                                                              uint32_t depth, bool *carryOut) {
	return TrackView_ExecuteMaterialTwoColorLineHandler(
	    context, primitiveContext, recordIndexStream, recordIndexStreamBytes, materialIndex, depth,
	    TRACK_VIEW_TWO_COLOUR_SHARED_FRAME_LINES_TOKEN, TRACK_VIEW_TWO_COLOUR_SHARED_FRAME_SETUP_TOKEN, carryOut);
}

static bool TrackView_DrawTwoColorFiveLineSharedFrameAlternateMaterial(
    TrackViewRawBspContext *context, TrackViewPrimitiveCallbackContext *primitiveContext,
    const uint8_t *recordIndexStream, size_t recordIndexStreamBytes, uint16_t materialIndex, uint32_t depth,
    bool *carryOut) {
	return TrackView_ExecuteMaterialTwoColorLineHandler(context, primitiveContext, recordIndexStream,
	                                                    recordIndexStreamBytes, materialIndex, depth,
	                                                    TRACK_VIEW_TWO_COLOUR_SHARED_FRAME_ALTERNATE_LINES_TOKEN,
	                                                    TRACK_VIEW_TWO_COLOUR_SHARED_FRAME_SETUP_TOKEN, carryOut);
}

static bool TrackView_DrawTwoColorFiveLineCrossFrameMaterial(TrackViewRawBspContext *context,
                                                             TrackViewPrimitiveCallbackContext *primitiveContext,
                                                             const uint8_t *recordIndexStream,
                                                             size_t recordIndexStreamBytes, uint16_t materialIndex,
                                                             uint32_t depth, bool *carryOut) {
	return TrackView_ExecuteMaterialTwoColorLineHandler(
	    context, primitiveContext, recordIndexStream, recordIndexStreamBytes, materialIndex, depth,
	    TRACK_VIEW_TWO_COLOUR_CROSS_FRAME_LINES_TOKEN, TRACK_VIEW_TWO_COLOUR_CROSS_FRAME_SETUP_TOKEN, carryOut);
}

static bool TrackView_DrawTwoColorFiveLineCrossFrameInverseMaterial(TrackViewRawBspContext *context,
                                                                    TrackViewPrimitiveCallbackContext *primitiveContext,
                                                                    const uint8_t *recordIndexStream,
                                                                    size_t recordIndexStreamBytes,
                                                                    uint16_t materialIndex, uint32_t depth,
                                                                    bool *carryOut) {
	return TrackView_ExecuteMaterialTwoColorLineHandler(
	    context, primitiveContext, recordIndexStream, recordIndexStreamBytes, materialIndex, depth,
	    TRACK_VIEW_TWO_COLOUR_CROSS_FRAME_INVERSE_LINES_TOKEN, TRACK_VIEW_TWO_COLOUR_CROSS_FRAME_SETUP_TOKEN, carryOut);
}

static bool TrackView_ExecuteMaterialFarPath(TrackViewRawBspContext *context,
                                             TrackViewPrimitiveCallbackContext *primitiveContext,
                                             const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                             uint16_t materialIndex, uint32_t depth, uint16_t countAndFlags,
                                             bool *carryOut) {
	TrackViewMaterialMidpointContext materialContext;
	uint16_t color;
	uint16_t darkerColor;
	uint16_t lighterColor;

	if (!TrackView_MaterialColorTriplet(context, materialIndex, depth, &color, &darkerColor, &lighterColor)) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_FAR_MATERIAL_COLOUR_TRIPLET;
		context->failed = true;
		return false;
	}
	materialContext =
	    (TrackViewMaterialMidpointContext){context->vertexRecords, context->vertexRecordCount, primitiveContext};
	return TrackView_ExecuteActiveMaterialEmitPath(context, &materialContext, recordIndexStream, recordIndexStreamBytes,
	                                               countAndFlags, color, carryOut);
}

static bool TrackView_DrawShadedTenLinesMaterial(TrackViewRawBspContext *context,
                                                 TrackViewPrimitiveCallbackContext *primitiveContext,
                                                 const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                 uint16_t materialIndex, uint32_t depth, bool *carryOut) {
	const uint8_t *lineList;
	const uint8_t *setupList;
	size_t lineListBytes;
	size_t setupBytes;
	SlipDraw3DVertexRecord *savedVertexRecords;
	uint32_t savedVertexBufferCursor;
	TrackViewMaterialMidpointContext materialContext;
	uint16_t lighter;
	uint16_t darker;
	bool lineRejected;
	uint16_t lineIndex;

	(void)materialIndex;
	(void)depth;

	if (context == NULL || primitiveContext == NULL || recordIndexStream == NULL || carryOut == NULL) {
		return false;
	}
	*carryOut = false;

	lighter =
	    TrackView_MaterialColorStep(context->materialRecord, context->materialRecordBytes, context->materialColor, 1);
	darker =
	    TrackView_MaterialColorStep(context->materialRecord, context->materialRecordBytes, context->materialColor, -1);
	lineList = TrackView_ShadedTenLinesData(TRACK_VIEW_SHADED_TEN_LINES_DATA_TOKEN, &lineListBytes);
	setupList = TrackView_ShadedTenLinesData(TRACK_VIEW_SHADED_TEN_LINES_SETUP_LIST_TOKEN, &setupBytes);
	if (lineList == NULL || setupList == NULL) {
		return false;
	}
	savedVertexRecords = context->vertexRecords;
	savedVertexBufferCursor = context->vertexBufferCursor;

	if (!TrackView_MaterialSetupVertices(context, primitiveContext, setupList, setupBytes, recordIndexStream,
	                                     recordIndexStreamBytes)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_VERTEX_SETUP;
		context->failed = true;
		return false;
	}
	materialContext =
	    (TrackViewMaterialMidpointContext){context->vertexRecords, context->vertexRecordCount, primitiveContext};
	if (lineListBytes < SLIP_MATERIAL_LINE_LIST_HEADER_BYTES ||
	    lineListBytes - SLIP_MATERIAL_LINE_LIST_HEADER_BYTES <
	        (size_t)SlipBytes_ReadLE16(lineList) * SLIP_MATERIAL_COLOURED_LINE_BYTES) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_SHADED_TEN_LINES_LIST;
		context->failed = true;
		return false;
	}

	for (lineIndex = 0; lineIndex < SlipBytes_ReadLE16(lineList); ++lineIndex) {
		const uint8_t *const line =
		    lineList + SLIP_MATERIAL_LINE_LIST_HEADER_BYTES + (size_t)lineIndex * SLIP_MATERIAL_COLOURED_LINE_BYTES;
		const uint16_t lineColor =
		    SlipBytes_ReadLE16(line + SLIP_MATERIAL_COLOURED_LINE_SELECTOR_OFFSET) != 0u ? darker : lighter;

		if (!TrackView_ExecuteLinePairMaterialEmitPath(context, &materialContext, line,
		                                               lineListBytes - SLIP_MATERIAL_LINE_LIST_HEADER_BYTES -
		                                                   (size_t)lineIndex * SLIP_MATERIAL_COLOURED_LINE_BYTES,
		                                               lineColor, &lineRejected)) {
			context->vertexRecords = savedVertexRecords;
			context->vertexBufferCursor = savedVertexBufferCursor;
			return false;
		}
	}

	if (!TrackView_MaterialStatePop(context)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		return false;
	}
	return true;
}

static bool TrackView_DrawDepthDetailedDarkLinesMaterial(TrackViewRawBspContext *context,
                                                         TrackViewPrimitiveCallbackContext *primitiveContext,
                                                         const uint8_t *recordIndexStream,
                                                         size_t recordIndexStreamBytes, uint16_t materialIndex,
                                                         uint32_t depth, bool *carryOut) {
	const uint8_t *setupList;
	const uint8_t *mainLineList;
	const uint8_t *nearDetailLineList;
	size_t setupBytes;
	size_t mainLineListBytes;
	size_t nearDetailLineListBytes;
	SlipDraw3DVertexRecord *savedVertexRecords;
	uint32_t savedVertexBufferCursor;
	TrackViewMaterialMidpointContext materialContext;
	uint16_t lineColor;
	uint16_t lineIndex;

	(void)materialIndex;
	if (context == NULL || primitiveContext == NULL || recordIndexStream == NULL || carryOut == NULL) {
		return false;
	}
	*carryOut = false;

	if ((int32_t)depth > SLIP_MATERIAL_DARK_LINES_MAXIMUM_DEPTH) {
		return true;
	}
	setupList =
	    TrackView_DepthDetailedDarkLinesData(TRACK_VIEW_DEPTH_DETAILED_DARK_LINES_SETUP_LIST_TOKEN, &setupBytes);
	mainLineList =
	    TrackView_DepthDetailedDarkLinesData(TRACK_VIEW_DEPTH_DETAILED_DARK_LINES_DATA_TOKEN, &mainLineListBytes);
	nearDetailLineList = TrackView_DepthDetailedDarkLinesData(
	    TRACK_VIEW_DEPTH_DETAILED_DARK_LINES_NEAR_DETAIL_LINE_LIST_TOKEN, &nearDetailLineListBytes);
	if (setupList == NULL || mainLineList == NULL || nearDetailLineList == NULL) {
		return false;
	}
	savedVertexRecords = context->vertexRecords;
	savedVertexBufferCursor = context->vertexBufferCursor;

	if (!TrackView_MaterialSetupScreenVertices(context, primitiveContext, setupList, setupBytes, recordIndexStream,
	                                           recordIndexStreamBytes, depth)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_SCREEN_VERTEX_SETUP;
		context->failed = true;
		return false;
	}

	lineColor =
	    TrackView_MaterialColorStep(context->materialRecord, context->materialRecordBytes, context->materialColor, -1);
	materialContext =
	    (TrackViewMaterialMidpointContext){context->vertexRecords, context->vertexRecordCount, primitiveContext};
	if (mainLineListBytes < SLIP_MATERIAL_LINE_LIST_HEADER_BYTES ||
	    mainLineListBytes - SLIP_MATERIAL_LINE_LIST_HEADER_BYTES <
	        (size_t)SlipBytes_ReadLE16(mainLineList) * SLIP_MATERIAL_LINE_PAIR_BYTES) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DEPTH_DETAILED_DARK_LINES_DATA_TOKEN;
		context->failed = true;
		return false;
	}

	for (lineIndex = 0; lineIndex < SlipBytes_ReadLE16(mainLineList); ++lineIndex) {
		const uint8_t *const mainLine =
		    mainLineList + SLIP_MATERIAL_LINE_LIST_HEADER_BYTES + (size_t)lineIndex * SLIP_MATERIAL_LINE_PAIR_BYTES;
		bool lineRejected;

		if (!TrackView_ExecuteLinePairMaterialEmitPath(context, &materialContext, mainLine,
		                                               mainLineListBytes - SLIP_MATERIAL_LINE_LIST_HEADER_BYTES -
		                                                   (size_t)lineIndex * SLIP_MATERIAL_LINE_PAIR_BYTES,
		                                               lineColor, &lineRejected)) {
			context->vertexRecords = savedVertexRecords;
			context->vertexBufferCursor = savedVertexBufferCursor;
			return false;
		}
	}

	if ((int32_t)depth <= SLIP_MATERIAL_DARK_LINES_DETAIL_DEPTH) {
		if (nearDetailLineListBytes < SLIP_MATERIAL_LINE_LIST_HEADER_BYTES ||
		    nearDetailLineListBytes - SLIP_MATERIAL_LINE_LIST_HEADER_BYTES <
		        (size_t)SlipBytes_ReadLE16(nearDetailLineList) * SLIP_MATERIAL_LINE_PAIR_BYTES) {
			context->vertexRecords = savedVertexRecords;
			context->vertexBufferCursor = savedVertexBufferCursor;
			context->failureAddress = TRACK_VIEW_DEPTH_DETAILED_DARK_LINES_NEAR_DETAIL_LINE_LIST_TOKEN;
			context->failed = true;
			return false;
		}
		for (lineIndex = 0; lineIndex < SlipBytes_ReadLE16(nearDetailLineList); ++lineIndex) {
			const uint8_t *const detailLine = nearDetailLineList + SLIP_MATERIAL_LINE_LIST_HEADER_BYTES +
			                                  (size_t)lineIndex * SLIP_MATERIAL_LINE_PAIR_BYTES;
			bool lineRejected;

			if (!TrackView_ExecuteLinePairMaterialEmitPath(context, &materialContext, detailLine,
			                                               nearDetailLineListBytes -
			                                                   SLIP_MATERIAL_LINE_LIST_HEADER_BYTES -
			                                                   (size_t)lineIndex * SLIP_MATERIAL_LINE_PAIR_BYTES,
			                                               lineColor, &lineRejected)) {
				context->vertexRecords = savedVertexRecords;
				context->vertexBufferCursor = savedVertexBufferCursor;
				return false;
			}
		}
	}

	if (!TrackView_MaterialStatePop(context)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		return false;
	}
	return true;
}

static bool TrackView_DrawAnimatedEightPolygonsMaterial(TrackViewRawBspContext *context,
                                                        TrackViewPrimitiveCallbackContext *primitiveContext,
                                                        const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                        uint16_t materialIndex, uint32_t depth, bool *carryOut) {
	static const uint32_t kPolygonAddresses[SLIP_MATERIAL_ANIMATED_POLYGON_COUNT] = {
	    TRACK_VIEW_ANIMATED_EIGHT_POLYGONS_DATA_TOKEN,
	    TRACK_VIEW_ANIMATED_EIGHT_POLYGONS_DATA_TOKEN + 1 * SLIP_MATERIAL_ANIMATED_QUAD_BYTES,
	    TRACK_VIEW_ANIMATED_EIGHT_POLYGONS_DATA_TOKEN + 2 * SLIP_MATERIAL_ANIMATED_QUAD_BYTES,
	    TRACK_VIEW_ANIMATED_EIGHT_POLYGONS_DATA_TOKEN + 3 * SLIP_MATERIAL_ANIMATED_QUAD_BYTES,
	    TRACK_VIEW_ANIMATED_EIGHT_POLYGONS_DATA_TOKEN + 4 * SLIP_MATERIAL_ANIMATED_QUAD_BYTES,
	    TRACK_VIEW_ANIMATED_EIGHT_POLYGONS_DATA_TOKEN + 5 * SLIP_MATERIAL_ANIMATED_QUAD_BYTES,
	    TRACK_VIEW_ANIMATED_EIGHT_POLYGONS_DATA_TOKEN + 6 * SLIP_MATERIAL_ANIMATED_QUAD_BYTES,
	    TRACK_VIEW_ANIMATED_EIGHT_POLYGONS_DATA_TOKEN + 7 * SLIP_MATERIAL_ANIMATED_QUAD_BYTES};
	const uint8_t *setupList;
	size_t setupBytes;
	SlipDraw3DVertexRecord *savedVertexRecords;
	uint32_t savedVertexBufferCursor;
	TrackViewMaterialMidpointContext materialContext;
	uint16_t darker;
	uint16_t lighter;
	uint32_t polygonIndex;

	(void)materialIndex;
	if (context == NULL || primitiveContext == NULL || recordIndexStream == NULL || carryOut == NULL) {
		return false;
	}
	*carryOut = false;

	if ((int32_t)depth > SLIP_MATERIAL_ANIMATED_POLYGONS_MAXIMUM_DEPTH) {
		return true;
	}

	darker =
	    TrackView_MaterialColorStep(context->materialRecord, context->materialRecordBytes, context->materialColor, -1);
	lighter =
	    TrackView_MaterialColorStep(context->materialRecord, context->materialRecordBytes, context->materialColor, 1);
	setupList = TrackView_AnimatedEightPolygonsData(TRACK_VIEW_ANIMATED_EIGHT_POLYGONS_SETUP_LIST_TOKEN, &setupBytes);
	if (setupList == NULL) {
		return false;
	}
	savedVertexRecords = context->vertexRecords;
	savedVertexBufferCursor = context->vertexBufferCursor;

	if (!TrackView_MaterialSetupVertices(context, primitiveContext, setupList, setupBytes, recordIndexStream,
	                                     recordIndexStreamBytes)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_VERTEX_SETUP;
		context->failed = true;
		return false;
	}
	materialContext =
	    (TrackViewMaterialMidpointContext){context->vertexRecords, context->vertexRecordCount, primitiveContext};

	for (polygonIndex = 0; polygonIndex < SLIP_MATERIAL_ANIMATED_POLYGON_COUNT; ++polygonIndex) {
		const uint8_t *polygon;
		size_t polygonBytes;
		bool polygonRejected;

		polygon = TrackView_AnimatedEightPolygonsData(kPolygonAddresses[polygonIndex], &polygonBytes);
		if (polygon == NULL || polygonBytes < sizeof(uint16_t)) {
			context->vertexRecords = savedVertexRecords;
			context->vertexBufferCursor = savedVertexBufferCursor;
			context->failureAddress = kPolygonAddresses[polygonIndex];
			context->failed = true;
			return false;
		}
		if (!TrackView_ExecuteActiveMaterialEmitPath(context, &materialContext, polygon + sizeof(uint16_t),
		                                             polygonBytes - sizeof(uint16_t), SlipBytes_ReadLE16(polygon),
		                                             (uint16_t)g_materialAnimLerp, &polygonRejected)) {
			context->vertexRecords = savedVertexRecords;
			context->vertexBufferCursor = savedVertexBufferCursor;
			return false;
		}
	}

	if (!TrackView_MaterialStatePop(context)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		return false;
	}
	(void)darker;
	(void)lighter;
	return true;
}

static bool TrackView_DrawAnimatedOrangeDashesMaterial(TrackViewRawBspContext *context,
                                                       TrackViewPrimitiveCallbackContext *primitiveContext,
                                                       const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                       uint16_t materialIndex, uint32_t depth, bool *carryOut) {
	const uint8_t *setupList;
	size_t setupBytes;
	SlipDraw3DVertexRecord *savedVertexRecords;
	uint32_t savedVertexBufferCursor;
	TrackViewMaterialMidpointContext materialContext;
	uint32_t baseColor;
	uint32_t litColor;
	uint32_t phase;
	uint32_t pairIndex;

	(void)materialIndex;
	if (context == NULL || primitiveContext == NULL || recordIndexStream == NULL || carryOut == NULL) {
		return false;
	}
	*carryOut = false;

	if ((int32_t)depth > SLIP_MATERIAL_ANIMATED_POLYGONS_MAXIMUM_DEPTH) {
		return true;
	}

	(void)TrackView_MaterialColorStep(context->materialRecord, context->materialRecordBytes, context->materialColor,
	                                  -1);
	(void)TrackView_MaterialColorStep(context->materialRecord, context->materialRecordBytes, context->materialColor, 1);

	if (!TrackView_MaterialPair(context->materialTable, context->materialTableBytes, context->materialGlobal,
	                            kTrackViewOrangeLightMaterialName, &baseColor, &litColor)) {
		baseColor = 0;
		litColor = 0;
	}
	setupList = TrackView_AnimatedOrangeDashesData(TRACK_VIEW_ANIMATED_ORANGE_DASHES_SETUP_LIST_TOKEN, &setupBytes);
	if (setupList == NULL) {
		return false;
	}
	savedVertexRecords = context->vertexRecords;
	savedVertexBufferCursor = context->vertexBufferCursor;

	if (!TrackView_MaterialSetupScreenVertices(context, primitiveContext, setupList, setupBytes, recordIndexStream,
	                                           recordIndexStreamBytes, depth)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_SCREEN_VERTEX_SETUP;
		context->failed = true;
		return false;
	}
	materialContext =
	    (TrackViewMaterialMidpointContext){context->vertexRecords, context->vertexRecordCount, primitiveContext};

	phase = ((g_materialAnimAccumulator & SLIP_MATERIAL_DASH_ACCUMULATOR_MASK) ^ SLIP_MATERIAL_DASH_ACCUMULATOR_MASK) >>
	        SLIP_MATERIAL_DASH_PHASE_SHIFT;
	for (pairIndex = 0; pairIndex < SLIP_MATERIAL_DASH_PHASE_COUNT; ++pairIndex) {
		const uint32_t mainDashPairAddress =
		    TRACK_VIEW_ANIMATED_ORANGE_DASHES_DATA_TOKEN + pairIndex * SLIP_MATERIAL_DASH_PAIR_BYTES;
		const uint8_t *const mainDashPair = TrackView_AnimatedOrangeDashesData(mainDashPairAddress, NULL);
		const uint16_t mainDashColor = phase == pairIndex ? (uint16_t)litColor : (uint16_t)baseColor;
		uint32_t mainListIndex;
		bool polygonRejected;

		if (mainDashPair == NULL) {
			return false;
		}
		for (mainListIndex = 0; mainListIndex < SLIP_MATERIAL_DASH_LISTS_PER_PAIR; ++mainListIndex) {
			const uint32_t mainListAddress =
			    SlipBytes_ReadLE32(mainDashPair + mainListIndex * SLIP_MATERIAL_DASH_LIST_ADDRESS_BYTES);
			size_t mainListBytes;
			const uint8_t *const mainPolygonList = TrackView_AnimatedOrangeDashesData(mainListAddress, &mainListBytes);

			if (mainPolygonList == NULL || mainListBytes < SLIP_MATERIAL_POLYGON_LIST_HEADER_BYTES) {
				context->vertexRecords = savedVertexRecords;
				context->vertexBufferCursor = savedVertexBufferCursor;
				context->failureAddress = TRACK_VIEW_DIAGNOSTIC_ORANGE_DASH_MAIN_LIST;
				context->failed = true;
				return false;
			}
			if (!TrackView_ExecuteActiveMaterialEmitPath(
			        context, &materialContext, mainPolygonList + SLIP_MATERIAL_POLYGON_LIST_HEADER_BYTES,
			        mainListBytes - SLIP_MATERIAL_POLYGON_LIST_HEADER_BYTES, SlipBytes_ReadLE16(mainPolygonList),
			        mainDashColor, &polygonRejected)) {
				context->vertexRecords = savedVertexRecords;
				context->vertexBufferCursor = savedVertexBufferCursor;
				return false;
			}
		}
	}

	if (context->detailLevel == SLIP_RENDER_FULL_DETAIL_LEVEL) {
		for (pairIndex = 0; pairIndex < SLIP_MATERIAL_DASH_DETAIL_PAIR_COUNT; ++pairIndex) {
			const uint32_t detailDashPairAddress =
			    TRACK_VIEW_ORANGE_DASH_DETAIL_PAIRS_DATA_TOKEN + pairIndex * SLIP_MATERIAL_DASH_PAIR_BYTES;
			const uint8_t *const detailDashPair = TrackView_AnimatedOrangeDashesData(detailDashPairAddress, NULL);

			const uint16_t detailDashColor =
			    phase == (pairIndex & SLIP_MATERIAL_DASH_PHASE_MASK) ? (uint16_t)litColor : (uint16_t)baseColor;
			uint32_t detailListIndex;
			bool polygonRejected;

			if (detailDashPair == NULL) {
				return false;
			}
			for (detailListIndex = 0; detailListIndex < SLIP_MATERIAL_DASH_LISTS_PER_PAIR; ++detailListIndex) {
				const uint32_t detailListAddress =
				    SlipBytes_ReadLE32(detailDashPair + detailListIndex * SLIP_MATERIAL_DASH_LIST_ADDRESS_BYTES);
				size_t detailListBytes;
				const uint8_t *const detailPolygonList =
				    TrackView_AnimatedOrangeDashesData(detailListAddress, &detailListBytes);

				if (detailPolygonList == NULL || detailListBytes < SLIP_MATERIAL_POLYGON_LIST_HEADER_BYTES) {
					context->vertexRecords = savedVertexRecords;
					context->vertexBufferCursor = savedVertexBufferCursor;
					context->failureAddress = TRACK_VIEW_DIAGNOSTIC_ORANGE_DASH_DETAIL_LIST;
					context->failed = true;
					return false;
				}
				if (!TrackView_ExecuteActiveMaterialEmitPath(
				        context, &materialContext, detailPolygonList + SLIP_MATERIAL_POLYGON_LIST_HEADER_BYTES,
				        detailListBytes - SLIP_MATERIAL_POLYGON_LIST_HEADER_BYTES,
				        SlipBytes_ReadLE16(detailPolygonList), detailDashColor, &polygonRejected)) {
					context->vertexRecords = savedVertexRecords;
					context->vertexBufferCursor = savedVertexBufferCursor;
					return false;
				}
			}
		}
	}

	if (!TrackView_MaterialStatePop(context)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		return false;
	}
	return true;
}

static bool TrackView_DrawAnimatedFloorLightDashesMaterial(TrackViewRawBspContext *context,
                                                           TrackViewPrimitiveCallbackContext *primitiveContext,
                                                           const uint8_t *recordIndexStream,
                                                           size_t recordIndexStreamBytes, uint16_t materialIndex,
                                                           uint32_t depth, bool *carryOut) {
	const uint8_t *blobBase;
	const uint8_t *polygon;
	const uint8_t *entryTable;
	const uint8_t *setupList;
	size_t blobBytes;
	size_t polygonBytes;
	size_t setupBytes;
	SlipDraw3DVertexRecord *savedVertexRecords;
	uint32_t savedVertexBufferCursor;
	TrackViewMaterialMidpointContext materialContext;
	uint32_t litIndex;
	uint32_t iteration;
	uint32_t baseColor;
	uint32_t litColor;
	bool materialRejected;
	bool polygonRejected;

	if (context == NULL || primitiveContext == NULL || recordIndexStream == NULL || carryOut == NULL) {
		return false;
	}
	*carryOut = false;

	if ((int32_t)depth > SLIP_MATERIAL_FLOOR_LIGHTS_MAXIMUM_DEPTH) {
		return true;
	}
	blobBase = TrackView_AnimatedFloorLightDashesData(TRACK_VIEW_ANIMATED_FLOOR_LIGHT_DASHES_DATA_TOKEN, &blobBytes);
	polygon = TrackView_AnimatedFloorLightDashesData(TRACK_VIEW_ANIMATED_FLOOR_LIGHT_DASHES_DATA_TOKEN, &polygonBytes);
	entryTable = TrackView_AnimatedFloorLightDashesData(TRACK_VIEW_ANIMATED_FLOOR_LIGHT_DASHES_ENTRY_TABLE_TOKEN, NULL);
	setupList =
	    TrackView_AnimatedFloorLightDashesData(TRACK_VIEW_ANIMATED_FLOOR_LIGHT_DASHES_SETUP_LIST_TOKEN, &setupBytes);
	if (blobBase == NULL || polygon == NULL || entryTable == NULL || setupList == NULL) {
		return false;
	}
	if (!TrackView_MaterialPair(context->materialTable, context->materialTableBytes, context->materialGlobal,
	                            kTrackViewFloorLightMaterialName, &baseColor, &litColor)) {

		baseColor = 0;
		litColor = 0;
	}
	savedVertexRecords = context->vertexRecords;
	savedVertexBufferCursor = context->vertexBufferCursor;

	if (!TrackView_MaterialSetupScreenVertices(context, primitiveContext, setupList, setupBytes, recordIndexStream,
	                                           recordIndexStreamBytes, depth)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_SCREEN_VERTEX_SETUP;
		context->failed = true;
		return false;
	}
	materialContext =
	    (TrackViewMaterialMidpointContext){context->vertexRecords, context->vertexRecordCount, primitiveContext};

	if (!TrackView_MaterialBegin(context, &materialContext, polygon, polygonBytes, 0, SLIP_POLYGON_RECTANGLE_VERTICES,
	                             SLIP_MATERIAL_NO_POLYGON_COLOUR, &materialRejected) ||
	    !TrackView_MaterialEnd(context)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		return false;
	}

	litIndex =
	    ((g_materialAnimAccumulator & SLIP_MATERIAL_DASH_ACCUMULATOR_MASK) ^ SLIP_MATERIAL_DASH_ACCUMULATOR_MASK) >>
	    SLIP_MATERIAL_DASH_PHASE_SHIFT;
	for (iteration = 0; iteration < SLIP_MATERIAL_DASH_DETAIL_PAIR_COUNT; ++iteration) {
		const uint8_t *const entry = entryTable + (size_t)iteration * SLIP_MATERIAL_DASH_PAIR_BYTES;
		const uint32_t maskedIteration = iteration & SLIP_MATERIAL_DASH_PHASE_MASK;

		const uint16_t color = (uint16_t)(litIndex == maskedIteration ? litColor : baseColor);
		uint32_t pointerIndex;

		for (pointerIndex = 0; pointerIndex < SLIP_MATERIAL_DASH_LISTS_PER_PAIR; ++pointerIndex) {
			const uint32_t listAddress =
			    SlipBytes_ReadLE32(entry + pointerIndex * SLIP_MATERIAL_DASH_LIST_ADDRESS_BYTES);
			size_t listBytes;
			const uint8_t *const list = TrackView_AnimatedFloorLightDashesData(listAddress, &listBytes);
			uint16_t countAndFlags;

			if (list == NULL || listBytes < SLIP_MATERIAL_POLYGON_LIST_HEADER_BYTES) {
				context->vertexRecords = savedVertexRecords;
				context->vertexBufferCursor = savedVertexBufferCursor;
				context->failureAddress = TRACK_VIEW_DIAGNOSTIC_FLOOR_LIGHT_DASH_LIST;
				context->failed = true;
				return false;
			}
			countAndFlags = SlipBytes_ReadLE16(list);
			if (!TrackView_ExecuteActiveMaterialEmitPath(
			        context, &materialContext, list + SLIP_MATERIAL_POLYGON_LIST_HEADER_BYTES,
			        listBytes - SLIP_MATERIAL_POLYGON_LIST_HEADER_BYTES, countAndFlags, color, &polygonRejected)) {
				context->vertexRecords = savedVertexRecords;
				context->vertexBufferCursor = savedVertexBufferCursor;
				return false;
			}
		}
	}

	if (!TrackView_MaterialStatePop(context)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		return false;
	}
	(void)materialIndex;
	(void)blobBase;
	return true;
}

static bool TrackView_DrawSevenLineFrameMaterial(TrackViewRawBspContext *context,
                                                 TrackViewPrimitiveCallbackContext *primitiveContext,
                                                 const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                 uint16_t materialIndex, uint32_t depth, bool *carryOut) {
	return TrackView_ExecuteMaterialLineHandler(
	    context, primitiveContext, recordIndexStream, recordIndexStreamBytes, materialIndex, depth,
	    TrackView_SevenLineFrameData, TRACK_VIEW_SEVEN_LINE_FRAME_SETUP_TOKEN, TRACK_VIEW_SEVEN_LINE_FRAME_DATA_TOKEN,
	    TRACK_VIEW_DIAGNOSTIC_MATERIAL_SCREEN_VERTEX_SETUP, TRACK_VIEW_SEVEN_LINE_FRAME_DIAGNOSTIC_DRAW, carryOut);
}

static bool TrackView_DrawCageSevenLinesMaterial(TrackViewRawBspContext *context,
                                                 TrackViewPrimitiveCallbackContext *primitiveContext,
                                                 const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                 uint16_t materialIndex, uint32_t depth, bool *carryOut) {

	(void)materialIndex;
	return TrackView_ExecuteMaterialLineHandler(
	    context, primitiveContext, recordIndexStream, recordIndexStreamBytes, TrackView_MaterialHandle(context), depth,
	    TrackView_CageSevenLinesData, TRACK_VIEW_CAGE_SEVEN_LINES_SETUP_TOKEN, TRACK_VIEW_CAGE_SEVEN_LINES_DATA_TOKEN,
	    TRACK_VIEW_DIAGNOSTIC_MATERIAL_SCREEN_VERTEX_SETUP, TRACK_VIEW_CAGE_SEVEN_LINES_DIAGNOSTIC_DRAW, carryOut);
}

static bool TrackView_ExecuteMaterialPolygonHandler(
    TrackViewRawBspContext *context, TrackViewPrimitiveCallbackContext *primitiveContext,
    const uint8_t *recordIndexStream, size_t recordIndexStreamBytes, uint16_t materialIndex, uint32_t depth,
    TrackViewMaterialHandlerDataFn handlerData, TrackViewMaterialHandlerDataFn setupHandlerData,
    uint32_t setupListAddress, uint32_t secondaryBasePolygonAddress, uint32_t secondaryDetailPolygonAddress,
    uint32_t primaryPolygonAddress, uint32_t lineListAddress, uint16_t secondaryDetailCountAndFlags,
    bool setupBeforeColors, uint32_t farPathAddress, uint32_t lineFailureAddress, bool *carryOut) {
	const uint8_t *secondaryBasePolygon;
	const uint8_t *secondaryDetailPolygon;
	const uint8_t *primaryPolygon;
	const uint8_t *lineList;
	const uint8_t *setupList;
	size_t secondaryBasePolygonBytes;
	size_t secondaryDetailPolygonBytes;
	size_t primaryPolygonBytes;
	size_t lineListBytes;
	size_t setupBytes;
	SlipDraw3DVertexRecord *savedVertexRecords;
	uint32_t savedVertexBufferCursor;
	TrackViewMaterialMidpointContext materialContext;
	uint16_t color;
	uint16_t darker;
	uint16_t lighter;
	uint16_t secondaryColor;
	uint16_t secondaryDarker;
	uint16_t secondaryLighter;
	bool materialRejected;
	bool lineRejected;

	if (context == NULL || primitiveContext == NULL || recordIndexStream == NULL || carryOut == NULL) {
		return false;
	}
	*carryOut = false;
	if ((int32_t)depth > SLIP_MATERIAL_POLYGONS_MAXIMUM_DEPTH) {
		(void)farPathAddress;
		return TrackView_ExecuteMaterialFarPath(context, primitiveContext, recordIndexStream, recordIndexStreamBytes,
		                                        materialIndex, depth, secondaryDetailCountAndFlags, carryOut);
	}
	setupList = setupHandlerData(setupListAddress, &setupBytes);
	secondaryBasePolygon = handlerData(secondaryBasePolygonAddress, &secondaryBasePolygonBytes);
	secondaryDetailPolygon = handlerData(secondaryDetailPolygonAddress, &secondaryDetailPolygonBytes);
	primaryPolygon = handlerData(primaryPolygonAddress, &primaryPolygonBytes);
	lineList = handlerData(lineListAddress, &lineListBytes);
	if (setupList == NULL || secondaryBasePolygon == NULL || secondaryDetailPolygon == NULL || primaryPolygon == NULL ||
	    lineList == NULL) {
		return false;
	}
	savedVertexRecords = context->vertexRecords;
	savedVertexBufferCursor = context->vertexBufferCursor;
	if (setupBeforeColors && !TrackView_MaterialSetupVertices(context, primitiveContext, setupList, setupBytes,
	                                                          recordIndexStream, recordIndexStreamBytes)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_VERTEX_SETUP;
		context->failed = true;
		return false;
	}
	if (!TrackView_MaterialColorTriplet(context, materialIndex, depth, &color, &darker, &lighter) ||
	    !TrackView_MaterialColorTriplet(context, context->materialDepthIndex, depth, &secondaryColor, &secondaryDarker,
	                                    &secondaryLighter)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_COLOUR_TRIPLET;
		context->failed = true;
		return false;
	}
	if (!setupBeforeColors && !TrackView_MaterialSetupVertices(context, primitiveContext, setupList, setupBytes,
	                                                           recordIndexStream, recordIndexStreamBytes)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_VERTEX_SETUP;
		context->failed = true;
		return false;
	}
	materialContext =
	    (TrackViewMaterialMidpointContext){context->vertexRecords, context->vertexRecordCount, primitiveContext};
	if (!TrackView_MaterialBegin(context, &materialContext, secondaryBasePolygon, secondaryBasePolygonBytes,
	                             context->materialDepthBase, 4, secondaryColor, &materialRejected) ||
	    !TrackView_MaterialEnd(context) ||
	    !TrackView_MaterialBegin(context, &materialContext, secondaryDetailPolygon, secondaryDetailPolygonBytes,
	                             context->materialDepthBase, secondaryDetailCountAndFlags, secondaryColor,
	                             &materialRejected) ||
	    !TrackView_MaterialEnd(context) ||
	    !TrackView_MaterialBegin(context, &materialContext, primaryPolygon, primaryPolygonBytes, 0, 4, color,
	                             &materialRejected)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		return false;
	}
	if (lineListBytes < SLIP_MATERIAL_LINE_LIST_HEADER_BYTES ||
	    lineListBytes - SLIP_MATERIAL_LINE_LIST_HEADER_BYTES <
	        (size_t)SlipBytes_ReadLE16(lineList) * SLIP_MATERIAL_COLOURED_LINE_BYTES) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = lineFailureAddress;
		context->failed = true;
		return false;
	}
	for (uint16_t lineIndex = 0; lineIndex < SlipBytes_ReadLE16(lineList); ++lineIndex) {
		const uint8_t *const line =
		    lineList + SLIP_MATERIAL_LINE_LIST_HEADER_BYTES + (size_t)lineIndex * SLIP_MATERIAL_COLOURED_LINE_BYTES;
		const uint16_t lineColor =
		    SlipBytes_ReadLE16(line + SLIP_MATERIAL_COLOURED_LINE_SELECTOR_OFFSET) != 0u ? darker : lighter;

		if (!TrackView_ExecuteLinePairMaterialEmitPath(context, &materialContext, line,
		                                               lineListBytes - SLIP_MATERIAL_LINE_LIST_HEADER_BYTES -
		                                                   (size_t)lineIndex * SLIP_MATERIAL_COLOURED_LINE_BYTES,
		                                               lineColor, &lineRejected)) {
			context->vertexRecords = savedVertexRecords;
			context->vertexBufferCursor = savedVertexBufferCursor;
			return false;
		}
	}
	if (!TrackView_MaterialEnd(context) || !TrackView_MaterialStatePop(context)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		return false;
	}
	(void)lighter;
	(void)darker;
	return true;
}

static bool TrackView_DrawIndependentSetupTriangleQuadsMaterial(TrackViewRawBspContext *context,
                                                                TrackViewPrimitiveCallbackContext *primitiveContext,
                                                                const uint8_t *recordIndexStream,
                                                                size_t recordIndexStreamBytes, uint16_t materialIndex,
                                                                uint32_t depth, bool *carryOut) {
	return TrackView_ExecuteMaterialPolygonHandler(
	    context, primitiveContext, recordIndexStream, recordIndexStreamBytes, materialIndex, depth,
	    TrackView_IndependentSetupTriangleQuadsData, TrackView_IndependentSetupTriangleQuadsData,
	    TRACK_VIEW_INDEPENDENT_SETUP_TRIANGLE_QUADS_SETUP_TOKEN,
	    TRACK_VIEW_INDEPENDENT_SETUP_TRIANGLE_QUADS_SECONDARY_BASE_TOKEN,
	    TRACK_VIEW_INDEPENDENT_SETUP_TRIANGLE_QUADS_SECONDARY_DETAIL_TOKEN,
	    TRACK_VIEW_INDEPENDENT_SETUP_TRIANGLE_QUADS_PRIMARY_TOKEN,
	    TRACK_VIEW_INDEPENDENT_SETUP_TRIANGLE_QUADS_LINES_TOKEN, 3, false,
	    TRACK_VIEW_INDEPENDENT_SETUP_TRIANGLE_QUADS_DIAGNOSTIC_FAR_PATH,
	    TRACK_VIEW_INDEPENDENT_SETUP_TRIANGLE_QUADS_DIAGNOSTIC_LINES, carryOut);
}

static bool TrackView_DrawShadedQuadsMaterial(TrackViewRawBspContext *context,
                                              TrackViewPrimitiveCallbackContext *primitiveContext,
                                              const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                              uint16_t materialIndex, uint32_t depth, bool *carryOut) {
	return TrackView_ExecuteMaterialPolygonHandler(
	    context, primitiveContext, recordIndexStream, recordIndexStreamBytes, materialIndex, depth,
	    TrackView_ShadedQuadsData, TrackView_ShadedQuadsData, TRACK_VIEW_SHADED_QUADS_SETUP_TOKEN,
	    TRACK_VIEW_SHADED_QUADS_DATA_TOKEN, TRACK_VIEW_SHADED_QUADS_SECONDARY_DETAIL_TOKEN,
	    TRACK_VIEW_SHADED_QUADS_PRIMARY_TOKEN, TRACK_VIEW_SHADED_QUADS_LINES_TOKEN, 4, true,
	    TRACK_VIEW_SHADED_QUADS_DIAGNOSTIC_FAR_PATH, TRACK_VIEW_SHADED_QUADS_DIAGNOSTIC_LINES, carryOut);
}

static bool TrackView_DrawShadedTriangleQuadsMaterial(TrackViewRawBspContext *context,
                                                      TrackViewPrimitiveCallbackContext *primitiveContext,
                                                      const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                      uint16_t materialIndex, uint32_t depth, bool *carryOut) {
	return TrackView_ExecuteMaterialPolygonHandler(
	    context, primitiveContext, recordIndexStream, recordIndexStreamBytes, materialIndex, depth,
	    TrackView_ShadedTriangleQuadsData, TrackView_ShadedTriangleQuadsData,
	    TRACK_VIEW_SHADED_TRIANGLE_QUADS_SETUP_TOKEN, TRACK_VIEW_SHADED_TRIANGLE_QUADS_DATA_TOKEN,
	    TRACK_VIEW_SHADED_TRIANGLE_QUADS_SECONDARY_DETAIL_TOKEN, TRACK_VIEW_SHADED_TRIANGLE_QUADS_PRIMARY_TOKEN,
	    TRACK_VIEW_SHADED_TRIANGLE_QUADS_LINES_TOKEN, 3, false, TRACK_VIEW_SHADED_TRIANGLE_QUADS_DIAGNOSTIC_FAR_PATH,
	    TRACK_VIEW_SHADED_TRIANGLE_QUADS_DIAGNOSTIC_LINES, carryOut);
}

static bool TrackView_DrawSharedSetupTriangleQuadsMaterial(TrackViewRawBspContext *context,
                                                           TrackViewPrimitiveCallbackContext *primitiveContext,
                                                           const uint8_t *recordIndexStream,
                                                           size_t recordIndexStreamBytes, uint16_t materialIndex,
                                                           uint32_t depth, bool *carryOut) {
	return TrackView_ExecuteMaterialPolygonHandler(
	    context, primitiveContext, recordIndexStream, recordIndexStreamBytes, materialIndex, depth,
	    TrackView_SharedSetupTriangleQuadsData, TrackView_ShadedTriangleQuadsData,
	    TRACK_VIEW_SHADED_TRIANGLE_QUADS_SETUP_TOKEN, TRACK_VIEW_SHARED_SETUP_TRIANGLE_QUADS_DATA_TOKEN,
	    TRACK_VIEW_SHARED_SETUP_TRIANGLE_QUADS_SECONDARY_DETAIL_TOKEN,
	    TRACK_VIEW_SHARED_SETUP_TRIANGLE_QUADS_PRIMARY_TOKEN, TRACK_VIEW_SHARED_SETUP_TRIANGLE_QUADS_LINES_TOKEN, 3,
	    false, TRACK_VIEW_SHARED_SETUP_TRIANGLE_QUADS_DIAGNOSTIC_FAR_PATH,
	    TRACK_VIEW_SHARED_SETUP_TRIANGLE_QUADS_DIAGNOSTIC_LINES, carryOut);
}

static bool TrackView_DrawRoadLinePolygonsMaterial(TrackViewRawBspContext *context,
                                                   TrackViewPrimitiveCallbackContext *primitiveContext,
                                                   const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                   uint16_t materialIndex, uint32_t depth, bool *carryOut) {
	static const uint32_t kPolygonAddresses[] = {
	    TRACK_VIEW_ROAD_SURFACE_POLYGON_TOKEN, TRACK_VIEW_ROAD_DARK_SURFACE_POLYGON_TOKEN,
	    TRACK_VIEW_ROAD_FIRST_LINE_POLYGON_TOKEN, TRACK_VIEW_ROAD_SECOND_LINE_POLYGON_TOKEN,
	    TRACK_VIEW_ROAD_THIRD_LINE_POLYGON_TOKEN};
	const uint8_t *setupList;
	size_t setupBytes;
	SlipDraw3DVertexRecord *savedVertexRecords;
	uint32_t savedVertexBufferCursor;
	TrackViewMaterialMidpointContext materialContext;
	uint16_t color;
	uint16_t darker;
	uint16_t lighter;
	uint16_t roadLineColor;
	uint16_t roadLineDarker;
	uint16_t roadLineLighter;
	uint32_t roadLineValue;
	uint16_t roadLineHandle;
	size_t polygonIndex;
	bool materialRejected;

	if (context == NULL || primitiveContext == NULL || recordIndexStream == NULL || carryOut == NULL) {
		return false;
	}
	*carryOut = false;

	if ((int32_t)depth > SLIP_MATERIAL_ROAD_LINES_MAXIMUM_DEPTH) {
		return TrackView_ExecuteMaterialFarPath(context, primitiveContext, recordIndexStream, recordIndexStreamBytes,
		                                        materialIndex, depth, 4, carryOut);
	}
	setupList = TrackView_RoadLinePolygonsData(TRACK_VIEW_ROAD_LINE_POLYGONS_SETUP_LIST_TOKEN, &setupBytes);
	if (setupList == NULL) {
		return false;
	}

	roadLineHandle = 0;
	roadLineValue = 0;
	{
		SlipDraw3DMaterialNumber lookup;

		if (context->materialTable != NULL &&
		    SlipDraw3D_GetMaterialNumber(context->materialTable, context->materialTableBytes, context->materialGlobal,
		                                 (const uint8_t *)kTrackViewRoadLineMaterialName,
		                                 sizeof(kTrackViewRoadLineMaterialName), &lookup) &&
		    !lookup.carryOut) {
			roadLineHandle = lookup.materialIndex;
			if (!TrackView_MaterialValue(context->materialTable, context->materialTableBytes, context->materialGlobal,
			                             kTrackViewRoadLineMaterialName, &roadLineValue)) {
				roadLineValue = 0;
			}
		}
	}
	savedVertexRecords = context->vertexRecords;
	savedVertexBufferCursor = context->vertexBufferCursor;

	if (!TrackView_MaterialSetupScreenVertices(context, primitiveContext, setupList, setupBytes, recordIndexStream,
	                                           recordIndexStreamBytes, depth)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_SCREEN_VERTEX_SETUP;
		context->failed = true;
		return false;
	}
	if (!TrackView_MaterialColorTriplet(context, materialIndex, depth, &color, &darker, &lighter) ||
	    !TrackView_MaterialColorTriplet(context, roadLineHandle, depth, &roadLineColor, &roadLineDarker,
	                                    &roadLineLighter)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_COLOUR_TRIPLET;
		context->failed = true;
		return false;
	}
	materialContext =
	    (TrackViewMaterialMidpointContext){context->vertexRecords, context->vertexRecordCount, primitiveContext};
	for (polygonIndex = 0; polygonIndex < sizeof(kPolygonAddresses) / sizeof(kPolygonAddresses[0]); ++polygonIndex) {
		const uint8_t *polygon;
		size_t polygonBytes;
		uint16_t polygonColor;
		uint32_t polygonAddress;

		polygon = TrackView_RoadLinePolygonsData(kPolygonAddresses[polygonIndex], &polygonBytes);
		if (polygon == NULL) {
			context->vertexRecords = savedVertexRecords;
			context->vertexBufferCursor = savedVertexBufferCursor;
			return false;
		}

		if (polygonIndex == 0) {
			polygonColor = color;
			polygonAddress = 0;
		} else if (polygonIndex == 1) {
			polygonColor = darker;
			polygonAddress = 0;
		} else {
			polygonColor = roadLineColor;
			polygonAddress = roadLineValue;
		}
		if (!TrackView_MaterialBegin(context, &materialContext, polygon, polygonBytes, polygonAddress, 4, polygonColor,
		                             &materialRejected) ||
		    !TrackView_MaterialEnd(context)) {
			context->vertexRecords = savedVertexRecords;
			context->vertexBufferCursor = savedVertexBufferCursor;
			return false;
		}
	}

	if (!TrackView_MaterialStatePop(context)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		return false;
	}
	(void)lighter;
	(void)roadLineDarker;
	(void)roadLineLighter;
	return true;
}

static bool TrackView_DrawSecondaryColorQuadsMaterial(TrackViewRawBspContext *context,
                                                      TrackViewPrimitiveCallbackContext *primitiveContext,
                                                      const uint8_t *recordIndexStream, size_t recordIndexStreamBytes,
                                                      uint16_t materialIndex, uint32_t depth, bool *carryOut) {
	const uint8_t *secondaryBasePolygon;
	const uint8_t *secondaryDetailPolygon;
	const uint8_t *primaryPolygon;
	const uint8_t *setupList;
	size_t secondaryBasePolygonBytes;
	size_t secondaryDetailPolygonBytes;
	size_t primaryPolygonBytes;
	size_t setupBytes;
	SlipDraw3DVertexRecord *savedVertexRecords;
	uint32_t savedVertexBufferCursor;
	TrackViewMaterialMidpointContext materialContext;
	uint16_t color;
	uint16_t darker;
	uint16_t lighter;
	uint16_t secondaryColor;
	uint16_t secondaryDarker;
	uint16_t secondaryLighter;
	bool materialRejected;

	if (context == NULL || primitiveContext == NULL || recordIndexStream == NULL || carryOut == NULL) {
		return false;
	}
	*carryOut = false;

	if ((int32_t)depth > SLIP_MATERIAL_POLYGONS_MAXIMUM_DEPTH) {
		return TrackView_ExecuteMaterialFarPath(context, primitiveContext, recordIndexStream, recordIndexStreamBytes,
		                                        materialIndex, depth, 4, carryOut);
	}
	setupList = TrackView_SecondaryColorQuadsData(TRACK_VIEW_SECONDARY_COLOR_QUADS_SETUP_LIST_TOKEN, &setupBytes);
	secondaryBasePolygon =
	    TrackView_SecondaryColorQuadsData(TRACK_VIEW_SECONDARY_COLOR_QUADS_DATA_TOKEN, &secondaryBasePolygonBytes);
	secondaryDetailPolygon = TrackView_SecondaryColorQuadsData(
	    TRACK_VIEW_SECONDARY_COLOR_QUADS_SECONDARY_DETAIL_POLYGON_TOKEN, &secondaryDetailPolygonBytes);
	primaryPolygon =
	    TrackView_SecondaryColorQuadsData(TRACK_VIEW_SECONDARY_COLOR_QUADS_PRIMARY_POLYGON_TOKEN, &primaryPolygonBytes);
	if (setupList == NULL || secondaryBasePolygon == NULL || secondaryDetailPolygon == NULL || primaryPolygon == NULL) {
		return false;
	}
	savedVertexRecords = context->vertexRecords;
	savedVertexBufferCursor = context->vertexBufferCursor;

	if (!TrackView_MaterialSetupVertices(context, primitiveContext, setupList, setupBytes, recordIndexStream,
	                                     recordIndexStreamBytes)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_VERTEX_SETUP;
		context->failed = true;
		return false;
	}
	if (!TrackView_MaterialColorTriplet(context, materialIndex, depth, &color, &darker, &lighter) ||
	    !TrackView_MaterialColorTriplet(context, context->materialDepthIndex, depth, &secondaryColor, &secondaryDarker,
	                                    &secondaryLighter)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_COLOUR_TRIPLET;
		context->failed = true;
		return false;
	}
	materialContext =
	    (TrackViewMaterialMidpointContext){context->vertexRecords, context->vertexRecordCount, primitiveContext};

	if (!TrackView_MaterialBegin(context, &materialContext, secondaryBasePolygon, secondaryBasePolygonBytes,
	                             context->materialDepthBase, 4, secondaryColor, &materialRejected) ||
	    !TrackView_MaterialEnd(context) ||
	    !TrackView_MaterialBegin(context, &materialContext, secondaryDetailPolygon, secondaryDetailPolygonBytes,
	                             context->materialDepthBase, 4, secondaryColor, &materialRejected) ||
	    !TrackView_MaterialEnd(context) ||
	    !TrackView_MaterialBegin(context, &materialContext, primaryPolygon, primaryPolygonBytes, 0, 4, color,
	                             &materialRejected) ||
	    !TrackView_MaterialEnd(context) || !TrackView_MaterialStatePop(context)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		return false;
	}
	(void)darker;
	(void)lighter;
	(void)secondaryDarker;
	(void)secondaryLighter;
	return true;
}

static bool TrackView_DrawSecondaryColorPolygonsWithLinesMaterial(TrackViewRawBspContext *context,
                                                                  TrackViewPrimitiveCallbackContext *primitiveContext,
                                                                  const uint8_t *recordIndexStream,
                                                                  size_t recordIndexStreamBytes, uint16_t materialIndex,
                                                                  uint32_t depth, bool *carryOut) {
	const uint8_t *secondaryBasePolygon;
	const uint8_t *secondaryDetailPolygon;
	const uint8_t *primaryPolygon;
	const uint8_t *lineList;
	const uint8_t *setupList;
	size_t secondaryBasePolygonBytes;
	size_t secondaryDetailPolygonBytes;
	size_t primaryPolygonBytes;
	size_t lineListBytes;
	size_t setupBytes;
	SlipDraw3DVertexRecord *savedVertexRecords;
	uint32_t savedVertexBufferCursor;
	TrackViewMaterialMidpointContext materialContext;
	uint16_t color;
	uint16_t darker;
	uint16_t lighter;
	uint16_t secondaryColor;
	uint16_t secondaryDarker;
	uint16_t secondaryLighter;
	bool materialRejected;
	bool lineRejected;

	if (context == NULL || primitiveContext == NULL || recordIndexStream == NULL || carryOut == NULL) {
		return false;
	}
	*carryOut = false;
	if ((int32_t)depth > SLIP_MATERIAL_POLYGONS_MAXIMUM_DEPTH) {
		return TrackView_ExecuteMaterialFarPath(context, primitiveContext, recordIndexStream, recordIndexStreamBytes,
		                                        materialIndex, depth, 3, carryOut);
	}
	if (!TrackView_MaterialColorTriplet(context, materialIndex, depth, &color, &darker, &lighter) ||
	    !TrackView_MaterialColorTriplet(context, context->materialDepthIndex, depth, &secondaryColor, &secondaryDarker,
	                                    &secondaryLighter)) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_COLOUR_TRIPLET;
		context->failed = true;
		return false;
	}
	setupList = TrackView_IndependentSetupTriangleQuadsData(TRACK_VIEW_INDEPENDENT_SETUP_TRIANGLE_QUADS_SETUP_TOKEN,
	                                                        &setupBytes);
	secondaryBasePolygon = TrackView_SecondaryColorPolygonsWithLinesData(
	    TRACK_VIEW_SECONDARY_COLOR_POLYGONS_WITH_LINES_SECONDARY_BASE_POLYGON_TOKEN, &secondaryBasePolygonBytes);
	secondaryDetailPolygon = TrackView_SecondaryColorPolygonsWithLinesData(
	    TRACK_VIEW_SECONDARY_COLOR_POLYGONS_WITH_LINES_SECONDARY_DETAIL_POLYGON_TOKEN, &secondaryDetailPolygonBytes);
	primaryPolygon = TrackView_SecondaryColorPolygonsWithLinesData(
	    TRACK_VIEW_SECONDARY_COLOR_POLYGONS_WITH_LINES_PRIMARY_POLYGON_TOKEN, &primaryPolygonBytes);
	lineList = TrackView_SecondaryColorPolygonsWithLinesData(
	    TRACK_VIEW_SECONDARY_COLOR_POLYGONS_WITH_LINES_LINE_LIST_TOKEN, &lineListBytes);
	if (setupList == NULL || secondaryBasePolygon == NULL || secondaryDetailPolygon == NULL || primaryPolygon == NULL ||
	    lineList == NULL) {
		return false;
	}
	savedVertexRecords = context->vertexRecords;
	savedVertexBufferCursor = context->vertexBufferCursor;
	if (!TrackView_MaterialSetupVertices(context, primitiveContext, setupList, setupBytes, recordIndexStream,
	                                     recordIndexStreamBytes)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_VERTEX_SETUP;
		context->failed = true;
		return false;
	}
	materialContext =
	    (TrackViewMaterialMidpointContext){context->vertexRecords, context->vertexRecordCount, primitiveContext};
	if (!TrackView_MaterialBegin(context, &materialContext, secondaryBasePolygon, secondaryBasePolygonBytes,
	                             context->materialDepthBase, 4, secondaryColor, &materialRejected) ||
	    !TrackView_MaterialEnd(context) ||
	    !TrackView_MaterialBegin(context, &materialContext, secondaryDetailPolygon, secondaryDetailPolygonBytes,
	                             context->materialDepthBase, 3, secondaryColor, &materialRejected) ||
	    !TrackView_MaterialEnd(context) ||
	    !TrackView_MaterialBegin(context, &materialContext, primaryPolygon, primaryPolygonBytes, 0, 4, color,
	                             &materialRejected)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		return false;
	}
	if (lineListBytes < SLIP_MATERIAL_LINE_LIST_HEADER_BYTES ||
	    lineListBytes - SLIP_MATERIAL_LINE_LIST_HEADER_BYTES <
	        (size_t)SlipBytes_ReadLE16(lineList) * SLIP_MATERIAL_COLOURED_LINE_BYTES) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_SECONDARY_COLOUR_LINE_LIST;
		context->failed = true;
		return false;
	}
	for (uint16_t lineIndex = 0; lineIndex < SlipBytes_ReadLE16(lineList); ++lineIndex) {
		const uint8_t *const line =
		    lineList + SLIP_MATERIAL_LINE_LIST_HEADER_BYTES + (size_t)lineIndex * SLIP_MATERIAL_COLOURED_LINE_BYTES;
		const uint16_t lineColor =
		    SlipBytes_ReadLE16(line + SLIP_MATERIAL_COLOURED_LINE_SELECTOR_OFFSET) != 0u ? darker : lighter;

		if (!TrackView_ExecuteLinePairMaterialEmitPath(context, &materialContext, line,
		                                               lineListBytes - SLIP_MATERIAL_LINE_LIST_HEADER_BYTES -
		                                                   (size_t)lineIndex * SLIP_MATERIAL_COLOURED_LINE_BYTES,
		                                               lineColor, &lineRejected)) {
			context->vertexRecords = savedVertexRecords;
			context->vertexBufferCursor = savedVertexBufferCursor;
			return false;
		}
	}
	if (!TrackView_MaterialEnd(context) || !TrackView_MaterialStatePop(context)) {
		context->vertexRecords = savedVertexRecords;
		context->vertexBufferCursor = savedVertexBufferCursor;
		return false;
	}
	(void)lighter;
	(void)darker;
	return true;
}

static const TrackViewMaterialHandler kTrackViewStandardMaterialHandlers[] = {
    NULL,
    TrackView_DrawAnimatedOrangeDashesMaterial,
    TrackView_DrawTwoColorSixteenLineFrameMaterial,
    TrackView_DrawTwoColorFiveLineSharedFrameMaterial,
    TrackView_DrawTwoColorFiveLineSharedFrameAlternateMaterial,
    TrackView_DrawTwoColorFiveLineCrossFrameMaterial,
    TrackView_DrawTwoColorFiveLineCrossFrameInverseMaterial,
    TrackView_DrawShadedTenLinesMaterial,
    TrackView_DrawNoOpShadedLinesMaterial,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    TrackView_DrawNoOpDepthLinesMaterial,
    TrackView_DrawDepthDetailedDarkLinesMaterial,
    NULL,
    NULL,
    NULL,
    NULL,
    TrackView_DrawNoOpAnimatedPolygonsMaterial,
    NULL,
    NULL,
    NULL,
    NULL,
    TrackView_DrawAnimatedEightPolygonsMaterial,
    NULL,
    NULL,
    NULL,
    NULL};

static const TrackViewMaterialHandler kTrackViewExtendedMaterialHandlers[] = {
    TrackView_DrawCageSevenLinesMaterial,
    TrackView_DrawCageFiveLinesMaterial,
    TrackView_DrawCageSixteenLinesMaterial,
    NULL,
    TrackView_DrawThreeLineDiagonalMaterial,
    TrackView_DrawSevenLineFrameMaterial,
    TrackView_DrawShadedQuadsMaterial,
    TrackView_DrawShadedTriangleQuadsMaterial,
    TrackView_DrawSharedSetupTriangleQuadsMaterial,
    TrackView_DrawIndependentSetupTriangleQuadsMaterial,
    TrackView_DrawSecondaryColorPolygonsWithLinesMaterial,
    TrackView_DrawSecondaryColorQuadsMaterial,
    TrackView_DrawSevenLineFrameAlternateMaterial,
    TrackView_DrawThreeLineStripMaterial,
    TrackView_DrawFiveLineFrameMaterial,
    TrackView_DrawAnimatedFloorLightDashesMaterial,
    TrackView_DrawRoadLinePolygonsMaterial,
    TrackView_DrawQuadOutlineMaterial,
    TrackView_DrawTwoConnectedLinesMaterial,
    TrackView_DrawTwoCornerLinesMaterial};

static bool TrackView_ExecuteExtendedMaterialDispatch(TrackViewRawBspContext *context,
                                                      TrackViewPrimitiveCallbackContext *primitiveContext,
                                                      const uint8_t *primitiveRecord, size_t recordBytesRemaining,
                                                      uint32_t materialDispatchValue, uint16_t countAndFlags,
                                                      uint16_t materialIndex, uint8_t materialFlags, bool *carryOut) {
	SlipDraw3DPerspectiveDepthVisit depthVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPerspectiveDepth depthResult;
	const TrackViewMaterialHandler *dispatchTable;
	size_t dispatchTableCount;
	TrackViewMaterialHandler materialHandler;
	const uint16_t polygonCountAndFlags = (uint16_t)(countAndFlags & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK);
	uint8_t handlerIndex;
	bool extendedMaterial;

	if (context == NULL || primitiveContext == NULL || primitiveRecord == NULL ||
	    recordBytesRemaining < SLIP_TRC_PRIMITIVE_HEADER_BYTES || carryOut == NULL) {
		return false;
	}

	if (context->projectState == NULL ||
	    !SlipDraw3D_PerspectiveDepth(context->vertexRecords, context->vertexRecordCount,
	                                 primitiveRecord + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET,
	                                 recordBytesRemaining - SLIP_TRC_PRIMITIVE_HEADER_BYTES, polygonCountAndFlags,
	                                 context->projectState->inverseProjectionScale, TrackView_TransformVertex,
	                                 primitiveContext, depthVisits, sizeof(depthVisits) / sizeof(depthVisits[0]),
	                                 &depthResult)) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_EXTENDED_MATERIAL_DISPATCH;
		context->failed = true;
		return false;
	}

	extendedMaterial = (materialFlags & SLIP_TRC_MATERIAL_EXTENDED) != 0;
	handlerIndex = materialFlags & (extendedMaterial ? SLIP_TRC_MATERIAL_EXTENDED_INDEX_MASK : UINT8_MAX);
	if (extendedMaterial) {
		dispatchTable = kTrackViewExtendedMaterialHandlers;
		dispatchTableCount = sizeof(kTrackViewExtendedMaterialHandlers) / sizeof(kTrackViewExtendedMaterialHandlers[0]);
	} else {
		dispatchTable = kTrackViewStandardMaterialHandlers;
		dispatchTableCount = sizeof(kTrackViewStandardMaterialHandlers) / sizeof(kTrackViewStandardMaterialHandlers[0]);
	}
	if (handlerIndex >= dispatchTableCount) {
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_EXTENDED_MATERIAL_DISPATCH;
		context->failed = true;
		return false;
	}

	context->materialDispatchState = materialDispatchValue;
	context->materialDispatchMaterialIndex = materialIndex;
	context->materialDispatchDepth = depthResult.fadeDepth;

	materialHandler = dispatchTable[handlerIndex];
	if (g_vehicleViewDumpDiagnostics) {
		fprintf(stderr,
		        "track_view_material_dispatch_0003f2c8 eax=0x%08x mat=0x%02x dx=0x%04x depth=0x%08x "
		        "table=%s index=%u\n",
		        materialDispatchValue, materialFlags, materialIndex, depthResult.fadeDepth,
		        extendedMaterial ? "extended" : "normal", handlerIndex);
	}
	if (materialHandler == NULL) {
		fprintf(stderr, "track_view_material_dispatch_0003f2c8 unported material=0x%02x\n", materialFlags);
		context->failureAddress = TRACK_VIEW_DIAGNOSTIC_EXTENDED_MATERIAL_DISPATCH;
		context->failed = true;
		return false;
	}
	return materialHandler(context, primitiveContext, primitiveRecord + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET,
	                       recordBytesRemaining - SLIP_TRC_PRIMITIVE_HEADER_BYTES, materialIndex, depthResult.fadeDepth,
	                       carryOut);
}

SlipView3DVec32 TrackView_ChunkSourcePoint(int16_t sourceX, int16_t sourceY, int16_t sourceZ, void *userData) {
	const TrackViewChunkCallbackContext *const context = (const TrackViewChunkCallbackContext *)userData;

	if (context == NULL) {
		return (SlipView3DVec32){0, 0, 0};
	}
	return SlipTrackWorld_SourceChunkPoint((uint16_t)sourceX, (uint16_t)sourceY, (uint16_t)sourceZ,
	                                       context->currentChunkOrigin);
}

SlipDraw3DVec32 TrackView_ChunkTransformPoint(uint32_t sourceX, uint32_t sourceY, uint32_t sourceZ,
                                              SlipDraw3DVertexRecord *record, void *userData) {
	const TrackViewChunkCallbackContext *const context = (const TrackViewChunkCallbackContext *)userData;
	SlipView3DVec32 transformed;

	(void)record;
	if (context == NULL ||
	    !SlipTrackWorld_TransformPoint((SlipView3DVec32){(int32_t)sourceX, (int32_t)sourceY, (int32_t)sourceZ},
	                                   context->viewMatrix, context->offset, &transformed)) {
		return (SlipDraw3DVec32){0, 0, 0};
	}
	return (SlipDraw3DVec32){transformed.x, transformed.y, transformed.z};
}

bool TrackView_RawBspClassify(const uint8_t *bspNode, uint16_t planeVertexIndex, uint16_t normalX, uint16_t normalY,
                              uint16_t normalZ, void *userData, bool *planeRejected) {
	TrackViewRawBspContext *const context = (TrackViewRawBspContext *)userData;
	SlipTrackWorldPlaneClassify classify;

	(void)bspNode;
	if (context == NULL || planeRejected == NULL ||
	    !SlipTrackWorld_ClassifyPlaneFromSource(
	        context->mode, planeVertexIndex, normalX, normalY, normalZ, (const uint8_t *)context->vertexRecords,
	        context->vertexRecordCount * sizeof(context->vertexRecords[0]), context->origin, TrackView_ChunkSourcePoint,
	        &context->chunkCallbacks, context->drawStateRecord != NULL ? &context->drawStateRecord->matrix : NULL,
	        &classify)) {
		if (context != NULL) {
			context->failed = true;
		}
		return false;
	}
	*planeRejected = classify.carry;
	return true;
}

static bool TrackView_DispatchRecord(const uint8_t *bspNode, uint32_t recordPayload, uint16_t recordKind,
                                     int32_t planeSide, void *userData, bool applyMembership, bool countRawCallback) {
	TrackViewRawBspContext *const context = (TrackViewRawBspContext *)userData;
	SlipTrackWorldDeferredMembership membership;
	SlipTrackWorldChunkDispatch chunkDispatch;
	uint16_t recordOffset;
	uint32_t recordToken;
	bool trackWorldDeferredMembershipCarry;

	(void)bspNode;
	if (context == NULL) {
		return false;
	}
	recordOffset = (uint16_t)recordPayload;
	recordToken = context->chunkBaseToken + recordOffset;
	trackWorldDeferredMembershipCarry = false;
	if (applyMembership) {
		if (!SlipTrackWorld_DeferredMembership(context->deferredList, context->deferredListBytes, recordToken,
		                                       context->deferredScan, context->deferredScanBytes, &membership)) {
			context->failed = true;
			return false;
		}
		trackWorldDeferredMembershipCarry = membership.setsCarry;
	}
	if (!SlipTrackWorld_ChunkDispatch(recordKind, recordOffset, context->chunkBase, context->chunkBaseBytes,
	                                  trackWorldDeferredMembershipCarry, &chunkDispatch)) {
		context->failed = true;
		return false;
	}
	if (countRawCallback) {
		++context->callbackCount;
	}
	if (g_vehicleViewDumpDiagnostics) {
		fprintf(stderr,
		        "TrackView_RawBspCallback_000378f0 index=%u record=0x%04x bx=0x%04x ecx=%d deferred=%u call33e5c=%u "
		        "call37819=%u\n",
		        context->callbackCount, (unsigned)recordOffset, (unsigned)recordKind, planeSide,
		        trackWorldDeferredMembershipCarry ? 1u : 0u, chunkDispatch.callTraversalCallback ? 1u : 0u,
		        chunkDispatch.callRecordCallback ? 1u : 0u);
	}
	if (!context->haveFirstCallback) {
		context->firstCallbackRecordOffset = recordPayload;
		context->firstCallbackRecordKind = recordKind;
		context->firstCallbackPlaneSide = planeSide;
		context->firstChunkDispatch = chunkDispatch;
		context->haveFirstCallback = true;
	}
	if (chunkDispatch.callTraversalCallback) {
		++context->traversalCallbackCount;
		if (context->traversalCallback == SLIP_TRACK_WORLD_TRAVERSAL_CALLBACK_COMPONENT) {
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_COMPONENT_TRAVERSAL_CALLBACK;
			SlipTrackWorldComponentGate primitiveCallbackGate;
			SlipDraw3DProjectIndex project;
			SlipTrackWorldCullBounds cullBounds;
			SlipTrackWorldComponentProject projectedPrimitiveVertex;
			SlipTrackWorldComponentSetup setup;
			SlipTrackWorldComponentTailVisit tailVisits[SLIP_COMPONENT_TAIL_VISIT_CAPACITY];
			SlipTrackWorldComponentTail tail;
			SlipDraw3DBuildVertexRecords buildVertices;
			SlipTrackWorldPrimitiveWalkerVisit primitiveVisits[SLIP_PRIMITIVE_WALKER_VISIT_CAPACITY];
			SlipTrackWorldPrimitiveWalker primitiveWalker;
			size_t tailVisitCount = 0;
			const size_t recordOffset = (size_t)(chunkDispatch.callbackRecord - context->chunkBase);
			bool trackWorldCullBoundsCarry;

			if (!SlipTrackWorld_ComponentGate(chunkDispatch.callbackRecord, context->chunkBaseBytes - recordOffset,
			                                  context->componentBase, context->componentBaseBytes,
			                                  context->componentBaseToken, recordPayload, context->mask,
			                                  &primitiveCallbackGate)) {
				context->failed = true;
				return false;
			}
			++context->componentGateCount;
			if (!context->haveFirstComponentGate) {
				context->firstComponentGate = primitiveCallbackGate;
				context->haveFirstComponentGate = true;
			}
			if (primitiveCallbackGate.branch != SLIP_TRACK_WORLD_COMPONENT_BRANCH_VISIBLE) {
				return true;
			}
			if (!SlipDraw3D_ProjectIndex(context->vertexRecords, context->vertexRecordCount,
			                             primitiveCallbackGate.sourcePointIndex, TrackView_ChunkTransformPoint,
			                             &context->chunkCallbacks, &project)) {
				context->failed = true;
				return false;
			}
			++context->projectedIndexCount;
			if (!context->haveFirstProjectIndex) {
				context->firstProjectedVertexIndex = primitiveCallbackGate.sourcePointIndex;
				context->firstProjectIndexWorld = project.world;
				context->haveFirstProjectIndex = true;
			}
			if (!SlipTrackWorld_CullBounds(primitiveCallbackGate.componentRecord,
			                               context->componentBaseBytes -
			                                   (size_t)(primitiveCallbackGate.componentRecord - context->componentBase),
			                               context->chunkCallbacks.viewMatrix,
			                               (SlipView3DVec32){project.world.x, project.world.y, project.world.z},
			                               (int32_t)context->minDepth, context->frustum.maxZ,
			                               TrackView_SphereCullCallback, TrackView_ProjectMaskCallback,
			                               &context->frustum, &cullBounds)) {
				context->failed = true;
				return false;
			}
			++context->componentCullCount;
			trackWorldCullBoundsCarry = cullBounds.branch != SLIP_TRACK_WORLD_CULL_BRANCH_VISIBLE;
			if (g_vehicleViewDumpDiagnostics) {
				fprintf(stderr,
				        "track_view_component_cull_00036695 leaf=0x%zx component=0x%zx world=%d,%d,%d radius=%d "
				        "branch=%u reject=%u mask=0x%08x sphere_reject=%u\n",
				        recordOffset, (size_t)(primitiveCallbackGate.componentRecord - context->componentBase),
				        project.world.x, project.world.y, project.world.z, cullBounds.radius,
				        (unsigned)cullBounds.branch, trackWorldCullBoundsCarry ? 1u : 0u, cullBounds.mask,
				        cullBounds.trackWorldIndirectCullCarry ? 1u : 0u);
			}
			if (trackWorldCullBoundsCarry) {
				++context->componentCullRejectCount;
			}
			if (!SlipTrackWorld_ComponentProject(chunkDispatch.callbackRecord, context->chunkBaseBytes - recordOffset,
			                                     (SlipView3DVec32){project.world.x, project.world.y, project.world.z},
			                                     trackWorldCullBoundsCarry, &projectedPrimitiveVertex)) {
				context->failed = true;
				return false;
			}
			++context->componentProjectCount;
			if (!context->haveFirstComponentProject) {
				context->firstComponentProject = projectedPrimitiveVertex;
				context->haveFirstComponentProject = true;
			}
			if (projectedPrimitiveVertex.branch != SLIP_TRACK_WORLD_COMPONENT_BRANCH_PROJECTED) {
				return true;
			}
			context->componentViewOrigin = projectedPrimitiveVertex.viewPosition;
			const SlipTrackWorldComponentRefuelCalls refuelCalls = {context, TrackView_BuildRefuelBeams,
			                                                        TrackView_StepEffectRandom};
			if (!SlipTrackWorld_ComponentSetup(
			        chunkDispatch.callbackRecord, context->chunkBaseBytes - recordOffset,
			        (uint32_t)cullBounds.lastPoint.x, (uint32_t)projectedPrimitiveVertex.viewPosition.z,
			        context->rendererFlags, context->textureMode, context->shading, context->componentDistance,
			        context->shadingSecondary, context->componentRadius, context->specialRecord,
			        TrackView_refuelBeams.active, TrackView_effectRandomState, context->shadows,
			        context->processedComponentCount, context->recordIndex, &refuelCalls, &setup)) {
				context->failed = true;
				return false;
			}
			++context->componentSetupCount;
			if (g_vehicleViewDumpDiagnostics) {
				fprintf(
				    stderr,
				    "track_view_component_setup_00039aca record=0x%zx in_flags=0x%08x out_flags=0x%08x ecx=0x%08x "
				    "gates=%08x,%08x thresholds=%d,%d child=0x%04x store=0x%04x\n",
				    recordOffset, context->rendererFlags, setup.drawFlags.rendererFlags, setup.componentViewZ,
				    context->shading, context->shadingSecondary, context->componentDistance, context->componentRadius,
				    SlipBytes_ReadLE16(chunkDispatch.callbackRecord + SLIP_TRD_SECTION_COMPONENT_OFFSET), setup.shade);
			}
			context->rendererFlags = setup.drawFlags.rendererFlags;
			context->processedComponentCount = setup.processedComponentCount;
			if (!TrackView_ApplyLoadedDrawState(context, setup.drawStateIndexAfter)) {
				context->failureAddress = TRACK_VIEW_DIAGNOSTIC_LOAD_DRAW_STATE;
				context->failed = true;
				return false;
			}
			if (!context->haveFirstComponentSetup) {
				context->firstComponentSetup = setup;
				context->haveFirstComponentSetup = true;
			}
			if (!SlipTrackWorld_ComponentTail(
			        primitiveCallbackGate.componentRecord,
			        context->componentBaseBytes -
			            (size_t)(primitiveCallbackGate.componentRecord - context->componentBase),
			        context->componentBase, context->componentBaseBytes, setup.shade, context->rendererFlags,
			        context->linkedComponentDrawArgument, setup.currentRecord, context->ambientLightScaleQ14,
			        context->scaledLightX, context->scaledLightY, context->scaledLightZ, context->directLightScaleQ14,
			        context->renderContextCount, context->primitiveCallback, context->recordIndex, tailVisits,
			        sizeof(tailVisits) / sizeof(tailVisits[0]), &tailVisitCount, &tail)) {
				context->failed = true;
				return false;
			}
			++context->componentTailCount;
			if (tail.callTrackWorldPrimitiveWalker) {
				++context->componentTailChildListCount;
			}
			context->componentTailNestedVisitCount += (uint32_t)tailVisitCount;
			if (!context->haveFirstComponentTail) {
				context->firstComponentTail = tail;
				context->haveFirstComponentTail = true;
			}
			if (tail.callBuildVertexRecords) {
				size_t childVertexSourceOffset;

				if (tail.vertexSource < context->componentBase ||
				    tail.vertexSource > context->componentBase + context->componentBaseBytes) {
					context->failed = true;
					return false;
				}
				childVertexSourceOffset = (size_t)(tail.vertexSource - context->componentBase);
				if (g_vehicleViewDumpDiagnostics) {
					const uint8_t *const vertexSource = tail.vertexSource;
					const size_t sourceBytes = context->componentBaseBytes - childVertexSourceOffset;

					fprintf(stderr,
					        "track_view_build_vertices_0001dcf7 source_offset=0x%zx count=%u stride=%u "
					        "bytes=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
					        childVertexSourceOffset, (uint32_t)tail.vertexCount,
					        (uint32_t)(uint16_t)tail.vertexSourceStride, sourceBytes > 0u ? vertexSource[0] : 0,
					        sourceBytes > 1u ? vertexSource[1] : 0, sourceBytes > 2u ? vertexSource[2] : 0,
					        sourceBytes > 3u ? vertexSource[3] : 0, sourceBytes > 4u ? vertexSource[4] : 0,
					        sourceBytes > 5u ? vertexSource[5] : 0, sourceBytes > 6u ? vertexSource[6] : 0,
					        sourceBytes > 7u ? vertexSource[7] : 0, sourceBytes > 8u ? vertexSource[8] : 0,
					        sourceBytes > 9u ? vertexSource[9] : 0, sourceBytes > 10u ? vertexSource[10] : 0,
					        sourceBytes > 11u ? vertexSource[11] : 0);
				}
				if (!TrackView_BuildVertexRecords(context, TRACK_VIEW_DIAGNOSTIC_COMPONENT_BUILD_VERTEX_RECORDS,
				                                  tail.vertexSource,
				                                  context->componentBaseBytes - childVertexSourceOffset,
				                                  tail.vertexCount, (int16_t)tail.vertexSourceStride,
				                                  TrackView_TransformVertex, TrackView_SourcePoint, &buildVertices)) {
					context->failed = true;
					return false;
				}
				++context->componentTailVertexBuildCount;
			}
			if (!TrackView_ApplyComponentLight(context, &tail)) {
				context->failed = true;
				return false;
			}
			if (tail.callTrackWorldPrimitiveWalker) {
				if (!SlipTrackWorld_PrimitiveWalker(
				        primitiveCallbackGate.componentRecord,
				        context->componentBaseBytes -
				            (size_t)(primitiveCallbackGate.componentRecord - context->componentBase),
				        context->componentBase, context->componentBaseBytes, setup.replayCount, primitiveVisits,
				        sizeof(primitiveVisits) / sizeof(primitiveVisits[0]), &primitiveWalker)) {
					context->failed = true;
					return false;
				}
				++context->primitiveWalkerCount;
				if (primitiveWalker.directCallbackBranch) {
					SlipTrackWorldDirectCallbackVisit directVisits[SLIP_DIRECT_CALLBACK_VISIT_CAPACITY];
					SlipTrackWorldDirectCallbackLoop directLoop;
					SlipTrackWorldDirectCallbackEnvironment directEnvironment = {
					    &setup, &tail, &primitiveWalker, projectedPrimitiveVertex.viewPosition, false};
					size_t directListOffset;
					uint16_t directCount;

					++context->primitiveWalkerDirectBranchCount;

					if ((size_t)(primitiveCallbackGate.componentRecord - context->componentBase) >
					        context->componentBaseBytes ||
					    context->componentBaseBytes -
					            (size_t)(primitiveCallbackGate.componentRecord - context->componentBase) <
					        SLIP_TRC_COMPONENT_PRIMITIVE_LIST_END) {
						context->failureAddress = TRACK_VIEW_DIAGNOSTIC_DIRECT_PRIMITIVE_LOOP;
						context->failed = true;
						return false;
					}
					directListOffset =
					    (size_t)SlipBytes_ReadLE16(primitiveCallbackGate.componentRecord + SLIP_TRACK_LINKED_OFFSET);
					if (directListOffset > context->componentBaseBytes ||
					    context->componentBaseBytes - directListOffset < SLIP_TRC_TABLE_COUNT_BYTES) {
						context->failureAddress = TRACK_VIEW_DIAGNOSTIC_DIRECT_PRIMITIVE_LOOP;
						context->failed = true;
						return false;
					}
					directCount = SlipBytes_ReadLE16(context->componentBase + directListOffset);
					context->directCallbackRecordCount = directCount;
					context->directCallbackActiveIndex = 0;
					memset(directVisits, 0, sizeof(directVisits));
					if (directCount > sizeof(directVisits) / sizeof(directVisits[0]) ||
					    !SlipTrackWorld_DirectCallbackLoop(
					        primitiveCallbackGate.componentRecord,
					        context->componentBaseBytes -
					            (size_t)(primitiveCallbackGate.componentRecord - context->componentBase),
					        context->componentBase, context->componentBaseBytes, context->primitiveCallback,
					        context->scaledLightX, TrackView_DirectCallback, context, &directEnvironment, NULL, 0,
					        directVisits, sizeof(directVisits) / sizeof(directVisits[0]), &directLoop)) {
						context->failureAddress = TRACK_VIEW_DIAGNOSTIC_DIRECT_PRIMITIVE_LOOP;
						context->failed = true;
						return false;
					}
					context->directCallbackCount += (uint32_t)directLoop.visitCount;
				} else {

					SlipTrackWorldDirectCallbackEnvironment inlineEnvironment = {
					    &setup, &tail, &primitiveWalker, projectedPrimitiveVertex.viewPosition, true};
					uint32_t inlineCallbackValue = context->scaledLightX;
					size_t inlineVisitIndex;

					for (inlineVisitIndex = 0; inlineVisitIndex < primitiveWalker.visitCount &&
					                           inlineVisitIndex < sizeof(primitiveVisits) / sizeof(primitiveVisits[0]);
					     ++inlineVisitIndex) {
						const SlipTrackWorldPrimitiveWalkerVisit *const inlineVisit =
						    &primitiveVisits[inlineVisitIndex];
						bool inlineCallbackRejected = false;

						if (inlineVisit->recordOffset >= context->componentBaseBytes) {
							context->failed = true;
							return false;
						}
						if (!TrackView_DirectCallback(context->componentBase + inlineVisit->recordOffset,
						                              context->componentBaseBytes - inlineVisit->recordOffset,
						                              inlineVisit->recordOffset, &inlineEnvironment,
						                              inlineCallbackValue, context, &inlineCallbackValue,
						                              &inlineCallbackRejected)) {
							return false;
						}
					}
				}
				if (!context->haveFirstPrimitiveWalker) {
					context->firstPrimitiveWalker = primitiveWalker;
					context->haveFirstPrimitiveWalker = true;
				}

				if (!TrackView_DrawComponentActors(setup.attachmentListOffset, setup.currentRecord,
				                                   setup.drawFlags.rendererFlags, projectedPrimitiveVertex.viewPosition,
				                                   context)) {
					context->failureAddress = TRACK_VIEW_DIAGNOSTIC_DRAW_COMPONENT_ACTORS;
					context->failed = true;
					return false;
				}
			}

			if (!TrackView_RestoreComponentLight(context, &tail)) {
				context->failed = true;
				return false;
			}

			if (tailVisitCount > 0) {
				SlipTrackWorldDirectCallbackEnvironment tailEnvironment = {&setup, &tail, NULL,
				                                                           projectedPrimitiveVertex.viewPosition};
				uint32_t tailCallbackValue = context->scaledLightX;
				size_t tailVisitIndex;

				for (tailVisitIndex = 0; tailVisitIndex < tailVisitCount; ++tailVisitIndex) {
					const SlipTrackWorldComponentTailVisit *const tailVisit = &tailVisits[tailVisitIndex];
					size_t tailVisitOffset;
					bool tailCallbackRejected = false;

					if (tailVisit->primitiveRecord < context->componentBase ||
					    tailVisit->primitiveRecord >= context->componentBase + context->componentBaseBytes) {
						context->failed = true;
						return false;
					}
					tailVisitOffset = (size_t)(tailVisit->primitiveRecord - context->componentBase);
					if (!TrackView_DirectCallback(
					        tailVisit->primitiveRecord, context->componentBaseBytes - tailVisitOffset, tailVisitOffset,
					        &tailEnvironment, tailCallbackValue, context, &tailCallbackValue, &tailCallbackRejected)) {
						return false;
					}
				}
			}

			if (tail.calledRestoreVertexBufferCursor &&
			    !TrackView_RestoreVertexBufferCursor(context, TRACK_VIEW_DIAGNOSTIC_COMPONENT_RESTORE_VERTEX_CURSOR)) {
				context->failureAddress = TRACK_VIEW_DIAGNOSTIC_RESTORE_VERTEX_CURSOR;
				context->failed = true;
				return false;
			}
			if (!TrackView_ApplyLoadedDrawState(context, tail.drawStateIndexAfter)) {
				context->failureAddress = TRACK_VIEW_DIAGNOSTIC_LOAD_DRAW_STATE;
				context->failed = true;
				return false;
			}
			return true;
		}
		if (context->traversalCallback != SLIP_TRACK_WORLD_TRAVERSAL_CALLBACK_DEFERRED_GATE) {
			++context->unsupportedTraversalCallbackCount;
			return true;
		}
		SlipDraw3DProjectIndex project;
		SlipTrackWorldDeferredCallbackGate deferredGateTrace;
		SlipTrackWorldDeferredContinuationGate continuation;
		SlipTrackWorldCullBounds cullBounds;
		SlipTrackWorldDeferredCullGate cullGate;
		SlipTrackWorldDeferredEntryWrite entryWrite;
		SlipTrackWorldDeferredExistingEntry existingEntry;
		SlipTrackWorldDeferredAppendTail appendTail;
		TrackViewRawBspGateTrace *trace = NULL;
		const uint32_t deferredCountBefore = *(const uint32_t *)(const void *)context->deferredList;
		const uint32_t objectCountBefore = *(const uint32_t *)(const void *)context->objectList;
		const uint32_t deferredEntryActiveBefore = context->deferredEntryActive;
		size_t recordOffset;

		recordOffset = (size_t)(chunkDispatch.callbackRecord - context->chunkBase);
		if (!SlipDraw3D_ProjectIndex(context->vertexRecords, context->vertexRecordCount,
		                             SlipBytes_ReadLE16(chunkDispatch.callbackRecord), TrackView_ChunkTransformPoint,
		                             &context->chunkCallbacks, &project)) {
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_CHUNK_PROJECT_INDEX;
			context->failed = true;
			return false;
		}
		if (!SlipTrackWorld_DeferredCallbackGate(
		        chunkDispatch.callbackRecord, context->chunkBaseBytes - recordOffset, recordToken,
		        (SlipView3DVec32){project.world.x, project.world.y, project.world.z}, context->objectList,
		        context->objectListBytes, context->deferredEntryActive, context->defaultTraversalGate, context->mask,
		        context->componentBaseToken, context->componentBase, context->componentBaseBytes,
		        context->renderContextCount, &deferredGateTrace)) {
			context->failureAddress = TRACK_VIEW_DIAGNOSTIC_DEFERRED_CALLBACK_GATE;
			context->failed = true;
			return false;
		}
		++context->projectedIndexCount;
		++context->deferredGateCount;
		if (context->gateTraceCount < sizeof(context->gateTrace) / sizeof(context->gateTrace[0])) {
			trace = &context->gateTrace[context->gateTraceCount++];
			*trace = (TrackViewRawBspGateTrace){
			    context->callbackCount,
			    recordToken,
			    recordOffset,
			    recordKind,
			    planeSide,
			    objectCountBefore,
			    deferredCountBefore,
			    deferredEntryActiveBefore,
			    context->deferredEntryActive,
			    deferredGateTrace.matchedEntry != NULL
			        ? ((const SlipTrackVisibilityEntry *)(const void *)deferredGateTrace.matchedEntry)->recordAddress
			        : 0,
			    *(const uint32_t *)(const void *)context->deferredList,
			    deferredGateTrace.branch,
			    deferredGateTrace.recordFound,
			    false};
		}
		if (deferredGateTrace.zeroCount) {
			++context->deferredGateZeroCount;
		}
		if (deferredGateTrace.branch == SLIP_TRACK_WORLD_DEFERRED_CALLBACK_GATE_BRANCH_NEW_ENTRY) {
			uint16_t componentOffset;
			const uint8_t *componentRecord;
			uint32_t objectCountBefore;
			size_t deferredEntryOffset;
			uint8_t *deferredEntry;
			uint32_t deferredEntryAddress;
			bool trackWorldCullBoundsCarry;

			++context->deferredGateContinue;
			if (!SlipTrackWorld_DeferredContinuationGate(
			        context->mask, context->objectList, context->objectListBytes, context->primaryLeft,
			        context->primaryTop, context->primaryRight, context->primaryBottom, &continuation)) {
				context->failed = true;
				return false;
			}
			if (continuation.branch != SLIP_TRACK_WORLD_DEFERRED_CONTINUATION_GATE_BRANCH_CULL) {
				if (!context->haveFirstProjectIndex) {
					context->firstProjectedVertexIndex = deferredGateTrace.recordCoordinateOffset;
					context->firstProjectIndexWorld = project.world;
					context->haveFirstProjectIndex = true;
				}
				if (!context->haveFirstGate) {
					context->firstGate = deferredGateTrace;
					context->haveFirstGate = true;
				}
				return true;
			}
			++context->deferredContinuationCount;
			objectCountBefore = *(const uint32_t *)(const void *)context->objectList;
			deferredEntryOffset =
			    SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES + (size_t)objectCountBefore * SLIP_TRACK_VISIBILITY_ENTRY_BYTES;
			if (deferredEntryOffset > context->objectListBytes ||
			    context->objectListBytes - deferredEntryOffset < SLIP_TRACK_VISIBILITY_ENTRY_BYTES ||
			    recordOffset > context->chunkBaseBytes || context->chunkBaseBytes - recordOffset < sizeof(uint32_t)) {
				context->failed = true;
				return false;
			}

			if (!TrackView_StoreClipBounds(context, context->primaryLeft, context->primaryTop, context->primaryRight,
			                               context->primaryBottom)) {
				context->failed = true;
				return false;
			}
			componentOffset = SlipBytes_ReadLE16(chunkDispatch.callbackRecord + SLIP_TRD_SECTION_COMPONENT_OFFSET);
			if ((size_t)componentOffset > context->componentBaseBytes) {
				context->failed = true;
				return false;
			}
			componentRecord = context->componentBase + componentOffset;
			if (!SlipTrackWorld_CullBounds(componentRecord, context->componentBaseBytes - (size_t)componentOffset,
			                               context->chunkCallbacks.viewMatrix, deferredGateTrace.viewPosition,
			                               (int32_t)context->minDepth, context->frustum.maxZ,
			                               TrackView_SphereCullCallback, TrackView_ProjectMaskCallback,
			                               &context->frustum, &cullBounds)) {
				context->failed = true;
				return false;
			}
			trackWorldCullBoundsCarry = cullBounds.branch != SLIP_TRACK_WORLD_CULL_BRANCH_VISIBLE;

			if (!TrackView_StoreClipBounds(context, context->viewportMinX, context->viewportMinY, context->viewportMaxX,
			                               context->viewportMaxY)) {
				context->failed = true;
				return false;
			}
			if (!SlipTrackWorld_DeferredCullGate(
			        chunkDispatch.callbackRecord, context->chunkBaseBytes - recordOffset, context->componentBaseToken,
			        context->componentBase, context->componentBaseBytes, context->objectList + deferredEntryOffset,
			        context->savedMaximumDepth, trackWorldCullBoundsCarry, context->viewportMinX, context->viewportMinY,
			        context->viewportMaxX, context->viewportMaxY, &cullGate)) {
				context->failed = true;
				return false;
			}
			++context->deferredCullCount;
			if (cullGate.branch == SLIP_TRACK_WORLD_DEFERRED_CULL_GATE_BRANCH_REJECTED) {
				++context->deferredCullRejectCount;
			} else {
				deferredEntry = context->objectList + deferredEntryOffset;
				deferredEntryAddress = context->objectListBaseToken + (uint32_t)deferredEntryOffset;
				if (!SlipTrackWorld_DeferredEntryWrite(
				        deferredEntry, context->objectListBytes - deferredEntryOffset, context->objectList,
				        context->objectListBytes, (uint32_t)deferredGateTrace.viewPosition.x,
				        (uint32_t)deferredGateTrace.viewPosition.y, (uint32_t)deferredGateTrace.viewPosition.z,
				        recordToken, context->primaryLeft, context->primaryTop, context->primaryRight,
				        context->primaryBottom, deferredEntryAddress, &entryWrite) ||
				    !SlipTrackWorld_DeferredAppendTail(context->deferredList, context->deferredListBytes,
				                                       context->deferredScanBaseToken, context->deferredScan,
				                                       context->deferredScanBytes, deferredEntryAddress, recordToken,
				                                       &appendTail)) {
					context->failed = true;
					return false;
				}
				++context->deferredEntryWriteCount;
				++context->deferredAppendCount;
				if (trace != NULL) {
					trace->deferredCountAfter = *(const uint32_t *)(const void *)context->deferredList;
					trace->appended = true;
				}
			}
		} else if (deferredGateTrace.branch == SLIP_TRACK_WORLD_DEFERRED_CALLBACK_GATE_BRANCH_EXISTING_DIRECT ||
		           deferredGateTrace.branch == SLIP_TRACK_WORLD_DEFERRED_CALLBACK_GATE_BRANCH_EXISTING_CONTINUATION) {
			uint8_t *matchedEntry;
			size_t matchedEntryOffset;
			uint32_t matchedEntryAddress;
			bool enter = deferredGateTrace.branch == SLIP_TRACK_WORLD_DEFERRED_CALLBACK_GATE_BRANCH_EXISTING_DIRECT;
			bool trackWorldCullBoundsCarry = false;

			if (deferredGateTrace.matchedEntry < context->objectList ||
			    deferredGateTrace.matchedEntry > context->objectList + context->objectListBytes) {
				context->failed = true;
				return false;
			}
			matchedEntryOffset = (size_t)(deferredGateTrace.matchedEntry - context->objectList);
			if (matchedEntryOffset > context->objectListBytes ||
			    context->objectListBytes - matchedEntryOffset < SLIP_TRACK_VISIBILITY_ENTRY_BYTES) {
				context->failed = true;
				return false;
			}
			matchedEntry = context->objectList + matchedEntryOffset;
			matchedEntryAddress = context->objectListBaseToken + (uint32_t)matchedEntryOffset;
			if (((const SlipTrackVisibilityEntry *)(const void *)matchedEntry)->useClipBounds == 0 &&
			    context->renderContextCount != 0) {
				uint16_t componentOffset;
				const uint8_t *componentRecord;

				if (!TrackView_StoreClipBounds(context, context->primaryLeft, context->primaryTop,
				                               context->primaryRight, context->primaryBottom)) {
					context->failed = true;
					return false;
				}
				componentOffset = SlipBytes_ReadLE16(chunkDispatch.callbackRecord + SLIP_TRD_SECTION_COMPONENT_OFFSET);
				if ((size_t)componentOffset > context->componentBaseBytes) {
					context->failed = true;
					return false;
				}
				componentRecord = context->componentBase + componentOffset;
				if (!SlipTrackWorld_CullBounds(componentRecord, context->componentBaseBytes - (size_t)componentOffset,
				                               context->chunkCallbacks.viewMatrix, deferredGateTrace.viewPosition,
				                               (int32_t)context->minDepth, context->frustum.maxZ,
				                               TrackView_SphereCullCallback, TrackView_ProjectMaskCallback,
				                               &context->frustum, &cullBounds)) {
					context->failed = true;
					return false;
				}
				trackWorldCullBoundsCarry = cullBounds.branch != SLIP_TRACK_WORLD_CULL_BRANCH_VISIBLE;

				if (!TrackView_StoreClipBounds(context, context->viewportMinX, context->viewportMinY,
				                               context->viewportMaxX, context->viewportMaxY)) {
					context->failed = true;
					return false;
				}
			}
			if (!SlipTrackWorld_DeferredExistingEntry(
			        enter, matchedEntry, context->objectListBytes - matchedEntryOffset, chunkDispatch.callbackRecord,
			        context->chunkBaseBytes - recordOffset, context->componentBaseToken, context->componentBase,
			        context->componentBaseBytes, context->objectList, context->objectListBytes,
			        context->renderContextCount, context->primaryLeft, context->primaryTop, context->primaryRight,
			        context->primaryBottom, trackWorldCullBoundsCarry, context->viewportMinX, context->viewportMinY,
			        context->viewportMaxX, context->viewportMaxY, &existingEntry)) {
				context->failed = true;
				return false;
			}
			if (enter) {
				context->deferredEntryActive = existingEntry.storedDeferredEntryActive;
				++context->deferredExistingDirectEntryCount;
				if (trace != NULL) {
					trace->deferredEntryActiveAfter = context->deferredEntryActive;
				}
			} else {
				++context->deferredExistingContinuationEntryCount;
			}
			if (existingEntry.branch == SLIP_TRACK_WORLD_DEFERRED_EXISTING_ENTRY_BRANCH_UNCLIPPED) {
				++context->deferredExistingClipBoundsCount;
			}

			if (!SlipTrackWorld_DeferredAppendTail(
			        context->deferredList, context->deferredListBytes, context->deferredScanBaseToken,
			        context->deferredScan, context->deferredScanBytes, matchedEntryAddress, recordToken, &appendTail)) {
				context->failed = true;
				return false;
			} else {
				++context->deferredAppendCount;
				if (trace != NULL) {
					trace->deferredCountAfter = *(const uint32_t *)(const void *)context->deferredList;
					trace->appended = true;
				}
			}
		}
		if (!context->haveFirstProjectIndex) {
			context->firstProjectedVertexIndex = deferredGateTrace.recordCoordinateOffset;
			context->firstProjectIndexWorld = project.world;
			context->haveFirstProjectIndex = true;
		}
		if (!context->haveFirstGate) {
			context->firstGate = deferredGateTrace;
			context->haveFirstGate = true;
		}
	}
	if (chunkDispatch.callRecordCallback) {
		SlipDraw3DProjectIndex project;
		size_t recordOffset;

		++context->recordCallbackCount;
		if (!SlipDraw3D_ProjectIndex(context->vertexRecords, context->vertexRecordCount, chunkDispatch.vertexIndex,
		                             TrackView_ChunkTransformPoint, &context->chunkCallbacks, &project)) {
			context->failed = true;
			return false;
		}
		++context->projectedIndexCount;
		if (!context->haveFirstProjectIndex) {
			context->firstProjectedVertexIndex = chunkDispatch.vertexIndex;
			context->firstProjectIndexWorld = project.world;
			context->haveFirstProjectIndex = true;
		}
		recordOffset = (size_t)(chunkDispatch.callbackRecord - context->chunkBase);
		if (context->recordCallback == SLIP_TRACK_WORLD_RECORD_CALLBACK_DEFERRED_HEADER) {
			SlipTrackWorldDeferredCallbackHeader header;

			++context->callbackHeaderCount;
			if (!SlipTrackWorld_DeferredCallbackHeader(
			        context->deferredEntryActive, context->renderContextCount, (uint32_t)project.world.x,
			        (uint32_t)project.world.y, (uint32_t)project.world.z, context->objectList, context->objectListBytes,
			        context->primaryLeft, context->primaryTop, context->primaryRight, context->primaryBottom,
			        &header)) {
				context->failed = true;
				return false;
			}
			if (header.branch == SLIP_TRACK_WORLD_DEFERRED_CALLBACK_HEADER_BRANCH_CULL) {
				SlipTrackWorldDeferredCallbackCull cull;
				bool trackWorldIndirectCullCarry;

				if (context->chunkBaseBytes - recordOffset < SLIP_TRACK_CALLBACK_CULL_RADIUS_END) {
					context->failed = true;
					return false;
				}
				++context->callbackHeaderContinueCount;
				trackWorldIndirectCullCarry = TrackView_SphereCullCallback(
				    (SlipView3DVec32){(int32_t)header.viewX, (int32_t)header.viewY, (int32_t)header.viewZ},
				    (int32_t)SlipBytes_ReadLE32(chunkDispatch.callbackRecord + SLIP_TRACK_CALLBACK_CULL_RADIUS_OFFSET),
				    &context->frustum);
				if (!SlipTrackWorld_DeferredCallbackCull(
				        chunkDispatch.callbackRecord, context->chunkBaseBytes - recordOffset,
				        context->savedMaximumDepth, header.viewX, header.viewY, header.viewZ,
				        trackWorldIndirectCullCarry, context->viewportMinX, context->viewportMinY,
				        context->viewportMaxX, context->viewportMaxY, &cull)) {
					context->failed = true;
					return false;
				}
				++context->callbackCullCount;
				if (cull.branch == SLIP_TRACK_WORLD_DEFERRED_CALLBACK_CULL_BRANCH_REJECTED) {
					++context->callbackCullRejectCount;
				} else {
					size_t deferredEntryOffset;
					uint32_t deferredEntryAddress;
					SlipTrackWorldDeferredCallbackWrite write;

					deferredEntryOffset = SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES +
					                      (size_t)(*(const uint32_t *)(const void *)context->objectList) *
					                          SLIP_TRACK_VISIBILITY_ENTRY_BYTES;
					if (deferredEntryOffset > context->objectListBytes ||
					    context->objectListBytes - deferredEntryOffset < SLIP_TRACK_VISIBILITY_ENTRY_BYTES) {
						context->failed = true;
						return false;
					}
					deferredEntryAddress = context->objectListBaseToken + (uint32_t)deferredEntryOffset;
					if (!SlipTrackWorld_DeferredCallbackWrite(
					        context->objectList + deferredEntryOffset, context->objectListBytes - deferredEntryOffset,
					        deferredEntryAddress, context->objectList, context->objectListBytes, context->deferredList,
					        context->deferredListBytes, context->deferredScanBaseToken, context->deferredScan,
					        context->deferredScanBytes, header.viewX, header.viewY, header.viewZ, recordToken,
					        context->primaryLeft, context->primaryTop, context->primaryRight, context->primaryBottom,
					        deferredEntryAddress, &write)) {
						context->failed = true;
						return false;
					}
					++context->callbackWriteCount;
				}
			}
		} else {
			if (!SlipTrackWorld_RecordVisibility(
			        chunkDispatch.callbackRecord, context->chunkBaseBytes - recordOffset, (uint32_t)project.world.x,
			        (uint32_t)project.world.y, (uint32_t)project.world.z, context->mask, context->mode,
			        context->minDepth, context->detailThreshold, &context->firstRecordVisibility)) {
				context->failed = true;
				return false;
			}
			context->haveFirstRecordVisibility = true;
			if (context->firstRecordVisibility.continues) {

				SlipView3DMatrix objectMatrix;
				SlipView3DMatrix viewMatrix;
				SlipTrackWorldRecordTransformSetup transformSetup;
				uint32_t facingModeFlag = 0;
				const uint8_t *const record = chunkDispatch.callbackRecord;
				const size_t recordBytes = context->chunkBaseBytes - recordOffset;

				++context->continuedRecordTransformCount;
				memset(&objectMatrix, 0, sizeof(objectMatrix));
				memset(&viewMatrix, 0, sizeof(viewMatrix));
				/*
				 * The original has no bounds check here: a record within 0x3a
				 * bytes of the chunk end reads its tail from whatever follows
				 * the chunk in memory. The port cannot reproduce those bytes,
				 * so such a record is skipped rather than failing the frame.
				 */
				if (recordBytes < SLIP_TRK_SHAPE_WITH_FACING_BYTES) {
					++context->truncatedRecordTransformCount;
					return true;
				}
				if (!SlipTrackWorld_RecordTransformSetup(record, recordBytes, context->recordIndex, &transformSetup)) {
					context->failed = true;
					return false;
				}

				if (transformSetup.callLoadDrawState &&
				    !TrackView_ApplyLoadedDrawState(context, transformSetup.drawStateIndexAfterAdvance)) {
					context->failureAddress = TRACK_VIEW_DIAGNOSTIC_LOAD_DRAW_STATE;
					context->failed = true;
					return false;
				}
				if (transformSetup.branch == SLIP_TRACK_WORLD_RECORD_MATRIX) {
					SlipTrackWorldRecordFacingTransform facingTransform;

					if (!SlipTrackWorld_RecordFacingTransform(record, recordBytes, context->cameraWorldX,
					                                          context->cameraWorldZ, &objectMatrix, &viewMatrix,
					                                          context->chunkCallbacks.viewMatrix, &facingTransform)) {
						context->failed = true;
						return false;
					}
					facingModeFlag = facingTransform.facingModeFlag;
				} else {
					SlipTrackWorldRecordMatrixTransform matrixTransform;

					if (!SlipTrackWorld_RecordMatrixTransform(record, recordBytes, &objectMatrix, &viewMatrix,
					                                          context->chunkCallbacks.viewMatrix, &matrixTransform)) {
						context->failed = true;
						return false;
					}
					facingModeFlag = matrixTransform.facingModeFlag;
				}
				{
					SlipTrackWorldRecordScaledCenter scaledCenter;
					SlipTrackWorldRecordSphereCull sphereCull;

					if (!SlipTrackWorld_RecordScaledCenter(
					        &viewMatrix, transformSetup.cachedCenterY, context->firstRecordVisibility.viewPositionX,
					        context->firstRecordVisibility.viewPositionY, context->firstRecordVisibility.viewPositionZ,
					        transformSetup.savedPositionZ, transformSetup.savedPositionYWithCenter,
					        transformSetup.savedPositionX, &scaledCenter)) {
						context->failed = true;
						return false;
					}
					if (!SlipTrackWorld_RecordSphereCull(
					        (SlipView3DVec32){(int32_t)scaledCenter.drawSetup.translationX,
					                          (int32_t)scaledCenter.drawSetup.translationY,
					                          (int32_t)scaledCenter.drawSetup.translationZ},
					        context->firstRecordVisibility.cachedRadius, TrackView_SphereCullCallback,
					        (void *)&context->frustum, 0, scaledCenter.cachedViewCenterZ, &sphereCull)) {
						context->failed = true;
						return false;
					}
					if (sphereCull.branch != SLIP_TRACK_WORLD_RECORD_CULLED) {
						SlipTrackWorldDrawFlags drawFlags;
						SlipTrackWorldRecordDrawDispatch drawDispatch;

						if (!SlipTrackWorld_UpdateDrawFlags(context->rendererFlags, (int32_t)sphereCull.viewDepth,
						                                    context->textureMode, context->shading,
						                                    context->componentDistance, context->shadingSecondary,
						                                    context->componentRadius, &drawFlags)) {
							context->failed = true;
							return false;
						}

						context->rendererFlags = drawFlags.rendererFlags;
						if (!SlipTrackWorld_RecordDrawDispatch(context->mask, context->rendererFlags, facingModeFlag,
						                                       context->rendererFlags, context->frameRenderFlags,
						                                       &drawDispatch)) {
							context->failed = true;
							return false;
						}
						if (drawDispatch.callSetRenderFlagsAfterMask) {
							context->rendererFlags = drawDispatch.renderFlagsToStore;
						}
						if (drawDispatch.callDrawShape &&
						    !TrackViewDrawSceneryShape(context, record, recordBytes, &objectMatrix, &viewMatrix,
						                               (SlipView3DVec32){(int32_t)scaledCenter.drawSetup.translationX,
						                                                 (int32_t)scaledCenter.drawSetup.translationY,
						                                                 (int32_t)scaledCenter.drawSetup.translationZ},
						                               (SlipView3DVec32){(int32_t)scaledCenter.drawSetup.rotationX,
						                                                 (int32_t)scaledCenter.drawSetup.rotationY,
						                                                 (int32_t)scaledCenter.drawSetup.rotationZ})) {
							context->failed = true;
							return false;
						}

						context->rendererFlags = drawDispatch.restoredFrameRenderFlags;
					}
				}
				{
					SlipTrackWorldRecordDrawRestore drawRestore;

					if (!SlipTrackWorld_RecordDrawRestore(transformSetup.drawStateIndexAfterAdvance, &drawRestore)) {
						context->failed = true;
						return false;
					}

					if (drawRestore.callLoadDrawState &&
					    !TrackView_ApplyLoadedDrawState(context, drawRestore.drawStateIndexAfterRestore)) {
						context->failureAddress = TRACK_VIEW_DIAGNOSTIC_LOAD_DRAW_STATE;
						context->failed = true;
						return false;
					}
				}
			} else {
				++context->truncatedRecordTransformCount;
			}
		}
	}
	return true;
}

bool TrackView_RawBspCallback(const uint8_t *bspNode, uint32_t recordPayload, uint16_t recordKind, int32_t planeSide,
                              void *userData) {
	return TrackView_DispatchRecord(bspNode, recordPayload, recordKind, planeSide, userData, true, true);
}

bool TrackView_ChunkFallback(const uint8_t *chunkRecord, size_t chunkBytesRemaining, void *userData) {
	TrackViewRawBspContext *const context = (TrackViewRawBspContext *)userData;
	SlipTrackWorldChunkAlternatePaths block;
	size_t recordOffset;

	if (context == NULL || chunkRecord == NULL || chunkRecord < context->chunkBase ||
	    chunkRecord > context->chunkBase + context->chunkBaseBytes ||
	    !SlipTrackWorld_ChunkAlternatePaths(chunkRecord, chunkBytesRemaining, context->chunkBase,
	                                        context->chunkBaseBytes, false, &block)) {
		return false;
	}
	if (block.callRecordCallback) {
		recordOffset = (size_t)(block.recordCallbackRecord - context->chunkBase);

		return TrackView_DispatchRecord(chunkRecord, (uint32_t)recordOffset, SLIP_TRACK_BSP_RECORD_CALLBACK_KIND, 0,
		                                context, false, false);
	}
	if (block.callTraversalCallback) {
		recordOffset = (size_t)(block.traversalCallbackRecord - context->chunkBase);

		return TrackView_DispatchRecord(chunkRecord, (uint32_t)recordOffset, SLIP_TRACK_BSP_TRAVERSAL_CALLBACK_KIND, 0,
		                                context, true, false);
	}
	return true;
}

void TrackView_RenderSetDiagnostics(bool enabled) { g_vehicleViewDumpDiagnostics = enabled; }

bool TrackView_RenderDiagnosticsEnabled(void) { return g_vehicleViewDumpDiagnostics; }

typedef struct VehicleViewRenderContext {
	SlipShape3D shape;
	SlipShape3DPrimitive primitive;
	SlipDraw3DVertexRecord *vertexRecords;
	size_t vertexRecordCount;
	SlipView3DMatrix matrix;
	SlipView3DVec32 translation;
	SlipView3DVec32 bspOrigin;
	SlipView3DVec32 lightVector;
	uint16_t shapeScaleShift;
	SlipDraw3DProjectState projectState;
	const struct VehicleArtActor *actor;
	const char *const *archives;
	size_t archiveCount;
	const struct VehicleViewMaterialTable *materials;
	TrackViewRawBspContext *trackContext;
	uint32_t ambientLight;
	uint32_t directLight;
	int lod;
	int primitiveCount;
	int drawnCount;
	int bspCarrySetCount;
	int bspCarryClearCount;
	int clipScreenCount;
	int clipRejectCount;
	int clipDepthCount;
	int rasterRejectCount;
	int rejectNearCount;
	int rejectFarCount;
	int rejectLeftCount;
	int rejectRightCount;
	int rejectTopCount;
	int rejectBottomCount;
	int rejectDepthPlaneCount;
} VehicleViewRenderContext;

typedef struct VehicleViewMaterialTable {
	uint16_t count;
	uint8_t *expandedTable;
	size_t expandedTableBytes;
	uint16_t materialResourceHandle;
	char materialName[SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY][SLIP_MAT_NAME_BYTES + 1];
	int32_t rampStart[SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY];
	int32_t rampEnd[SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY];
	int16_t textureTransparency[SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY];
	int16_t skipFlatPolygon[SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY];
	uint32_t fixedShade[SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY];
	uint32_t ambientCoefficient[SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY];
	uint32_t diffuseCoefficient[SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY];
	uint32_t specularCoefficient[SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY];
	uint32_t vertexShading[SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY];
	char textureName[SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY][SLIP_MAT_TEXTURE_NAME_BYTES + 1];
	SlipResourcePayload texturePayload[SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY];
	SlipSprite texture[SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY];
	bool hasTexture[SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY];
} VehicleViewMaterialTable;

static int TrackView_VehicleViewDrawSortChild(VehicleViewRenderContext *ctx, uint32_t childIndex);
static int TrackView_ArticSortCallback(VehicleViewRenderContext *ctx, const SlipShape3DPrimitive *primitive);
static bool TrackView_ArticDrawOne(TrackViewRawBspContext *context, uint8_t *part);

static int32_t TrackView_VehicleViewArithmeticShiftRight32(int32_t value, uint32_t count) {
	count &= SLIP_DWORD_SHIFT_COUNT_MASK;
	if (count == 0) {
		return value;
	}
	if (value >= 0) {
		return value >> count;
	}
	return (int32_t)~((uint32_t)(~value) >> count);
}

static int32_t TrackView_VehicleViewSignExtendShiftLeft32(int16_t value, uint32_t count) {
	return (int32_t)((uint32_t)(int32_t)value << (count & SLIP_DWORD_SHIFT_COUNT_MASK));
}

static int32_t TrackView_VehicleViewMatrixDotProduct32(int16_t sourceX, int16_t coefficientX, int16_t sourceY,
                                                       int16_t coefficientY, int16_t sourceZ, int16_t coefficientZ) {
	uint32_t sum = (uint32_t)((int32_t)sourceX * coefficientX);

	sum += (uint32_t)((int32_t)sourceY * coefficientY);
	sum += (uint32_t)((int32_t)sourceZ * coefficientZ);
	return (int32_t)sum;
}

static SlipDraw3DVec32 TrackView_VehicleViewTransform(uint32_t sourceX, uint32_t sourceY, uint32_t sourceZ,
                                                      SlipDraw3DVertexRecord *record, void *userData) {
	VehicleViewRenderContext *const ctx = (VehicleViewRenderContext *)userData;
	SlipView3DVec32 transformed;

	(void)record;
	if (ctx->shapeScaleShift == 0) {
		transformed = SlipView3D_TransformPosition16(
		    &ctx->matrix, (SlipView3DVec32){(int16_t)sourceX, (int16_t)sourceY, (int16_t)sourceZ});
	} else {
		const int16_t x = (int16_t)sourceX;
		const int16_t y = (int16_t)sourceY;
		const int16_t z = (int16_t)sourceZ;
		const uint32_t shift = (uint32_t)(SLIP_Q14_FRACTION_BITS - (int32_t)ctx->shapeScaleShift);

		transformed.x = TrackView_VehicleViewArithmeticShiftRight32(
		    TrackView_VehicleViewMatrixDotProduct32(x, ctx->matrix.m[0], y, ctx->matrix.m[3], z, ctx->matrix.m[6]),
		    shift);
		transformed.y = TrackView_VehicleViewArithmeticShiftRight32(
		    TrackView_VehicleViewMatrixDotProduct32(x, ctx->matrix.m[1], y, ctx->matrix.m[4], z, ctx->matrix.m[7]),
		    shift);
		transformed.z = TrackView_VehicleViewArithmeticShiftRight32(
		    TrackView_VehicleViewMatrixDotProduct32(x, ctx->matrix.m[2], y, ctx->matrix.m[5], z, ctx->matrix.m[8]),
		    shift);
	}

	return (SlipDraw3DVec32){(int32_t)((uint32_t)transformed.x + (uint32_t)ctx->translation.x),
	                         (int32_t)((uint32_t)transformed.y + (uint32_t)ctx->translation.y),
	                         (int32_t)((uint32_t)transformed.z + (uint32_t)ctx->translation.z)};
}

static SlipView3DVec32 TrackView_VehicleViewBspSource(int16_t x, int16_t y, int16_t z, void *userData) {
	const VehicleViewRenderContext *const ctx = (const VehicleViewRenderContext *)userData;
	if (ctx->shapeScaleShift == 0) {
		return (SlipView3DVec32){x, y, z};
	}
	return (SlipView3DVec32){TrackView_VehicleViewSignExtendShiftLeft32(x, ctx->shapeScaleShift),
	                         TrackView_VehicleViewSignExtendShiftLeft32(y, ctx->shapeScaleShift),
	                         TrackView_VehicleViewSignExtendShiftLeft32(z, ctx->shapeScaleShift)};
}

static SlipDraw3DVec32 TrackView_VehicleViewSource(uint32_t x, uint32_t y, uint32_t z, SlipDraw3DVertexRecord *record,
                                                   void *userData) {
	(void)record;
	SlipView3DVec32 source = TrackView_VehicleViewBspSource((int16_t)x, (int16_t)y, (int16_t)z, userData);
	return (SlipDraw3DVec32){source.x, source.y, source.z};
}

static void TrackView_VehicleViewProject(SlipDraw3DVec32 world, int32_t *screenX, int32_t *screenY, void *userData) {
	VehicleViewRenderContext *const ctx = (VehicleViewRenderContext *)userData;

	SlipDraw3D_ProjectCheckedPerspectiveCallback(world, screenX, screenY, &ctx->projectState);
}

static int TrackView_ActorBuildIndexedRing(VehicleViewRenderContext *ctx, TrackViewRawBspContext *trackContext,
                                           const SlipShape3D *shape, const SlipShape3DPrimitiveStream *stream,
                                           const SlipDraw3DMaterialGate *materialGate,
                                           SlipDraw3DClipFlagVisit *clipFlagVisits, size_t clipFlagVisitCapacity,
                                           SlipDraw3DPostPlaneBoundsVisit *postBoundsVisits,
                                           size_t postBoundsVisitCapacity,
                                           SlipDraw3DPostPlaneClipRecordVisit *postClipRecordVisits,
                                           size_t postClipRecordVisitCapacity,
                                           SlipDraw3DPostPlaneClipPlaneVisit *postClipPlaneVisits,
                                           size_t postClipPlaneVisitCapacity, SlipDraw3DSolidRingExecute *result) {
	SlipDraw3DRecordPool *pool;
	uint8_t *recordPoolBytes;
	size_t recordPoolByteSize;
	SlipDraw3DFirstActiveRecord firstActive;
	uint32_t firstRecordOffset;
	uint32_t currentRecordOffset;
	uint32_t indexStreamOffset = 0;
	uint32_t materialInputOffset = 0;
	uint32_t allClipFlags = SLIP_CLIP_ALL;
	uint32_t anyClipFlags = 0;
	uint16_t loopCount;
	SlipDraw3DVertexLighting lighting;

	if (ctx == NULL || trackContext == NULL || shape == NULL || stream == NULL || materialGate == NULL ||
	    result == NULL || !stream->hasExtendedPayload || stream->vertexCount == 0 ||
	    stream->vertexCount > SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT || stream->indexOffset > shape->size ||
	    shape->size - stream->indexOffset < (size_t)stream->vertexCount * SLIP_SERIALIZED_INDEX_BYTES ||
	    stream->extraPayloadOffset > shape->size ||
	    shape->size - stream->extraPayloadOffset < (size_t)stream->vertexCount * SLIP_SERIALIZED_NORMAL_BYTES) {
		return 0;
	}

	lighting = (SlipDraw3DVertexLighting){.light = {ctx->lightVector.x, ctx->lightVector.y, ctx->lightVector.z},
	                                      .origin = {ctx->bspOrigin.x, ctx->bspOrigin.y, ctx->bspOrigin.z},
	                                      .flags = trackContext->rendererFlags,
	                                      .direct = trackContext->directLight,
	                                      .ambient = trackContext->ambientLight,
	                                      .fadeStart = SlipDraw3D_fadeStart,
	                                      .fadeEnd = SlipDraw3D_fadeEnd,
	                                      .fadeRange = SlipDraw3D_fadeRange,
	                                      .fadeShade = SlipDraw3D_fadeColour,
	                                      .overrideRamp = trackContext->limitEnabled,
	                                      .rampStart = trackContext->limitStart,
	                                      .rampEnd = trackContext->limitEnd,
	                                      .transform = TrackView_VehicleViewSource,
	                                      .transformContext = ctx};
	memset(result, 0, sizeof(*result));
	pool = trackContext->drawRecordPool;
	if (pool == NULL) {
		return 0;
	}
	recordPoolBytes = SlipDraw3D_RecordPoolBytes(pool);
	recordPoolByteSize = SlipDraw3D_RecordPoolByteSize();
	if (recordPoolBytes == NULL || !SlipDraw3D_AllocateFirstActiveRecord(recordPoolBytes, recordPoolByteSize,
	                                                                     pool->freeHeadOffset, &firstActive)) {
		return 0;
	}
	pool->inputActiveHeadOffset = firstActive.inputActiveHeadOffset;
	firstRecordOffset = pool->inputActiveHeadOffset;
	currentRecordOffset = firstRecordOffset;
	loopCount = stream->vertexCount;

	for (;;) {
		SlipDraw3DDrawRecord *currentRecord;
		SlipDraw3DIndexedRecordCopy copy;
		SlipDraw3DMaterialBytes materialBytes;
		uint16_t vertexIndex;
		uint32_t projectFlags;
		uint32_t color;

		vertexIndex = SlipBytes_ReadLE16(shape->data + stream->indexOffset + indexStreamOffset);
		if (vertexIndex >= ctx->vertexRecordCount) {
			return 0;
		}
		projectFlags = SlipDraw3D_ProjectVertex(&ctx->vertexRecords[vertexIndex], &ctx->projectState,
		                                        TrackView_VehicleViewTransform, TrackView_VehicleViewProject,
		                                        TrackView_VehicleViewProject, ctx);
		currentRecord = SlipDraw3D_RecordPoolDrawRecord(pool, currentRecordOffset);
		if (currentRecord == NULL ||
		    !SlipDraw3D_CopyIndexedRecord(currentRecord, (const uint8_t *)ctx->vertexRecords,
		                                  ctx->vertexRecordCount * sizeof(*ctx->vertexRecords),
		                                  shape->data + stream->indexOffset,
		                                  (size_t)stream->vertexCount * SLIP_SERIALIZED_INDEX_BYTES, indexStreamOffset,
		                                  allClipFlags, anyClipFlags, projectFlags, &copy)) {
			return 0;
		}

		color = SlipDraw3D_VertexColor(
		    (const void *)materialGate->materialRecord, &ctx->vertexRecords[vertexIndex],
		    (int16_t)SlipBytes_ReadLE16(shape->data + stream->extraPayloadOffset + materialInputOffset),
		    (int16_t)SlipBytes_ReadLE16(shape->data + stream->extraPayloadOffset + materialInputOffset +
		                                SLIP_SERIALIZED_NORMAL_Y_OFFSET),
		    (int16_t)SlipBytes_ReadLE16(shape->data + stream->extraPayloadOffset + materialInputOffset +
		                                SLIP_SERIALIZED_NORMAL_Z_OFFSET),
		    &lighting);
		if (!SlipDraw3D_StoreMaterialBytes(currentRecord, shape->data + stream->extraPayloadOffset,
		                                   (size_t)stream->vertexCount * SLIP_SERIALIZED_NORMAL_BYTES,
		                                   materialInputOffset, materialGate->materialRecord, (uint16_t)color,
		                                   &materialBytes)) {
			return 0;
		}
		allClipFlags = copy.allClipFlagsAfter;
		anyClipFlags = copy.anyClipFlagsAfter;
		if (--loopCount == 0) {
			break;
		}
		{
			SlipDraw3DAppendRecord append;

			if (!SlipDraw3D_AppendRecord(recordPoolBytes, recordPoolByteSize, pool->freeHeadOffset, currentRecordOffset,
			                             indexStreamOffset, materialInputOffset, &append)) {
				return 0;
			}
			currentRecordOffset = append.appendedRecordOffset;
			indexStreamOffset = append.indexStreamOffsetAfter;
			materialInputOffset = append.materialInputOffsetAfter;
		}
	}
	{
		SlipDraw3DCloseRecordRing close;

		if (!SlipDraw3D_CloseRecordRing(recordPoolBytes, recordPoolByteSize, currentRecordOffset, firstRecordOffset,
		                                &close)) {
			return 0;
		}
	}
	result->build = (SlipDraw3DBuildResult){allClipFlags, anyClipFlags, 1u, 0u, 0u};
	result->returned = true;
	if (!SlipDraw3D_ClipDispatchExecute(
	        recordPoolBytes, recordPoolByteSize, pool->inputActiveHeadOffset, pool->freeHeadOffset, anyClipFlags,
	        allClipFlags, ctx->projectState.renderFlags, SLIP_INTERPOLATE_SHADE, ctx->projectState.minZ,
	        ctx->projectState.maxZ, ctx->projectState.minX, ctx->projectState.maxX, ctx->projectState.minY,
	        ctx->projectState.maxY, TrackView_VehicleViewProject, TrackView_VehicleViewProject, ctx, false, NULL, 0, 0,
	        0, 0, 0, 0, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, clipFlagVisits, clipFlagVisitCapacity, postBoundsVisits,
	        postBoundsVisitCapacity, postClipRecordVisits, postClipRecordVisitCapacity, postClipPlaneVisits,
	        postClipPlaneVisitCapacity, &result->dispatch)) {
		return 0;
	}
	pool->inputActiveHeadOffset = result->dispatch.activeHeadOffsetOut;
	result->activeHeadOffsetOut = pool->inputActiveHeadOffset;
	result->carryOut = result->dispatch.dispatch.carryOut;
	return 1;
}

static bool TrackView_VehicleViewMaterialsFromPayload(const SlipResourcePayload *payload,
                                                      VehicleViewMaterialTable *materials) {
	const uint8_t *raw;
	SlipDraw3DMaterialInstall install;
	size_t expandedTableBytes;
	uint16_t count;
	uint16_t i;

	if (payload == NULL || payload->data == NULL || materials == NULL || payload->size < SLIP_MAT_HEADER_BYTES) {
		return false;
	}
	count = SlipBytes_ReadLE16(payload->data);
	if (SlipBytes_ReadLE16(payload->data + SLIP_MAT_VERSION_OFFSET) != SLIP_MAT_VERSION) {
		return false;
	}
	if ((size_t)count > (payload->size - SLIP_MAT_HEADER_BYTES) / SLIP_MAT_RECORD_BYTES) {
		return false;
	}

	memset(materials, 0, sizeof(*materials));
	materials->count = count > SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY ? SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY : count;
	expandedTableBytes =
	    SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES + (size_t)count * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE;
	materials->expandedTable = (uint8_t *)malloc(expandedTableBytes);
	if (materials->expandedTable == NULL ||
	    !SlipDraw3D_SetMaterialsNoExisting(payload->data, payload->size, 0, 1u, materials->expandedTable,
	                                       expandedTableBytes, &install)) {
		free(materials->expandedTable);
		materials->expandedTable = NULL;
		return false;
	}
	materials->expandedTableBytes = expandedTableBytes;
	materials->materialResourceHandle = install.storedMaterialGlobal;
	raw = payload->data + SLIP_MAT_HEADER_BYTES;
	for (i = 0; i < materials->count; ++i) {
		const uint8_t *const record = raw + (size_t)i * SLIP_MAT_RECORD_BYTES;
		const uint8_t start = record[SLIP_MAT_RAMP_START_OFFSET];
		const uint8_t rawEnd = record[SLIP_MAT_RAMP_END_OFFSET];
		const uint16_t rampBits = SlipBytes_ReadLE16(record + SLIP_MAT_DITHER_BITS_OFFSET);
		const uint32_t countAdjust =
		    rampBits < SLIP_MAT_DITHER_SHIFT_MASK ? ((uint32_t)1u << rampBits) - 1u : UINT32_MAX;
		uint32_t diff = (uint32_t)rawEnd - (uint32_t)start;
		int32_t end;

		memcpy(materials->materialName[i], record, SLIP_MAT_NAME_BYTES);
		materials->materialName[i][SLIP_MAT_NAME_BYTES] = '\0';
		for (uint16_t j = 0; j < SLIP_MAT_NAME_BYTES; ++j) {
			if (materials->materialName[i][j] == ' ') {
				materials->materialName[i][j] = '\0';
				break;
			}
		}
		if (diff > SLIP_MAT_MAXIMUM_RAMP_RANGE) {
			diff = SLIP_MAT_MAXIMUM_RAMP_RANGE;
		}
		end = (int32_t)((uint32_t)start + diff - countAdjust);
		materials->rampStart[i] = (int32_t)(uint32_t)start;
		materials->rampEnd[i] = end;
		materials->textureTransparency[i] = (int16_t)(int8_t)record[SLIP_MAT_TRANSPARENCY_OFFSET];
		materials->skipFlatPolygon[i] = (int16_t)(int8_t)record[SLIP_MAT_SKIP_FLAT_OFFSET];
		materials->fixedShade[i] = SlipBytes_ReadLE16(record + SLIP_MAT_FIXED_SHADE_OFFSET);

		materials->vertexShading[i] = (uint32_t)(int32_t)(int8_t)record[SLIP_MAT_VERTEX_SHADING_OFFSET];
		materials->ambientCoefficient[i] = SlipBytes_ReadLE16(record + SLIP_MAT_AMBIENT_OFFSET);
		materials->diffuseCoefficient[i] = SlipBytes_ReadLE16(record + SLIP_MAT_DIFFUSE_OFFSET);
		materials->specularCoefficient[i] = SlipBytes_ReadLE16(record + SLIP_MAT_SPECULAR_OFFSET);
		memcpy(materials->textureName[i], record + SLIP_MAT_TEXTURE_NAME_OFFSET, SLIP_MAT_TEXTURE_NAME_BYTES);
		materials->textureName[i][SLIP_MAT_TEXTURE_NAME_BYTES] = '\0';
		for (uint16_t j = 0; j < SLIP_MAT_TEXTURE_NAME_BYTES; ++j) {
			if (materials->textureName[i][j] == ' ') {
				materials->textureName[i][j] = '\0';
				break;
			}
		}
	}
	return true;
}

static bool TrackView_VehicleViewMaterialsFromTrackTable(const uint8_t *expandedTable, size_t tableBytes,
                                                         VehicleViewMaterialTable *materials) {
	uint32_t count;
	uint32_t i;

	if (expandedTable == NULL || materials == NULL || tableBytes < SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES) {
		return false;
	}
	count = SlipBytes_ReadLE32(expandedTable);
	if (count == 0u || (size_t)count > (tableBytes - SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES) /
	                                       SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE) {
		return false;
	}
	memset(materials, 0, sizeof(*materials));
	materials->count =
	    count > SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY ? SLIP_VEHICLE_PREVIEW_MATERIAL_CAPACITY : (uint16_t)count;
	for (i = 0; i < materials->count; ++i) {
		const uint8_t *const record = expandedTable + SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES +
		                              (size_t)i * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE;
		uint16_t j;

		memcpy(materials->materialName[i], record, SLIP_MAT_NAME_BYTES);
		materials->materialName[i][SLIP_MAT_NAME_BYTES] = 0;
		for (j = 0; j < SLIP_MAT_NAME_BYTES; ++j) {
			if (materials->materialName[i][j] == ' ') {
				materials->materialName[i][j] = 0;
				break;
			}
		}
		materials->rampStart[i] = (int32_t)SlipBytes_ReadLE32(record + offsetof(SlipDraw3DMaterialRecord, rampStart));
		materials->rampEnd[i] = (int32_t)SlipBytes_ReadLE32(record + offsetof(SlipDraw3DMaterialRecord, rampEnd));
		materials->textureTransparency[i] =
		    (int16_t)SlipBytes_ReadLE16(record + offsetof(SlipDraw3DMaterialRecord, textureTransparency));
		materials->skipFlatPolygon[i] =
		    (int16_t)SlipBytes_ReadLE16(record + offsetof(SlipDraw3DMaterialRecord, skipFlatPolygon));
		materials->fixedShade[i] = SlipBytes_ReadLE32(record + offsetof(SlipDraw3DMaterialRecord, fixedShade));
		materials->ambientCoefficient[i] =
		    SlipBytes_ReadLE32(record + offsetof(SlipDraw3DMaterialRecord, ambientCoefficient));
		materials->diffuseCoefficient[i] =
		    SlipBytes_ReadLE32(record + offsetof(SlipDraw3DMaterialRecord, diffuseCoefficient));
		materials->specularCoefficient[i] =
		    SlipBytes_ReadLE32(record + offsetof(SlipDraw3DMaterialRecord, specularCoefficient));
		materials->vertexShading[i] = SlipBytes_ReadLE32(record + offsetof(SlipDraw3DMaterialRecord, vertexShading));
		memcpy(materials->textureName[i], record + offsetof(SlipDraw3DMaterialRecord, textureName),
		       SLIP_MAT_TEXTURE_NAME_BYTES);
		materials->textureName[i][SLIP_MAT_TEXTURE_NAME_BYTES] = 0;
		for (j = 0; j < SLIP_MAT_TEXTURE_NAME_BYTES; ++j) {
			if (materials->textureName[i][j] == ' ') {
				materials->textureName[i][j] = 0;
				break;
			}
		}
	}
	return true;
}

static void TrackView_VehicleViewMaterialsFreeTextures(VehicleViewMaterialTable *materials) {
	uint16_t i;

	if (materials == NULL) {
		return;
	}

	for (i = 0; i < materials->count; ++i) {
		materials->hasTexture[i] = false;
	}
	free(materials->expandedTable);
	materials->expandedTable = NULL;
	materials->expandedTableBytes = 0;
	materials->materialResourceHandle = 0;
}

static bool TrackView_LoadMaterialTexturePayload(const char *const *archives, size_t archiveCount, const char *name,
                                                 SlipResourcePayload *payload) {
	const char *wildcard;
	char resolvedName[SLIP_RESOURCE_NAME_BUFFER_BYTES];
	char digit;

	if (SlipResource_LoadByName(archives, archiveCount, name, payload)) {
		return true;
	}
	wildcard = strchr(name, '*');
	if (wildcard == NULL || strlen(name) >= sizeof(resolvedName)) {
		return false;
	}

	memcpy(resolvedName, name, strlen(name) + 1u);
	for (digit = '0'; digit <= '9'; ++digit) {
		resolvedName[wildcard - name] = digit;
		if (SlipResource_LoadByName(archives, archiveCount, resolvedName, payload)) {
			return true;
		}
	}
	return false;
}

int TrackView_FindNameRecord(void *user, const char name[SLIP_RESOURCE_NAME_BUFFER_BYTES], uint32_t *handle) {
	TrackViewResourceHandleRegistry *const registry = user;
	if (registry->hostResources)
		return TrackView_FindNamedResource(user, name, handle);
	char key[SLIP_RESOURCE_NAME_BUFFER_BYTES] = {0};
	for (size_t index = 0; index < SLIP_RESOURCE_NAME_BYTES && name[index] != 0; ++index)
		key[index] = (char)SlipResource_Uppercase((uint8_t)name[index]);
	for (size_t index = 0; index < registry->entryCount; ++index) {
		if (memcmp(registry->entries[index].name, key, sizeof(key)) == 0) {
			*handle = registry->entries[index].resourceHandle;
			return 1;
		}
	}
	return 0;
}

int TrackView_FindNamedResource(void *user, const char name[SLIP_RESOURCE_NAME_BUFFER_BYTES],
                                uint32_t *resourceHandle) {
	TrackViewResourceHandleRegistry *const registry = user;
	uint16_t handle;
	if (!SlipResourceHost_Find(NULL, name, &handle))
		return 0;
	*resourceHandle = handle;
	for (size_t i = 0; i < registry->entryCount; ++i)
		if (registry->entries[i].resourceHandle == handle)
			return 1;
	if (registry->entryCount >= sizeof(registry->entries) / sizeof(registry->entries[0]))
		return 0;
	TrackViewResourceHandleEntry *const entry = &registry->entries[registry->entryCount++];
	memset(entry, 0, sizeof(*entry));
	for (size_t i = 0; i < sizeof(entry->name) - 1u && name[i] != '\0'; ++i)
		entry->name[i] = (char)SlipResource_Uppercase((uint8_t)name[i]);
	entry->resourceHandle = handle;
	return 1;
}

int TrackView_LoadNamedResource(void *user, const char name[SLIP_RESOURCE_NAME_BUFFER_BYTES],
                                uint32_t *resourceHandle) {
	TrackViewResourceHandleRegistry *const registry = (TrackViewResourceHandleRegistry *)user;
	SlipResourcePayload payload = {0};
	char canonicalName[SLIP_RESOURCE_NAME_BUFFER_BYTES] = {0};
	size_t i;

	if (registry == NULL || resourceHandle == NULL || name == NULL || name[0] == '\0') {
		return 0;
	}
	for (i = 0; i + 1u < sizeof(canonicalName) && name[i] != '\0'; ++i) {
		canonicalName[i] = (char)SlipResource_Uppercase((uint8_t)name[i]);
	}
	if (registry->hostResources) {
		uint16_t handle;
		if (!SlipResourceHost_Load(NULL, canonicalName, &handle))
			return 0;
		*resourceHandle = handle;
		for (i = 0; i < registry->entryCount; ++i)
			if (registry->entries[i].resourceHandle == handle)
				return 1;
		if (registry->entryCount >= sizeof(registry->entries) / sizeof(registry->entries[0]))
			return 0;
		TrackViewResourceHandleEntry *const entry = &registry->entries[registry->entryCount++];
		memcpy(entry->name, canonicalName, sizeof(canonicalName));
		entry->resourceHandle = handle;
		entry->payload = SlipResourceHost_Payload(handle);
		return 1;
	}

	uint16_t namedHandle;
	if (!SlipResourceHost_Find(NULL, canonicalName, &namedHandle))
		return 0;
	for (i = 0; i < registry->entryCount; ++i) {
		if (memcmp(registry->entries[i].name, canonicalName, sizeof(canonicalName)) == 0) {
			*resourceHandle = registry->entries[i].resourceHandle;
			return 1;
		}
	}
	if (!SlipResource_LoadByName(registry->archives, registry->archiveCount, canonicalName, &payload)) {
		return 0;
	}
	if (registry->entryCount >= sizeof(registry->entries) / sizeof(registry->entries[0])) {
		return 0;
	}
	*resourceHandle = namedHandle;
	memcpy(registry->entries[registry->entryCount].name, canonicalName, sizeof(canonicalName));
	registry->entries[registry->entryCount].resourceHandle = *resourceHandle;
	registry->entries[registry->entryCount].payload = payload;
	++registry->entryCount;
	return 1;
}

void TrackView_ReleaseResource(void *user, uint32_t handle) {
	TrackViewResourceHandleRegistry *const registry = user;
	if (registry->hostResources) {
		SlipResourceHost_Release(NULL, (uint16_t)handle);
		return;
	}
	for (size_t index = 0; index < registry->entryCount; ++index) {
		if (registry->entries[index].resourceHandle == handle) {
			SlipResource_ReleaseHandle(&registry->entries[index].payload);
			return;
		}
	}
}

void TrackView_ReleaseSequence(TrackViewResourceHandleRegistry *registry, const uint16_t *handles, uint16_t count) {
	while (count != 0) {
		TrackView_ReleaseResource(registry, *handles++);
		--count;
	}
}

bool TrackView_LoadResourceHandlePayload(const TrackViewResourceHandleRegistry *registry, uint32_t resourceHandle,
                                         SlipResourcePayload *payload) {
	size_t i;

	if (registry == NULL || payload == NULL || resourceHandle == 0u) {
		return false;
	}
	memset(payload, 0, sizeof(*payload));
	if (registry->hostResources) {
		if (!SlipResourceHost_IsResident(NULL, (uint16_t)resourceHandle))
			return false;
		*payload = SlipResourceHost_Payload((uint16_t)resourceHandle);
		return true;
	}
	for (i = 0; i < registry->entryCount; ++i) {
		if (registry->entries[i].resourceHandle == resourceHandle) {
			return SlipResource_LoadByName(registry->archives, registry->archiveCount, registry->entries[i].name,
			                               payload);
		}
	}
	return false;
}

/* Native bindings for the resource calls in raster and radius-query entries. */
static bool TrackView_LockResourceHandlePayload(const TrackViewResourceHandleRegistry *registry, uint32_t handle,
                                                SlipResourcePayload *payload) {
	if (registry == NULL || !registry->hostResources)
		return TrackView_LoadResourceHandlePayload(registry, handle, payload);
	(void)SlipResourceHost_Lock(NULL, (uint16_t)handle);
	*payload = SlipResourceHost_Payload((uint16_t)handle);
	return true;
}

static void TrackView_UnlockResourceHandlePayload(const TrackViewResourceHandleRegistry *registry, uint32_t handle) {
	if (registry != NULL && registry->hostResources)
		SlipResourceHost_Unlock(NULL, (uint16_t)handle);
}

static void TrackView_ProjectCloudCorner(int16_t side, int16_t offset, int32_t originX, int32_t originY, int16_t *outX,
                                         int16_t *outY) {
	if (!SlipRaceDisplay_ready || !SlipRaceGpu_Active()) {
		SlipDraw3D_ProjectSpriteCorner(side, offset, (uint16_t)originX, (uint16_t)originY, outX, outY);
		return;
	}
	int16_t x, y;
	SlipDraw3D_ProjectSpriteCorner(side, offset, 0, 0, &x, &y);
	const int32_t scaledX =
	    originX + (int32_t)((int64_t)SlipRaceDisplay_ScaleWorldOffset(x) * SLIP_DRAW3D_SQUARE_PIXEL_SCALE_NUMERATOR /
	                        SLIP_DRAW3D_SQUARE_PIXEL_SCALE_DENOMINATOR);
	const int32_t scaledY = originY + SlipRaceDisplay_ScaleWorldOffset(y);
	/* The sprite clipper takes signed words; saturate offscreen corners instead
	 * of wrapping a very large sky
	 * sprite back into the viewport. */
	*outX = (int16_t)(scaledX < INT16_MIN ? INT16_MIN : scaledX > INT16_MAX ? INT16_MAX : scaledX);
	*outY = (int16_t)(scaledY < INT16_MIN ? INT16_MIN : scaledY > INT16_MAX ? INT16_MAX : scaledY);
}

static bool TrackView_DrawCloudSprite(TrackViewRawBspContext *context, const SlipResourcePayload *sprite,
                                      uint16_t spriteHandle, SlipView3DVec32 view, int16_t *screenX, int16_t *screenY,
                                      bool resourceOwned) {
	int32_t projectedX, projectedY;
	SlipDraw3DScreenPoint16 corners[4];
	if (context == NULL ||
	    (!resourceOwned && (sprite == NULL || sprite->data == NULL || sprite->size < SLIP_SPRITE_HEADER_BYTES)))
		return false;

	if (!SlipDraw3D_ProjectScreen((SlipDraw3DVec32){view.x, view.y, view.z}, context->projectState, &projectedX,
	                              &projectedY))
		return false;

	const uint8_t *const dimensionBytes = resourceOwned ? SlipResourceHost_Lock(NULL, spriteHandle) : sprite->data;
	const int16_t halfWidth = (int16_t)(SlipBytes_ReadLE16(dimensionBytes) >> 1);
	const int16_t negativeHeight = (int16_t)(0u - SlipBytes_ReadLE16(dimensionBytes + SLIP_SPRITE_HEIGHT_OFFSET));
	if (resourceOwned)
		SlipResourceHost_Unlock(NULL, spriteHandle);
	TrackView_ProjectCloudCorner((int16_t)-halfWidth, negativeHeight, projectedX, projectedY, &corners[0].x,
	                             &corners[0].y);
	TrackView_ProjectCloudCorner(halfWidth, negativeHeight, projectedX, projectedY, &corners[1].x, &corners[1].y);

	bool axisAligned = corners[1].y == corners[0].y && corners[1].x > corners[0].x;
	TrackView_ProjectCloudCorner(halfWidth, 0, projectedX, projectedY, &corners[2].x, &corners[2].y);
	if (corners[2].x != corners[1].x)
		axisAligned = false;
	TrackView_ProjectCloudCorner((int16_t)-halfWidth, 0, projectedX, projectedY, &corners[3].x, &corners[3].y);

	if (corners[3].y != corners[2].y || corners[3].x != corners[0].x || corners[3].y < corners[0].y)
		axisAligned = false;
	if (screenX != NULL)
		*screenX = (int16_t)projectedX;
	if (screenY != NULL)
		*screenY = (int16_t)projectedY;
	if (axisAligned) {
		int16_t minX, minY, maxX, maxY;
		Raster_GetClipRect(&minX, &minY, &maxX, &maxY);
		Raster_SetClipRect((int16_t)context->projectState->minX, (int16_t)context->projectState->minY,
		                   (int16_t)context->projectState->maxX, (int16_t)context->projectState->maxY);
		if (resourceOwned) {
			SlipSpriteHost_effectResources.drawScaled(NULL, spriteHandle, corners[0].x, corners[0].y, corners[2].x,
			                                          corners[2].y);
		} else {
			Raster_DrawSpriteScaled(sprite->data, sprite->size, sprite->data + SLIP_SPRITE_HEADER_BYTES,
			                        sprite->size - SLIP_SPRITE_HEADER_BYTES, corners[0].x, corners[0].y, corners[2].x,
			                        corners[2].y);
		}
		Raster_SetClipRect(minX, minY, maxX, maxY);
		return true;
	}

	const SlipDraw3DScreenPoint16 *points[4] = {corners, corners + 1, corners + 2, corners + 3};
	const SlipDraw3DTextureCoordinates uv[4] = {
	    {0, 0}, {SLIP_Q14_ONE, 0}, {SLIP_Q14_ONE, SLIP_Q14_ONE}, {0, SLIP_Q14_ONE}};
	SlipDraw3DReturnActiveVisit returns[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DReturnActiveRing returned;
	SlipDraw3DSpritePointTextureRingVisit ringVisits[4];
	SlipDraw3DSpritePointTextureRing ring;
	SlipDraw3DProjectState *const state = context->projectState;
	const uint32_t savedFlags = context->rendererFlags;
	context->rendererFlags |= SLIP_RENDER_MASKED_TEXTURE | SLIP_RENDER_ALTERNATE_TEXTURE_RASTER;
	bool ok = SlipDraw3D_ReturnActiveRing(context->drawRecordPool, returns, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT,
	                                      &returned) &&
	          SlipDraw3D_SpritePointTextureRing(context->drawRecordPool, points, uv, 4, spriteHandle, state->minX,
	                                            state->maxX, state->minY, state->maxY, 0, ringVisits, 4, &ring);
	bool rejected = ok && ring.carryOut;
	if (ok && ring.calledClipScreen) {
		SlipDraw3DClipFlagVisit flagVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DPostPlaneBoundsVisit boundsVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DPostPlaneClipRecordVisit recordVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DPostPlaneClipPlaneVisit planeVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DScreenPlaneExecute clipped;
		TrackViewPostPlaneArgs post = TrackView_PostPlaneArgs(context);
		ok = SlipDraw3D_ScreenPlaneExecute(
		    SlipDraw3D_RecordPoolBytes(context->drawRecordPool), SlipDraw3D_RecordPoolByteSize(),
		    context->drawRecordPool->inputActiveHeadOffset, context->drawRecordPool->freeHeadOffset,
		    ring.anyFlagsBeforeDispatch, ring.mode, state->minX, state->maxX, state->minY, state->maxY,
		    post.hasPostPlanes, post.planeBase, post.planeBytes, post.planeHeadOffset, post.limitXMin, post.limitXMax,
		    post.limitYMin, post.limitYMax, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, flagVisits,
		    SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, boundsVisits, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, recordVisits,
		    SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, planeVisits, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, &clipped);
		if (ok) {
			context->drawRecordPool->inputActiveHeadOffset = clipped.activeHeadOffsetOut;
			rejected = clipped.dispatch.carryOut;
		}
	}
	if (ok && !rejected) {
		SlipDraw3DTexturedDispatchPoint dispatchPoints[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DTexturedDispatchVisit dispatchVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DTexturedDispatch dispatch;
		ok = SlipDraw3D_PrepareTexturedDispatch(
		    context->drawRecordPool, context->drawRecordPool->inputActiveHeadOffset, ring.drawMode,
		    context->rendererFlags, ring.textureHandle, context->reverseTraversal, 0, dispatchPoints,
		    SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, dispatchVisits, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, &dispatch);
		if (ok) {
			RasterTexturedPoint rasterPoints[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT] = {0};
			for (size_t i = 0; i < dispatch.pointCount; ++i) {
				rasterPoints[i].x = dispatchPoints[i].screenX;
				rasterPoints[i].y = dispatchPoints[i].screenY;
				rasterPoints[i].u = dispatchPoints[i].textureU;
				rasterPoints[i].v = dispatchPoints[i].textureV;
				/* Screen-space cloud corners have no world depth in the recycled records. */
				rasterPoints[i].depth = SlipRaceGpu_Active() ? 1 : dispatchPoints[i].depth;
			}
			SlipResourcePayload rasterPayload;
			if (resourceOwned) {
				(void)SlipResourceHost_Lock(NULL, spriteHandle);
				rasterPayload = SlipResourceHost_Payload(spriteHandle);
				sprite = &rasterPayload;
			}
			RasterAffineScanlineLoopVisit *visits = NULL;
			if (SlipRaceGpu_Active()) {
				SlipRaceGpu_Texture(sprite->data, sprite->size, 0, (const RasterTexturedPoint *)rasterPoints,
				                    (uint32_t)dispatch.pointCount);
			} else {
				visits = calloc(SLIP_TEXTURED_RASTER_VISIT_CAPACITY, sizeof(*visits));
				size_t pixels;
				ok = visits != NULL && Raster_DrawAffineTexturedPolygon(sprite->data, sprite->size, rasterPoints,
				                                                        (uint32_t)dispatch.pointCount, 0, visits,
				                                                        SLIP_TEXTURED_RASTER_VISIT_CAPACITY, &pixels);
			}
			if (resourceOwned)
				SlipResourceHost_Unlock(NULL, spriteHandle);
			free(visits);
		}
	}
	context->rendererFlags = savedFlags;
	return ok;
}

bool TrackView_DrawSprite(TrackViewRawBspContext *context, SlipView3DVec32 view, int32_t radius,
                          uint16_t spriteHandle) {
	SlipResourcePayload sprite = {0};
	int32_t left, top, right, bottom;
	int16_t savedMinX, savedMinY, savedMaxX, savedMaxY;
	if (context == NULL || context->projectState == NULL || context->resourceRegistry == NULL)
		return false;

	SlipDraw3DReturnActiveVisit returnVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DReturnActiveRing returned;
	if (!SlipDraw3D_ReturnActiveRing(context->drawRecordPool, returnVisits, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT,
	                                 &returned))
		return false;
	if ((int32_t)((uint32_t)view.z - (uint32_t)radius) < (int32_t)context->projectState->minZ ||
	    (int32_t)((uint32_t)view.z + (uint32_t)radius) > (int32_t)context->projectState->maxZ) {
		return true;
	}

	SlipDraw3DProjectState *const state = context->projectState;
	if (state->auxiliaryClipPlaneEnabled) {

		const int32_t x = (int32_t)((uint32_t)view.x - (uint32_t)state->auxiliaryClipPlaneOrigin.x);
		const int32_t y = (int32_t)((uint32_t)view.y - (uint32_t)state->auxiliaryClipPlaneOrigin.y);
		const int32_t z = (int32_t)((uint32_t)view.z - (uint32_t)state->auxiliaryClipPlaneOrigin.z);
		uint64_t dot = (uint64_t)((int64_t)x * state->auxiliaryClipPlaneNormal.x);
		dot += (uint64_t)((int64_t)y * state->auxiliaryClipPlaneNormal.y);
		dot += (uint64_t)((int64_t)z * state->auxiliaryClipPlaneNormal.z);
		uint32_t distance =
		    (uint32_t)(dot >> SLIP_NORMAL_FRACTION_BITS) + (uint32_t)((dot >> SLIP_NORMAL_ROUND_BIT) & 1u);
		if ((int32_t)distance < 0)
			distance = 0u - distance;
		if (distance <= (uint32_t)radius) {

			const int32_t left = (int32_t)((uint32_t)view.x - (uint32_t)radius);
			const int32_t right = (int32_t)((uint32_t)view.x + (uint32_t)radius);
			const int32_t low = (int32_t)((uint32_t)view.y - (uint32_t)radius);
			const int32_t high = (int32_t)((uint32_t)view.y + (uint32_t)radius);
			SlipDraw3DVec32 corners[4] = {
			    {left, low, view.z}, {left, high, view.z}, {right, high, view.z}, {right, low, view.z}};
			const SlipDraw3DVec32 *points[4] = {corners, corners + 1, corners + 2, corners + 3};
			const SlipDraw3DTextureCoordinates uv[4] = {
			    {0, SLIP_Q14_ONE}, {0, 0}, {SLIP_Q14_ONE, 0}, {SLIP_Q14_ONE, SLIP_Q14_ONE}};
			uint32_t common = UINT32_MAX;
			for (unsigned i = 0; i < 4; ++i)
				common &= state->projectMask(corners[i], state);
			if (common != 0)
				return true;
			const uint32_t savedFlags = state->renderFlags;
			state->renderFlags = SLIP_RENDER_DISABLE_SPECULAR;
			if (radius < SLIP_TRACK_SPRITE_MINIMUM_SOLID_RADIUS)
				state->renderFlags |= SLIP_RENDER_WIREFRAME;
			if (view.z >= (int32_t)((uint32_t)radius * (uint32_t)state->projectionScale))
				state->renderFlags |= SLIP_RENDER_DISABLE_TEXTURES;
			SlipDraw3DClipFlagVisit flagVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
			SlipDraw3DPostPlaneBoundsVisit boundsVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
			SlipDraw3DPostPlaneClipRecordVisit recordVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
			SlipDraw3DPostPlaneClipPlaneVisit planeVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
			SlipDraw3DSpritePolygon polygon;
			TrackViewPostPlaneArgs post = TrackView_PostPlaneArgs(context);
			const int built = SlipDraw3D_SpritePolygon(
			    context->drawRecordPool, points, uv, 4, spriteHandle, state, post.hasPostPlanes, post.planeBase,
			    post.planeBytes, post.planeHeadOffset, post.limitXMin, post.limitXMax, post.limitYMin, post.limitYMax,
			    SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, flagVisits, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, boundsVisits,
			    SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, recordVisits, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, planeVisits,
			    SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, &polygon);
			state->renderFlags = savedFlags;
			if (!built)
				return false;
			if (polygon.carryOut)
				return true;

			SlipDraw3DTexturedDispatchPoint dispatchPoints[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
			SlipDraw3DTexturedDispatchVisit dispatchVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
			SlipDraw3DTexturedDispatch dispatch;
			const uint32_t savedDrawFlags = context->rendererFlags;
			context->rendererFlags |= SLIP_RENDER_MASKED_TEXTURE | SLIP_RENDER_ALTERNATE_TEXTURE_RASTER;
			const int prepared = SlipDraw3D_PrepareTexturedDispatch(
			    context->drawRecordPool, context->drawRecordPool->inputActiveHeadOffset, polygon.drawMode,
			    context->rendererFlags, polygon.textureHandle, context->reverseTraversal, 0, dispatchPoints,
			    SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, dispatchVisits, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, &dispatch);
			bool drawn = prepared != 0;
			if (drawn && TrackView_LockResourceHandlePayload(context->resourceRegistry, (uint16_t)polygon.textureHandle,
			                                                 &sprite)) {
				RasterTexturedPoint rasterPoints[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT] = {0};
				for (size_t i = 0; i < dispatch.pointCount; ++i) {
					rasterPoints[i].x = dispatchPoints[i].screenX;
					rasterPoints[i].y = dispatchPoints[i].screenY;
					rasterPoints[i].u = dispatchPoints[i].textureU;
					rasterPoints[i].v = dispatchPoints[i].textureV;
					rasterPoints[i].depth = dispatchPoints[i].depth;
				}
				RasterAffineScanlineLoopVisit *visits = NULL;
				if (SlipRaceGpu_Active()) {
					SlipRaceGpu_Texture(sprite.data, sprite.size, 0, rasterPoints, (uint32_t)dispatch.pointCount);
					drawn = true;
				} else {
					visits = calloc(SLIP_TEXTURED_RASTER_VISIT_CAPACITY, sizeof(*visits));
					size_t pixels;
					drawn = visits != NULL && Raster_DrawAffineTexturedPolygon(
					                              sprite.data, sprite.size, rasterPoints, (uint32_t)dispatch.pointCount,
					                              0, visits, SLIP_TEXTURED_RASTER_VISIT_CAPACITY, &pixels);
				}
				TrackView_UnlockResourceHandlePayload(context->resourceRegistry, (uint16_t)polygon.textureHandle);
				free(visits);
			}
			context->rendererFlags = savedDrawFlags;
			return drawn;
		}
	}
	SlipDraw3DVec32 firstCorner = {(int32_t)((uint32_t)view.x - (uint32_t)radius),
	                               (int32_t)((uint32_t)view.y + (uint32_t)radius), view.z};

	const uint16_t firstMask = (uint16_t)context->projectState->projectMask(firstCorner, context->projectState);
	context->projectState->projectPrimary(firstCorner, &left, &top, context->projectState);

	left = (int16_t)(uint16_t)left;
	top = (int16_t)(uint16_t)top;
	if (left > (int16_t)context->projectState->maxX || top > (int16_t)context->projectState->maxY) {
		return true;
	}
	SlipDraw3DVec32 secondCorner = {(int32_t)((uint32_t)view.x + (uint32_t)radius),
	                                (int32_t)((uint32_t)view.y - (uint32_t)radius), view.z};

	if (((uint16_t)context->projectState->projectMask(secondCorner, context->projectState) & firstMask) != 0) {
		return true;
	}
	context->projectState->projectPrimary(secondCorner, &right, &bottom, context->projectState);

	right = (int16_t)(uint16_t)right;
	bottom = (int16_t)(uint16_t)bottom;
	if (right < (int16_t)context->projectState->minX || bottom < (int16_t)context->projectState->minY) {
		return true;
	}
	Raster_GetClipRect(&savedMinX, &savedMinY, &savedMaxX, &savedMaxY);
	Raster_SetClipRect((int16_t)context->projectState->minX, (int16_t)context->projectState->minY,
	                   (int16_t)context->projectState->maxX, (int16_t)context->projectState->maxY);

	if (context->resourceRegistry != NULL && context->resourceRegistry->hostResources)
		SlipSpriteHost_effectResources.drawScaled(SlipSpriteHost_effectResources.context, spriteHandle, (int16_t)left,
		                                          (int16_t)top, (int16_t)right, (int16_t)bottom);
	else if (TrackView_LoadResourceHandlePayload(context->resourceRegistry, spriteHandle, &sprite) &&
	         sprite.size > SLIP_SPRITE_HEADER_BYTES)
		Raster_DrawSpriteScaled(sprite.data, sprite.size, sprite.data + SLIP_SPRITE_HEADER_BYTES,
		                        sprite.size - SLIP_SPRITE_HEADER_BYTES, (int16_t)left, (int16_t)top, (int16_t)right,
		                        (int16_t)bottom);
	Raster_SetClipRect(savedMinX, savedMinY, savedMaxX, savedMaxY);
	return true;
}

bool TrackView_ExecuteBonusDrawCallback(TrackViewRawBspContext *context, uint32_t objectRecord) {
	const uint16_t objectOffset = (uint16_t)objectRecord;
	SlipView3DVec32 view;
	if (context == NULL || context->objectTable == NULL ||
	    (size_t)objectOffset + SLIP_OBJECT_DOS_STRIDE > context->objectTableBytes ||
	    !SlipObject_ViewPosition(context->objectTable, context->objectTableBytes, objectOffset, &view))
		return false;
	const SlipObject *const object = &context->objectTable[objectOffset / SLIP_OBJECT_DOS_STRIDE];
	return TrackView_DrawSprite(context, view, (int32_t)object->drawExtent, (uint16_t)object->drawData);
}

bool TrackView_QueueCrossEffect(TrackViewRawBspContext *context, uint32_t objectRecord) {
	SlipView3DVec32 view;
	if (context == NULL || SlipDraw3D_listPool == NULL ||
	    !SlipObject_ViewPosition(context->objectTable, context->objectTableBytes, (uint16_t)objectRecord, &view))
		return false;
	const uint32_t payload = ((uint32_t)view.x & SLIP_CALLBACK_PAYLOAD_UPPER_WORD_MASK) | (uint16_t)objectRecord;
	SlipDraw3D_ListInsert(&SlipDraw3D_listState, SlipDraw3D_listPool,
	                      (size_t)SlipDraw3D_listState.capacity * sizeof(*SlipDraw3D_listPool), (uint32_t)view.z,
	                      TrackView_DrawCrossEffect, payload);
	return true;
}

bool TrackView_DrawCrossEffect(TrackViewRawBspContext *context, uint32_t objectRecord) {
	SlipObjectExtentReadResult radius;
	SlipView3DVec32 view;
	SlipObjectSlotDataReadResult slot;
	if (context == NULL || context->projectState == NULL ||
	    !SlipObject_GetDrawExtent(context->objectTable, context->objectTableBytes, objectRecord, &radius) ||
	    !SlipObject_ViewPosition(context->objectTable, context->objectTableBytes, objectRecord, &view) ||
	    !SlipObject_GetDrawData(context->objectTable, context->objectTableBytes, objectRecord, &slot))
		return false;
	SlipDraw3DProjectState *const state = context->projectState;
	const SlipDraw3DVec32 position = {view.x, view.y, view.z};
	const int32_t detail = (int32_t)SlipDraw3D_DetailValue(state->projectionMode, SlipDraw3D_minimumDepth,
	                                                       radius.drawExtent, (uint32_t)view.z);
	if (detail > SLIP_CROSS_EFFECT_MAXIMUM_DETAIL)
		return true;
	const uint8_t color = slot.drawData >> SLIP_WORD_BITS;
	if (detail <= SLIP_CROSS_EFFECT_POINT_MAXIMUM_DETAIL) {
		SlipDraw3D_DrawPoint(position, color, state);
		return true;
	}
	int32_t x, y;
	if (!SlipDraw3D_ProjectVisiblePoint(position, state, &x, &y))
		return true;
	if (context->maths == NULL)
		return false;

	/* Keep the original detail gates, but project the radius with the current camera. */
	int32_t radiusX, radiusY;
	state->projectPrimary((SlipDraw3DVec32){(int32_t)radius.drawExtent, (int32_t)radius.drawExtent, view.z}, &radiusX,
	                      &radiusY, state);
	radiusX -= state->centerX;
	radiusY = state->centerY - radiusY;
	const int16_t angle = slot.drawData;
	const int16_t sine = SlipView3D_SinQ14(context->maths, angle);
	const int16_t cosine = SlipView3D_CosQ14(context->maths, angle);
	const int32_t firstX = (int32_t)(((int64_t)radiusX * sine) >> SLIP_CROSS_EFFECT_ARM_SCALE_SHIFT);
	const int32_t firstY = (int32_t)(((int64_t)radiusY * cosine) >> SLIP_CROSS_EFFECT_ARM_SCALE_SHIFT);
	const int32_t secondX = (int32_t)((-(int64_t)radiusX * cosine) >> SLIP_CROSS_EFFECT_ARM_SCALE_SHIFT);
	const int32_t secondY = (int32_t)(((int64_t)radiusY * sine) >> SLIP_CROSS_EFFECT_ARM_SCALE_SHIFT);
	int16_t minX, minY, maxX, maxY;
	Raster_GetClipRect(&minX, &minY, &maxX, &maxY);
	Raster_SetClipRect((int16_t)state->minX, (int16_t)state->minY, (int16_t)state->maxX, (int16_t)state->maxY);
	Raster_DrawLineClipped(color, (int16_t)(x - firstX), (int16_t)(y - firstY), (int16_t)(x + firstX),
	                       (int16_t)(y + firstY));
	Raster_DrawLineClipped(color, (int16_t)(x - secondX), (int16_t)(y - secondY), (int16_t)(x + secondX),
	                       (int16_t)(y + secondY));
	Raster_SetClipRect(minX, minY, maxX, maxY);
	return true;
}

bool TrackView_QueueAnimatedEffect(TrackViewRawBspContext *context, uint32_t objectRecord) {
	SlipView3DVec32 view;
	if (context == NULL || SlipDraw3D_listPool == NULL ||
	    !SlipObject_ViewPosition(context->objectTable, context->objectTableBytes, (uint16_t)objectRecord, &view)) {
		return false;
	}
	/* The caller's draw dispatch does not consume the insertion carry flag. */
	SlipDraw3D_ListInsert(&SlipDraw3D_listState, SlipDraw3D_listPool,
	                      (size_t)SlipDraw3D_listState.capacity * sizeof(*SlipDraw3D_listPool), (uint32_t)view.z,
	                      TrackView_DrawAnimatedEffect,
	                      ((uint32_t)view.x & SLIP_CALLBACK_PAYLOAD_UPPER_WORD_MASK) | (uint16_t)objectRecord);
	return true;
}

bool TrackView_DrawAnimatedEffect(TrackViewRawBspContext *context, uint32_t objectRecord) {
	const uint16_t objectOffset = (uint16_t)objectRecord;
	SlipView3DVec32 view;
	if (context == NULL || context->objectTable == NULL ||
	    (size_t)objectOffset + SLIP_OBJECT_DOS_STRIDE > context->objectTableBytes)
		return false;
	const SlipObject *const object = &context->objectTable[objectOffset / SLIP_OBJECT_DOS_STRIDE];
	const uint16_t spriteHandle = (uint16_t)object->drawData;
	const int32_t radius = (int32_t)object->drawExtent;
	if (!SlipObject_ViewPosition(context->objectTable, context->objectTableBytes, objectOffset, &view))
		return false;

	if (TrackView_RenderDiagnosticsEnabled())
		fprintf(stderr, "animated_draw object=%u view=%d,%d,%d radius=%d sprite=%u clip=%d,%d\n", objectOffset, view.x,
		        view.y, view.z, radius, spriteHandle, (int)context->projectState->minY,
		        (int)context->projectState->maxY);
	return TrackView_DrawSprite(context, view, radius, spriteHandle);
}

bool TrackView_QueueTimedEffect(TrackViewRawBspContext *context, uint32_t objectRecord) {
	SlipView3DVec32 view;
	if (context == NULL || SlipDraw3D_listPool == NULL ||
	    !SlipObject_ViewPosition(context->objectTable, context->objectTableBytes, (uint16_t)objectRecord, &view)) {
		return false;
	}
	/* The caller's draw dispatch does not consume the insertion carry flag. */
	SlipDraw3D_ListInsert(&SlipDraw3D_listState, SlipDraw3D_listPool,
	                      (size_t)SlipDraw3D_listState.capacity * sizeof(*SlipDraw3D_listPool), (uint32_t)view.z,
	                      TrackView_DrawTimedEffect, objectRecord);
	return true;
}

bool TrackView_DrawTimedEffect(TrackViewRawBspContext *context, uint32_t objectRecord) {
	const uint16_t objectOffset = (uint16_t)objectRecord;
	SlipView3DVec32 view;
	if (context == NULL || context->objectTable == NULL ||
	    (size_t)objectOffset + SLIP_OBJECT_DOS_STRIDE > context->objectTableBytes)
		return false;
	const SlipObject *const object = &context->objectTable[objectOffset / SLIP_OBJECT_DOS_STRIDE];
	const uint16_t spriteHandle = (uint16_t)object->drawData;
	const int32_t radius = (int32_t)object->drawExtent;
	if (!SlipObject_ViewPosition(context->objectTable, context->objectTableBytes, objectOffset, &view))
		return false;
	return TrackView_DrawSprite(context, view, radius, spriteHandle);
}

static const TrackViewImpactSprites *TrackView_impactSprites;
static uint16_t TrackView_previousImpactSprite;

static const SlipTrackSlotRecord *TrackView_RefuelSlot(void *user, uint16_t object) {
	TrackViewRawBspContext *const context = user;
	SlipTrackWorldSlotListSelect selected;
	(void)SlipTrackWorld_SelectSlotListEntry(0, context->slotListBaseAddress, context->objectTable,
	                                         context->objectTableBytes, object, &selected);
	return (const void *)(context->slotListBase + (selected.slotAddress - context->slotListBaseAddress));
}

static void TrackView_RefuelPosition(void *user, uint16_t object, uint32_t partTag, uint32_t pointTag,
                                     SlipView3DVec32 *position) {
	TrackViewRawBspContext *const context = user;
	SlipArticSlotPosition result = {(uint32_t)position->x, (uint32_t)position->y, (uint32_t)position->z, false};
	(void)SlipArticSlot_WorldPosition(
	    partTag, pointTag, object, context->objectTable, context->objectTableBytes, context->articSlotPool,
	    context->articSlotPoolBytes, context->articSlotPoolAddress, context->articSlotPool, context->articSlotPoolBytes,
	    context->articSlotPoolAddress, context->maths, &result);
	*position = (SlipView3DVec32){(int32_t)result.positionX, (int32_t)result.positionY, (int32_t)result.positionZ};
}

static const SlipView3DMatrix *TrackView_RefuelMatrix(void *user, uint16_t object) {
	TrackViewRawBspContext *const context = user;
	static SlipView3DMatrix matrix;
	SlipObjectMatrixCopy copied;
	(void)SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, object, &matrix, &copied);
	return &matrix;
}

uint16_t TrackView_StepEffectRandom(void *user) {
	(void)user;
	TrackView_effectRandomState = SlipTrackWorld_RandomStep(TrackView_effectRandomState);
	return TrackView_effectRandomState;
}

static void TrackView_RefuelClip(void *user, SlipView3DVec32 start, SlipView3DVec32 *end) {
	TrackViewRawBspContext *const context = user;
	(void)SlipTrackWorld_ClipRefuelBeam(context->chunkBase, context->chunkBaseBytes, context->chunkBaseToken,
	                                    context->componentBase, context->componentBaseBytes, context->trackCellTable,
	                                    context->trackCellTableBytes, start, *end, end);
}

static SlipTrackBeamRecord *TrackView_RefuelAllocate(void *user) {
	(void)user;
	return SlipTrackWorld_AllocateBeam(&SlipTrackWorld_beams);
}

void TrackView_BuildRefuelBeams(void *user, const uint8_t *section, uint32_t incomingBeamX,
                                const SlipTrackWorldDrawFlags *flags, uint32_t *active) {
	TrackViewRawBspContext *const context = user;
	const SlipRefuelBeamCalls calls = {context,
	                                   TrackView_RefuelSlot,
	                                   TrackView_RefuelPosition,
	                                   TrackView_RefuelMatrix,
	                                   TrackView_StepEffectRandom,
	                                   TrackView_RefuelClip,
	                                   TrackView_RefuelAllocate};
	context->rendererFlags = flags->rendererFlags;
	TrackView_refuelBeams.section = SlipRacePlayer_refuelSection;
	TrackView_refuelBeams.built = SlipRace_refuelBeamsBuilt;
	const SlipTrackSectionDrawLinks *const sectionLinks = (const void *)section;
	SlipRefuel_BuildBeams(&TrackView_refuelBeams, context->chunkBaseToken + (uint32_t)(section - context->chunkBase),
	                      sectionLinks->firstDrawOffset, (const void *)context->slotDrawBase,
	                      context->slotDrawBaseAddress, context->maths, incomingBeamX, flags->rendererSetFlagsTarget,
	                      &calls);
	SlipRace_refuelBeamsBuilt = TrackView_refuelBeams.built;
	*active = TrackView_refuelBeams.active;
}

void TrackView_SetImpactSprites(const TrackViewImpactSprites *sprites) { TrackView_impactSprites = sprites; }

static uint16_t TrackView_SelectImpactSprite(const TrackViewImpactSprites *sprites, uint16_t previous) {
	uint16_t selected = 0;
	for (unsigned attempts = SLIP_IMPACT_SPRITE_SELECTION_ATTEMPTS; attempts != 0; --attempts) {
		TrackView_effectRandomState = SlipTrackWorld_RandomStep(TrackView_effectRandomState);
		const uint16_t index = (uint16_t)(((uint32_t)TrackView_effectRandomState * sprites->count) >> SLIP_WORD_BITS);
		selected = sprites->handles[index];
		if (selected != previous)
			break;
	}
	return selected;
}

static bool TrackView_DrawImpact(TrackViewRawBspContext *context, SlipView3DVec32 endpoint) {
	if (TrackView_impactSprites == NULL)
		return true;
	TrackView_previousImpactSprite =
	    TrackView_SelectImpactSprite(TrackView_impactSprites, TrackView_previousImpactSprite);
	return TrackView_DrawSprite(context, endpoint, SLIP_IMPACT_SPRITE_RADIUS, TrackView_previousImpactSprite);
}

static bool TrackView_DrawPointPolygon(TrackViewRawBspContext *context, const SlipDraw3DVec32 *const *points,
                                       uint16_t count, uint32_t color, const uint16_t *shades) {
	SlipDraw3DReturnActiveVisit returnedVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DReturnActiveRing returned;
	if (!SlipDraw3D_ReturnActiveRing(context->drawRecordPool, returnedVisits, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT,
	                                 &returned))
		return false;
	SlipDraw3DClipFlagVisit flags[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneBoundsVisit bounds[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneClipRecordVisit records[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneClipPlaneVisit planes[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	TrackViewPostPlaneArgs post = TrackView_PostPlaneArgs(context);
	SlipDraw3DPointPolygon polygon;
	if (!SlipDraw3D_PointPolygon(
	        context->drawRecordPool, points, shades, count, color, context->projectState, post.hasPostPlanes,
	        post.planeBase, post.planeBytes, post.planeHeadOffset, post.limitXMin, post.limitXMax, post.limitYMin,
	        post.limitYMax, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, flags, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, bounds,
	        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, records, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, planes,
	        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, &polygon))
		return false;
	if (polygon.carryOut)
		return true;
	SlipDraw3DRasterPoint rasterPoints[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DFlatRingDispatch raster;
	if (!SlipDraw3D_RasterizeFlatRing(context->drawRecordPool, context->drawRecordPool->inputActiveHeadOffset,
	                                  polygon.drawMode, context->rendererFlags, color, 0,
	                                  context->reverseTraversal == 0 ? SLIP_DRAW3D_RECORD_NEXT_OFFSET
	                                                                 : SLIP_DRAW3D_RECORD_PREV_OFFSET,
	                                  rasterPoints, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, &raster))
		return false;
	++context->emitPathRasterizedCount;
	return true;
}

enum {
	SLIP_BLASTER_OUTER_HALF_WIDTH = 160,
	SLIP_BLASTER_INNER_HALF_WIDTH = 60,
	SLIP_BLASTER_DRAW_IMPACT = 0x80000000u,
	SLIP_BEAM_COLOUR_MASK = 0x7fff,
	SLIP_BEAM_OUTER_COLOUR_SHIFT = 16,
	SLIP_REFUEL_BEAM_HALF_WIDTH = 64,
	SLIP_REFUEL_BEAM_SEGMENT_SHIFT = 3,
	SLIP_REFUEL_BEAM_SEGMENT_COUNT = 1 << SLIP_REFUEL_BEAM_SEGMENT_SHIFT,
	SLIP_REFUEL_BEAM_MINIMUM_DISPLACEMENT = 512,
	SLIP_REFUEL_BEAM_DISPLACEMENT_RANGE = 2048,
	SLIP_REFUEL_BEAM_DISPLACEMENT_SIGN_BIT = 0x8000,
	SLIP_REFUEL_BEAM_FACING_LIMIT_Q14 = SLIP_Q14_ONE - 3
};

bool TrackView_DrawBeam(TrackViewRawBspContext *context, uint32_t beamIndex) {
	if (context == NULL || context->projectState == NULL || context->drawRecordPool == NULL ||
	    beamIndex >= SlipTrackWorld_beams.recordCount)
		return false;
	const SlipTrackBeamRecord *const beam =
	    &(SlipTrackWorld_beams.resourceRecords ? SlipTrackWorld_beams.resourceRecords
	                                           : SlipTrackWorld_beams.records)[beamIndex];
	SlipObjectPosition camera;
	SlipObjectMatrixCopy copied;
	SlipView3DMatrix basis;
	if (!SlipObject_Position(context->objectTable, context->objectTableBytes, 0, &camera) ||
	    !SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, 0, &basis, &copied))
		return false;
	SlipView3DVec32 start = {(int32_t)((uint32_t)beam->start.x - camera.positionX),
	                         (int32_t)((uint32_t)beam->start.y - camera.positionY),
	                         (int32_t)((uint32_t)beam->start.z - camera.positionZ)};
	SlipView3DVec32 end = {(int32_t)((uint32_t)beam->end.x - camera.positionX),
	                       (int32_t)((uint32_t)beam->end.y - camera.positionY),
	                       (int32_t)((uint32_t)beam->end.z - camera.positionZ)};
	start = SlipView3D_TransformPositionByRows(&basis, start);
	end = SlipView3D_TransformPositionByRows(&basis, end);
	SlipView3DVec32 delta = {(int32_t)((uint32_t)end.x - (uint32_t)start.x),
	                         (int32_t)((uint32_t)end.y - (uint32_t)start.y),
	                         (int32_t)((uint32_t)end.z - (uint32_t)start.z)};
	SlipView3DNormalizeVector3D direction;
	if (!SlipView3D_NormalizeVector3D((uint32_t)delta.x, (uint32_t)delta.y, (uint32_t)delta.z, &direction))
		return false;
	if (beam->type != SLIP_TRACK_BEAM_BLASTER && (int16_t)direction.unitZQ14 > SLIP_REFUEL_BEAM_FACING_LIMIT_Q14)
		return true;
	SlipView3D_BuildFacingBasis(&basis, (int16_t)direction.unitXQ14, (int16_t)direction.unitYQ14,
	                            (int16_t)direction.unitZQ14, 0, 0, -SLIP_Q14_ONE);
	SlipTrackWorldAxisPlaneClassify facing;
	if (!SlipTrackWorld_ClassifyAxisPlane(context->projectState->projectionMode, (uint32_t)start.x, (uint32_t)start.y,
	                                      (uint32_t)start.z, (uint16_t)basis.m[3], (uint16_t)basis.m[4],
	                                      (uint16_t)basis.m[5], &facing))
		return false;
	if (facing.carry) {
		if (context->maths == NULL)
			return false;
		SlipView3D_ApplyRow0Row1Rotation(context->maths, (int16_t)SLIP_ANGLE_HALF_TURN, &basis);
		if (!SlipTrackWorld_ClassifyAxisPlane(context->projectState->projectionMode, (uint32_t)start.x,
		                                      (uint32_t)start.y, (uint32_t)start.z, (uint16_t)basis.m[3],
		                                      (uint16_t)basis.m[4], (uint16_t)basis.m[5], &facing))
			return false;
		if (facing.carry)
			return true;
	}
	SlipDraw3DVec32 corners[SLIP_POLYGON_RECTANGLE_VERTICES];
	const SlipDraw3DVec32 *points[SLIP_POLYGON_RECTANGLE_VERTICES] = {corners, corners + 1, corners + 2, corners + 3};
	if (beam->type == SLIP_TRACK_BEAM_BLASTER) {
		if ((beam->material & SLIP_BLASTER_DRAW_IMPACT) != 0 && beam->continuation == 0 &&
		    !TrackView_DrawImpact(context, end))
			return false;
		{
			SlipView3DVec32 startLeftOffset =
			    SlipView3D_TransformPosition16(&basis, (SlipView3DVec32){-SLIP_BLASTER_OUTER_HALF_WIDTH, 0, 0});
			corners[0] = (SlipDraw3DVec32){(int32_t)((uint32_t)start.x + (uint32_t)startLeftOffset.x),
			                               (int32_t)((uint32_t)start.y + (uint32_t)startLeftOffset.y),
			                               (int32_t)((uint32_t)start.z + (uint32_t)startLeftOffset.z)};
			SlipView3DVec32 endLeftOffset =
			    SlipView3D_TransformPosition16(&basis, (SlipView3DVec32){-SLIP_BLASTER_OUTER_HALF_WIDTH, 0, 0});
			corners[1] = (SlipDraw3DVec32){(int32_t)((uint32_t)end.x + (uint32_t)endLeftOffset.x),
			                               (int32_t)((uint32_t)end.y + (uint32_t)endLeftOffset.y),
			                               (int32_t)((uint32_t)end.z + (uint32_t)endLeftOffset.z)};
			SlipView3DVec32 endRightOffset =
			    SlipView3D_TransformPosition16(&basis, (SlipView3DVec32){SLIP_BLASTER_OUTER_HALF_WIDTH, 0, 0});
			corners[2] = (SlipDraw3DVec32){(int32_t)((uint32_t)end.x + (uint32_t)endRightOffset.x),
			                               (int32_t)((uint32_t)end.y + (uint32_t)endRightOffset.y),
			                               (int32_t)((uint32_t)end.z + (uint32_t)endRightOffset.z)};
			SlipView3DVec32 startRightOffset =
			    SlipView3D_TransformPosition16(&basis, (SlipView3DVec32){SLIP_BLASTER_OUTER_HALF_WIDTH, 0, 0});
			corners[3] = (SlipDraw3DVec32){(int32_t)((uint32_t)start.x + (uint32_t)startRightOffset.x),
			                               (int32_t)((uint32_t)start.y + (uint32_t)startRightOffset.y),
			                               (int32_t)((uint32_t)start.z + (uint32_t)startRightOffset.z)};
			if (!TrackView_DrawPointPolygon(context, points, SLIP_POLYGON_RECTANGLE_VERTICES,
			                                (beam->material >> SLIP_BEAM_OUTER_COLOUR_SHIFT) & SLIP_BEAM_COLOUR_MASK,
			                                NULL))
				return false;
		}

		{
			SlipView3DVec32 startLeftOffset =
			    SlipView3D_TransformPosition16(&basis, (SlipView3DVec32){-SLIP_BLASTER_INNER_HALF_WIDTH, 0, 0});
			corners[0] = (SlipDraw3DVec32){(int32_t)((uint32_t)start.x + (uint32_t)startLeftOffset.x),
			                               (int32_t)((uint32_t)start.y + (uint32_t)startLeftOffset.y),
			                               (int32_t)((uint32_t)start.z + (uint32_t)startLeftOffset.z)};
			SlipView3DVec32 endLeftOffset =
			    SlipView3D_TransformPosition16(&basis, (SlipView3DVec32){-SLIP_BLASTER_INNER_HALF_WIDTH, 0, 0});
			corners[1] = (SlipDraw3DVec32){(int32_t)((uint32_t)end.x + (uint32_t)endLeftOffset.x),
			                               (int32_t)((uint32_t)end.y + (uint32_t)endLeftOffset.y),
			                               (int32_t)((uint32_t)end.z + (uint32_t)endLeftOffset.z)};
			SlipView3DVec32 endRightOffset =
			    SlipView3D_TransformPosition16(&basis, (SlipView3DVec32){SLIP_BLASTER_INNER_HALF_WIDTH, 0, 0});
			corners[2] = (SlipDraw3DVec32){(int32_t)((uint32_t)end.x + (uint32_t)endRightOffset.x),
			                               (int32_t)((uint32_t)end.y + (uint32_t)endRightOffset.y),
			                               (int32_t)((uint32_t)end.z + (uint32_t)endRightOffset.z)};
			SlipView3DVec32 startRightOffset =
			    SlipView3D_TransformPosition16(&basis, (SlipView3DVec32){SLIP_BLASTER_INNER_HALF_WIDTH, 0, 0});
			corners[3] = (SlipDraw3DVec32){(int32_t)((uint32_t)start.x + (uint32_t)startRightOffset.x),
			                               (int32_t)((uint32_t)start.y + (uint32_t)startRightOffset.y),
			                               (int32_t)((uint32_t)start.z + (uint32_t)startRightOffset.z)};
			if (!TrackView_DrawPointPolygon(context, points, SLIP_POLYGON_RECTANGLE_VERTICES,
			                                beam->material & SLIP_BEAM_COLOUR_MASK, NULL))
				return false;
		}

	} else {

		SlipView3DVec32 step = {delta.x >> SLIP_REFUEL_BEAM_SEGMENT_SHIFT, delta.y >> SLIP_REFUEL_BEAM_SEGMENT_SHIFT,
		                        delta.z >> SLIP_REFUEL_BEAM_SEGMENT_SHIFT};
		SlipView3DVec32 center = start;
		TrackView_effectRandomState = SlipTrackWorld_RandomStep(TrackView_effectRandomState);
		const uint32_t color =
		    (TrackView_effectRandomState & (SLIP_REFUEL_COLOUR_RAMP_END - SLIP_REFUEL_COLOUR_RAMP_START)) +
		    SLIP_REFUEL_COLOUR_RAMP_START;
		uint32_t sign = 0;
		for (unsigned segment = 0; segment < SLIP_REFUEL_BEAM_SEGMENT_COUNT; ++segment) {
			center.x = (int32_t)((uint32_t)center.x + (uint32_t)step.x);
			center.y = (int32_t)((uint32_t)center.y + (uint32_t)step.y);
			center.z = (int32_t)((uint32_t)center.z + (uint32_t)step.z);
			TrackView_effectRandomState = SlipTrackWorld_RandomStep(TrackView_effectRandomState);
			uint16_t displacement =
			    (uint16_t)(((uint32_t)TrackView_effectRandomState * SLIP_REFUEL_BEAM_DISPLACEMENT_RANGE) >>
			               SLIP_WORD_BITS) +
			    SLIP_REFUEL_BEAM_MINIMUM_DISPLACEMENT;
			sign = ~sign;
			if ((sign & SLIP_REFUEL_BEAM_DISPLACEMENT_SIGN_BIT) == 0)
				displacement = (uint16_t)(0u - displacement);
			SlipView3DVec32 offset =
			    SlipView3D_TransformPosition16(&basis, (SlipView3DVec32){(int16_t)displacement, 0, 0});
			end = (SlipView3DVec32){(int32_t)((uint32_t)center.x + (uint32_t)offset.x),
			                        (int32_t)((uint32_t)center.y + (uint32_t)offset.y),
			                        (int32_t)((uint32_t)center.z + (uint32_t)offset.z)};
			{
				SlipView3DVec32 startLeftOffset =
				    SlipView3D_TransformPosition16(&basis, (SlipView3DVec32){-SLIP_REFUEL_BEAM_HALF_WIDTH, 0, 0});
				corners[0] = (SlipDraw3DVec32){(int32_t)((uint32_t)start.x + (uint32_t)startLeftOffset.x),
				                               (int32_t)((uint32_t)start.y + (uint32_t)startLeftOffset.y),
				                               (int32_t)((uint32_t)start.z + (uint32_t)startLeftOffset.z)};
				SlipView3DVec32 endLeftOffset =
				    SlipView3D_TransformPosition16(&basis, (SlipView3DVec32){-SLIP_REFUEL_BEAM_HALF_WIDTH, 0, 0});
				corners[1] = (SlipDraw3DVec32){(int32_t)((uint32_t)end.x + (uint32_t)endLeftOffset.x),
				                               (int32_t)((uint32_t)end.y + (uint32_t)endLeftOffset.y),
				                               (int32_t)((uint32_t)end.z + (uint32_t)endLeftOffset.z)};
				SlipView3DVec32 endRightOffset =
				    SlipView3D_TransformPosition16(&basis, (SlipView3DVec32){SLIP_REFUEL_BEAM_HALF_WIDTH, 0, 0});
				corners[2] = (SlipDraw3DVec32){(int32_t)((uint32_t)end.x + (uint32_t)endRightOffset.x),
				                               (int32_t)((uint32_t)end.y + (uint32_t)endRightOffset.y),
				                               (int32_t)((uint32_t)end.z + (uint32_t)endRightOffset.z)};
				SlipView3DVec32 startRightOffset =
				    SlipView3D_TransformPosition16(&basis, (SlipView3DVec32){SLIP_REFUEL_BEAM_HALF_WIDTH, 0, 0});
				corners[3] = (SlipDraw3DVec32){(int32_t)((uint32_t)start.x + (uint32_t)startRightOffset.x),
				                               (int32_t)((uint32_t)start.y + (uint32_t)startRightOffset.y),
				                               (int32_t)((uint32_t)start.z + (uint32_t)startRightOffset.z)};
				if (!TrackView_DrawPointPolygon(context, points, SLIP_POLYGON_RECTANGLE_VERTICES, color, NULL))
					return false;
			}

			start = end;
		}
	}
	return true;
}

static void TrackView_DrawCloudLayer(TrackViewRawBspContext *context, uint16_t layerResource,
                                     const SlipView3DMaths *maths, uint16_t yaw, const SlipView3DMatrix *viewMatrix,
                                     uint32_t detailLevel, const TrackViewCloudEntry *entries, uint16_t entryCount,
                                     const SlipResourcePayload *spritePayloads, size_t spritePayloadCount,
                                     unsigned *drawnCount, unsigned *skippedCount) {
	SlipView3DMatrix rotation;
	uint16_t i;

	if (context == NULL || maths == NULL || viewMatrix == NULL || entries == NULL || entryCount == 0u ||
	    spritePayloads == NULL) {
		return;
	}

	SlipView3D_BuildYawMatrix(maths, (int16_t)yaw, &rotation);
	if (layerResource != 0) {
		const uint8_t *const layer = SlipResourceHost_Lock(NULL, layerResource);
		entryCount = SlipBytes_ReadLE16(layer);
		entries = (const TrackViewCloudEntry *)(const void *)(layer + 2);
	}
	for (i = 0; i < entryCount; ++i) {
		const TrackViewCloudEntry *const entry = &entries[i];
		const uint16_t handle = entry->spriteHandle;
		int16_t spriteWidth;
		SlipView3DVec32 local;
		SlipView3DVec32 rotated;
		SlipView3DVec32 view;
		const SlipResourcePayload *sprite = NULL;
		bool skip = false;

		spriteWidth = (int16_t)entry->width;
		if (spriteWidth >= SLIP_CLOUD_FULL_DETAIL_MINIMUM_WIDTH && detailLevel < SLIP_RENDER_FULL_DETAIL_LEVEL) {
			skip = true;
		}
		if (!skip) {
			if (layerResource != 0) {
				sprite = NULL;
			} else if (handle == 0u || (size_t)handle > spritePayloadCount) {
				skip = true;
			} else
				sprite = spritePayloads + (handle - 1u);
		}
		if (!skip) {
			local.x = entry->position.x;
			local.y = entry->position.y;
			local.z = entry->position.z;

			rotated = SlipView3D_TransformPosition16(&rotation, local);
			view = SlipView3D_TransformVector(viewMatrix, rotated);

			if (view.z < 0) {
				skip = true;
			}
		}
		if (!skip) {
			int16_t screenX;
			int16_t screenY;

			if (TrackView_DrawCloudSprite(context, sprite, handle, view, &screenX, &screenY, layerResource != 0)) {
				if (drawnCount != NULL) {
					++*drawnCount;
				}
			} else {
				skip = true;
			}
		}
		if (skip && skippedCount != NULL) {
			++*skippedCount;
		}
	}
	if (layerResource != 0)
		SlipResourceHost_Unlock(NULL, layerResource);
}

static void TrackView_DrawSilhouetteLayer(TrackViewRawBspContext *context, uint16_t layerResource,
                                          const SlipView3DMatrix *viewMatrix, const TrackViewCloudEntry *entries,
                                          uint16_t entryCount, const SlipResourcePayload *spritePayloads,
                                          size_t spritePayloadCount, unsigned *drawnCount, unsigned *skippedCount) {
	uint16_t i;

	if (context == NULL || viewMatrix == NULL || entries == NULL || entryCount == 0u || spritePayloads == NULL) {
		return;
	}
	if (layerResource != 0) {
		const uint8_t *const layer = SlipResourceHost_Lock(NULL, layerResource);
		entryCount = SlipBytes_ReadLE16(layer);
		entries = (const TrackViewCloudEntry *)(const void *)(layer + 2);
	}
	for (i = 0; i < entryCount; ++i) {
		const TrackViewCloudEntry *const entry = &entries[i];
		const uint16_t handle = entry->spriteHandle;
		SlipView3DVec32 local;
		SlipView3DVec32 view;
		const SlipResourcePayload *sprite = NULL;

		if (layerResource == 0 && handle != 0u && (size_t)handle <= spritePayloadCount) {
			sprite = spritePayloads + (handle - 1u);
		}
		if (layerResource == 0 && sprite == NULL) {
			if (skippedCount != NULL) {
				++*skippedCount;
			}
			continue;
		}
		local.x = entry->position.x;

		local.y = 0;
		local.z = entry->position.z;

		view = SlipView3D_TransformVector(viewMatrix, local);

		if (view.z < 0) {
			if (skippedCount != NULL) {
				++*skippedCount;
			}
			continue;
		}
		{
			int16_t screenX;
			int16_t screenY;

			if (TrackView_DrawCloudSprite(context, sprite, handle, view, &screenX, &screenY, layerResource != 0)) {
				if (drawnCount != NULL) {
					++*drawnCount;
				}
			} else if (skippedCount != NULL) {
				++*skippedCount;
			}
		}
	}
	if (layerResource != 0)
		SlipResourceHost_Unlock(NULL, layerResource);
}

static const uint8_t kEgyptNearCloudLayout[] = {
    0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x01, 0x00, 0x00, 0x30, 0x80, 0x0a, 0x02, 0x00, 0x00, 0x53, 0x20,
    0x0b, 0x03, 0x00, 0x00, 0x90, 0x00, 0x0a, 0x04, 0x00, 0x00, 0xb8, 0x20, 0x0a, 0x05, 0x00, 0x00, 0xe0, 0x80, 0x09};

static const uint8_t kChicagoNearCloudLayout[] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x01, 0x00,
                                                  0x00, 0x30, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x80, 0x00, 0x0a};

static const uint8_t kChicagoFarCloudLayout[] = {
    0x0f, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x20, 0x00, 0x02, 0x04, 0x00, 0x00, 0x40, 0x00,
    0x02, 0x05, 0x00, 0x00, 0x60, 0x00, 0x02, 0x06, 0x00, 0x00, 0x80, 0x00, 0x02, 0x07, 0x00, 0x00, 0xa0, 0x00, 0x02,
    0x08, 0x00, 0x00, 0xc0, 0x00, 0x02, 0x09, 0x00, 0x00, 0x10, 0x00, 0x07, 0x0a, 0x00, 0x00, 0x30, 0x20, 0x07, 0x0b,
    0x00, 0x00, 0x50, 0x40, 0x07, 0x0c, 0x00, 0x00, 0x70, 0x00, 0x07, 0x0d, 0x00, 0x00, 0x90, 0x20, 0x07, 0x0e, 0x00,
    0x00, 0xb0, 0x40, 0x07, 0x0f, 0x00, 0x00, 0xd0, 0x80, 0x06, 0x10, 0x00, 0x00, 0xf0, 0xb0, 0x06};

static const uint8_t kHawaiiNearCloudLayout[] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                 0x0d, 0x01, 0x00, 0x00, 0x40, 0x00, 0x0d};

static const uint8_t kHawaiiFarCloudLayout[] = {
    0x0f, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x07, 0x03, 0x00, 0x00, 0x20, 0x00, 0x07, 0x04, 0x00, 0x00, 0x40, 0x00,
    0x07, 0x05, 0x00, 0x00, 0x60, 0x00, 0x07, 0x06, 0x00, 0x00, 0x80, 0x00, 0x07, 0x07, 0x00, 0x00, 0xa0, 0x00, 0x07,
    0x08, 0x00, 0x00, 0xc0, 0x00, 0x07, 0x09, 0x00, 0x00, 0xd0, 0x00, 0x07, 0x0a, 0x00, 0x00, 0xe0, 0x00, 0x07, 0x0b,
    0x00, 0x00, 0x50, 0x80, 0x02, 0x0c, 0x00, 0x00, 0x70, 0x80, 0x02, 0x0d, 0x00, 0x00, 0x90, 0x80, 0x02, 0x0e, 0x00,
    0x00, 0xb0, 0x80, 0x02, 0x0f, 0x00, 0x00, 0xd0, 0x80, 0x02, 0x10, 0x00, 0x00, 0xf0, 0x80, 0x02};

static const uint8_t kTokyoSilhouetteLayout[] = {
    0x06, 0x00, 0x02, 0x00, 0x00, 0xd8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x01, 0x00, 0x00, 0x40, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x5e, 0x00, 0x00, 0x00, 0x00, 0x20, 0x76, 0x00, 0x00, 0x01, 0x00, 0x40, 0x91, 0x00, 0x00};

static const uint8_t kLondonSilhouetteLayout[] = {0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
                                                  0x00, 0x00, 0x52, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa1,
                                                  0x00, 0x00, 0x00, 0x00, 0x00, 0xd2, 0x00, 0x00};

static const uint8_t kFranceNearCloudLayout[] = {
    0x09, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x02, 0x00, 0x56, 0x21, 0x20, 0x04, 0x03, 0x00, 0x43, 0x3a, 0xf0,
    0x04, 0x02, 0x00, 0x43, 0x3f, 0x10, 0x04, 0x00, 0x00, 0x67, 0x61, 0xa0, 0x04, 0x03, 0x00, 0x34, 0x73, 0x50, 0x04,
    0x01, 0x00, 0x12, 0xaa, 0x30, 0x04, 0x00, 0x00, 0xe4, 0xc0, 0x60, 0x04, 0x02, 0x00, 0x4f, 0xe2, 0x90, 0x04};

static const uint8_t kFranceFarCloudLayout[] = {
    0x18, 0x00, 0x08, 0x00, 0x20, 0x01, 0x00, 0x02, 0x09, 0x00, 0x40, 0x13, 0x20, 0x02, 0x0a, 0x00, 0x20, 0x21, 0xe0,
    0x01, 0x08, 0x00, 0x70, 0x32, 0x00, 0x02, 0x0a, 0x00, 0x45, 0x40, 0xb4, 0x01, 0x09, 0x00, 0xf0, 0x50, 0x3c, 0x02,
    0x08, 0x00, 0x40, 0x61, 0xe7, 0x01, 0x0a, 0x00, 0x23, 0x71, 0x7f, 0x02, 0x08, 0x00, 0x20, 0x81, 0x90, 0x01, 0x0a,
    0x00, 0x00, 0x93, 0x07, 0x02, 0x09, 0x00, 0x20, 0xa1, 0x10, 0x02, 0x0a, 0x00, 0x00, 0xb2, 0xc0, 0x01, 0x09, 0x00,
    0xd0, 0xc0, 0x90, 0x01, 0x0a, 0x00, 0xe0, 0xd0, 0xd6, 0x01, 0x08, 0x00, 0x20, 0xe1, 0x24, 0x02, 0x0a, 0x00, 0xd7,
    0xf0, 0xe2, 0x01, 0x04, 0x00, 0x12, 0x08, 0x00, 0x03, 0x05, 0x00, 0x56, 0x28, 0x20, 0x03, 0x06, 0x00, 0x43, 0x48,
    0xf0, 0x02, 0x07, 0x00, 0x67, 0x68, 0xa0, 0x03, 0x05, 0x00, 0x34, 0x88, 0x50, 0x03, 0x07, 0x00, 0x12, 0xa8, 0x30,
    0x03, 0x04, 0x00, 0xe4, 0xc8, 0x60, 0x03, 0x06, 0x00, 0x4f, 0xe8, 0x90, 0x03};

static const uint8_t kNewYorkNearCloudLayout[] = {
    0x0e, 0x00, 0x08, 0x00, 0x12, 0xeb, 0x00, 0x01, 0x01, 0x00, 0x00, 0x7c, 0x30, 0x0a, 0x02, 0x00, 0x00, 0x8d,
    0x78, 0x01, 0x03, 0x00, 0x23, 0x9b, 0x00, 0x05, 0x04, 0x00, 0x45, 0xaa, 0x80, 0x07, 0x05, 0x00, 0x12, 0xbb,
    0x12, 0x03, 0x06, 0x00, 0x12, 0xca, 0x00, 0x03, 0x07, 0x00, 0x45, 0xdc, 0x70, 0x06, 0x09, 0x00, 0xff, 0xfc,
    0xd0, 0x00, 0x0a, 0x00, 0x22, 0x0c, 0x00, 0x08, 0x0b, 0x00, 0x56, 0x1c, 0x89, 0x06, 0x0c, 0x00, 0x23, 0x20,
    0x30, 0x01, 0x0d, 0x00, 0x00, 0x82, 0x00, 0x0c, 0x0e, 0x00, 0x00, 0x97, 0x00, 0x0b};

static const uint8_t kNewYorkFarCloudLayout[] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00};

enum {
	SLIP_CLOUD_LAYOUT_HEADER_BYTES = sizeof(uint16_t),
	SLIP_CLOUD_LAYOUT_RECORD_BYTES = 6,
	SLIP_CLOUD_LAYOUT_YAW_OFFSET = 2,
	SLIP_CLOUD_LAYOUT_PITCH_OFFSET = 4,
	SLIP_CLOUD_LAYER_HEADER_BYTES = sizeof(uint16_t),
	SLIP_CLOUD_ENTRY_BYTES = sizeof(TrackViewCloudEntry),
	SLIP_CLOUD_EGYPT_HAWAII_YAW_STEP = 0x10,
	SLIP_CLOUD_CHICAGO_YAW_STEP = 0x80,
	SLIP_CLOUD_FRANCE_YAW_STEP = 0x60
};

static bool TrackView_BuildCloudLayer(const SlipView3DMaths *maths, const uint8_t *layout, size_t layoutBytes,
                                      const SlipResourcePayload *spritePayloads, size_t spritePayloadCount,
                                      const uint16_t *spriteResources, uint16_t *layerResource, bool silhouette,
                                      TrackViewCloudEntry **entriesOut, uint16_t *entryCountOut) {
	(void)spritePayloadCount;
	uint16_t count;
	TrackViewCloudEntry *entries;
	uint16_t i;

	if (layout == NULL || layoutBytes < SLIP_CLOUD_LAYOUT_HEADER_BYTES || spritePayloads == NULL ||
	    entriesOut == NULL || entryCountOut == NULL) {
		return false;
	}
	count = SlipBytes_ReadLE16(layout);
	*entryCountOut = 0;
	*entriesOut = NULL;
	if (count == 0u ||
	    layoutBytes < (size_t)SLIP_CLOUD_LAYOUT_HEADER_BYTES + (size_t)count * SLIP_CLOUD_LAYOUT_RECORD_BYTES) {
		return false;
	}

	if (!SlipResourceHost_Allocate(NULL, (uint32_t)count * SLIP_CLOUD_ENTRY_BYTES + SLIP_CLOUD_LAYER_HEADER_BYTES, 0,
	                               layerResource))
		return false;
	uint8_t *const layerBytes = SlipResourceHost_LockWritable(NULL, *layerResource);
	TrackView_WriteLE16(layerBytes, count);
	entries = (TrackViewCloudEntry *)(void *)(layerBytes + SLIP_CLOUD_LAYER_HEADER_BYTES);
	for (i = 0; i < count; ++i) {
		const uint8_t *const record =
		    layout + SLIP_CLOUD_LAYOUT_HEADER_BYTES + (size_t)i * SLIP_CLOUD_LAYOUT_RECORD_BYTES;
		TrackViewCloudEntry *const entry = &entries[i];
		const uint16_t spriteIndex = SlipBytes_ReadLE16(record);
		const int16_t yaw = (int16_t)SlipBytes_ReadLE16(record + SLIP_CLOUD_LAYOUT_YAW_OFFSET);
		const int16_t pitch = (int16_t)SlipBytes_ReadLE16(record + SLIP_CLOUD_LAYOUT_PITCH_OFFSET);
		SlipView3DMatrix rotation;
		SlipView3DVec32 position;

		entry->spriteHandle = spriteResources[spriteIndex];

		const uint8_t *const spriteBytes = SlipResourceHost_Lock(NULL, entry->spriteHandle);
		entry->width = SlipBytes_ReadLE16(spriteBytes);
		entry->height = SlipBytes_ReadLE16(spriteBytes + SLIP_SPRITE_HEIGHT_OFFSET);
		SlipResourceHost_Unlock(NULL, entry->spriteHandle);

		SlipView3D_BuildYawMatrix(maths, yaw, &rotation);
		if (!silhouette)
			SlipView3D_ApplyPitchMatrix(maths, pitch, &rotation);

		position.x = rotation.m[6];
		position.y = rotation.m[7];
		position.z = rotation.m[8];
		entry->position.x = (int16_t)position.x;
		entry->position.y = (int16_t)position.y;
		entry->position.z = (int16_t)position.z;
	}
	SlipResourceHost_Unlock(NULL, *layerResource);
	*entriesOut = entries;
	*entryCountOut = count;
	return true;
}

static void TrackView_FreeCloudLayers(TrackViewCloudState *state) {
	if (state->nearResource != 0)
		SlipResourceHost_Release(NULL, state->nearResource);
	state->nearResource = 0;
	state->nearEntries = NULL;
	state->nearCount = 0;
	if (state->farResource != 0)
		SlipResourceHost_Release(NULL, state->farResource);
	state->farResource = 0;
	state->farEntries = NULL;
	state->farCount = 0;
}

static void TrackView_FreeSilhouetteLayer(TrackViewCloudState *state) {
	if (state->silhouetteResource != 0)
		SlipResourceHost_Release(NULL, state->silhouetteResource);
	state->silhouetteResource = 0;
	state->silhouetteEntries = NULL;
	state->silhouetteCount = 0;
}

void TrackView_ShutdownClouds(TrackViewCloudState *state) {
	TrackView_FreeCloudLayers(state);
	TrackView_FreeSilhouetteLayer(state);
}

static TrackViewCloudState *TrackView_exitCloudState;

static void TrackView_ExitClouds(void) { TrackView_ShutdownClouds(TrackView_exitCloudState); }

void TrackView_InitializeClouds(TrackViewCloudState *state) {
	SlipRace_cloudScrollPhase = 0;
	SlipRace_cloudScrollFinePhase = 0;
	TrackView_exitCloudState = state;
	SlipRuntime_RegisterExit(TrackView_ExitClouds);
}

static bool TrackView_BuildCloudLayers(TrackViewCloudState *state, const SlipView3DMaths *maths, uint16_t yawStep,
                                       const uint8_t *nearLayout, size_t nearLayoutBytes, const uint8_t *farLayout,
                                       size_t farLayoutBytes) {
	SlipRace_cloudScrollStep = yawStep;
	TrackView_FreeCloudLayers(state);
	if (nearLayout != NULL &&
	    !TrackView_BuildCloudLayer(maths, nearLayout, nearLayoutBytes, state->spritePayloads, state->spriteCount,
	                               state->spriteResources, &state->nearResource, false, &state->nearEntries,
	                               &state->nearCount)) {
		return false;
	}
	if (farLayout != NULL && !TrackView_BuildCloudLayer(maths, farLayout, farLayoutBytes, state->spritePayloads,
	                                                    state->spriteCount, state->spriteResources, &state->farResource,
	                                                    false, &state->farEntries, &state->farCount)) {
		return false;
	}
	return true;
}

static bool TrackView_BuildSilhouetteLayer(TrackViewCloudState *state, const SlipView3DMaths *maths,
                                           const uint8_t *layout, size_t layoutBytes) {
	if (layout == NULL || SlipBytes_ReadLE16(layout) == 0)
		return true;
	TrackView_FreeSilhouetteLayer(state);
	return TrackView_BuildCloudLayer(maths, layout, layoutBytes, state->spritePayloads, state->spriteCount,
	                                 state->spriteResources, &state->silhouetteResource, true,
	                                 &state->silhouetteEntries, &state->silhouetteCount);
}

static bool TrackView_BuildClouds(TrackViewCloudState *state, const SlipView3DMaths *maths, uint16_t yawStep,
                                  const uint8_t *nearLayout, size_t nearLayoutBytes, const uint8_t *farLayout,
                                  size_t farLayoutBytes, const uint8_t *silhouetteLayout,
                                  size_t silhouetteLayoutBytes) {

	bool cloudsBuilt =
	    TrackView_BuildCloudLayers(state, maths, yawStep, nearLayout, nearLayoutBytes, farLayout, farLayoutBytes);
	bool silhouettesBuilt = TrackView_BuildSilhouetteLayer(state, maths, silhouetteLayout, silhouetteLayoutBytes);
	return cloudsBuilt && silhouettesBuilt;
}

static bool TrackView_TrackLifecycleNoOp(const TrackViewTrackLifecycleArgs *args) {
	(void)args;
	return true;
}

static bool TrackView_LoadCloudSprites(TrackViewCloudState *state, const char *pattern, uint32_t first, uint16_t count,
                                       unsigned slot) {
	if (!SlipResourceHost_LoadSequence(NULL, pattern, first, count, state->spriteResources + slot))
		SlipGame_ResourceFailure();
	for (uint16_t i = 0; i < count; ++i)
		state->spritePayloads[slot + i] = SlipResourceHost_Payload(state->spriteResources[slot + i]);
	return true;
}

enum {
	SLIP_CLOUD_EGYPT_GROUP_A_COUNT = 2,
	SLIP_CLOUD_EGYPT_GROUP_B_COUNT = 2,
	SLIP_CLOUD_EGYPT_GROUP_C_COUNT = 2,
	SLIP_CLOUD_EGYPT_SPRITE_COUNT =
	    SLIP_CLOUD_EGYPT_GROUP_A_COUNT + SLIP_CLOUD_EGYPT_GROUP_B_COUNT + SLIP_CLOUD_EGYPT_GROUP_C_COUNT,
	SLIP_CLOUD_CHICAGO_GROUP_A_COUNT = 2,
	SLIP_CLOUD_CHICAGO_GROUP_B_COUNT = 7,
	SLIP_CLOUD_CHICAGO_GROUP_C_COUNT = 8,
	SLIP_CLOUD_CHICAGO_SPRITE_COUNT =
	    SLIP_CLOUD_CHICAGO_GROUP_A_COUNT + SLIP_CLOUD_CHICAGO_GROUP_B_COUNT + SLIP_CLOUD_CHICAGO_GROUP_C_COUNT,
	SLIP_CLOUD_HAWAII_GROUP_A_COUNT = 2,
	SLIP_CLOUD_HAWAII_GROUP_B_COUNT = 9,
	SLIP_CLOUD_HAWAII_GROUP_C_COUNT = 6,
	SLIP_CLOUD_HAWAII_SPRITE_COUNT =
	    SLIP_CLOUD_HAWAII_GROUP_A_COUNT + SLIP_CLOUD_HAWAII_GROUP_B_COUNT + SLIP_CLOUD_HAWAII_GROUP_C_COUNT,
	SLIP_CLOUD_TOKYO_GROUP_A_COUNT = 3,
	SLIP_CLOUD_TOKYO_SPRITE_COUNT = SLIP_CLOUD_TOKYO_GROUP_A_COUNT,
	SLIP_CLOUD_LONDON_GROUP_A_COUNT = 2,
	SLIP_CLOUD_LONDON_SPRITE_COUNT = SLIP_CLOUD_LONDON_GROUP_A_COUNT,
	SLIP_CLOUD_FRANCE_GROUP_A_COUNT = 4,
	SLIP_CLOUD_FRANCE_GROUP_B_COUNT = 4,
	SLIP_CLOUD_FRANCE_GROUP_C_COUNT = 3,
	SLIP_CLOUD_FRANCE_SPRITE_COUNT =
	    SLIP_CLOUD_FRANCE_GROUP_A_COUNT + SLIP_CLOUD_FRANCE_GROUP_B_COUNT + SLIP_CLOUD_FRANCE_GROUP_C_COUNT,
	SLIP_CLOUD_NEW_YORK_GROUP_A_COUNT = 1,
	SLIP_CLOUD_NEW_YORK_GROUP_B_COUNT = 14,
	SLIP_CLOUD_NEW_YORK_SPRITE_COUNT = SLIP_CLOUD_NEW_YORK_GROUP_A_COUNT + SLIP_CLOUD_NEW_YORK_GROUP_B_COUNT,
};

static bool TrackView_InitEgyptClouds(const TrackViewTrackLifecycleArgs *args) {
	TrackViewCloudState *const state = args->cloudState;

	if (!TrackView_LoadCloudSprites(state, "cancld*A.spr", 1u, SLIP_CLOUD_EGYPT_GROUP_A_COUNT, 0) ||
	    !TrackView_LoadCloudSprites(state, "cancld*B.spr", 1u, SLIP_CLOUD_EGYPT_GROUP_B_COUNT,
	                                SLIP_CLOUD_EGYPT_GROUP_A_COUNT) ||
	    !TrackView_LoadCloudSprites(state, "cancld*C.spr", 1u, SLIP_CLOUD_EGYPT_GROUP_C_COUNT,
	                                SLIP_CLOUD_EGYPT_GROUP_A_COUNT + SLIP_CLOUD_EGYPT_GROUP_B_COUNT)) {
		return false;
	}
	state->spriteCount = SLIP_CLOUD_EGYPT_SPRITE_COUNT;
	return TrackView_BuildClouds(state, args->maths, SLIP_CLOUD_EGYPT_HAWAII_YAW_STEP, kEgyptNearCloudLayout,
	                             sizeof(kEgyptNearCloudLayout), NULL, 0, NULL, 0);
}

static bool TrackView_ReleaseEgyptClouds(const TrackViewTrackLifecycleArgs *args) {
	SlipResourceHost_ReleaseSequence(NULL, args->cloudState->spriteResources, SLIP_CLOUD_EGYPT_SPRITE_COUNT);
	args->cloudState->spriteCount = 0;
	return true;
}

static bool TrackView_InitChicagoClouds(const TrackViewTrackLifecycleArgs *args) {
	TrackViewCloudState *const state = args->cloudState;

	if (!TrackView_LoadCloudSprites(state, "hawcld*A.spr", 1u, SLIP_CLOUD_CHICAGO_GROUP_A_COUNT, 0) ||
	    !TrackView_LoadCloudSprites(state, "hawcld*B.spr", 1u, SLIP_CLOUD_CHICAGO_GROUP_B_COUNT,
	                                SLIP_CLOUD_CHICAGO_GROUP_A_COUNT) ||
	    !TrackView_LoadCloudSprites(state, "hawcld*C.spr", 1u, SLIP_CLOUD_CHICAGO_GROUP_C_COUNT,
	                                SLIP_CLOUD_CHICAGO_GROUP_A_COUNT + SLIP_CLOUD_CHICAGO_GROUP_B_COUNT)) {
		return false;
	}
	state->spriteCount = SLIP_CLOUD_CHICAGO_SPRITE_COUNT;
	return TrackView_BuildClouds(state, args->maths, SLIP_CLOUD_CHICAGO_YAW_STEP, kChicagoNearCloudLayout,
	                             sizeof(kChicagoNearCloudLayout), kChicagoFarCloudLayout,
	                             sizeof(kChicagoFarCloudLayout), NULL, 0);
}

static bool TrackView_ReleaseChicagoClouds(const TrackViewTrackLifecycleArgs *args) {
	SlipResourceHost_ReleaseSequence(NULL, args->cloudState->spriteResources, SLIP_CLOUD_CHICAGO_SPRITE_COUNT);
	args->cloudState->spriteCount = 0;
	return true;
}

static bool TrackView_InitHawaiiClouds(const TrackViewTrackLifecycleArgs *args) {
	TrackViewCloudState *const state = args->cloudState;

	if (!TrackView_LoadCloudSprites(state, "loncld*A.spr", 1u, SLIP_CLOUD_HAWAII_GROUP_A_COUNT, 0) ||
	    !TrackView_LoadCloudSprites(state, "loncld*B.spr", 1u, SLIP_CLOUD_HAWAII_GROUP_B_COUNT,
	                                SLIP_CLOUD_HAWAII_GROUP_A_COUNT) ||
	    !TrackView_LoadCloudSprites(state, "loncld*C.spr", 1u, SLIP_CLOUD_HAWAII_GROUP_C_COUNT,
	                                SLIP_CLOUD_HAWAII_GROUP_A_COUNT + SLIP_CLOUD_HAWAII_GROUP_B_COUNT)) {
		return false;
	}
	state->spriteCount = SLIP_CLOUD_HAWAII_SPRITE_COUNT;
	return TrackView_BuildClouds(state, args->maths, SLIP_CLOUD_EGYPT_HAWAII_YAW_STEP, kHawaiiNearCloudLayout,
	                             sizeof(kHawaiiNearCloudLayout), kHawaiiFarCloudLayout, sizeof(kHawaiiFarCloudLayout),
	                             NULL, 0);
}

static bool TrackView_ReleaseHawaiiClouds(const TrackViewTrackLifecycleArgs *args) {
	SlipResourceHost_ReleaseSequence(NULL, args->cloudState->spriteResources, SLIP_CLOUD_HAWAII_SPRITE_COUNT);
	args->cloudState->spriteCount = 0;
	return true;
}

static bool TrackView_InitTokyoClouds(const TrackViewTrackLifecycleArgs *args) {
	TrackViewCloudState *const state = args->cloudState;

	if (!TrackView_LoadCloudSprites(state, "eghill*.spr", 1u, SLIP_CLOUD_TOKYO_GROUP_A_COUNT, 0)) {
		return false;
	}
	state->spriteCount = SLIP_CLOUD_TOKYO_SPRITE_COUNT;
	return TrackView_BuildClouds(state, args->maths, 0, NULL, 0, NULL, 0, kTokyoSilhouetteLayout,
	                             sizeof(kTokyoSilhouetteLayout));
}

static bool TrackView_ReleaseTokyoClouds(const TrackViewTrackLifecycleArgs *args) {
	SlipResourceHost_ReleaseSequence(NULL, args->cloudState->spriteResources, SLIP_CLOUD_TOKYO_SPRITE_COUNT);
	args->cloudState->spriteCount = 0;
	return true;
}

static bool TrackView_InitLondonClouds(const TrackViewTrackLifecycleArgs *args) {
	TrackViewCloudState *const state = args->cloudState;

	if (!TrackView_LoadCloudSprites(state, "norhill*.spr", 1u, SLIP_CLOUD_LONDON_GROUP_A_COUNT, 0)) {
		return false;
	}
	state->spriteCount = SLIP_CLOUD_LONDON_SPRITE_COUNT;
	return TrackView_BuildClouds(state, args->maths, 0, NULL, 0, NULL, 0, kLondonSilhouetteLayout,
	                             sizeof(kLondonSilhouetteLayout));
}

static bool TrackView_ReleaseLondonClouds(const TrackViewTrackLifecycleArgs *args) {
	SlipResourceHost_ReleaseSequence(NULL, args->cloudState->spriteResources, SLIP_CLOUD_LONDON_SPRITE_COUNT);
	args->cloudState->spriteCount = 0;
	return true;
}

static bool TrackView_InitFranceClouds(const TrackViewTrackLifecycleArgs *args) {
	TrackViewCloudState *const state = args->cloudState;

	if (!TrackView_LoadCloudSprites(state, "Amacld*A.spr", 1u, SLIP_CLOUD_FRANCE_GROUP_A_COUNT, 0) ||
	    !TrackView_LoadCloudSprites(state, "Amacld*B.spr", 1u, SLIP_CLOUD_FRANCE_GROUP_B_COUNT,
	                                SLIP_CLOUD_FRANCE_GROUP_A_COUNT) ||
	    !TrackView_LoadCloudSprites(state, "Amacld*C.spr", 1u, SLIP_CLOUD_FRANCE_GROUP_C_COUNT,
	                                SLIP_CLOUD_FRANCE_GROUP_A_COUNT + SLIP_CLOUD_FRANCE_GROUP_B_COUNT)) {
		return false;
	}
	state->spriteCount = SLIP_CLOUD_FRANCE_SPRITE_COUNT;
	return TrackView_BuildClouds(state, args->maths, SLIP_CLOUD_FRANCE_YAW_STEP, kFranceNearCloudLayout,
	                             sizeof(kFranceNearCloudLayout), kFranceFarCloudLayout, sizeof(kFranceFarCloudLayout),
	                             NULL, 0);
}

static bool TrackView_ReleaseFranceClouds(const TrackViewTrackLifecycleArgs *args) {
	SlipResourceHost_ReleaseSequence(NULL, args->cloudState->spriteResources, SLIP_CLOUD_FRANCE_SPRITE_COUNT);
	args->cloudState->spriteCount = 0;
	return true;
}

static bool TrackView_InitNewYorkClouds(const TrackViewTrackLifecycleArgs *args) {
	TrackViewCloudState *const state = args->cloudState;

	if (!SlipResourceHost_Load(NULL, "NYSUNS.spr", &state->spriteResources[0]) ||
	    !TrackView_LoadCloudSprites(state, "NYcl**.spr", 1u, SLIP_CLOUD_NEW_YORK_GROUP_B_COUNT,
	                                SLIP_CLOUD_NEW_YORK_GROUP_A_COUNT)) {
		SlipGame_ResourceFailure();
	}
	state->spriteCount = SLIP_CLOUD_NEW_YORK_SPRITE_COUNT;
	return TrackView_BuildClouds(state, args->maths, 0, kNewYorkNearCloudLayout, sizeof(kNewYorkNearCloudLayout),
	                             kNewYorkFarCloudLayout, sizeof(kNewYorkFarCloudLayout), NULL, 0);
}

static bool TrackView_ReleaseNewYorkClouds(const TrackViewTrackLifecycleArgs *args) {
	SlipResourceHost_ReleaseSequence(NULL, args->cloudState->spriteResources, SLIP_CLOUD_NEW_YORK_SPRITE_COUNT);
	args->cloudState->spriteCount = 0;
	return true;
}

const TrackViewTrackLifecycleCallback g_trackViewInitCallbacks[kDriverCount] = {
    TrackView_TrackLifecycleNoOp, TrackView_InitChicagoClouds, TrackView_TrackLifecycleNoOp, TrackView_InitLondonClouds,
    TrackView_TrackLifecycleNoOp, TrackView_InitEgyptClouds,   TrackView_InitFranceClouds,   TrackView_InitHawaiiClouds,
    TrackView_InitTokyoClouds,    TrackView_InitNewYorkClouds};

const TrackViewTrackLifecycleCallback g_trackViewCleanupCallbacks[kDriverCount] = {
    TrackView_TrackLifecycleNoOp,  TrackView_ReleaseChicagoClouds, TrackView_TrackLifecycleNoOp,
    TrackView_ReleaseLondonClouds, TrackView_TrackLifecycleNoOp,   TrackView_ReleaseEgyptClouds,
    TrackView_ReleaseFranceClouds, TrackView_ReleaseHawaiiClouds,  TrackView_ReleaseTokyoClouds,
    TrackView_ReleaseNewYorkClouds};

void TrackView_DrawClouds(void *userData) {
	TrackViewCloudDrawContext *const draw = (TrackViewCloudDrawContext *)userData;
	TrackViewCloudState *state;
	uint32_t savedRenderFlags;

	if (draw == NULL || draw->context == NULL || draw->maths == NULL || draw->viewMatrix == NULL ||
	    draw->cloudState == NULL) {
		return;
	}
	state = draw->cloudState;

	savedRenderFlags = draw->context->rendererFlags;
	draw->context->rendererFlags = SLIP_CLOUD_RENDER_FLAGS;

	TrackView_DrawCloudLayer(draw->context, state->farResource, draw->maths, draw->farYaw, draw->viewMatrix,
	                         draw->detailLevel, state->farEntries, state->farCount, state->spritePayloads,
	                         state->spriteCount, draw->drawnCount, draw->skippedCount);

	TrackView_DrawSilhouetteLayer(draw->context, state->silhouetteResource, draw->viewMatrix, state->silhouetteEntries,
	                              state->silhouetteCount, state->spritePayloads, state->spriteCount, draw->drawnCount,
	                              draw->skippedCount);

	TrackView_DrawCloudLayer(draw->context, state->nearResource, draw->maths, draw->nearYaw, draw->viewMatrix,
	                         draw->detailLevel, state->nearEntries, state->nearCount, state->spritePayloads,
	                         state->spriteCount, draw->drawnCount, draw->skippedCount);

	draw->context->rendererFlags = savedRenderFlags;
}

static bool TrackView_WriteTexturedPointBuffer(const SlipDraw3DTexturedDispatchPoint *points, size_t pointCount,
                                               uint8_t *pointBuffer, size_t pointBufferBytes) {
	size_t i;

	if (points == NULL || pointBuffer == NULL || pointCount > SIZE_MAX / sizeof(RasterTexturedPoint) ||
	    pointBufferBytes < pointCount * sizeof(RasterTexturedPoint)) {
		return false;
	}
	memset(pointBuffer, 0, pointCount * sizeof(RasterTexturedPoint));
	for (i = 0; i < pointCount; ++i) {
		uint8_t *const point = pointBuffer + i * sizeof(RasterTexturedPoint);

		TrackView_WriteLE32(point + offsetof(RasterTexturedPoint, x), (uint32_t)points[i].screenX);
		TrackView_WriteLE32(point + offsetof(RasterTexturedPoint, y), (uint32_t)points[i].screenY);
		TrackView_WriteLE32(point + offsetof(RasterTexturedPoint, u), points[i].textureU);
		TrackView_WriteLE32(point + offsetof(RasterTexturedPoint, v), points[i].textureV);
		TrackView_WriteLE32(point + offsetof(RasterTexturedPoint, depth), (uint32_t)points[i].depth);
	}
	return true;
}

static void TrackView_VehicleViewMaterialsLoadTextures(VehicleViewMaterialTable *materials, const char *const *archives,
                                                       size_t archiveCount) {
	uint16_t i;

	if (materials == NULL) {
		return;
	}

	for (i = 0; i < materials->count; ++i) {
		if (materials->textureName[i][0] == '\0') {
			continue;
		}
		if (!TrackView_LoadMaterialTexturePayload(archives, archiveCount, materials->textureName[i],
		                                          &materials->texturePayload[i])) {
			continue;
		}
		if (SlipSprite_FromPayload(&materials->texturePayload[i], &materials->texture[i])) {
			materials->hasTexture[i] = true;
		} else {
		}
	}
}

static uint32_t TrackView_VehicleViewNormalizedLightComponent(uint32_t value, uint32_t total) {
	const uint32_t scale = (SLIP_Q14_ONE * SLIP_Q14_ONE) / total;

	return (uint32_t)(((uint64_t)value * scale) >> SLIP_Q14_FRACTION_BITS);
}

static uint32_t TrackView_VehicleViewNormalizedLightDirect(void) {
	return TrackView_VehicleViewNormalizedLightComponent(SLIP_VEHICLE_PREVIEW_DIRECT_LIGHT_Q14,
	                                                     SLIP_VEHICLE_PREVIEW_DIRECT_LIGHT_Q14 +
	                                                         SLIP_VEHICLE_PREVIEW_AMBIENT_LIGHT_Q14);
}

static uint32_t TrackView_VehicleViewNormalizedLightAmbient(void) {
	return TrackView_VehicleViewNormalizedLightComponent(SLIP_VEHICLE_PREVIEW_AMBIENT_LIGHT_Q14,
	                                                     SLIP_VEHICLE_PREVIEW_DIRECT_LIGHT_Q14 +
	                                                         SLIP_VEHICLE_PREVIEW_AMBIENT_LIGHT_Q14);
}

static uint32_t TrackView_VehicleViewProjectMask(SlipDraw3DVec32 point, void *userData) {
	const VehicleViewRenderContext *const ctx = (const VehicleViewRenderContext *)userData;
	SlipDraw3DRefreshMode0Projection refresh;
	SlipTrackWorldProjectFrustum frustum;
	uint32_t mask = 0;

	if (ctx == NULL || !SlipDraw3D_RefreshProjectFrustum(&ctx->projectState, 0, &refresh)) {
		return 0;
	}
	frustum = (SlipTrackWorldProjectFrustum){
	    refresh.maxXStep,        refresh.minXStep,       refresh.minYStep,           refresh.maxYStep,
	    refresh.minXPlaneDepthQ, refresh.minXPlaneNegXQ, refresh.maxXPlaneNegDepthQ, refresh.maxXPlaneXQ,
	    refresh.maxYPlaneDepthQ, refresh.maxYPlaneYQ,    refresh.minYPlaneNegDepthQ, refresh.minYPlaneNegYQ,
	    ctx->projectState.minZ,  ctx->projectState.maxZ};
	if (!SlipTrackWorld_ProjectMask((SlipView3DVec32){point.x, point.y, point.z}, &frustum, &mask)) {
		return 0;
	}
	return mask;
}

static int TrackView_SubmitShapePolygon(VehicleViewRenderContext *ctx, uint16_t countAndFlags, int16_t x, int16_t y,
                                        int16_t z, uint16_t material, bool textured) {
	const SlipShape3D *const shape = &ctx->shape;
	const SlipShape3DPrimitive *const primitive = &ctx->primitive;
	SlipShape3DPrimitiveStream stream;
	int ok;
	if (!SlipShape3D_ParsePrimitiveStream(shape, primitive, &stream))
		return 0;
	TrackViewRawBspContext *const trackContext = ctx->trackContext;
	SlipShape3DPrimitiveDispatch primitiveDispatch;
	SlipDraw3DReturnActiveVisit returnVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DClipFlagVisit clipFlagVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneBoundsVisit postBoundsVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneClipRecordVisit postClipRecordVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneClipPlaneVisit postClipPlaneVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DReturnActiveRing returnActive;
	SlipDraw3DMaterialGate materialGate;
	SlipDraw3DRegularSetup regularSetup;
	SlipDraw3DSolidRingExecute solidRing;
	SlipDraw3DFlatRingDispatch flatRing;
	SlipDraw3DRasterPoint flatPoints[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	RasterShadedPoint shadedPoints[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	const uint8_t *const primitiveRecord = shape->data + shape->primitiveOffset + primitive->primitiveOffset;
	const size_t primitiveRecordBytes = shape->size - (shape->primitiveOffset + primitive->primitiveOffset);
	uint16_t polygonCountAndFlags;
	uint8_t color = (uint8_t)trackContext->materialColor;

	primitiveDispatch = (SlipShape3DPrimitiveDispatch){.countAndFlags = countAndFlags,
	                                                   .normalX = (uint16_t)x,
	                                                   .normalY = (uint16_t)y,
	                                                   .normalZ = (uint16_t)z,
	                                                   .materialIndex = material};
	if (textured) {
		TrackViewPrimitiveCallbackContext primitiveContext;
		bool handled = false;
		bool fallbackLow = false;
		uint32_t callbackResult = 0;
		bool carryOut = false;
		const uint32_t pixelsBefore = trackContext->directCallbackHighPixelsWrittenCount;

		memset(&primitiveContext, 0, sizeof(primitiveContext));
		primitiveContext.viewMatrix = ctx->matrix;
		primitiveContext.transformedObjectOffset = ctx->translation;
		primitiveContext.projectState = &ctx->projectState;
		primitiveContext.frustum = &trackContext->frustum;
		primitiveContext.shapeScaleShift = ctx->shapeScaleShift;
		primitiveContext.shapeScaleRightShift = (uint16_t)(SLIP_Q14_FRACTION_BITS - ctx->shapeScaleShift);
		primitiveContext.shapePath = true;
		if (!TrackView_ExecuteHighTexturedCallback(
		        trackContext, &primitiveContext, primitiveRecord, primitiveRecordBytes, primitive->primitiveOffset,
		        primitiveDispatch.countAndFlags, primitiveDispatch.normalX, primitiveDispatch.normalY,
		        primitiveDispatch.normalZ, primitiveDispatch.materialIndex, &handled, &fallbackLow, &callbackResult,
		        &carryOut)) {
			return 0;
		}
		(void)callbackResult;
		(void)carryOut;
		if (handled) {
			if (trackContext->directCallbackHighPixelsWrittenCount > pixelsBefore) {
				++ctx->drawnCount;
			}
			return 1;
		}
		if (!fallbackLow) {
			return 1;
		}
	}
	polygonCountAndFlags = (uint16_t)(primitiveDispatch.countAndFlags & ~SLIP_PRIMITIVE_TEXTURE_COORDINATES);

	if (trackContext->postPlaneHead == 0 && (ctx->projectState.renderFlags & SLIP_SHAPE_INSIDE_VIEW) != 0) {
		SlipDraw3DVertexLighting lighting = {.light = {ctx->lightVector.x, ctx->lightVector.y, ctx->lightVector.z},
		                                     .origin = {ctx->bspOrigin.x, ctx->bspOrigin.y, ctx->bspOrigin.z},
		                                     .flags = trackContext->rendererFlags,
		                                     .direct = trackContext->directLight,
		                                     .ambient = trackContext->ambientLight,
		                                     .fadeStart = SlipDraw3D_fadeStart,
		                                     .fadeEnd = SlipDraw3D_fadeEnd,
		                                     .fadeRange = SlipDraw3D_fadeRange,
		                                     .fadeShade = SlipDraw3D_fadeColour,
		                                     .overrideRamp = trackContext->limitEnabled,
		                                     .rampStart = trackContext->limitStart,
		                                     .rampEnd = trackContext->limitEnd,
		                                     .transform = TrackView_VehicleViewSource,
		                                     .transformContext = ctx};
		if (!SlipDraw3D_DrawUnclippedPolygon((const void *)trackContext->materialTable, primitiveDispatch.materialIndex,
		                                     polygonCountAndFlags, (int16_t)primitiveDispatch.normalX,
		                                     (int16_t)primitiveDispatch.normalY, (int16_t)primitiveDispatch.normalZ,
		                                     ctx->vertexRecords, ctx->vertexRecordCount,
		                                     shape->data + stream.indexOffset, shape->size - stream.indexOffset,
		                                     &ctx->projectState, TrackView_VehicleViewTransform,
		                                     TrackView_VehicleViewProject, &lighting, &trackContext->materialColor)) {
			trackContext->failureAddress = TRACK_VIEW_DIAGNOSTIC_UNCLIPPED_POLYGON;
			trackContext->failed = true;
			return 0;
		}
		++ctx->drawnCount;
		return 1;
	}

	++trackContext->emitPathCount;
	if (!SlipDraw3D_ReturnActiveRing(trackContext->drawRecordPool, returnVisits,
	                                 sizeof(returnVisits) / sizeof(returnVisits[0]), &returnActive) ||
	    !SlipDraw3D_MaterialGate(trackContext->materialTable, trackContext->materialTableBytes,
	                             primitiveDispatch.materialIndex, trackContext->rendererFlags, polygonCountAndFlags,
	                             shape->data + stream.indexOffset,
	                             (size_t)stream.vertexCount * SLIP_SERIALIZED_INDEX_BYTES, &materialGate)) {
		trackContext->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_GATE;
		trackContext->failed = true;
		return 0;
	}
	(void)returnActive;
	++trackContext->emitPathMaterialGateCount;
	if (materialGate.branch == SLIP_DRAW3D_MATERIAL_GATE_BRANCH_REJECT) {
		return 1;
	}
	if (materialGate.branch != SLIP_DRAW3D_MATERIAL_GATE_BRANCH_REGULAR &&
	    materialGate.branch != SLIP_DRAW3D_MATERIAL_GATE_BRANCH_INDEXED) {
		return 1;
	}

	const uint16_t polygonVertexCount = polygonCountAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
	if (materialGate.branch == SLIP_DRAW3D_MATERIAL_GATE_BRANCH_REGULAR &&
	    !TrackView_MaterialColorNormal((const void *)materialGate.materialRecord, trackContext, ctx->lightVector,
	                                   primitiveDispatch.normalX, primitiveDispatch.normalY, polygonVertexCount,
	                                   ctx->vertexRecords, ctx->vertexRecordCount, shape->data + stream.indexOffset,
	                                   (size_t)stream.vertexCount * SLIP_SERIALIZED_INDEX_BYTES, polygonCountAndFlags,
	                                   &ctx->projectState, TrackView_VehicleViewTransform, ctx, &color)) {
		trackContext->failureAddress = TRACK_VIEW_DIAGNOSTIC_MATERIAL_NORMAL_COLOUR;
		trackContext->failed = true;
		return 0;
	}
	if (materialGate.branch == SLIP_DRAW3D_MATERIAL_GATE_BRANCH_REGULAR) {
		if (!SlipDraw3D_RegularSetup(materialGate.materialRecord,
		                             trackContext->materialTableBytes -
		                                 (size_t)(materialGate.materialRecord - trackContext->materialTable),
		                             polygonCountAndFlags, color, &regularSetup)) {
			trackContext->failureAddress = TRACK_VIEW_DIAGNOSTIC_REGULAR_MATERIAL_SETUP;
			trackContext->failed = true;
			return 0;
		}
	} else {
		regularSetup = (SlipDraw3DRegularSetup){.materialRecord = materialGate.materialRecord,
		                                        .materialDitherBits = 0,
		                                        .drawMode = materialGate.drawMode,
		                                        .countAndFlags = polygonCountAndFlags,
		                                        .maskedIndex = materialGate.indexedRecordIndex,
		                                        .materialColor = color,
		                                        .storedCountAndFlags = polygonCountAndFlags,
		                                        .storedMaterialColor = color,
		                                        .mode = materialGate.mode};
	}
	if (g_vehicleViewDumpDiagnostics) {
		const uint8_t *const materialRecord = materialGate.materialRecord;
		const size_t materialOffset = (materialRecord >= trackContext->materialTable &&
		                               materialRecord <= trackContext->materialTable + trackContext->materialTableBytes)
		                                  ? (size_t)(materialRecord - trackContext->materialTable)
		                                  : (size_t)-1;
		fprintf(stderr,
		        "track_view_actor_primitive_000260fc shape_off=0x%08x bp=0x%04x bp_mask=0x%04x dx=0x%04x "
		        "material_off=0x%zx color=0x%02x mode=%u field2c=0x%08x verts=%u\n",
		        primitive->primitiveOffset, primitiveDispatch.countAndFlags, polygonCountAndFlags,
		        primitiveDispatch.materialIndex, materialOffset, color, regularSetup.drawMode,
		        regularSetup.materialDitherBits, (unsigned)stream.vertexCount);
	}
	if (materialGate.branch == SLIP_DRAW3D_MATERIAL_GATE_BRANCH_INDEXED) {
		ok = TrackView_ActorBuildIndexedRing(
		    ctx, trackContext, shape, &stream, &materialGate, clipFlagVisits,
		    sizeof(clipFlagVisits) / sizeof(clipFlagVisits[0]), postBoundsVisits,
		    sizeof(postBoundsVisits) / sizeof(postBoundsVisits[0]), postClipRecordVisits,
		    sizeof(postClipRecordVisits) / sizeof(postClipRecordVisits[0]), postClipPlaneVisits,
		    sizeof(postClipPlaneVisits) / sizeof(postClipPlaneVisits[0]), &solidRing);
	} else {
		ok = SlipDraw3D_BuildSolidRingExecute(
		    trackContext->drawRecordPool, ctx->vertexRecords, ctx->vertexRecordCount, shape->data + stream.indexOffset,
		    (size_t)stream.vertexCount * SLIP_SERIALIZED_INDEX_BYTES, (uint16_t)regularSetup.maskedIndex,
		    regularSetup.materialDitherBits, &ctx->projectState, TrackView_VehicleViewTransform,
		    TrackView_VehicleViewProject, TrackView_VehicleViewProject, ctx, false, NULL, 0, 0, 0, 0, 0, 0,
		    SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, clipFlagVisits, sizeof(clipFlagVisits) / sizeof(clipFlagVisits[0]),
		    postBoundsVisits, sizeof(postBoundsVisits) / sizeof(postBoundsVisits[0]), postClipRecordVisits,
		    sizeof(postClipRecordVisits) / sizeof(postClipRecordVisits[0]), postClipPlaneVisits,
		    sizeof(postClipPlaneVisits) / sizeof(postClipPlaneVisits[0]), &solidRing);
	}
	if (!ok) {
		trackContext->failureAddress = TRACK_VIEW_DIAGNOSTIC_SOLID_RING;
		trackContext->failed = true;
		return 0;
	}
	++trackContext->emitPathSolidRingCount;
	if (solidRing.carryOut) {
		return 1;
	}
	if (regularSetup.drawMode == SLIP_POLYGON_DRAW_SHADED) {
		size_t shadedPointCount = 0;
		size_t mode1PointCount = 0;
		size_t shadedIndex;

		if (!TrackView_ActorCollectRingPoints(trackContext->drawRecordPool, solidRing.activeHeadOffsetOut, flatPoints,
		                                      sizeof(flatPoints) / sizeof(flatPoints[0]), &shadedPointCount)) {
			trackContext->failureAddress = TRACK_VIEW_DIAGNOSTIC_FLAT_RING;
			trackContext->failed = true;
			return 0;
		}
		if (shadedPointCount >= 3u) {
			uint8_t flatColor = 0;
			bool flatColorUniform = false;
			if (!TrackView_ActorCollectShadedRing(trackContext->drawRecordPool, solidRing.activeHeadOffsetOut,
			                                      shadedPoints, sizeof(shadedPoints) / sizeof(shadedPoints[0]),
			                                      &mode1PointCount, &flatColor, &flatColorUniform)) {
				trackContext->failureAddress = TRACK_VIEW_DIAGNOSTIC_ACTOR_SHADED_RING;
				trackContext->failed = true;
				return 0;
			}
			if (g_vehicleViewDumpDiagnostics) {
				fprintf(stderr,
				        "track_view_actor_shaded_0001b01a shape_off=0x%08x points=%zu uniform=%u flat=%02x "
				        "light=%d,%d,%d direct=0x%08x ambient=0x%08x xy=",
				        primitive->primitiveOffset, mode1PointCount, flatColorUniform ? 1u : 0u, flatColor,
				        ctx->lightVector.x, ctx->lightVector.y, ctx->lightVector.z, trackContext->directLight,
				        trackContext->ambientLight);
				for (shadedIndex = 0; shadedIndex < mode1PointCount; ++shadedIndex) {
					fprintf(stderr, "%s%d,%d", shadedIndex == 0 ? "" : ",", shadedPoints[shadedIndex].x,
					        shadedPoints[shadedIndex].y);
				}
				fprintf(stderr, " colors=");
				for (shadedIndex = 0; shadedIndex < mode1PointCount; ++shadedIndex) {
					fprintf(stderr, "%s%02x", shadedIndex == 0 ? "" : ",",
					        (uint8_t)(shadedPoints[shadedIndex].shade >> SLIP_SHADE_COLOUR_SHIFT));
				}
				fprintf(stderr, "\n");
			}
			if (mode1PointCount >= 3u) {
				if (flatColorUniform) {
					size_t mode1PointIndex;

					for (mode1PointIndex = 0; mode1PointIndex < mode1PointCount; ++mode1PointIndex) {
						flatPoints[mode1PointIndex].x = shadedPoints[mode1PointIndex].x;
						flatPoints[mode1PointIndex].y = shadedPoints[mode1PointIndex].y;
					}
					Raster_DrawSolidFlatPolygon(flatColor, (const RasterPoint *)flatPoints, (uint16_t)mode1PointCount);
				} else {
					Raster_DrawShadedFlatPolygon(shadedPoints, (uint16_t)mode1PointCount);
				}
				++ctx->drawnCount;
				++trackContext->emitPathFlatDispatchCount;
				++trackContext->emitPathRasterizedCount;
			}
			return 1;
		}
	}
	if (!SlipDraw3D_RasterizeFlatRing(
	        trackContext->drawRecordPool, solidRing.activeHeadOffsetOut, regularSetup.drawMode,
	        trackContext->rendererFlags, regularSetup.storedMaterialColor, regularSetup.materialDitherBits,
	        SLIP_DRAW3D_RECORD_NEXT_OFFSET, flatPoints, sizeof(flatPoints) / sizeof(flatPoints[0]), &flatRing)) {
		trackContext->failureAddress = TRACK_VIEW_DIAGNOSTIC_FLAT_RING;
		trackContext->failed = true;
		return 0;
	}
	++trackContext->emitPathFlatDispatchCount;
	if (flatRing.rasterized) {
		++ctx->drawnCount;
		++trackContext->emitPathRasterizedCount;
	}
	return 1;
}

static int32_t TrackView_ShapePolygonDepth(void *context, uint16_t countAndFlags, const uint8_t *indices) {
	VehicleViewRenderContext *const ctx = context;
	SlipRendererState renderer = {.projection = ctx->projectState,
	                              .activeVertices = ctx->vertexRecords,
	                              .activeTransform = TrackView_VehicleViewTransform};
	return SlipRenderer_PolygonDepth(&renderer, countAndFlags, indices, ctx);
}

static uint32_t TrackView_ShapeDrawFlags(void *context) {
	return ((VehicleViewRenderContext *)context)->trackContext->rendererFlags;
}

static void TrackView_SetShapeDrawFlags(void *context, uint16_t flags) {
	((VehicleViewRenderContext *)context)->trackContext->rendererFlags = flags;
}

static void TrackView_SubmitShapeSolid(void *context, uint16_t countAndFlags, int16_t x, int16_t y, int16_t z,
                                       uint16_t material, const uint8_t *indices) {
	(void)indices;
	VehicleViewRenderContext *const ctx = context;
	if (!TrackView_SubmitShapePolygon(ctx, countAndFlags, x, y, z, material, false))
		ctx->trackContext->failed = true;
}

static void TrackView_SubmitShapeTextured(void *context, uint16_t countAndFlags, int16_t x, int16_t y, int16_t z,
                                          uint16_t material, const uint8_t *indices) {
	(void)indices;
	VehicleViewRenderContext *const ctx = context;
	if (!TrackView_SubmitShapePolygon(ctx, countAndFlags, x, y, z, material, true))
		ctx->trackContext->failed = true;
}

static void TrackView_DispatchShapePrimitive(void *context, const uint8_t *primitive, uint32_t traversalValue) {
	VehicleViewRenderContext *const ctx = context;
	const uint32_t offset = (uint32_t)(primitive - ctx->shape.data - ctx->shape.primitiveOffset);
	if (!SlipShape3D_ParsePrimitive(&ctx->shape, offset, (int32_t)traversalValue, 0, &ctx->primitive)) {
		ctx->trackContext->failed = true;
		return;
	}
	++ctx->primitiveCount;
	SlipShapeDispatchCalls calls = {ctx,
	                                TrackView_ShapePolygonDepth,
	                                TrackView_ShapeDrawFlags,
	                                TrackView_SetShapeDrawFlags,
	                                TrackView_SubmitShapeSolid,
	                                TrackView_SubmitShapeTextured};
	SlipShape_DispatchPrimitive(primitive, traversalValue, &calls);
}

enum {
	VEHICLE_ART_MAX_SLOTS = 64,
	VEHICLE_ART_MAX_POINTS = 16,
	VEHICLE_ART_SHAPE_LOD_COUNT = 8,
	VEHICLE_ART_SHAPE_NAME_SIZE = 14
};

typedef struct VehicleArtPoint {
	uint32_t nameTag;
	SlipView3DVec32 position;
} VehicleArtPoint;

typedef struct VehicleArtSlot {
	uint32_t recordOffset;
	uint32_t childOffset;
	uint32_t siblingOffset;
	uint32_t nameTag;
	uint32_t rotationCallbackOffset;
	int32_t localX;
	int32_t localY;
	int32_t localZ;
	char bodyShapes[VEHICLE_ART_SHAPE_LOD_COUNT][VEHICLE_ART_SHAPE_NAME_SIZE];
	char replayShapes[VEHICLE_ART_SHAPE_LOD_COUNT][VEHICLE_ART_SHAPE_NAME_SIZE];
	VehicleArtPoint points[VEHICLE_ART_MAX_POINTS];
	size_t pointCount;
	struct VehicleArtSlot *firstChild;
	struct VehicleArtSlot *nextSibling;
	int16_t animationAngle;
	SlipView3DMatrix modelMatrix;
	SlipView3DMatrix drawMatrix;
	SlipView3DVec32 worldPosition;
	SlipView3DVec32 drawTranslation;
	SlipView3DVec32 bspOrigin;
	SlipView3DVec32 lightVector;
} VehicleArtSlot;

typedef struct VehicleArtActor {
	SlipActorRecord record;
	SlipActorPartRecord parts[VEHICLE_ART_MAX_SLOTS];
	SlipActorRenderState render;
	SlipActorRenderCalls calls;
	SlipView3DMatrix objectMatrix, cameraMatrix;
	SlipView3DVec32 position, cameraPosition, light;
	const char *const *archives;
	size_t archiveCount;
	SlipDraw3DProjectState *projection;
	const VehicleViewMaterialTable *materials;
	TrackViewRawBspContext *trackContext;
	int drawn;
	VehicleArtSlot slots[VEHICLE_ART_MAX_SLOTS];
	size_t slotCount;
	VehicleArtSlot *root;
	int32_t lodRanges[VEHICLE_ART_SHAPE_LOD_COUNT];
	int drawChildrenAfterParent;
} VehicleArtActor;

static void TrackView_VehicleArtCopyShapeName(char dst[VEHICLE_ART_SHAPE_NAME_SIZE], const uint8_t *src,
                                              size_t srcAvailable) {
	size_t i;
	const size_t limit = srcAvailable < VEHICLE_ART_SHAPE_NAME_SIZE ? srcAvailable : VEHICLE_ART_SHAPE_NAME_SIZE;

	for (i = 0; i + 1u < limit && src[i] != 0; ++i) {
		dst[i] = (char)src[i];
	}
	dst[i] = '\0';
}

static VehicleArtSlot *TrackView_VehicleArtFindSlot(VehicleArtActor *actor, uint32_t recordOffset) {
	size_t i;

	for (i = 0; i < actor->slotCount; ++i) {
		if (actor->slots[i].recordOffset == recordOffset) {
			return &actor->slots[i];
		}
	}
	return NULL;
}

static VehicleArtSlot *TrackView_VehicleArtParseRecord(VehicleArtActor *actor, const uint8_t *data, size_t size,
                                                       uint32_t recordOffset);

static VehicleArtSlot *TrackView_VehicleArtParseList(VehicleArtActor *actor, const uint8_t *data, size_t size,
                                                     uint32_t firstOffset) {
	VehicleArtSlot *first = NULL;
	VehicleArtSlot *previous = NULL;
	uint32_t offset = firstOffset;
	size_t guard = 0;

	while (offset != 0 && offset < size && guard++ < VEHICLE_ART_MAX_SLOTS) {
		VehicleArtSlot *const slot = TrackView_VehicleArtParseRecord(actor, data, size, offset);

		if (slot == NULL) {
			break;
		}
		if (first == NULL) {
			first = slot;
		}
		if (previous != NULL) {
			previous->nextSibling = slot;
		}
		previous = slot;
		offset = slot->siblingOffset;
		if (offset == firstOffset) {
			break;
		}
	}
	return first;
}

static VehicleArtSlot *TrackView_VehicleArtParseRecord(VehicleArtActor *actor, const uint8_t *data, size_t size,
                                                       uint32_t recordOffset) {
	VehicleArtSlot *slot;
	size_t i;

	if (recordOffset + SLIP_ART_PART_POINT_COUNT_OFFSET > size) {
		return NULL;
	}
	slot = TrackView_VehicleArtFindSlot(actor, recordOffset);
	if (slot != NULL) {
		return slot;
	}
	if (actor->slotCount >= VEHICLE_ART_MAX_SLOTS) {
		return NULL;
	}

	slot = &actor->slots[actor->slotCount++];
	memset(slot, 0, sizeof(*slot));
	slot->recordOffset = recordOffset;
	slot->nameTag = SlipBytes_ReadLE32(data + recordOffset);
	slot->childOffset = SlipBytes_ReadLE32(data + recordOffset + SLIP_ART_PART_CHILD_OFFSET);
	slot->siblingOffset = SlipBytes_ReadLE32(data + recordOffset + SLIP_ART_PART_SIBLING_OFFSET);
	slot->rotationCallbackOffset = SlipBytes_ReadLE32(data + recordOffset + SLIP_ART_PART_ROTATION_CALLBACK_OFFSET);
	slot->localX = SlipBytes_ReadLEI32(data + recordOffset + SLIP_ART_PART_POSITION_OFFSET);
	slot->localY =
	    SlipBytes_ReadLEI32(data + recordOffset + (SLIP_ART_PART_POSITION_OFFSET + SLIP_ART_POSITION_Y_OFFSET));
	slot->localZ =
	    SlipBytes_ReadLEI32(data + recordOffset + (SLIP_ART_PART_POSITION_OFFSET + SLIP_ART_POSITION_Z_OFFSET));
	for (i = 0; i < VEHICLE_ART_SHAPE_LOD_COUNT; ++i) {
		TrackView_VehicleArtCopyShapeName(
		    slot->bodyShapes[i],
		    data + recordOffset + SLIP_ART_PART_SHAPE_NAMES_OFFSET + i * VEHICLE_ART_SHAPE_NAME_SIZE,
		    size - (recordOffset + SLIP_ART_PART_SHAPE_NAMES_OFFSET + i * VEHICLE_ART_SHAPE_NAME_SIZE));
		TrackView_VehicleArtCopyShapeName(
		    slot->replayShapes[i],
		    data + recordOffset + SLIP_ART_PART_REPLAY_SHAPE_NAMES_OFFSET + i * VEHICLE_ART_SHAPE_NAME_SIZE,
		    size - (recordOffset + SLIP_ART_PART_REPLAY_SHAPE_NAMES_OFFSET + i * VEHICLE_ART_SHAPE_NAME_SIZE));
	}
	if (recordOffset + SLIP_ART_PART_POINTS_OFFSET <= size) {
		uint32_t pointCount = SlipBytes_ReadLE32(data + recordOffset + SLIP_ART_PART_POINT_COUNT_OFFSET);

		if (pointCount > SLIP_ART_POINT_CAPACITY) {
			pointCount = SLIP_ART_POINT_CAPACITY;
		}
		if (pointCount > VEHICLE_ART_MAX_POINTS) {
			pointCount = VEHICLE_ART_MAX_POINTS;
		}
		for (i = 0; i < pointCount; ++i) {
			const uint32_t pointOffset =
			    recordOffset + SLIP_ART_PART_POINTS_OFFSET + (uint32_t)i * SLIP_ART_POINT_BYTES;

			if (pointOffset + SLIP_ART_POINT_BYTES > size) {
				break;
			}
			slot->points[i].nameTag = SlipBytes_ReadLE32(data + pointOffset);
			slot->points[i].position.x = SlipBytes_ReadLEI32(data + pointOffset + SLIP_ART_POINT_POSITION_OFFSET);
			slot->points[i].position.y =
			    SlipBytes_ReadLEI32(data + pointOffset + (SLIP_ART_POINT_POSITION_OFFSET + SLIP_ART_POSITION_Y_OFFSET));
			slot->points[i].position.z =
			    SlipBytes_ReadLEI32(data + pointOffset + (SLIP_ART_POINT_POSITION_OFFSET + SLIP_ART_POSITION_Z_OFFSET));
		}
		slot->pointCount = i;
	}
	if (slot->childOffset != 0) {
		slot->firstChild = TrackView_VehicleArtParseList(actor, data, size, slot->childOffset);
	}
	return slot;
}

static bool TrackView_VehicleArtActorFromPayload(const SlipResourcePayload *payload, VehicleArtActor *actor) {
	uint32_t rootOffset;
	size_t i;

	if (payload == NULL || payload->data == NULL || payload->size < SLIP_ART_PREVIEW_HEADER_BYTES || actor == NULL) {
		return false;
	}
	memset(actor, 0, sizeof(*actor));
	rootOffset = SlipBytes_ReadLE32(payload->data + SLIP_ART_ROOT_PART_OFFSET);
	actor->drawChildrenAfterParent = SlipBytes_ReadLEI32(payload->data + SLIP_ART_SORT_CHILDREN_OFFSET) == 0;
	for (i = 0; i < VEHICLE_ART_SHAPE_LOD_COUNT; ++i) {
		actor->lodRanges[i] =
		    SlipBytes_ReadLEI32(payload->data + SLIP_ART_LOD_DISTANCES_OFFSET + i * SLIP_ART_DISTANCE_BYTES);
	}
	actor->root = TrackView_VehicleArtParseRecord(actor, payload->data, payload->size, rootOffset);
	return actor->root != NULL;
}

SlipView3DMatrix TrackView_VehicleViewIdentityMatrix(void) {
	SlipView3DMatrix matrix;

	memset(&matrix, 0, sizeof(matrix));
	matrix.m[0] = SLIP_Q14_ONE;
	matrix.m[4] = SLIP_Q14_ONE;
	matrix.m[8] = SLIP_Q14_ONE;
	return matrix;
}

void TrackView_VehicleViewResetActorState(void) {
	g_vehicleViewActorMatrix = TrackView_VehicleViewIdentityMatrix();
	g_vehicleViewActorMatrixValid = true;
	g_vehicleViewLastTickMs = SlipSdl_TicksMs();
	g_vehicleViewTimerSuppressedSamples = SLIP_VEHICLE_PREVIEW_INITIAL_TIMER_SAMPLES;
	memset(g_vehicleViewFanAngles, 0, sizeof(g_vehicleViewFanAngles));
	g_vehicleViewJetAngle = 0;
	g_vehicleViewJetDirection = 0;
}

void TrackView_TrackGlobeResetActorState(void) {

	if (!g_trackSelectGlobeMatrixValid) {
		g_trackSelectGlobeMatrix = TrackView_VehicleViewIdentityMatrix();
		g_trackSelectGlobeMatrixValid = true;
	}
	SlipFrameTimer_Reset();
}

uint32_t TrackView_ProjectMask(SlipView3DVec32 point, void *userData) {
	return TrackView_ProjectMaskCallback(point, userData);
}

bool TrackView_SphereCull(SlipView3DVec32 center, int32_t radius, void *userData) {
	return TrackView_SphereCullCallback(center, radius, userData);
}

uint16_t TrackView_VehicleViewFrameStep(uint32_t deltaMs) {
	SlipFrameTimer_SetDelta((uint16_t)deltaMs);
	return (uint16_t)SlipFrameTimer_Step();
}

static uint16_t TrackView_FrameStepRead(void) { return (uint16_t)SlipFrameTimer_Step(); }

void TrackView_MaterialAnimationTick(void) {
	uint32_t updatedAnimationAccumulator;
	uint32_t folded;
	uint64_t product;

	updatedAnimationAccumulator = g_materialAnimAccumulator + TrackView_FrameStepRead();
	g_materialAnimAccumulator = updatedAnimationAccumulator;
	folded = updatedAnimationAccumulator & SLIP_MATERIAL_ANIMATION_PHASE_MASK;

	if (folded > SLIP_Q14_HALF) {
		folded ^= SLIP_MATERIAL_ANIMATION_PHASE_MASK;
	}
	folded = folded >> SLIP_MATERIAL_ANIMATION_AMPLITUDE_SHIFT;
	folded += SLIP_Q14_HALF;
	g_materialAnimWave = (uint16_t)folded;
	product = (uint64_t)g_materialAnimWave * (uint64_t)(uint32_t)(g_materialAnimRangeHigh - g_materialAnimRangeLow);
	g_materialAnimLerp = (uint32_t)(product >> SLIP_Q14_FRACTION_BITS) + g_materialAnimRangeLow;
}

static uint16_t TrackView_VehicleViewFrameTimerUpdate(void) {
	uint64_t now = SlipSdl_TicksMs();
	uint64_t elapsed;
	uint32_t deltaMs;
	const uint32_t minDeltaMs = 14;
	const uint32_t maximumDeltaMs = 1000;

	if (g_vehicleViewTimerSuppressedSamples > 0) {
		--g_vehicleViewTimerSuppressedSamples;
		g_vehicleViewLastTickMs = now;
		return 0;
	}

	elapsed = now - g_vehicleViewLastTickMs;
	if (elapsed < minDeltaMs) {
		SlipSdl_DelayMs((uint32_t)(minDeltaMs - elapsed));
		now = SlipSdl_TicksMs();
		elapsed = now - g_vehicleViewLastTickMs;
	}
	g_vehicleViewLastTickMs = now;
	deltaMs = elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
	if (deltaMs > maximumDeltaMs) {
		deltaMs = maximumDeltaMs;
	}
	if (deltaMs < minDeltaMs) {
		deltaMs = minDeltaMs;
	}
	return TrackView_VehicleViewFrameStep(deltaMs);
}

uint16_t TrackView_TrackGlobeFrameTimerUpdate(void) {
	SlipFrameTimer_Update((uint32_t)SlipSdl_TicksMs());
	return (uint16_t)SlipFrameTimer_Step();
}

static void TrackView_VehicleViewActorCallback(SlipView3DMatrix *actorObjectMatrix, const SlipView3DMaths *maths,
                                               uint16_t frameStep) {
	int32_t product;
	int16_t angle;

	if (actorObjectMatrix == NULL || maths == NULL || frameStep == 0) {
		return;
	}

	product = SLIP_VEHICLE_PREVIEW_ACTOR_ANGLE_STEP * (int16_t)frameStep;
	angle = (int16_t)(product >> SLIP_Q14_FRACTION_BITS);
	if (angle != 0) {
		SlipView3D_ApplyRow0Row2Rotation(maths, angle, actorObjectMatrix);
		SlipView3D_OrthonormalizeForwardBasis(actorObjectMatrix);
	}
}

static VehicleArtSlot *TrackView_VehicleArtFindSlotByNameTag(VehicleArtActor *actor, uint32_t nameTag) {
	size_t i;

	if (actor == NULL) {
		return NULL;
	}
	for (i = 0; i < actor->slotCount; ++i) {
		if (actor->slots[i].nameTag == nameTag) {
			return &actor->slots[i];
		}
	}
	return NULL;
}

static void TrackView_VehicleViewStoreArtSlotAngle(VehicleArtActor *actor, uint32_t nameTag, int16_t angle) {
	VehicleArtSlot *const slot = TrackView_VehicleArtFindSlotByNameTag(actor, nameTag);

	if (slot != NULL) {
		slot->animationAngle = angle;
	}
}

static void TrackView_VehicleViewUpdateArtAnimations(VehicleArtActor *actor, uint16_t frameStep) {
	int16_t fanStep;
	int16_t jetStep;
	int32_t jetAngle;
	int i;

	if (actor == NULL || frameStep == 0) {
		return;
	}

	fanStep = (int16_t)((SLIP_VEHICLE_PREVIEW_FAN_ANGLE_STEP * (int16_t)frameStep) >> SLIP_Q14_FRACTION_BITS);
	jetStep = (int16_t)(fanStep >> SLIP_VEHICLE_PREVIEW_JET_RATE_SHIFT);
	jetAngle = g_vehicleViewJetAngle;
	if (g_vehicleViewJetDirection == 0) {
		jetAngle += jetStep;
		if (jetAngle >= SLIP_ANGLE_QUARTER_TURN) {
			jetAngle = SLIP_ANGLE_QUARTER_TURN;
			g_vehicleViewJetDirection = 1;
		}
	} else {
		jetAngle -= jetStep;
		if (jetAngle <= 0) {
			jetAngle = 0;
			g_vehicleViewJetDirection = 0;
		}
	}
	g_vehicleViewJetAngle = (int16_t)jetAngle;

	for (i = 0; i < SLIP_VEHICLE_PREVIEW_ANIMATED_PART_COUNT; ++i) {
		const uint32_t fanTag = SLIP_ACTOR_FIRST_FAN + (uint32_t)i;
		const uint32_t jetTag = SLIP_ACTOR_FIRST_JET + (uint32_t)i;

		if (TrackView_VehicleArtFindSlotByNameTag(actor, fanTag) != NULL) {
			g_vehicleViewFanAngles[i] = (int16_t)(g_vehicleViewFanAngles[i] + fanStep);
			TrackView_VehicleViewStoreArtSlotAngle(actor, fanTag, g_vehicleViewFanAngles[i]);
		}
		TrackView_VehicleViewStoreArtSlotAngle(actor, jetTag, g_vehicleViewJetAngle);
	}
}

typedef struct TrackViewShapeBinding {
	VehicleViewRenderContext render; /* First member is the renderer callback ABI. */
	SlipActorShapeState shape;
	SlipResourcePayload payload;
	const SlipResourcePayload *resident;
	const char *name;
	uint16_t resource;
	SlipShapeSortState sort;
	SlipActorShapeSortNode sortCallback;
	const SlipActorShapeCalls *calls;
} TrackViewShapeBinding;

static void TrackView_ShapeSetup(void *context, SlipView3DVec32 view, SlipView3DVec32 world) {
	TrackViewShapeBinding *const binding = context;
	binding->shape.viewPosition = view;
	binding->shape.worldPosition = world;
}

static void TrackView_ShapeMatrices(void *context, const SlipView3DMatrix *world, const SlipView3DMatrix *draw) {
	(void)world;
	TrackViewShapeBinding *const binding = context;
	VehicleViewRenderContext *const ctx = &binding->render;
	binding->shape.drawMatrix = *draw;
	SlipDraw3DStateRecord *const state = ctx->trackContext->drawStateRecord;
	if (state != NULL) {
		state->matrix = *draw;
		state->origin = (SlipDraw3DVec32){ctx->bspOrigin.x, ctx->bspOrigin.y, ctx->bspOrigin.z};
		ctx->trackContext->origin = ctx->bspOrigin;
		if (ctx->directLight != 0)
			state->lightVector = (SlipDraw3DVec32){ctx->lightVector.x, ctx->lightVector.y, ctx->lightVector.z};
	}
}

static uint8_t *TrackView_ShapeLock(void *context, uint16_t resource) {
	TrackViewShapeBinding *const binding = context;
	if (binding->resource != 0) {
		(void)SlipResourceHost_Lock(NULL, resource);
		binding->payload = SlipResourceHost_Payload(resource);
	} else if (binding->resident != NULL)
		binding->payload = *binding->resident;
	else if (!SlipResource_LoadByName(binding->render.archives, binding->render.archiveCount, binding->name,
	                                  &binding->payload))
		SlipRuntime_Fatal("Could not load shape resource");
	if (!SlipShape3D_FromPayload(binding->payload.data, binding->payload.size, &binding->render.shape))
		SlipRuntime_Fatal("Invalid shape resource");
	return binding->payload.data;
}

static void TrackView_ShapeUnlock(void *context, uint16_t resource) {
	TrackViewShapeBinding *const binding = context;
	if (binding->resource != 0)
		SlipResourceHost_Unlock(NULL, resource);
	/* Resident payloads belong to the native resource cache. */
}

static void TrackView_ShapePrepare(void *context, uint8_t *shape) {
	TrackViewShapeBinding *const binding = context;
	TrackViewRawBspContext *const track = binding->render.trackContext;
	SlipShape3DPrepare result;
	if (!SlipShape3D_Prepare(0, shape, binding->payload.size, track->materialTable, track->materialTableBytes,
	                         track->materialGlobal, NULL, 0, NULL, 0, &result))
		SlipRuntime_Fatal("Could not prepare shape materials");
}

static uint16_t TrackView_ShapeFlags(void *context) {
	return (uint16_t)((TrackViewShapeBinding *)context)->render.projectState.renderFlags;
}

static void TrackView_SetShapeFlags(void *context, uint16_t flags) {
	SlipDraw3DProjectState *const projection = &((TrackViewShapeBinding *)context)->render.projectState;
	if (projection->auxiliaryClipPlaneEnabled == 0)
		flags &= (uint16_t)~SLIP_SHAPE_CLIP_AUXILIARY;
	projection->renderFlags = flags;
	if ((flags & SLIP_SHAPE_CLIP_AUXILIARY) != 0) {
		projection->depthOrigin = projection->auxiliaryClipPlaneOrigin;
		projection->depthNormal = projection->auxiliaryClipPlaneNormal;
	}
}

static uint32_t TrackView_ShapeClassify(void *context, SlipView3DVec32 center, int32_t radius) {
	return SlipDraw3D_ClassifyShapeBounds((SlipDraw3DVec32){center.x, center.y, center.z}, radius,
	                                      &((TrackViewShapeBinding *)context)->render.projectState);
}

static void TrackView_ShapeBounds(void *context, SlipView3DVec32 minimum, SlipView3DVec32 maximum) {
	(void)context;
	SlipRenderer_SetBounds(&SlipRendererHost_state, minimum, maximum);
}

static SlipActorShapeBounds TrackView_ShapeProjectBounds(void *context, SlipView3DVec32 center,
                                                         SlipView3DMatrix *matrix) {
	return SlipRenderer_ProjectBounds(&SlipRendererHost_state, center, matrix,
	                                  &((TrackViewShapeBinding *)context)->render.projectState);
}

static void TrackView_ShapeVertices(void *context, uint8_t *shape) {
	TrackViewShapeBinding *const binding = context;
	VehicleViewRenderContext *const ctx = &binding->render;
	uint16_t count;
	if (!SlipShape3D_GetVertexCount(&ctx->shape, &count))
		SlipRuntime_Fatal("Invalid shape vertices");
	binding->shape.shape = shape;
	ctx->shapeScaleShift = ctx->shape.scaleShift;
	ctx->vertexRecordCount = count;
	SlipRendererState *const hostRenderer = ctx->trackContext->hostRenderer;
	if (hostRenderer != NULL) {
		if (!TrackView_BuildVertexRecords(ctx->trackContext, TRACK_VIEW_DIAGNOSTIC_PREVIEW_BUILD_VERTEX_RECORDS,
		                                  shape + ctx->shape.vertexOffset + SLIP_SHAPE_TABLE_COUNT_BYTES,
		                                  ctx->shape.size - ctx->shape.vertexOffset - SLIP_SHAPE_TABLE_COUNT_BYTES,
		                                  count, SLIP_SHAPE_VERTEX_BYTES, NULL, NULL, NULL))
			SlipRuntime_Fatal("Could not bind shape vertex workspace");
		ctx->vertexRecords = ctx->trackContext->vertexRecords;
	} else {
		ctx->vertexRecords = malloc((size_t)count * sizeof(*ctx->vertexRecords));
		if (ctx->vertexRecords == NULL ||
		    !SlipDraw3D_BuildVertexRecords(ctx->vertexRecords, count,
		                                   shape + ctx->shape.vertexOffset + SLIP_SHAPE_TABLE_COUNT_BYTES,
		                                   ctx->shape.size - ctx->shape.vertexOffset - SLIP_SHAPE_TABLE_COUNT_BYTES,
		                                   count, SLIP_SHAPE_VERTEX_BYTES, NULL, NULL, NULL, 0, 0, NULL))
			SlipRuntime_Fatal("Could not allocate shape vertices");
	}
	ctx->trackContext->vertexRecords = ctx->vertexRecords;
	ctx->trackContext->vertexRecordCount = count;
	ctx->trackContext->projectState = &ctx->projectState;
}

static bool TrackView_ShapePlaneCarry(void *context, int16_t x, int16_t y, int16_t z, uint16_t vertex) {
	VehicleViewRenderContext *const ctx = &((TrackViewShapeBinding *)context)->render;
	SlipTrackWorldPlaneClassify classification;
	if (!SlipTrackWorld_ClassifyPlaneFromSource(ctx->projectState.projectionMode, vertex, (uint16_t)x, (uint16_t)y,
	                                            (uint16_t)z, (const uint8_t *)ctx->vertexRecords,
	                                            ctx->vertexRecordCount * sizeof(*ctx->vertexRecords), ctx->bspOrigin,
	                                            TrackView_VehicleViewBspSource, ctx, &ctx->matrix, &classification))
		SlipRuntime_Fatal("Could not classify shape plane");
	return classification.carry != 0;
}

static bool TrackView_ShapePlaneVisible(void *context, int16_t x, int16_t y, int16_t z, uint16_t vertex) {
	return !TrackView_ShapePlaneCarry(context, x, y, z, vertex);
}

static void TrackView_ShapeSortNode(void *context, uint32_t index, uint16_t type, int32_t classification,
                                    const uint8_t *node) {
	TrackViewShapeBinding *const binding = context;
	VehicleViewRenderContext *const ctx = &binding->render;
	if (type == SLIP_SHAPE_SORT_NODE_CHILD && (ctx->actor != NULL || ctx->trackContext->articSortCallback)) {
		SlipShape3DPrimitive child = {.primitiveOffset = index,
		                              .nodeKind = type,
		                              .firstChildOffset = SlipBytes_ReadLE32(node),
		                              .secondChildOffset = SlipBytes_ReadLE32(node + SLIP_SHAPE_SORT_CHILD_1_OFFSET)};
		TrackViewRawBspContext *const track = ctx->trackContext;
		if (ctx->actor != NULL ? ctx->actor->drawChildrenAfterParent : track->articDrawChildren == 0)
			return;
		if ((child.firstChildOffset | child.secondChildOffset) != 0)
			SlipRuntime_Fatal("ArticShapeDrawSortNode - child problem");
		const uint32_t clipLevel = track->recordIndex;
		SlipDraw3DStateRecord *const parentRecord = track->drawStateRecord;
		SlipDraw3DStateRecord nativeChildRecord = {0};
		bool nativeRecords = track->drawStateRecords == NULL || track->vertexBufferBase == NULL;
		if (!TrackView_ApplyLoadedDrawState(track, clipLevel + 1u))
			SlipRuntime_Fatal("ArticShapeDrawSortNode - clip state unavailable");
		if (nativeRecords)
			track->drawStateRecord = &nativeChildRecord;
		if (track->articSortCallback)
			(void)TrackView_ArticSortCallback(ctx, &child);
		else
			(void)TrackView_VehicleViewDrawSortChild(ctx, index);
		if (!TrackView_ApplyLoadedDrawState(track, clipLevel))
			SlipRuntime_Fatal("ArticShapeDrawSortNode - clip state unavailable");
		if (nativeRecords) {
			track->drawStateRecord = parentRecord;
			track->origin = (SlipView3DVec32){parentRecord->origin.x, parentRecord->origin.y, parentRecord->origin.z};
			track->transform = parentRecord->transform;
			track->sourcePoint = parentRecord->sourcePoint;
		}
		return;
	}
	binding->sortCallback(&binding->shape, index, type, classification, node, binding->calls);
}

static void TrackView_ShapeTraverse(void *context, const uint8_t *sort, SlipActorShapeSortNode callback,
                                    SlipActorShapeState *state, const SlipActorShapeCalls *calls) {
	(void)state;
	TrackViewShapeBinding *const binding = context;
	binding->sortCallback = callback;
	binding->calls = calls;
	SlipShapeSortCalls sortCalls = {binding, TrackView_ShapePlaneCarry};
	SlipShapeSort_Run(&binding->sort, sort, TrackView_ShapeSortNode, binding, &sortCalls);
}

static void TrackView_ShapeUnsorted(void *context, uint8_t *shape) {
	TrackViewShapeBinding *const binding = context;
	SlipShapePrimitiveCalls calls = {binding, TrackView_ShapePlaneVisible};
	SlipShape_DrawUnsorted(&binding->shape, shape, &calls);
}

static void TrackView_ShapeRestoreVertexCursor(void *context) {
	TrackViewShapeBinding *const binding = context;
	SlipRendererState *const hostRenderer = binding->render.trackContext->hostRenderer;
	if (hostRenderer != NULL &&
	    !TrackView_RestoreVertexBufferCursor(binding->render.trackContext,
	                                         TRACK_VIEW_DIAGNOSTIC_ACTOR_PREVIEW_RESTORE_VERTEX_CURSOR))
		SlipRuntime_Fatal("Could not restore shape vertex workspace");
}

static int TrackView_DrawVehicleViewShape(const SlipResourcePayload *residentShape, const char *const *archives,
                                          size_t archiveCount, const char *shapeName, const SlipView3DMatrix *matrix,
                                          SlipView3DVec32 translation, SlipView3DVec32 bspOrigin,
                                          SlipView3DVec32 lightVector, SlipDraw3DProjectState *projectState,
                                          const VehicleArtActor *actor, int lod,
                                          const VehicleViewMaterialTable *materials, uint32_t ambientLight,
                                          uint32_t directLight, TrackViewRawBspContext *trackContext) {
	if (residentShape == NULL && (shapeName == NULL || shapeName[0] == '\0'))
		return 0;
	TrackViewShapeBinding binding = {
	    .resident = residentShape,
	    .name = shapeName,
	    .resource = trackContext->hostShapeResource,
	    .shape = {.viewPosition = translation, .primitive = TrackView_DispatchShapePrimitive},
	    .render = {.matrix = *matrix,
	               .translation = translation,
	               .bspOrigin = bspOrigin,
	               .lightVector = lightVector,
	               .projectState = *projectState,
	               .actor = actor,
	               .archives = archives,
	               .archiveCount = archiveCount,
	               .materials = materials,
	               .trackContext = trackContext,
	               .ambientLight = ambientLight,
	               .directLight = directLight,
	               .lod = lod}};
	SlipDraw3DVertexRecord *const savedVertices = trackContext->vertexRecords;
	const size_t savedCount = trackContext->vertexRecordCount;
	SlipDraw3DProjectState *const savedProjection = trackContext->projectState;
	SlipActorShapeCalls calls = {.context = &binding,
	                             .setup = TrackView_ShapeSetup,
	                             .matrices = TrackView_ShapeMatrices,
	                             .lock = TrackView_ShapeLock,
	                             .unlock = TrackView_ShapeUnlock,
	                             .prepare = TrackView_ShapePrepare,
	                             .getShapeFlags = TrackView_ShapeFlags,
	                             .setShapeFlags = TrackView_SetShapeFlags,
	                             .classify = TrackView_ShapeClassify,
	                             .bounds = TrackView_ShapeBounds,
	                             .projectBounds = TrackView_ShapeProjectBounds,
	                             .vertices = TrackView_ShapeVertices,
	                             .traverse = TrackView_ShapeTraverse,
	                             .unsorted = TrackView_ShapeUnsorted,
	                             .restoreVertexCursor = TrackView_ShapeRestoreVertexCursor};
	if (actor != NULL || trackContext->articSortCallback) {
		SlipActorRenderState render = {0};
		SlipActorPartRecord part = {.hasDrawShape = true,
		                            .drawShape = binding.resource,
		                            .drawMatrix = *matrix,
		                            .worldMatrix = *matrix,
		                            .drawPosition = translation};
		binding.shape.actor = &render;
		SlipActorShape_DrawPart(&binding.shape, &part, &calls);
	} else {
		SlipShape_Draw(&binding.shape, binding.resource, matrix, matrix, &calls);
	}
	trackContext->vertexRecords = savedVertices;
	trackContext->vertexRecordCount = savedCount;
	trackContext->projectState = savedProjection;
	if (trackContext->hostRenderer == NULL)
		free(binding.render.vertexRecords);
	return binding.render.drawnCount > 0;
}

static bool TrackView_DrawShape(const SlipResourcePayload *residentShape, TrackViewRawBspContext *context,
                                const char *shapeName, const SlipView3DMatrix *objectMatrix,
                                const SlipView3DMatrix *viewMatrix, SlipView3DVec32 viewPosition,
                                SlipView3DVec32 worldPosition) {
	SlipDraw3DOriginSetup origin;

	if (context == NULL || (residentShape == NULL && shapeName == NULL) || objectMatrix == NULL || viewMatrix == NULL ||
	    context->resourceRegistry == NULL || context->projectState == NULL || context->drawStateRecord == NULL) {
		return false;
	}
	context->directLight = SlipDraw3D_directLight;
	context->ambientLight = SlipDraw3D_ambientLight;

	if (!SlipDraw3D_SetOrigin(
	        context->drawStateRecord, viewMatrix, objectMatrix,
	        (SlipDraw3DVec32){(int32_t)context->cameraWorldX, (int32_t)context->cameraWorldY,
	                          (int32_t)context->cameraWorldZ},
	        (SlipDraw3DVec32){worldPosition.x, worldPosition.y, worldPosition.z}, context->directLight != 0,
	        (SlipDraw3DVec32){context->lightInput.x, context->lightInput.y, context->lightInput.z}, &origin)) {
		return false;
	}
	context->origin = (SlipView3DVec32){origin.origin.x, origin.origin.y, origin.origin.z};
	if (residentShape == NULL && shapeName[0] == 0) {
		return true;
	}

	{
		static VehicleViewMaterialTable sceneryMaterials;
		static const uint8_t *sceneryMaterialsSource = NULL;
		static uint32_t sceneryMaterialsCount = 0;
		const VehicleViewMaterialTable *materials = NULL;

		if (context->hostShapeResource == 0) {
			SlipResourcePayload preparePayload = residentShape != NULL ? *residentShape : (SlipResourcePayload){0};

			if (residentShape != NULL ||
			    SlipResource_LoadByName(context->resourceRegistry->archives, context->resourceRegistry->archiveCount,
			                            shapeName, &preparePayload)) {
				SlipShape3DPrepare prepare;

				(void)SlipShape3D_Prepare(0, preparePayload.data, preparePayload.size, context->materialTable,
				                          context->materialTableBytes, context->materialGlobal, NULL, 0, NULL, 0,
				                          &prepare);
			}
		}
		if (sceneryMaterialsSource != context->materialTable ||
		    (context->materialTableBytes >= SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES &&
		     sceneryMaterialsCount != SlipBytes_ReadLE32(context->materialTable))) {
			if (TrackView_VehicleViewMaterialsFromTrackTable(context->materialTable, context->materialTableBytes,
			                                                 &sceneryMaterials)) {
				if (!context->resourceRegistry->hostResources)
					TrackView_VehicleViewMaterialsLoadTextures(&sceneryMaterials, context->resourceRegistry->archives,
					                                           context->resourceRegistry->archiveCount);
				sceneryMaterialsSource = context->materialTable;
				sceneryMaterialsCount = SlipBytes_ReadLE32(context->materialTable);
			}
		}
		if (sceneryMaterialsSource == context->materialTable) {
			materials = &sceneryMaterials;
		}

		(void)TrackView_DrawVehicleViewShape(
		    residentShape, context->resourceRegistry->archives, context->resourceRegistry->archiveCount, shapeName,
		    viewMatrix, viewPosition, (SlipView3DVec32){origin.origin.x, origin.origin.y, origin.origin.z},
		    (SlipView3DVec32){context->drawStateRecord->lightVector.x, context->drawStateRecord->lightVector.y,
		                      context->drawStateRecord->lightVector.z},
		    context->projectState, NULL, 0, materials, context->ambientLight, context->directLight, context);
	}
	return true;
}

bool TrackViewDrawSceneryShape(TrackViewRawBspContext *context, const uint8_t *record, size_t recordBytes,
                               const SlipView3DMatrix *objectMatrix, const SlipView3DMatrix *viewMatrix,
                               SlipView3DVec32 viewPosition, SlipView3DVec32 worldPosition) {
	char shapeName[SLIP_TRK_SHAPE_NAME_BYTES + 1];
	if (record == NULL || recordBytes < SLIP_TRK_SHAPE_WITH_FACING_BYTES) {
		return false;
	}

	memcpy(shapeName, record, SLIP_TRK_SHAPE_NAME_BYTES);
	shapeName[SLIP_TRK_SHAPE_NAME_BYTES] = '\0';
	const uint16_t savedResource = context->hostShapeResource;
	if (context->resourceRegistry->hostResources)
		context->hostShapeResource = SlipBytes_ReadLE16(record + SLIP_TRK_SHAPE_HANDLE_OFFSET);
	bool result = TrackView_DrawShape(NULL, context, shapeName, objectMatrix, viewMatrix, viewPosition, worldPosition);
	context->hostShapeResource = savedResource;
	return result;
}

bool TrackView_DrawShapeEffect(TrackViewRawBspContext *context, uint32_t objectRecord) {
	const uint16_t object = (uint16_t)objectRecord;
	SlipObjectSlotDataReadResult shapeHandle;
	SlipObjectPosition position;
	SlipView3DVec32 view;
	SlipView3DMatrix objectMatrix;
	SlipObjectMatrixCopy matrixCopy;
	SlipResourcePayload payload = {0};
	SlipShape3D shape;
	if (context == NULL ||
	    !SlipObject_GetDrawData(context->objectTable, context->objectTableBytes, object, &shapeHandle)) {
		return false;
	}
	if (object == 0) {
		return true;
	}
	if (!SlipObject_Position(context->objectTable, context->objectTableBytes, object, &position) ||
	    !SlipObject_ViewPosition(context->objectTable, context->objectTableBytes, object, &view) ||
	    !SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, object, &objectMatrix, &matrixCopy)) {
		return false;
	}
	const SlipView3DMatrix *const drawMatrix = SlipObject_DrawMatrix(context->objectTable, object);

	if (!TrackView_LockResourceHandlePayload(context->resourceRegistry, (uint16_t)shapeHandle.drawData, &payload) ||
	    !SlipShape3D_FromPayload(payload.data, payload.size, &shape)) {
		return false;
	}
	TrackView_UnlockResourceHandlePayload(context->resourceRegistry, (uint16_t)shapeHandle.drawData);
	bool culled = context->projectState->sphereOutside((SlipDraw3DVec32){view.x, view.y, view.z}, shape.boundingRadius,
	                                                   context->projectState);
	if (g_vehicleViewDumpDiagnostics)
		fprintf(stderr, "shape_effect_draw object=%u view=%d,%d,%d radius=%d culled=%u\n", object, view.x, view.y,
		        view.z, shape.boundingRadius, culled);
	if (culled) {
		return true;
	}
	const uint16_t savedResource = context->hostShapeResource;
	if (context->resourceRegistry->hostResources)
		context->hostShapeResource = (uint16_t)shapeHandle.drawData;
	bool result = TrackView_DrawShape(
	    NULL, context, TrackView_ResourceNameFromHandle(context->resourceRegistry, (uint16_t)shapeHandle.drawData),
	    &objectMatrix, drawMatrix, view,
	    (SlipView3DVec32){(int32_t)position.positionX, (int32_t)position.positionY, (int32_t)position.positionZ});
	context->hostShapeResource = savedResource;
	return result;
}

bool TrackView_DrawWeaponProjectile(TrackViewRawBspContext *context, uint32_t objectHandle) {
	const uint16_t object = (uint16_t)objectHandle;
	SlipObjectPosition position;
	SlipView3DVec32 view;
	SlipView3DMatrix objectMatrix;
	SlipObjectMatrixCopy copied;
	SlipObjectSlotDataReadResult shapeHandle;
	(void)SlipObject_Position(context->objectTable, context->objectTableBytes, object, &position);
	(void)SlipObject_ViewPosition(context->objectTable, context->objectTableBytes, object, &view);

	SlipView3DVec32 world = {(int32_t)position.positionX, (int32_t)position.positionY, (int32_t)position.positionZ};
	(void)SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, object, &objectMatrix, &copied);
	const SlipView3DMatrix *const drawMatrix = SlipObject_DrawMatrix(context->objectTable, object);
	(void)SlipObject_GetDrawData(context->objectTable, context->objectTableBytes, object, &shapeHandle);

	SlipResourcePayload payload = {0};
	SlipShape3D shape;
	if (!TrackView_LockResourceHandlePayload(context->resourceRegistry, (uint16_t)shapeHandle.drawData, &payload) ||
	    !SlipShape3D_FromPayload(payload.data, payload.size, &shape))
		return false;
	TrackView_UnlockResourceHandlePayload(context->resourceRegistry, (uint16_t)shapeHandle.drawData);
	if (context->projectState->sphereOutside((SlipDraw3DVec32){view.x, view.y, view.z}, shape.boundingRadius,
	                                         context->projectState))
		return true;
	const uint16_t savedResource = context->hostShapeResource;
	if (context->resourceRegistry->hostResources)
		context->hostShapeResource = (uint16_t)shapeHandle.drawData;
	bool result = TrackView_DrawShape(
	    NULL, context, TrackView_ResourceNameFromHandle(context->resourceRegistry, (uint16_t)shapeHandle.drawData),
	    &objectMatrix, drawMatrix, view, world);
	context->hostShapeResource = savedResource;
	return result;
}

bool TrackView_DrawDoor(TrackViewRawBspContext *context, uint32_t object) {
	SlipObjectSlotDataReadResult privateState;
	SlipObjectPosition cameraPosition, position;
	SlipObjectMatrixCopy copied;
	SlipView3DMatrix cameraMatrix, objectMatrix;
	SlipView3DVec32 view;
	static SlipView3DVec16 clipDirection;
	if (context == NULL ||
	    !SlipObject_GetDrawData(context->objectTable, context->objectTableBytes, object, &privateState))
		return false;
	/* Translate the original static-table pointer to its typed host record. */
	const uint32_t doorOffset = privateState.drawData - SLIP_TRACK_DOOR_TABLE_DOS_TOKEN;
	if (doorOffset % SLIP_TRACK_DOOR_RECORD_BYTES != 0 ||
	    doorOffset / SLIP_TRACK_DOOR_RECORD_BYTES >= SLIP_TRACK_DOOR_CAPACITY)
		return false;
	const SlipTrackDoorRecord *const door = &SlipTrackWorld_doors[doorOffset / SLIP_TRACK_DOOR_RECORD_BYTES];
	SlipResourcePayload payload = SlipResourceHost_Payload(door->shapeHandle);
	clipDirection = (SlipView3DVec16){door->directionX, door->directionY, door->directionZ};
	if (!SlipObject_Position(context->objectTable, context->objectTableBytes, 0, &cameraPosition) ||
	    !SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, 0, &cameraMatrix, &copied))
		return false;
	SlipView3DVec32 planeOrigin = SlipView3D_TransformPositionByRows(
	    &cameraMatrix, (SlipView3DVec32){(int32_t)((uint32_t)door->planeOrigin.x - cameraPosition.positionX),
	                                     (int32_t)((uint32_t)door->planeOrigin.y - cameraPosition.positionY),
	                                     (int32_t)((uint32_t)door->planeOrigin.z - cameraPosition.positionZ)});
	SlipView3DVec32 normal =
	    SlipView3D_TransformVector(&cameraMatrix, (SlipView3DVec32){(int16_t)(0u - (uint16_t)clipDirection.x),
	                                                                (int16_t)(0u - (uint16_t)clipDirection.y),
	                                                                (int16_t)(0u - (uint16_t)clipDirection.z)});
	SlipDraw3D_SetAuxiliaryClipPlane(context->projectState,
	                                 (SlipDraw3DVec32){planeOrigin.x, planeOrigin.y, planeOrigin.z}, (int16_t)normal.x,
	                                 (int16_t)normal.y, (int16_t)normal.z);
	bool ok = SlipObject_Position(context->objectTable, context->objectTableBytes, object, &position) &&
	          SlipObject_ViewPosition(context->objectTable, context->objectTableBytes, object, &view) &&
	          SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, object, &objectMatrix, &copied);
	if (ok) {
		const SlipView3DMatrix *const drawMatrix = SlipObject_DrawMatrix(context->objectTable, (uint16_t)object);

		const uint8_t *const shapeBytes = SlipResourceHost_Lock(NULL, door->shapeHandle);
		const uint32_t radius = SlipBytes_ReadLE32(shapeBytes + SLIP_SHAPE_RADIUS_OFFSET);
		SlipResourceHost_Unlock(NULL, door->shapeHandle);
		if (!TrackView_SphereCullCallback(view, radius, &context->frustum)) {
			const uint16_t previousResource = context->hostShapeResource;
			context->hostShapeResource = door->shapeHandle;
			/* Refresh the consumed payload after the original lock query. */
			payload = SlipResourceHost_Payload(door->shapeHandle);
			ok = TrackView_DrawShape(&payload, context, NULL, &objectMatrix, drawMatrix, view,
			                         (SlipView3DVec32){(int32_t)position.positionX, (int32_t)position.positionY,
			                                           (int32_t)position.positionZ});
			context->hostShapeResource = previousResource;
		}
	}
	SlipDraw3D_ClearAuxiliaryClipPlane(context->projectState);
	return ok;
}

bool TrackView_DrawDoorReverse(TrackViewRawBspContext *context, uint32_t object) {
	SlipView3DMatrix matrix;
	SlipObjectMatrixCopy copied;
	SlipObjectMatrixInstall installed;
	if (context == NULL ||
	    !SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, object, &matrix, &copied))
		return false;
	matrix.m[0] = (int16_t)(0u - (uint16_t)matrix.m[0]);
	matrix.m[2] = (int16_t)(0u - (uint16_t)matrix.m[2]);
	matrix.m[6] = (int16_t)(0u - (uint16_t)matrix.m[6]);
	matrix.m[8] = (int16_t)(0u - (uint16_t)matrix.m[8]);
	if (!SlipObject_MatrixInstall(context->objectTable, context->objectTableBytes, object, &matrix, &installed))
		return false;
	bool drawn = TrackView_DrawDoor(context, object);
	if (!SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, object, &matrix, &copied))
		return false;
	matrix.m[0] = (int16_t)(0u - (uint16_t)matrix.m[0]);
	matrix.m[2] = (int16_t)(0u - (uint16_t)matrix.m[2]);
	matrix.m[6] = (int16_t)(0u - (uint16_t)matrix.m[6]);
	matrix.m[8] = (int16_t)(0u - (uint16_t)matrix.m[8]);
	return SlipObject_MatrixInstall(context->objectTable, context->objectTableBytes, object, &matrix, &installed) &&
	       drawn;
}

bool TrackView_QueueShapeEffect(TrackViewRawBspContext *context, uint32_t objectRecord) {
	SlipView3DVec32 view;
	if (context == NULL || SlipDraw3D_listPool == NULL ||
	    !SlipObject_ViewPosition(context->objectTable, context->objectTableBytes, (uint16_t)objectRecord, &view)) {
		return false;
	}
	SlipDraw3D_ListInsert(&SlipDraw3D_listState, SlipDraw3D_listPool,
	                      (size_t)SlipDraw3D_listState.capacity * sizeof(*SlipDraw3D_listPool), (uint32_t)view.z,
	                      TrackView_DrawShapeEffect, (uint16_t)objectRecord);
	return true;
}

bool TrackView_SceneryCallback(uint32_t recordAddress, uint16_t resourceHandleIndex, const uint8_t *record,
                               size_t recordBytes, const SlipView3DMatrix *objectMatrix,
                               const SlipView3DMatrix *viewMatrix, SlipView3DVec32 viewPosition,
                               SlipView3DVec32 worldPosition, uint32_t renderFlags, void *userData) {
	TrackViewRawBspContext *const context = (TrackViewRawBspContext *)userData;
	uint32_t savedRenderFlags;
	bool result;

	(void)recordAddress;
	(void)resourceHandleIndex;
	if (context == NULL) {
		return false;
	}
	savedRenderFlags = context->rendererFlags;
	context->rendererFlags = renderFlags;
	result =
	    TrackViewDrawSceneryShape(context, record, recordBytes, objectMatrix, viewMatrix, viewPosition, worldPosition);
	context->rendererFlags = savedRenderFlags;
	return result;
}

static SlipActorRecord *TrackView_PreviewActor(void *context, uint16_t object) {
	(void)object;
	return &((VehicleArtActor *)context)->record;
}

static const SlipView3DMatrix *TrackView_PreviewMatrix(void *context, uint16_t object) {
	VehicleArtActor *const actor = context;
	return object == 0 ? &actor->cameraMatrix : &actor->objectMatrix;
}

static SlipView3DVec32 TrackView_PreviewPosition(void *context, uint16_t object) {
	VehicleArtActor *const actor = context;
	return object == 0 ? actor->cameraPosition : actor->position;
}

static SlipView3DVec32 TrackView_PreviewViewPosition(void *context, uint16_t object) {
	(void)object;
	VehicleArtActor *const actor = context;
	SlipView3DVec32 delta = {(int32_t)((uint32_t)actor->position.x - (uint32_t)actor->cameraPosition.x),
	                         (int32_t)((uint32_t)actor->position.y - (uint32_t)actor->cameraPosition.y),
	                         (int32_t)((uint32_t)actor->position.z - (uint32_t)actor->cameraPosition.z)};
	return SlipView3D_TransformPositionByRows(&actor->cameraMatrix, delta);
}

static uint32_t TrackView_PreviewReciprocal(void *context) {
	return ((VehicleArtActor *)context)->projection->inverseProjectionScale;
}

static void TrackView_PreviewDrawPart(void *context, SlipActorPartRecord *part) {
	VehicleArtActor *const actor = context;
	if (!part->hasDrawShape)
		return;
	VehicleArtSlot *const slot = &actor->slots[part - actor->parts];
	SlipView3DVec32 delta = {
	    (int32_t)((uint32_t)actor->cameraPosition.x - (uint32_t)actor->position.x - (uint32_t)part->worldPosition.x),
	    (int32_t)((uint32_t)actor->cameraPosition.y - (uint32_t)actor->position.y - (uint32_t)part->worldPosition.y),
	    (int32_t)((uint32_t)actor->cameraPosition.z - (uint32_t)actor->position.z - (uint32_t)part->worldPosition.z)};
	SlipView3DVec32 origin = SlipView3D_TransformPositionByRows(&part->worldMatrix, delta);
	SlipView3DVec32 light = SlipView3D_TransformVector(&part->worldMatrix, actor->light);
	actor->drawn += TrackView_DrawVehicleViewShape(
	    NULL, actor->archives, actor->archiveCount, slot->bodyShapes[actor->render.shapeLodIndex], &part->drawMatrix,
	    part->drawPosition, origin, light, actor->projection, actor, (int)actor->render.shapeLodIndex, actor->materials,
	    actor->trackContext->ambientLight, actor->trackContext->directLight, actor->trackContext);
}

static void TrackView_BindPreviewActor(VehicleArtActor *actor) {
	actor->record.ownerObject = 1; /* Native identity of the preview object; zero is the camera. */
	actor->record.childrenInSortTree = !actor->drawChildrenAfterParent;
	actor->record.partCount = (uint32_t)actor->slotCount;
	for (size_t lod = 0; lod < VEHICLE_ART_SHAPE_LOD_COUNT; ++lod)
		actor->record.lodDistances[lod] = (uint32_t)actor->lodRanges[lod];
	for (size_t index = 0; index < actor->slotCount; ++index) {
		VehicleArtSlot *const slot = &actor->slots[index];
		SlipActorPartRecord *const part = &actor->parts[index];
		actor->record.parts[index] = part;
		part->tag = slot->nameTag;
		part->localPosition = (SlipView3DVec32){slot->localX, slot->localY, slot->localZ};
		part->angle = (uint16_t)slot->animationAngle;
		part->rotationCallbackOffset = slot->rotationCallbackOffset;
		for (size_t lod = 0; lod < VEHICLE_ART_SHAPE_LOD_COUNT; ++lod)
			part->shapes[lod] =
			    slot->bodyShapes[lod][0] != 0 ? (uint16_t)(index * VEHICLE_ART_SHAPE_LOD_COUNT + lod + 1) : 0;
		if (slot->firstChild != NULL) {
			part->firstChild = &actor->parts[slot->firstChild - actor->slots];
			SlipActorPartRecord *previous = NULL;
			for (VehicleArtSlot *child = slot->firstChild; child != NULL; child = child->nextSibling) {
				SlipActorPartRecord *const native = &actor->parts[child - actor->slots];
				native->parent = part;
				native->previousSibling = previous;
				if (previous != NULL)
					previous->nextSibling = native;
				previous = native;
			}
			previous->nextSibling = part->firstChild;
			part->firstChild->previousSibling = previous;
		}
	}
	actor->record.parts[0] = &actor->parts[actor->root - actor->slots];
	actor->calls = (SlipActorRenderCalls){.transform = {.access = {actor, TrackView_PreviewActor},
	                                                    .getObjectPosition = TrackView_PreviewPosition,
	                                                    .getObjectMatrix = TrackView_PreviewMatrix},
	                                      .viewPosition = TrackView_PreviewViewPosition,
	                                      .projectionReciprocal = TrackView_PreviewReciprocal,
	                                      .drawPart = TrackView_PreviewDrawPart};
}

static uint8_t *TrackView_ArticPointer(TrackViewRawBspContext *context, uint32_t partAddress, size_t requiredBytes) {
	uint32_t offset;

	if (context == NULL || context->articSlotPool == NULL || partAddress < context->articSlotPoolAddress) {
		return NULL;
	}
	offset = partAddress - context->articSlotPoolAddress;
	if ((size_t)offset > context->articSlotPoolBytes || requiredBytes > context->articSlotPoolBytes - (size_t)offset) {
		return NULL;
	}
	return context->articSlotPool + offset;
}

static void TrackView_ArticReadMatrix(const uint8_t *source, SlipView3DMatrix *matrix) {
	for (size_t i = 0; i < sizeof(matrix->m) / sizeof(matrix->m[0]); ++i) {
		matrix->m[i] = (int16_t)SlipBytes_ReadLE16(source + i * sizeof(matrix->m[0]));
	}
}

static void TrackView_ArticWriteMatrix(uint8_t *destination, const SlipView3DMatrix *matrix) {
	for (size_t i = 0; i < sizeof(matrix->m) / sizeof(matrix->m[0]); ++i) {
		TrackView_WriteLE16(destination + i * sizeof(matrix->m[0]), (uint16_t)matrix->m[i]);
	}
}

static const char *TrackView_ResourceNameFromHandle(const TrackViewResourceHandleRegistry *registry, uint16_t handle) {
	if (registry == NULL || handle == 0) {
		return NULL;
	}
	for (size_t i = 0; i < registry->entryCount; ++i) {
		if ((uint16_t)registry->entries[i].resourceHandle == handle) {
			return registry->entries[i].name;
		}
	}
	return NULL;
}

static uint32_t TrackView_ProjectionReciprocal(const TrackViewRawBspContext *context) {
	return context->projectState->inverseProjectionScale;
}

static bool TrackView_ArticShape(TrackViewRawBspContext *context, const uint8_t *partRecord, uint16_t *shapeHandle,
                                 bool *carry) {
	uint32_t lodIndex;

	if (context == NULL || partRecord == NULL || shapeHandle == NULL || carry == NULL) {
		return false;
	}
	lodIndex = context->articSelectedLod;
	const SlipArticPartHeader *const part = (const void *)partRecord;
	*shapeHandle = part->shapes[lodIndex];
	*carry = *shapeHandle == 0;
	return true;
}

static bool TrackView_ArticReplayShape(TrackViewRawBspContext *context, const uint8_t *partRecord,
                                       uint16_t *shapeHandle, bool *carry) {
	uint32_t lodIndex;

	if (context == NULL || partRecord == NULL || shapeHandle == NULL || carry == NULL) {
		return false;
	}
	lodIndex = context->articReplayLod;
	const SlipArticPartHeader *const part = (const void *)partRecord;
	*shapeHandle = part->replayShapes[lodIndex];
	*carry = *shapeHandle == 0;
	return true;
}

static bool TrackView_ArticPrepare(TrackViewRawBspContext *context, uint8_t *part) {
	uint16_t shapeHandle;
	bool carry;
	SlipView3DMatrix localMatrix;
	SlipView3DMatrix cameraMatrix;
	SlipView3DMatrix drawMatrix;
	SlipObjectMatrixCopy matrixCopy;
	SlipObjectPosition cameraPosition;
	SlipView3DVec32 worldDelta;
	SlipView3DVec32 drawPosition;
	uint32_t firstChildAddress;

	if (context == NULL || part == NULL || context->objectTable == NULL) {
		return false;
	}
	TrackView_WriteLE32(part + offsetof(SlipArticPartRecord, header.drawShape), UINT32_MAX);
	if (!TrackView_ArticShape(context, part, &shapeHandle, &carry)) {
		return false;
	}
	if (!carry) {
		TrackView_WriteLE32(part + offsetof(SlipArticPartRecord, header.drawShape), shapeHandle);
	}

	TrackView_ArticReadMatrix(part + offsetof(SlipArticPartRecord, worldMatrix), &localMatrix);
	if (!SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, 0, &cameraMatrix, &matrixCopy)) {
		return false;
	}
	SlipView3D_ComposeMatrix(&localMatrix, &cameraMatrix, &drawMatrix);
	TrackView_ArticWriteMatrix(part + offsetof(SlipArticPartRecord, drawMatrix), &drawMatrix);

	if (!SlipObject_Position(context->objectTable, context->objectTableBytes, 0, &cameraPosition)) {
		return false;
	}
	worldDelta.x = (int32_t)(0u - cameraPosition.positionX +
	                         SlipBytes_ReadLE32(part + offsetof(SlipArticPartRecord, header.worldPosition.x)) +
	                         (uint32_t)context->articActorPosition.x);
	worldDelta.y = (int32_t)(0u - cameraPosition.positionY +
	                         SlipBytes_ReadLE32(part + offsetof(SlipArticPartRecord, header.worldPosition.y)) +
	                         (uint32_t)context->articActorPosition.y);
	worldDelta.z = (int32_t)(0u - cameraPosition.positionZ +
	                         SlipBytes_ReadLE32(part + offsetof(SlipArticPartRecord, header.worldPosition.z)) +
	                         (uint32_t)context->articActorPosition.z);
	drawPosition = SlipView3D_TransformPositionByRows(&cameraMatrix, worldDelta);
	TrackView_WriteLE32(part + offsetof(SlipArticPartRecord, header.drawPosition.x), (uint32_t)drawPosition.x);
	TrackView_WriteLE32(part + offsetof(SlipArticPartRecord, header.drawPosition.y), (uint32_t)drawPosition.y);
	TrackView_WriteLE32(part + offsetof(SlipArticPartRecord, header.drawPosition.z), (uint32_t)drawPosition.z);

	firstChildAddress = SlipBytes_ReadLE32(part + offsetof(SlipArticPartRecord, header.firstChild));
	if (firstChildAddress != 0) {
		uint32_t childAddress = firstChildAddress;

		do {
			uint8_t *const childPart = TrackView_ArticPointer(context, childAddress, SLIP_ARTIC_PART_DRAW_MATRICES_END);

			if (childPart == NULL || !TrackView_ArticPrepare(context, childPart)) {
				return false;
			}
			childAddress = SlipBytes_ReadLE32(childPart + offsetof(SlipArticPartRecord, header.nextSibling));
		} while (childAddress != firstChildAddress);
	}
	return true;
}

static bool TrackView_ArticDrawOne(TrackViewRawBspContext *context, uint8_t *part) {
	uint32_t shapeHandle;
	const char *shapeName;
	SlipView3DMatrix localMatrix;
	SlipView3DMatrix drawMatrix;
	SlipView3DVec32 drawPosition;
	SlipView3DVec32 worldPosition;
	SlipView3DVec32 cameraDelta;
	SlipView3DVec32 origin;
	SlipView3DVec32 light;
	SlipView3DVec32 lightInput;
	SlipObjectPosition cameraPosition;
	bool savedSortCallback;

	if (context == NULL || part == NULL || context->resourceRegistry == NULL || context->projectState == NULL) {
		return false;
	}
	shapeHandle = SlipBytes_ReadLE32(part + offsetof(SlipArticPartRecord, header.drawShape));
	if (shapeHandle == UINT32_MAX) {
		return true;
	}
	shapeName = TrackView_ResourceNameFromHandle(context->resourceRegistry, (uint16_t)shapeHandle);
	if (shapeName == NULL ||
	    !SlipObject_Position(context->objectTable, context->objectTableBytes, 0, &cameraPosition)) {
		return false;
	}
	TrackView_ArticReadMatrix(part + offsetof(SlipArticPartRecord, worldMatrix), &localMatrix);
	TrackView_ArticReadMatrix(part + offsetof(SlipArticPartRecord, drawMatrix), &drawMatrix);
	drawPosition =
	    (SlipView3DVec32){(int32_t)SlipBytes_ReadLE32(part + offsetof(SlipArticPartRecord, header.drawPosition.x)),
	                      (int32_t)SlipBytes_ReadLE32(part + offsetof(SlipArticPartRecord, header.drawPosition.y)),
	                      (int32_t)SlipBytes_ReadLE32(part + offsetof(SlipArticPartRecord, header.drawPosition.z))};
	worldPosition =
	    (SlipView3DVec32){(int32_t)(SlipBytes_ReadLE32(part + offsetof(SlipArticPartRecord, header.worldPosition.x)) +
	                                (uint32_t)context->articActorPosition.x),
	                      (int32_t)(SlipBytes_ReadLE32(part + offsetof(SlipArticPartRecord, header.worldPosition.y)) +
	                                (uint32_t)context->articActorPosition.y),
	                      (int32_t)(SlipBytes_ReadLE32(part + offsetof(SlipArticPartRecord, header.worldPosition.z)) +
	                                (uint32_t)context->articActorPosition.z)};
	cameraDelta = (SlipView3DVec32){(int32_t)(cameraPosition.positionX - (uint32_t)worldPosition.x),
	                                (int32_t)(cameraPosition.positionY - (uint32_t)worldPosition.y),
	                                (int32_t)(cameraPosition.positionZ - (uint32_t)worldPosition.z)};
	origin = SlipView3D_TransformPositionByRows(&localMatrix, cameraDelta);
	lightInput = context->lightInput;

	light = TrackView_DrawStateLightVector(context);
	if (context->directLight != 0)
		light = SlipView3D_TransformVector(&localMatrix, lightInput);
	savedSortCallback = context->articSortCallback;
	context->articSortCallback = true;
	const uint16_t savedResource = context->hostShapeResource;
	if (context->resourceRegistry->hostResources)
		context->hostShapeResource = (uint16_t)shapeHandle;
	(void)TrackView_DrawVehicleViewShape(NULL, context->resourceRegistry->archives,
	                                     context->resourceRegistry->archiveCount, shapeName, &drawMatrix, drawPosition,
	                                     origin, light, context->projectState, NULL, (int)context->articSelectedLod,
	                                     NULL, context->ambientLight, context->directLight, context);
	context->hostShapeResource = savedResource;
	context->articSortCallback = savedSortCallback;
	return !context->failed;
}

static bool TrackView_ArticDraw(TrackViewRawBspContext *context, uint8_t *part) {
	uint32_t firstChildAddress;

	if (!TrackView_ArticDrawOne(context, part)) {
		return false;
	}
	if (context->articDrawChildren != 0) {
		return true;
	}
	firstChildAddress = SlipBytes_ReadLE32(part + offsetof(SlipArticPartRecord, header.firstChild));
	if (firstChildAddress != 0) {
		uint32_t childAddress = firstChildAddress;

		do {
			uint8_t *const childPart = TrackView_ArticPointer(context, childAddress, SLIP_ARTIC_PART_DRAW_MATRICES_END);

			if (childPart == NULL || !TrackView_ArticDraw(context, childPart)) {
				return false;
			}
			childAddress = SlipBytes_ReadLE32(childPart + offsetof(SlipArticPartRecord, header.nextSibling));
		} while (childAddress != firstChildAddress);
	}
	return true;
}

static bool TrackView_ArticDrawCallback(TrackViewRawBspContext *context, uint16_t objectOffset) {
	SlipView3DVec32 viewPosition;
	SlipObjectPosition actorPosition;
	uint8_t *actorRecord;
	bool zeroFlag;
	uint64_t product;
	int32_t lodDistance;
	uint32_t lodIndex;
	uint32_t rootPartAddress;
	uint8_t *rootPart;

	if (context == NULL || context->projectState == NULL || context->maths == NULL ||
	    !SlipArticSlot_Rebuild(objectOffset, context->objectTable, context->objectTableBytes, context->articSlotPool,
	                           context->articSlotPoolBytes, context->articSlotPoolAddress, context->articSlotPool,
	                           context->articSlotPoolBytes, context->articSlotPoolAddress, context->maths) ||
	    !SlipObject_ViewPosition(context->objectTable, context->objectTableBytes, objectOffset, &viewPosition) ||
	    !SlipObject_Position(context->objectTable, context->objectTableBytes, objectOffset, &actorPosition) ||
	    !SlipArticSlot_TestOwner(objectOffset, context->objectTable, context->objectTableBytes, context->articSlotPool,
	                             context->articSlotPoolBytes, context->articSlotPoolAddress, &actorRecord, &zeroFlag)) {
		return false;
	}

	const SlipArticActorHeader *const actor = (const void *)actorRecord;
	context->articViewDepth = viewPosition.z;
	context->articActorPosition = (SlipView3DVec32){(int32_t)actorPosition.positionX, (int32_t)actorPosition.positionY,
	                                                (int32_t)actorPosition.positionZ};
	context->articActor = actorRecord;
	context->articDrawChildren = actor->childrenInSortTree;
	product = (uint64_t)((int64_t)(int32_t)TrackView_ProjectionReciprocal(context) * (int64_t)context->articViewDepth);
	lodDistance = (int32_t)(uint32_t)(product >> SLIP_DRAW3D_SCALE_FRACTION_BITS);
	for (lodIndex = 0; lodIndex < SLIP_ART_LOD_COUNT; ++lodIndex) {
		if (lodDistance < (int32_t)actor->lodDistances[lodIndex]) {
			break;
		}
	}
	if (lodIndex == SLIP_ART_LOD_COUNT) {
		return true;
	}
	if (lodIndex < context->articMinimumLod) {
		lodIndex = context->articMinimumLod;
	}
	context->articSelectedLod = lodIndex;
	rootPartAddress = actor->parts[0];
	rootPart = TrackView_ArticPointer(context, rootPartAddress, SLIP_ARTIC_PART_DRAW_MATRICES_END);
	if (rootPart == NULL || !TrackView_ArticPrepare(context, rootPart)) {
		return false;
	}
	return TrackView_ArticDraw(context, rootPart);
}

static bool TrackView_ApplyPostPlaneProjection(TrackViewRawBspContext *context, SlipView3DVec32 point,
                                               SlipView3DVec32 *pointOut, bool *carryOut) {
	uint64_t dotProduct;
	uint32_t planeDot;
	uint64_t scaleProduct;
	uint32_t projectionScale;
	SlipView3DVec32 scaled;

	if (context == NULL || pointOut == NULL || carryOut == NULL) {
		return false;
	}
	dotProduct = (uint64_t)((int64_t)(int32_t)((uint32_t)point.x - (uint32_t)context->postPlanePointX) *
	                        context->postPlaneNormalX);
	dotProduct += (uint64_t)((int64_t)(int32_t)((uint32_t)point.y - (uint32_t)context->postPlanePointY) *
	                         context->postPlaneNormalY);
	dotProduct += (uint64_t)((int64_t)(int32_t)((uint32_t)point.z - (uint32_t)context->postPlanePointZ) *
	                         context->postPlaneNormalZ);
	planeDot =
	    (uint32_t)(dotProduct >> SLIP_NORMAL_FRACTION_BITS) + (uint32_t)((dotProduct >> SLIP_NORMAL_ROUND_BIT) & 1u);
	if ((int32_t)planeDot < 0) {
		*pointOut = point;
		*carryOut = true;
		return true;
	}
	scaleProduct = (uint64_t)planeDot * context->postPlaneScale;
	projectionScale = (uint32_t)(scaleProduct >> SLIP_DRAW3D_SCALE_FRACTION_BITS) +
	                  (uint32_t)((scaleProduct >> (SLIP_DRAW3D_SCALE_FRACTION_BITS - 1)) & 1u);
	scaled = SlipView3D_ScaleVector((int16_t)context->cameraLightX, (int16_t)context->cameraLightY,
	                                (int16_t)context->cameraLightZ, (int32_t)projectionScale);
	*pointOut = (SlipView3DVec32){(int32_t)((uint32_t)point.x + (uint32_t)scaled.x),
	                              (int32_t)((uint32_t)point.y + (uint32_t)scaled.y),
	                              (int32_t)((uint32_t)point.z + (uint32_t)scaled.z)};
	*carryOut = false;
	return true;
}

static bool TrackView_TransformPostPlanePoint(TrackViewRawBspContext *context, SlipView3DVec32 point,
                                              SlipView3DVec32 *pointOut, bool *carryOut) {
	return TrackView_ApplyPostPlaneProjection(context, point, pointOut, carryOut);
}

static bool TrackView_ShadowBounds(TrackViewRawBspContext *context, const SlipShape3D *shape,
                                   VehicleViewRenderContext *view, bool *rejected) {
	SlipView3DVec32 projected;
	bool carry;
	*rejected = true;

	if (!TrackView_ApplyPostPlaneProjection(context, view->translation, &projected, &carry))
		return false;
	if (carry)
		return true;
	const int32_t radius = (int32_t)((uint64_t)((int64_t)(int32_t)context->postPlaneScale * shape->boundingRadius) >>
	                                 SLIP_DRAW3D_SCALE_FRACTION_BITS);
	if (!SlipTrackWorld_SphereCull(projected, radius, &context->frustum, &carry))
		return false;
	if (carry)
		return true;
	SlipShape3DHeader header;
	if (shape->size < sizeof(header))
		return false;
	memcpy(&header, shape->data, sizeof(header));
	const SlipView3DVec32 corners[SLIP_TRACK_BOUNDING_CORNER_COUNT] = {
	    {header.minimumX, header.minimumY, header.minimumZ}, {header.minimumX, header.minimumY, header.maximumZ},
	    {header.maximumX, header.minimumY, header.maximumZ}, {header.maximumX, header.minimumY, header.minimumZ},
	    {header.minimumX, header.maximumY, header.minimumZ}, {header.minimumX, header.maximumY, header.maximumZ},
	    {header.maximumX, header.maximumY, header.maximumZ}, {header.maximumX, header.maximumY, header.minimumZ}};
	uint16_t all = UINT16_MAX;
	for (size_t i = 0; i < SLIP_TRACK_BOUNDING_CORNER_COUNT; ++i) {

		SlipView3DVec32 point = SlipView3D_TransformPositionByColumns(&view->matrix, corners[i]);
		point.x = (int32_t)((uint32_t)point.x + (uint32_t)view->translation.x);
		point.y = (int32_t)((uint32_t)point.y + (uint32_t)view->translation.y);
		point.z = (int32_t)((uint32_t)point.z + (uint32_t)view->translation.z);
		if (!TrackView_TransformPostPlanePoint(context, point, &projected, &carry))
			return false;
		if (!carry) {
			const uint16_t mask = (uint16_t)SlipTrackWorld_ClassifyPoint(
			    projected,
			    TrackView_VehicleViewProjectMask((SlipDraw3DVec32){projected.x, projected.y, projected.z}, view),
			    view->projectState.minZ, view->projectState.maxZ);
			if (mask == 0) {
				if (i < SLIP_TRACK_BOUNDING_CORNER_COUNT - 1) {
					*rejected = false;
					return true;
				}
			} else {
				all &= mask;
			}
		}
	}
	*rejected = all != 0;
	return true;
}

static bool TrackView_ShadowPolygon(TrackViewRawBspContext *context, VehicleViewRenderContext *view,
                                    const uint8_t *primitive, size_t primitiveBytes) {
	SlipDraw3DRecordPool *const pool = context->drawRecordPool;
	SlipDraw3DReturnActiveVisit returnedVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DReturnActiveRing returned;
	if (!SlipDraw3D_ReturnActiveRing(pool, returnedVisits, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, &returned) ||
	    primitiveBytes < SLIP_PRIMITIVE_HEADER_BYTES)
		return false;
	SlipDraw3DVec32 light = context->drawStateRecord->lightVector;
	const int16_t dot =
	    (int16_t)SlipView3D_DotProductQ14(SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_NORMAL_X_OFFSET),
	                                      SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_NORMAL_Y_OFFSET),
	                                      SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_NORMAL_Z_OFFSET),
	                                      (uint16_t)light.x, (uint16_t)light.y, (uint16_t)light.z, NULL);
	if (dot >= 0)
		return true;
	const uint16_t count = SlipBytes_ReadLE16(primitive) & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
	if (count == 0 || count > SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT ||
	    primitiveBytes - SLIP_PRIMITIVE_HEADER_BYTES < (size_t)count * SLIP_SERIALIZED_INDEX_BYTES)
		return false;
	context->materialColor = (uint8_t)context->postPlaneColor;
	uint32_t all = UINT32_MAX;
	for (size_t i = 0; i < count; ++i) {
		const uint16_t index =
		    SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_HEADER_BYTES + i * SLIP_SERIALIZED_INDEX_BYTES);
		if (index >= view->vertexRecordCount)
			return false;
		SlipDraw3DVertexRecord *const vertex = &view->vertexRecords[index];
		if (!(vertex->flags & SLIP_VERTEX_TRANSFORMED)) {
			vertex->world = TrackView_VehicleViewTransform((uint16_t)vertex->sourceX, (uint16_t)vertex->sourceY,
			                                               (uint16_t)vertex->sourceZ, vertex, view);
			vertex->flags |= SLIP_VERTEX_TRANSFORMED;
			SlipView3DVec32 projected;
			bool carry;
			if (!TrackView_ApplyPostPlaneProjection(
			        context, (SlipView3DVec32){vertex->world.x, vertex->world.y, vertex->world.z}, &projected, &carry))
				return false;
			if (carry)
				return true;
			vertex->world = (SlipDraw3DVec32){projected.x, projected.y, projected.z};
		}
		uint32_t mask = TrackView_VehicleViewProjectMask(vertex->world, view);
		if (vertex->world.z <= view->projectState.minZ)
			mask |= SLIP_BOX_CLIP_NEAR;
		if (vertex->world.z >= view->projectState.maxZ)
			mask |= SLIP_BOX_CLIP_FAR;
		all &= mask;
	}
	if (all != 0)
		return true;

	uint32_t first = 0, previous = 0;
	all = SLIP_CLIP_ALL;
	uint32_t any = 0;
	for (size_t i = 0; i < count; ++i) {
		SlipDraw3DLinkedDrawRecord *const sentinel = &pool->records[pool->freeHeadOffset / sizeof(pool->records[0])];
		const uint32_t current = sentinel->links.nextOffset;
		if (current == pool->freeHeadOffset || current >= sizeof(pool->records))
			return false;
		SlipDraw3DLinkedDrawRecord *const record = &pool->records[current / sizeof(pool->records[0])];
		sentinel->links.nextOffset = record->links.nextOffset;
		pool->records[record->links.nextOffset / sizeof(pool->records[0])].links.prevOffset = pool->freeHeadOffset;
		if (i == 0) {
			first = current;
			pool->inputActiveHeadOffset = first;
		} else {
			pool->records[previous / sizeof(pool->records[0])].links.nextOffset = current;
			record->links.prevOffset = previous;
		}
		SlipDraw3DVertexRecord *const vertex = &view->vertexRecords[SlipBytes_ReadLE16(
		    primitive + SLIP_PRIMITIVE_HEADER_BYTES + i * SLIP_SERIALIZED_INDEX_BYTES)];
		const uint32_t flags =
		    SlipDraw3D_ProjectVertex(vertex, &view->projectState, TrackView_VehicleViewTransform,
		                             TrackView_VehicleViewProject, TrackView_VehicleViewProject, view);
		record->drawRecord.world = vertex->world;
		record->drawRecord.screenX = vertex->screenX;
		record->drawRecord.screenY = vertex->screenY;
		record->drawRecord.flags = vertex->flags;
		record->drawRecord.depth = vertex->depth;
		all &= flags;
		any |= flags;
		previous = current;
	}
	pool->records[previous / sizeof(pool->records[0])].links.nextOffset = first;
	pool->records[first / sizeof(pool->records[0])].links.prevOffset = previous;

	SlipDraw3DClipFlagVisit flags[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneBoundsVisit bounds[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];

	SlipDraw3DPostPlaneClipRecordVisit
	    records[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT * SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DPostPlaneClipPlaneVisit planes[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DClipDispatchExecute dispatch;
	TrackViewPostPlaneArgs post = TrackView_PostPlaneArgs(context);
	SlipDraw3DProjectState *const state = &view->projectState;
	if (!SlipDraw3D_ClipDispatchExecute(
	        SlipDraw3D_RecordPoolBytes(pool), SlipDraw3D_RecordPoolByteSize(), first, pool->freeHeadOffset, any, all,
	        state->renderFlags, 0, state->minZ, state->maxZ, state->minX, state->maxX, state->minY, state->maxY,
	        TrackView_VehicleViewProject, TrackView_VehicleViewProject, view, post.hasPostPlanes, post.planeBase,
	        post.planeBytes, post.planeHeadOffset, post.limitXMin, post.limitXMax, post.limitYMin, post.limitYMax,
	        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, flags, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, bounds,
	        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, records,
	        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT * SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, planes,
	        SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, &dispatch))
		return false;
	pool->inputActiveHeadOffset = dispatch.activeHeadOffsetOut;
	if (dispatch.dispatch.carryOut)
		return true;
	SlipDraw3DRasterPoint points[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DFlatRingDispatch raster;
	return SlipDraw3D_RasterizeFlatRing(
	           pool, pool->inputActiveHeadOffset, 0, context->rendererFlags, (uint8_t)context->postPlaneColor, 0,
	           context->reverseTraversal == 0 ? SLIP_DRAW3D_RECORD_NEXT_OFFSET : SLIP_DRAW3D_RECORD_PREV_OFFSET, points,
	           SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT, &raster) != 0;
}

static bool TrackView_ShadowShape(TrackViewRawBspContext *context, uint16_t resource, const char *name,
                                  const SlipView3DMatrix *matrix, SlipView3DVec32 translation, SlipView3DVec32 origin,
                                  SlipView3DVec32 light) {

	context->drawStateRecord->matrix = *matrix;
	context->drawStateRecord->origin = (SlipDraw3DVec32){origin.x, origin.y, origin.z};
	context->drawStateRecord->lightVector = (SlipDraw3DVec32){light.x, light.y, light.z};
	context->origin = origin;
	SlipResourcePayload payload = {0};
	SlipShape3D shape;
	uint16_t vertexCount;
	bool hostResource = context->resourceRegistry->hostResources;
	if (hostResource) {
		(void)SlipResourceHost_Lock(NULL, resource);
		payload = SlipResourceHost_Payload(resource);
	} else if (!SlipResource_LoadByName(context->resourceRegistry->archives, context->resourceRegistry->archiveCount,
	                                    name, &payload))
		return false;
	if (!SlipShape3D_FromPayload(payload.data, payload.size, &shape) || shape.version != SLIP_SHAPE_VERSION ||
	    !SlipShape3D_GetVertexCount(&shape, &vertexCount))
		return false;
	VehicleViewRenderContext view = {0};
	view.matrix = *matrix;
	view.translation = translation;
	view.shapeScaleShift = shape.scaleShift;
	view.projectState = *context->projectState;
	bool rejected;
	if (!TrackView_ShadowBounds(context, &shape, &view, &rejected))
		return false;
	if (rejected) {
		if (hostResource)
			SlipResourceHost_Unlock(NULL, resource);
		return true;
	}
	view.projectState.renderFlags = shape.scaleShift == 0 ? 0 : SLIP_SHAPE_SHORT_COORDINATES;
	view.vertexRecordCount = vertexCount;
	bool ok;
	if (context->hostRenderer != NULL) {
		ok = TrackView_BuildVertexRecords(context, TRACK_VIEW_DIAGNOSTIC_PREVIEW_BUILD_VERTEX_RECORDS,
		                                  shape.data + shape.vertexOffset + SLIP_SHAPE_TABLE_COUNT_BYTES,
		                                  shape.size - shape.vertexOffset - SLIP_SHAPE_TABLE_COUNT_BYTES, vertexCount,
		                                  SLIP_SHAPE_VERTEX_BYTES, NULL, NULL, NULL);
		view.vertexRecords = context->vertexRecords;
	} else {
		view.vertexRecords = malloc((size_t)vertexCount * sizeof(*view.vertexRecords));
		if (view.vertexRecords == NULL)
			return false;
		ok = SlipDraw3D_BuildVertexRecords(view.vertexRecords, vertexCount,
		                                   shape.data + shape.vertexOffset + SLIP_SHAPE_TABLE_COUNT_BYTES,
		                                   shape.size - shape.vertexOffset - SLIP_SHAPE_TABLE_COUNT_BYTES, vertexCount,
		                                   SLIP_SHAPE_VERTEX_BYTES, NULL, NULL, NULL, 0, 0, NULL) != 0;
	}
	if (ok && shape.primitiveOffset != 0) {
		if ((shape.primitiveOffset > shape.size || shape.size - shape.primitiveOffset < SLIP_SHAPE_TABLE_COUNT_BYTES)) {
			ok = false;
		} else {
			const uint16_t count = SlipBytes_ReadLE16(shape.data + shape.primitiveOffset);
			uint32_t offset = shape.primitiveOffset + SLIP_SHAPE_TABLE_COUNT_BYTES;
			for (size_t i = 0; ok && i < count; ++i) {
				SlipShape3DPrimitiveAdvanceWrapper advance = {0};
				ok = (offset <= shape.size && shape.size - offset >= SLIP_PRIMITIVE_HEADER_BYTES) &&
				     TrackView_ShadowPolygon(context, &view, shape.data + offset, shape.size - offset) &&
				     SlipShape3D_PrimitiveAdvanceWrapper(shape.data + offset, shape.size - offset, offset, &advance);
				if (ok)
					offset = advance.advance.nextRecordOffset;
			}
		}
	}

	if (hostResource)
		SlipResourceHost_Unlock(NULL, resource);
	if (context->hostRenderer != NULL)
		ok =
		    TrackView_RestoreVertexBufferCursor(context, TRACK_VIEW_DIAGNOSTIC_VEHICLE_PREVIEW_RESTORE_VERTEX_CURSOR) &&
		    ok;
	else
		free(view.vertexRecords);
	return ok;
}

bool TrackView_ExecuteArticDrawCallback(TrackViewRawBspContext *context, uint32_t objectRecord) {
	const uint16_t objectOffset = (uint16_t)objectRecord;
	SlipView3DVec32 originalPosition;
	SlipView3DVec32 adjustedPosition;
	bool carry;
	SlipView3DMatrix objectMatrix;
	SlipView3DMatrix drawMatrix;
	SlipObjectMatrixCopy matrixCopy;
	SlipObjectPosition actorPosition;
	SlipObjectPosition cameraPosition;
	uint8_t *actorRecord;
	bool zeroFlag;
	uint64_t product;
	int32_t lodDistance;
	uint32_t lodIndex;
	uint32_t rootPartAddress;
	uint8_t *rootPart;
	uint16_t shapeHandle;
	bool shapeCarry;
	const char *shapeName;
	SlipView3DVec32 cameraDelta;
	SlipView3DVec32 origin;
	SlipView3DVec32 light;

	if (context == NULL || context->projectState == NULL ||
	    !SlipObject_ViewPosition(context->objectTable, context->objectTableBytes, objectOffset, &originalPosition) ||
	    !TrackView_TransformPostPlanePoint(context, originalPosition, &adjustedPosition, &carry)) {
		return false;
	}
	if (carry) {
		return true;
	}
	context->articViewDepth = adjustedPosition.z;
	if (!SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, objectOffset, &objectMatrix,
	                           &matrixCopy)) {
		return false;
	}
	product = (uint64_t)((int64_t)(int32_t)TrackView_ProjectionReciprocal(context) * (int64_t)context->articViewDepth);
	lodDistance = (int32_t)(uint32_t)(product >> SLIP_DRAW3D_SCALE_FRACTION_BITS);
	if (!SlipArticSlot_TestOwner(objectOffset, context->objectTable, context->objectTableBytes, context->articSlotPool,
	                             context->articSlotPoolBytes, context->articSlotPoolAddress, &actorRecord, &zeroFlag)) {
		return false;
	}

	const SlipArticActorHeader *const actor = (const void *)actorRecord;
	for (lodIndex = 0; lodIndex < SLIP_ART_LOD_COUNT; ++lodIndex) {
		if (lodDistance < (int32_t)actor->replayLodDistances[lodIndex]) {
			break;
		}
	}
	if (lodIndex == SLIP_ART_LOD_COUNT) {
		return true;
	}
	if (lodIndex < context->articMinimumLod) {
		lodIndex = context->articMinimumLod;
	}
	context->articReplayLod = lodIndex;
	context->articActor = actorRecord;
	rootPartAddress = actor->parts[0];
	rootPart = TrackView_ArticPointer(context, rootPartAddress, SLIP_ARTIC_PART_DRAW_MATRICES_END);
	if (rootPart == NULL || !TrackView_ArticReplayShape(context, rootPart, &shapeHandle, &shapeCarry)) {
		return false;
	}
	if (shapeCarry) {
		return true;
	}
	shapeName = TrackView_ResourceNameFromHandle(context->resourceRegistry, shapeHandle);
	if (shapeName == NULL ||
	    !SlipObject_Position(context->objectTable, context->objectTableBytes, objectOffset, &actorPosition) ||
	    !SlipObject_Position(context->objectTable, context->objectTableBytes, 0, &cameraPosition)) {
		return false;
	}
	drawMatrix = *SlipObject_DrawMatrix(context->objectTable, objectOffset);
	cameraDelta = (SlipView3DVec32){(int32_t)(cameraPosition.positionX - actorPosition.positionX),
	                                (int32_t)(cameraPosition.positionY - actorPosition.positionY),
	                                (int32_t)(cameraPosition.positionZ - actorPosition.positionZ)};
	origin = SlipView3D_TransformPositionByRows(&objectMatrix, cameraDelta);
	light = TrackView_DrawStateLightVector(context);
	if (context->directLight != 0)
		light = SlipView3D_TransformVector(&objectMatrix, context->lightInput);
	return TrackView_ShadowShape(context, shapeHandle, shapeName, &drawMatrix, originalPosition, origin, light);
}

bool TrackView_ExecuteSlotDrawCallback(TrackViewRawBspContext *context, uint32_t objectRecord) {
	const uint16_t objectOffset = (uint16_t)objectRecord;
	if (!TrackView_ArticDrawCallback(context, objectOffset)) {
		return false;
	}
	if (context->collisionBodyDraw != 0) {
		return false;
	}
	return true;
}

bool TrackView_ExecuteDroneDrawCallback(TrackViewRawBspContext *context, uint32_t objectRecord) {
	const uint16_t objectOffset = (uint16_t)objectRecord;
	if (!TrackView_ArticDrawCallback(context, objectOffset)) {
		return false;
	}
	if (context->collisionBodyDraw != 0) {
		return false;
	}
	return true;
}

static int TrackView_ArticSortCallback(VehicleViewRenderContext *ctx, const SlipShape3DPrimitive *primitive) {
	TrackViewRawBspContext *context;
	uint32_t partAddress;
	uint8_t *part;

	if (ctx == NULL || primitive == NULL || ctx->trackContext == NULL) {
		return 0;
	}
	context = ctx->trackContext;
	if (context->articDrawChildren == 0 || context->articActor == NULL) {
		return 1;
	}
	if ((primitive->firstChildOffset | primitive->secondChildOffset) != 0) {
		return 0;
	}
	partAddress = SlipBytes_ReadLE32(context->articActor + offsetof(SlipArticActorHeader, parts) +
	                                 primitive->primitiveOffset * SLIP_ARTIC_PART_POINTER_BYTES);
	part = TrackView_ArticPointer(context, partAddress, SLIP_ARTIC_PART_DRAW_MATRICES_END);
	if (part == NULL) {
		return 0;
	}
	return TrackView_ArticDrawOne(context, part) ? 1 : 0;
}

static int TrackView_VehicleViewDrawSortChild(VehicleViewRenderContext *ctx, uint32_t childIndex) {
	if (ctx == NULL || ctx->actor == NULL || ctx->actor->drawChildrenAfterParent ||
	    childIndex >= ctx->actor->slotCount) {
		return 1;
	}

	VehicleArtActor *const actor = (VehicleArtActor *)ctx->actor;
	TrackView_PreviewDrawPart(actor, &actor->parts[childIndex]);
	return 1;
}

bool TrackView_DrawVehicleViewModel(const char *resPath, int driver, SlipView3DMatrix *actorObjectMatrix,
                                    uint16_t frameStep) {
	SlipShape3D_Initialize();
	char secondaryPath[SLIP_TRACK_RESOURCE_SECONDARY_PATH_BYTES];
	const char *archives[2];
	size_t archiveCount;
	SlipResourcePayload artPayload = {0};
	SlipResourcePayload materialPayload = {0};
	SlipView3DMaths maths = {0};
	VehicleArtActor actor;
	VehicleViewMaterialTable materials = {0};
	const VehicleViewMaterialTable *materialTable = NULL;
	SlipDraw3DMaterialFrameSlots materialFrameSlots;
	TrackViewResourceHandleRegistry resourceRegistry;
	SlipDraw3DRecordPool drawRecordPool;
	SlipDraw3DRecordPoolInit drawRecordPoolInit;
	TrackViewRawBspContext trackContext;
	SlipDraw3DStateRecord drawStateRecord;
	SlipDraw3DRefreshMode0Projection refresh;
	SlipView3DMatrix staticActorObjectMatrix;
	SlipView3DMatrix cameraMatrix;
	SlipDraw3DProjectState projectState;
	SlipView3DVec32 actorPosition = {SLIP_VIEWER_OBJECT_X, SLIP_VIEWER_OBJECT_Y, SLIP_VIEWER_OBJECT_Z};
	SlipView3DVec32 cameraPosition;
	SlipView3DVec32 cameraOffset;
	char artName[SLIP_VEHICLE_PREVIEW_RESOURCE_NAME_BYTES];
	char materialName[SLIP_VEHICLE_PREVIEW_RESOURCE_NAME_BYTES];
	const int16_t angle = 0;
	bool ok = false;

	if (driver < 0 || driver >= kDriverCount) {
		return false;
	}
	archiveCount = SlipMenu_BuildArchiveList(resPath, secondaryPath, archives);
	if (archiveCount == 0 || !TrackView_LoadMaths(&maths, archives, archiveCount)) {
		return false;
	}

	SlipMenu_MakeDriverSpriteName(artName, sizeof(artName), "RACER", driver, ".ART");
	if (!SlipResource_LoadByName(archives, archiveCount, artName, &artPayload)) {
		return false;
	}
	if (!TrackView_VehicleArtActorFromPayload(&artPayload, &actor)) {
		return false;
	}
	SlipMenu_MakeDriverSpriteName(materialName, sizeof(materialName), "VIEW", driver, ".MAT");
	if (SlipResource_LoadByName(archives, archiveCount, materialName, &materialPayload) &&
	    TrackView_VehicleViewMaterialsFromPayload(&materialPayload, &materials)) {
		memset(&resourceRegistry, 0, sizeof(resourceRegistry));
		resourceRegistry.archives = archives;
		resourceRegistry.archiveCount = archiveCount;
		if (!SlipDraw3D_LoadMaterialFrameSlots(materials.expandedTable, materials.expandedTableBytes,
		                                       TrackView_LoadNamedResource, &resourceRegistry, &materialFrameSlots)) {
			TrackView_VehicleViewMaterialsFreeTextures(&materials);
			return false;
		}
		TrackView_VehicleViewMaterialsLoadTextures(&materials, archives, archiveCount);
		materialTable = &materials;
	}
	if (actorObjectMatrix == NULL) {
		staticActorObjectMatrix = TrackView_VehicleViewIdentityMatrix();
		actorObjectMatrix = &staticActorObjectMatrix;
	}
	TrackView_VehicleViewUpdateArtAnimations(&actor, frameStep);
	TrackView_VehicleViewActorCallback(actorObjectMatrix, &maths, frameStep);
	SlipDraw3D_InitDefaultProjectState(&projectState);

	SlipView3D_BuildYawMatrix(&maths, angle, &cameraMatrix);
	SlipView3D_ApplyPitchMatrix(&maths, g_vehicleViewParams[driver].pitchAngle, &cameraMatrix);
	SlipView3D_OrthonormalizeForwardBasis(&cameraMatrix);
	cameraOffset = SlipView3D_TransformPositionByColumns(
	    &cameraMatrix, (SlipView3DVec32){0, 0, -(int32_t)g_vehicleViewParams[driver].distanceLowWord});
	cameraPosition.x = actorPosition.x + cameraOffset.x;
	cameraPosition.y = actorPosition.y + cameraOffset.y;
	cameraPosition.z = actorPosition.z + cameraOffset.z;

	SlipDraw3D_SetViewport(&projectState, SLIP_VEHICLE_PREVIEW_VIEWPORT_LEFT, SLIP_VEHICLE_PREVIEW_VIEWPORT_TOP,
	                       SLIP_VEHICLE_PREVIEW_VIEWPORT_RIGHT, SLIP_VEHICLE_PREVIEW_VIEWPORT_BOTTOM,
	                       SLIP_VEHICLE_PREVIEW_VIEWPORT_CENTER_X,
	                       SLIP_VEHICLE_PREVIEW_VIEWPORT_CENTER_Y + g_vehicleViewParams[driver].centerYOffset);
	if (!SlipDraw3D_RefreshProjectFrustum(&projectState, 0, &refresh) ||
	    !SlipDraw3D_InitRecordPool(&drawRecordPool, &drawRecordPoolInit)) {
		if (materialTable != NULL) {
			TrackView_VehicleViewMaterialsFreeTextures(&materials);
		}
		return false;
	}
	memset(&drawStateRecord, 0, sizeof(drawStateRecord));
	memset(&trackContext, 0, sizeof(trackContext));
	trackContext.materialTable = materials.expandedTable;
	trackContext.materialTableBytes = materials.expandedTableBytes;
	trackContext.materialGlobal = materials.materialResourceHandle;
	trackContext.drawRecordPool = &drawRecordPool;
	trackContext.projectState = &projectState;
	trackContext.drawStateRecord = &drawStateRecord;
	trackContext.resourceRegistry = &resourceRegistry;
	trackContext.frustum = (SlipTrackWorldProjectFrustum){
	    refresh.maxXStep,        refresh.minXStep,       refresh.minYStep,           refresh.maxYStep,
	    refresh.minXPlaneDepthQ, refresh.minXPlaneNegXQ, refresh.maxXPlaneNegDepthQ, refresh.maxXPlaneXQ,
	    refresh.maxYPlaneDepthQ, refresh.maxYPlaneYQ,    refresh.minYPlaneNegDepthQ, refresh.minYPlaneNegYQ,
	    projectState.minZ,       projectState.maxZ};
	trackContext.rendererFlags = projectState.renderFlags;
	trackContext.ambientLight = TrackView_VehicleViewNormalizedLightAmbient();
	trackContext.directLight = TrackView_VehicleViewNormalizedLightDirect();
	trackContext.lightInput =
	    (SlipView3DVec32){SLIP_VEHICLE_PREVIEW_LIGHT_DIAGONAL_Q14, SLIP_VEHICLE_PREVIEW_LIGHT_DIAGONAL_Q14, 0};

	Raster_SetClipRect(SLIP_VEHICLE_PREVIEW_VIEWPORT_LEFT, SLIP_VEHICLE_PREVIEW_VIEWPORT_TOP,
	                   SLIP_VEHICLE_PREVIEW_VIEWPORT_RIGHT, SLIP_VEHICLE_PREVIEW_VIEWPORT_BOTTOM);
	if (actor.slotCount > SLIP_VEHICLE_PREVIEW_ORIGINAL_PART_CAPACITY)
		SlipRuntime_Fatal("ART exceeds the original sixteen-part actor table");
	actor.objectMatrix = *actorObjectMatrix;
	actor.cameraMatrix = cameraMatrix;
	actor.position = actorPosition;
	actor.cameraPosition = cameraPosition;
	actor.light = trackContext.lightInput;
	actor.archives = archives;
	actor.archiveCount = archiveCount;
	actor.projection = &projectState;
	actor.materials = materialTable;
	actor.trackContext = &trackContext;
	TrackView_BindPreviewActor(&actor);
	SlipActorPool pool = {0};
	SlipActor_Draw(&actor.render, &pool, actor.record.ownerObject, &maths, &actor.calls);
	ok = actor.drawn > 0;
	Raster_SetClipRect(0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1);

	if (materialTable != NULL) {
		TrackView_VehicleViewMaterialsFreeTextures(&materials);
	}
	return ok;
}

static bool TrackView_MaterialPayloadExpandedBytes(const SlipResourcePayload *payload, size_t *expandedBytes) {
	uint16_t count;

	if (payload == NULL || payload->data == NULL || expandedBytes == NULL || payload->size < SLIP_MAT_HEADER_BYTES ||
	    SlipBytes_ReadLE16(payload->data + SLIP_MAT_VERSION_OFFSET) != SLIP_MAT_VERSION) {
		return false;
	}
	count = SlipBytes_ReadLE16(payload->data);
	if ((size_t)count > (payload->size - SLIP_MAT_HEADER_BYTES) / SLIP_DRAW3D_RAW_MATERIAL_RECORD_SIZE) {
		return false;
	}
	*expandedBytes =
	    SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES + (size_t)count * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE;
	return true;
}

static bool TrackView_BuildGlobeMaterialTable(const char *const *archives, size_t archiveCount,
                                              const char *materialName, uint8_t **materialTable,
                                              size_t *materialTableBytes, uint16_t *materialGlobal,
                                              SlipDraw3DMaterialInstall *install,
                                              SlipDraw3DMaterialFrameSlots *frameSlots,
                                              TrackViewResourceHandleRegistry *resourceRegistry) {
	SlipResourcePayload materialPayload = {0};
	size_t expandedBytes;
	uint8_t *expandedTable = NULL;

	if (archives == NULL || materialName == NULL || materialTable == NULL || materialTableBytes == NULL ||
	    materialGlobal == NULL || install == NULL || frameSlots == NULL || resourceRegistry == NULL) {
		return false;
	}
	*materialTable = NULL;
	*materialTableBytes = 0;
	*materialGlobal = 0;
	memset(install, 0, sizeof(*install));
	memset(frameSlots, 0, sizeof(*frameSlots));
	memset(resourceRegistry, 0, sizeof(*resourceRegistry));
	resourceRegistry->archives = archives;
	resourceRegistry->archiveCount = archiveCount;
	if (!SlipResource_LoadByName(archives, archiveCount, materialName, &materialPayload) ||
	    !TrackView_MaterialPayloadExpandedBytes(&materialPayload, &expandedBytes)) {
		return false;
	}
	expandedTable = (uint8_t *)malloc(expandedBytes);
	if (expandedTable == NULL ||
	    !SlipDraw3D_SetMaterialsNoExisting(materialPayload.data, materialPayload.size, 0, 1u, expandedTable,
	                                       expandedBytes, install) ||
	    !SlipDraw3D_LoadMaterialFrameSlots(expandedTable, expandedBytes, TrackView_LoadNamedResource, resourceRegistry,
	                                       frameSlots)) {
		free(expandedTable);
		return false;
	}
	*materialTable = expandedTable;
	*materialTableBytes = expandedBytes;
	*materialGlobal = install->storedMaterialGlobal;
	return true;
}

bool TrackView_BuildTrackMaterialTable(const char *const *archives, size_t archiveCount, uint8_t resourceIndexMinus,
                                       uint8_t **materialTable, size_t *materialTableBytes, uint16_t *materialGlobal,
                                       TrackViewResourceHandleRegistry *resourceRegistryOut) {
	char trackMaterialName[SLIP_TRACK_MATERIAL_NAME_BUFFER_BYTES];
	uint16_t sourceResource;
	snprintf(trackMaterialName, sizeof(trackMaterialName), "%s.MAT", g_trackResourceNames[resourceIndexMinus]);
	if (!SlipResourceHost_Load(NULL, trackMaterialName, &sourceResource))
		return false;
	const uint8_t *source = SlipResourceHost_Lock(NULL, sourceResource);
	SlipMaterial_Install(&SlipMaterialHost_install, source, &SlipMaterialHost_installCalls);
	SlipResourceHost_Unlock(NULL, sourceResource);
	SlipResourceHost_Release(NULL, sourceResource);
	if (!SlipResourceHost_Load(NULL, "CARS.MAT", &sourceResource))
		return false;
	source = SlipResourceHost_Lock(NULL, sourceResource);
	SlipMaterial_Install(&SlipMaterialHost_install, source, &SlipMaterialHost_installCalls);
	SlipResourceHost_Unlock(NULL, sourceResource);
	SlipResourceHost_Release(NULL, sourceResource);
	*materialGlobal = SlipMaterialHost_residency.resource;
	SlipResourcePayload payload = SlipResourceHost_Payload(*materialGlobal);
	*materialTable = payload.data;
	*materialTableBytes = payload.size;
	TrackView_MaterialBytes(*materialTable, SlipMaterialHost_residency.table);
	memset(resourceRegistryOut, 0, sizeof(*resourceRegistryOut));
	resourceRegistryOut->archives = archives;
	resourceRegistryOut->archiveCount = archiveCount;
	resourceRegistryOut->hostResources = true;
	return true;
}

enum {
	SLIP_GLOBE_FREE_YAW_STEP = 0x0800,
	SLIP_GLOBE_FREE_PITCH_STEP = 0x1000,
	SLIP_GLOBE_TRACK_ALIGNMENT_STEP = 0x3000,
	SLIP_GLOBE_LONGITUDE_ORIGIN_DEGREES = 180,
	SLIP_GLOBE_LATITUDE_ORIGIN_DEGREES = 90,
	SLIP_GLOBE_FULL_TURN_DEGREES = 360,
	SLIP_GLOBE_DEGREES_TO_ANGLE_SHIFT = 16,
	SLIP_GLOBE_FLAG_LONGITUDE_BIAS = 0x600,
	SLIP_GLOBE_VIEW_DISTANCE = 6500,
	SLIP_GLOBE_FLAG_PASS_DEPTH_THRESHOLD = 5500,
	SLIP_GLOBE_RADIUS = 2000,
	SLIP_GLOBE_FLAG_RETRACTION_DISTANCE = 640,
	SLIP_GLOBE_VIEWPORT_CENTRE_X = 91,
	SLIP_GLOBE_VIEWPORT_CENTRE_Y = 109,
	SLIP_GLOBE_AMBIENT_LIGHT_Q14 = 0x1800,
	SLIP_GLOBE_DIRECT_LIGHT_Q14 = 0x2800
};

static void TrackView_TrackGlobeUpdateMatrix(const SlipView3DMaths *maths, uint16_t trackResourceHandle,
                                             SlipView3DMatrix *matrix, uint32_t rotationArgument) {
	SlipFrameTimerValues timerValues;
	uint32_t product;
	uint16_t angle;
	const int16_t *record;
	uint16_t resourceIndex;

	timerValues = SlipFrameTimer_Values();
	if (trackResourceHandle == 0) {
		product = SLIP_GLOBE_FREE_YAW_STEP * (uint16_t)timerValues.stepQ14;
		angle = (uint16_t)(product >> SLIP_Q14_FRACTION_BITS);
		SlipView3D_ApplyColumn0Column2Rotation(maths, (int16_t)angle, matrix);
		product = SLIP_GLOBE_FREE_PITCH_STEP * (uint16_t)timerValues.stepQ14;
		angle = (uint16_t)(product >> SLIP_Q14_FRACTION_BITS);
		SlipView3D_ApplyPitchMatrix(maths, (int16_t)angle, matrix);
		SlipView3D_OrthonormalizeForwardBasis(matrix);
		return;
	}

	resourceIndex = (uint16_t)(trackResourceHandle - 1u);
	if (resourceIndex >= kDriverCount) {
		return;
	}
	record = g_trackGlobeOrientation[resourceIndex];
	product = SLIP_GLOBE_TRACK_ALIGNMENT_STEP * (uint16_t)timerValues.stepQ14;
	angle = (uint16_t)(product >> SLIP_Q14_FRACTION_BITS);

	SlipView3D_RotateForwardTowards(maths, matrix, record[SLIP_GLOBE_FORWARD_VECTOR_OFFSET],
	                                record[SLIP_GLOBE_FORWARD_VECTOR_OFFSET + 1],
	                                record[SLIP_GLOBE_FORWARD_VECTOR_OFFSET + 2],
	                                (rotationArgument & SLIP_CALLBACK_PAYLOAD_UPPER_WORD_MASK) | angle);
	SlipView3D_RotateRightTowards(
	    maths, matrix, record[SLIP_GLOBE_RIGHT_VECTOR_OFFSET], record[SLIP_GLOBE_RIGHT_VECTOR_OFFSET + 1],
	    record[SLIP_GLOBE_RIGHT_VECTOR_OFFSET + 2], (rotationArgument & SLIP_CALLBACK_PAYLOAD_UPPER_WORD_MASK) | angle);
	SlipView3D_OrthonormalizeForwardBasis(matrix);
}

bool SlipTrackGlobe_UpdateGivenMatrix(const char *resPath, uint16_t trackResourceHandle, SlipView3DMatrix *matrix,
                                      uint32_t rotationArgument) {
	char secondaryPath[SLIP_TRACK_RESOURCE_SECONDARY_PATH_BYTES];
	const char *archives[2];
	size_t archiveCount;
	SlipView3DMaths maths = {0};

	archiveCount = SlipMenu_BuildArchiveList(resPath, secondaryPath, archives);
	if (archiveCount == 0 || !TrackView_LoadMaths(&maths, archives, archiveCount)) {
		return false;
	}
#ifdef SLIP_REPLAY_HARNESS
	extern void SlipUiCapture_TraceMatrix(const SlipView3DMatrix *matrix, bool after);
	SlipUiCapture_TraceMatrix(matrix, false);
#endif
	TrackView_TrackGlobeUpdateMatrix(&maths, trackResourceHandle, matrix, rotationArgument);
#ifdef SLIP_REPLAY_HARNESS
	SlipUiCapture_TraceMatrix(matrix, true);
#endif
	return true;
}

bool SlipTrackGlobe_UpdateMatrix(const char *resPath, uint16_t track) {
	if (!g_trackSelectGlobeMatrixValid)
		TrackView_TrackGlobeResetActorState();
	return SlipTrackGlobe_UpdateGivenMatrix(resPath, track, &g_trackSelectGlobeMatrix, 0);
}

static SlipView3DVec32 TrackView_TrackGlobeFlagSurfacePoint(const SlipView3DMaths *maths,
                                                            uint16_t trackResourceHandle) {
	int32_t longitude;
	int32_t latitude;
	int16_t lonAngle;
	int16_t latAngle;
	int16_t latRadius;
	uint16_t resourceIndex;

	resourceIndex = (uint16_t)(trackResourceHandle - 1u);

	longitude = ((((int32_t)g_trackGlobeFlagCoords[resourceIndex][0] + SLIP_GLOBE_LONGITUDE_ORIGIN_DEGREES)
	              << SLIP_GLOBE_DEGREES_TO_ANGLE_SHIFT) /
	             SLIP_GLOBE_FULL_TURN_DEGREES) -
	            SLIP_ANGLE_QUARTER_TURN + SLIP_GLOBE_FLAG_LONGITUDE_BIAS;
	latitude = (((-(int32_t)g_trackGlobeFlagCoords[resourceIndex][1] + SLIP_GLOBE_LATITUDE_ORIGIN_DEGREES)
	             << SLIP_GLOBE_DEGREES_TO_ANGLE_SHIFT) /
	            SLIP_GLOBE_FULL_TURN_DEGREES);
	lonAngle = (int16_t)(uint16_t)longitude;
	latAngle = (int16_t)(uint16_t)latitude;

	latRadius =
	    (int16_t)(uint16_t)(((int32_t)SlipView3D_SinQ14(maths, latAngle) * SLIP_Q14_ONE) >> SLIP_Q14_FRACTION_BITS);

	return (SlipView3DVec32){
	    (int16_t)(uint16_t)(((int32_t)SlipView3D_CosQ14(maths, lonAngle) * latRadius) >> SLIP_Q14_FRACTION_BITS),
	    (int16_t)(uint16_t)(((int32_t)SlipView3D_CosQ14(maths, latAngle) * SLIP_Q14_ONE) >> SLIP_Q14_FRACTION_BITS),
	    (int16_t)(uint16_t)(((int32_t)SlipView3D_SinQ14(maths, lonAngle) * latRadius) >> SLIP_Q14_FRACTION_BITS)};
}

static bool TrackView_TrackGlobeShouldDrawFlagPass(const SlipView3DMatrix *matrix, SlipView3DVec32 flagSurface,
                                                   bool secondPass) {
	SlipView3DVec32 transformed;
	int32_t depthTest;

	transformed = SlipView3D_TransformPositionByColumns(matrix, flagSurface);
	depthTest = transformed.z + SLIP_GLOBE_VIEW_DISTANCE - SLIP_GLOBE_FLAG_PASS_DEPTH_THRESHOLD;

	return secondPass ? depthTest < 0 : depthTest >= 0;
}

static int TrackView_DrawTrackSelectFlag(const char *const *archives, size_t archiveCount,
                                         const SlipView3DMatrix *matrix, SlipView3DVec32 flagTranslation,
                                         SlipView3DVec32 clipPlaneOrigin, SlipView3DVec32 flagBspOrigin,
                                         SlipDraw3DProjectState *projectState, TrackViewRawBspContext *trackContext,
                                         uint16_t flagResource) {
	SlipDraw3DProjectState flagProjectState = *projectState;

	const SlipView3DVec32 flagLight = SlipView3D_TransformVector(matrix, trackContext->lightInput);

	SlipDraw3D_SetAuxiliaryClipPlane(&flagProjectState,
	                                 (SlipDraw3DVec32){clipPlaneOrigin.x, clipPlaneOrigin.y, clipPlaneOrigin.z},
	                                 matrix->m[3], matrix->m[4], matrix->m[5]);
	trackContext->hostShapeResource = flagResource;
	return TrackView_DrawVehicleViewShape(NULL, archives, archiveCount, "FLAG.SHP", matrix, flagTranslation,
	                                      flagBspOrigin, flagLight, &flagProjectState, NULL, 0, NULL,
	                                      SLIP_GLOBE_AMBIENT_LIGHT_Q14, SLIP_GLOBE_DIRECT_LIGHT_Q14, trackContext);
}

static int16_t TrackView_TrackGlobeScaleAxisQ14(uint16_t grow, int16_t axis) {
	const int32_t product = (int32_t)(int16_t)grow * axis;

	return (int16_t)(uint16_t)((uint32_t)product >> SLIP_Q14_FRACTION_BITS);
}

static bool TrackView_DrawGlobeResources(const char *resPath, uint16_t trackResourceHandle, uint16_t growAmount,
                                         const SlipView3DMatrix *matrix, uint16_t globeResource,
                                         uint16_t flagResource) {
	if (globeResource == 0)
		SlipShape3D_Initialize();
	char secondaryPath[SLIP_TRACK_RESOURCE_SECONDARY_PATH_BYTES];
	const char *archives[2];
	size_t archiveCount;
	SlipView3DMaths maths = {0};
	SlipDraw3DProjectState projectState;
	SlipDraw3DRefreshMode0Projection refresh;
	SlipDraw3DMaterialInstall materialInstall;
	SlipDraw3DMaterialFrameSlots materialFrameSlots;
	TrackViewResourceHandleRegistry resourceRegistry;
	SlipDraw3DRecordPool localRecordPool;
	SlipDraw3DRecordPool *drawRecordPool = &localRecordPool;
	SlipDraw3DRecordPoolInit drawRecordPoolInit;
	TrackViewRawBspContext trackContext;
	SlipDraw3DStateRecord drawStateRecord;
	uint8_t *materialTable = NULL;
	size_t materialTableBytes = 0;
	uint16_t materialGlobal = 0;
	SlipView3DVec32 translation = {0, 0, SLIP_GLOBE_VIEW_DISTANCE};
	SlipView3DVec32 originDelta = {0, 0, -SLIP_GLOBE_VIEW_DISTANCE};
	SlipView3DVec32 flagSurface;
	SlipView3DVec32 flagTranslation;
	SlipView3DVec32 flagClipPlaneOrigin;
	SlipView3DVec32 flagOutward;
	SlipView3DVec32 flagOriginDelta;
	SlipView3DVec32 flagBspOrigin;
	SlipView3DMatrix flagMatrix = {{SLIP_Q14_ONE, 0, 0, 0, SLIP_Q14_ONE, 0, 0, 0, SLIP_Q14_ONE}};
	SlipView3DVec32 bspOrigin;
	SlipView3DVec32 lightVector;
	uint16_t growDisplacement;
	bool ok = false;

	archiveCount = SlipMenu_BuildArchiveList(resPath, secondaryPath, archives);
	if (archiveCount == 0 || !TrackView_LoadMaths(&maths, archives, archiveCount))
		return false;
	if (globeResource != 0) {
		materialGlobal = SlipMaterialHost_residency.resource;
		SlipResourcePayload payload = SlipResourceHost_Payload(materialGlobal);
		materialTable = payload.data;
		materialTableBytes = payload.size;
		TrackView_MaterialBytes(materialTable, SlipMaterialHost_residency.table);
		memset(&resourceRegistry, 0, sizeof(resourceRegistry));
		resourceRegistry.hostResources = true;
		drawRecordPool =
		    SlipResourceStorage_RecordPool(SlipResource_handles[SlipRendererHost_state.polygonResource].block);
	} else if (!TrackView_BuildGlobeMaterialTable(archives, archiveCount, "GLOBE.MAT", &materialTable,
	                                              &materialTableBytes, &materialGlobal, &materialInstall,
	                                              &materialFrameSlots, &resourceRegistry)) {
		return false;
	}
	if (!SlipDraw3D_InitRecordPool(drawRecordPool, &drawRecordPoolInit)) {
		if (globeResource == 0)
			free(materialTable);
		return false;
	}

	SlipDraw3D_InitDefaultProjectState(&projectState);
	if (globeResource != 0) {
		projectState.minZ = (int32_t)SlipDraw3D_minimumDepth;
		projectState.maxZ = (int32_t)SlipDraw3D_maximumDepth;
	}
	SlipDraw3D_SetViewport(&projectState, 0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1,
	                       SLIP_GLOBE_VIEWPORT_CENTRE_X, SLIP_GLOBE_VIEWPORT_CENTRE_Y);
	projectState.renderFlags = SLIP_RENDER_ALTERNATE_TEXTURE_RASTER;
	if (!SlipDraw3D_RefreshProjectFrustum(&projectState, 0, &refresh)) {
		if (globeResource == 0)
			free(materialTable);
		return false;
	}
	memset(&drawStateRecord, 0, sizeof(drawStateRecord));
	memset(&trackContext, 0, sizeof(trackContext));
	trackContext.materialTable = materialTable;
	trackContext.materialTableBytes = materialTableBytes;
	trackContext.materialGlobal = materialGlobal;
	trackContext.materialFrameIndex = 0;
	trackContext.drawRecordPool = drawRecordPool;
	trackContext.projectState = &projectState;
	trackContext.drawStateRecord = &drawStateRecord;
	trackContext.resourceRegistry = &resourceRegistry;
	if (globeResource != 0) {
		trackContext.hostRenderer = &SlipRendererHost_state;
		trackContext.hostShapeResource = globeResource;
		trackContext.drawStateRecords = SlipResourceStorage_DrawStateRecords(
		    SlipResource_handles[SlipRendererHost_state.stateResource].block, SlipRendererHost_state.stateCount);
		trackContext.drawStateRecordCount = SlipRendererHost_state.stateCount;
		trackContext.drawStateRecord = trackContext.drawStateRecords;
		trackContext.vertexBufferBase = SlipRendererHost_state.vertexBase;
		trackContext.vertexBufferRecordCapacity = SlipRendererHost_state.vertexCapacity;
		trackContext.vertexBufferLimit = SlipRendererHost_state.vertexCapacity * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
	}

	trackContext.frustum = (SlipTrackWorldProjectFrustum){
	    refresh.maxXStep,        refresh.minXStep,       refresh.minYStep,           refresh.maxYStep,
	    refresh.minXPlaneDepthQ, refresh.minXPlaneNegXQ, refresh.maxXPlaneNegDepthQ, refresh.maxXPlaneXQ,
	    refresh.maxYPlaneDepthQ, refresh.maxYPlaneYQ,    refresh.minYPlaneNegDepthQ, refresh.minYPlaneNegYQ,
	    projectState.minZ,       projectState.maxZ};
	trackContext.rendererFlags = SLIP_RENDER_ALTERNATE_TEXTURE_RASTER;
	trackContext.directLight = SLIP_GLOBE_DIRECT_LIGHT_Q14;
	trackContext.lightInput = (SlipView3DVec32){0, -SLIP_Q14_ONE, 0};
	trackContext.ambientLight = SLIP_GLOBE_AMBIENT_LIGHT_Q14;

	bspOrigin = SlipView3D_TransformPositionByRows(matrix, originDelta);
	lightVector = SlipView3D_TransformVector(matrix, (SlipView3DVec32){0, -SLIP_Q14_ONE, 0});

	if (trackResourceHandle == 0) {
		trackContext.hostShapeResource = globeResource;
		ok = TrackView_DrawVehicleViewShape(NULL, archives, archiveCount, "GLOBE.SHP", matrix, translation, bspOrigin,
		                                    lightVector, &projectState, NULL, 0, NULL, SLIP_GLOBE_AMBIENT_LIGHT_Q14,
		                                    SLIP_GLOBE_DIRECT_LIGHT_Q14, &trackContext) > 0;
		if (globeResource == 0)
			free(materialTable);
		return ok;
	}

	if (trackResourceHandle > kDriverCount) {
		if (globeResource == 0)
			free(materialTable);
		return false;
	}

	growDisplacement =
	    (uint16_t)(((uint32_t)(uint16_t)(SLIP_Q14_ONE - growAmount) * SLIP_GLOBE_FLAG_RETRACTION_DISTANCE) >>
	               SLIP_Q14_FRACTION_BITS);
	flagSurface = TrackView_TrackGlobeFlagSurfacePoint(&maths, trackResourceHandle);

	flagSurface.x = (int16_t)(uint16_t)((flagSurface.x * SLIP_GLOBE_RADIUS) >> SLIP_Q14_FRACTION_BITS);
	flagSurface.y = (int16_t)(uint16_t)((flagSurface.y * SLIP_GLOBE_RADIUS) >> SLIP_Q14_FRACTION_BITS);
	flagSurface.z = (int16_t)(uint16_t)((flagSurface.z * SLIP_GLOBE_RADIUS) >> SLIP_Q14_FRACTION_BITS);
	flagOutward = SlipView3D_TransformPositionByColumns(matrix, flagSurface);
	flagTranslation = flagOutward;
	flagTranslation.z += SLIP_GLOBE_VIEW_DISTANCE;

	flagClipPlaneOrigin = flagTranslation;

	{
		SlipView3DNormalizeLength3D flagNormal;
		SlipView3DMatrix flagBasis;

		(void)SlipView3D_NormalizeLength3D((uint32_t)(uint16_t)flagSurface.x, (uint32_t)(uint16_t)flagSurface.y,
		                                   (uint32_t)(uint16_t)flagSurface.z, &flagNormal);
		(void)SlipView3D_BuildMatrixFromVector(&flagBasis, (int16_t)(uint16_t)flagNormal.unitXQ14,
		                                       (int16_t)(uint16_t)flagNormal.unitYQ14,
		                                       (int16_t)(uint16_t)flagNormal.unitZQ14);
		SlipView3D_ApplyPitchMatrix(&maths, -SLIP_ANGLE_QUARTER_TURN, &flagBasis);
		SlipView3D_MultiplyMatrix(&flagBasis, matrix, &flagMatrix);
	}

	flagTranslation.x -= TrackView_TrackGlobeScaleAxisQ14(growDisplacement, flagMatrix.m[3]);
	flagTranslation.y -= TrackView_TrackGlobeScaleAxisQ14(growDisplacement, flagMatrix.m[4]);
	flagTranslation.z -= TrackView_TrackGlobeScaleAxisQ14(growDisplacement, flagMatrix.m[5]);
	flagOriginDelta.x = -flagTranslation.x;
	flagOriginDelta.y = -flagTranslation.y;
	flagOriginDelta.z = -flagTranslation.z;
	flagBspOrigin = SlipView3D_TransformPositionByRows(&flagMatrix, flagOriginDelta);
	if (TrackView_TrackGlobeShouldDrawFlagPass(matrix, flagSurface, false)) {
		ok = TrackView_DrawTrackSelectFlag(archives, archiveCount, &flagMatrix, flagTranslation, flagClipPlaneOrigin,
		                                   flagBspOrigin, &projectState, &trackContext, flagResource) > 0;
	}
	trackContext.hostShapeResource = globeResource;
	ok = TrackView_DrawVehicleViewShape(NULL, archives, archiveCount, "GLOBE.SHP", matrix, translation, bspOrigin,
	                                    lightVector, &projectState, NULL, 0, NULL, SLIP_GLOBE_AMBIENT_LIGHT_Q14,
	                                    SLIP_GLOBE_DIRECT_LIGHT_Q14, &trackContext) > 0;
	if (TrackView_TrackGlobeShouldDrawFlagPass(matrix, flagSurface, true)) {
		ok = TrackView_DrawTrackSelectFlag(archives, archiveCount, &flagMatrix, flagTranslation, flagClipPlaneOrigin,
		                                   flagBspOrigin, &projectState, &trackContext, flagResource) > 0 ||
		     ok;
	}

	if (globeResource == 0)
		free(materialTable);
	return ok;
}

bool SlipTrackGlobe_DrawGivenResources(const char *resPath, uint16_t track, uint16_t grow,
                                       const SlipView3DMatrix *matrix, uint16_t globe, uint16_t flag) {
	return TrackView_DrawGlobeResources(resPath, track, grow, matrix, globe, flag);
}

bool SlipTrackGlobe_DrawGivenMatrix(const char *resPath, uint16_t track, uint16_t grow,
                                    const SlipView3DMatrix *matrix) {
	return TrackView_DrawGlobeResources(resPath, track, grow, matrix, 0, 0);
}

bool SlipTrackGlobe_Draw(const char *resPath, uint16_t track, uint16_t grow) {
	return SlipTrackGlobe_DrawGivenMatrix(resPath, track, grow, &g_trackSelectGlobeMatrix);
}

bool SlipTrackGlobe_DrawRetained(const char *resPath, uint16_t track, uint16_t grow, uint16_t globeResource,
                                 uint16_t flagResource) {
	return SlipTrackGlobe_DrawGivenResources(resPath, track, grow, &g_trackSelectGlobeMatrix, globeResource,
	                                         flagResource);
}

enum {
	SLIP_STARTUP_TIMED_CLOCK_HZ = 70,
	SLIP_STARTUP_TIMED_OUTPUT_COUNT = 2,
	/* Original timer output identity, retained for lookup alongside the host pointer. */
	SLIP_STARTUP_GLOBE_DISTANCE_DOS_IDENTITY = 0x56af0,
	SLIP_STARTUP_VERTEX_CAPACITY = 150,
	SLIP_STARTUP_MINIMUM_DEPTH = 12,
	SLIP_STARTUP_VIEWPORT_CENTRE_X = 260,
	SLIP_STARTUP_VIEWPORT_CENTRE_Y = 40,
	SLIP_STARTUP_LIGHT_DIAGONAL_Q14 = 0x24f3,
	SLIP_STARTUP_GLOBE_DISTANCE_START = 200,
	SLIP_STARTUP_GLOBE_DISTANCE_END = 16500,
	SLIP_STARTUP_GLOBE_DISTANCE_TICKS = 350,
	SLIP_STARTUP_GLOBE_MINIMUM_DISTANCE = 2200,
	SLIP_STARTUP_GLOBE_ROTATION_STEP = SLIP_ANGLE_QUARTER_TURN,
	SLIP_STARTUP_TEXTURE_SCROLL_STEP_Q14 = 0x5000,
	SLIP_STARTUP_CREDITS_DURATION_MS = 2000,
	SLIP_STARTUP_MILLISECONDS_PER_SECOND = 1000,
	SLIP_STARTUP_CREDITS_Y = 180,
	SLIP_STARTUP_VERSION_Y = 190
};

typedef struct SlipStartupIntroTimedValue {
	int32_t fadeValue;
} SlipStartupIntroTimedValue;

static SlipTimedValues startupTimedValues;
static int32_t *startupTimedValueOutputs[SLIP_STARTUP_TIMED_OUTPUT_COUNT];
static uint32_t startupTimedClockRate;

static bool TrackView_StartupRegisterTimedClock(void *context, uint32_t rate) {
	(void)context;
	startupTimedClockRate = rate;
	return true;
}

static void TrackView_StartupRemoveTimedClock(void *context) {
	(void)context;
	startupTimedClockRate = 0;
}

static const SlipTimedValueTimerCalls startupTimedValueTimer = {NULL, TrackView_StartupRegisterTimedClock,
                                                                TrackView_StartupRemoveTimedClock};

static const SlipInputCode g_startupIntroHiddenSequence[] = {
    SLIP_INPUT_SCAN_P, SLIP_INPUT_SCAN_A, SLIP_INPUT_SCAN_T, SLIP_INPUT_SCAN_P, SLIP_INPUT_SCAN_H,
    SLIP_INPUT_SCAN_E, SLIP_INPUT_SCAN_L, SLIP_INPUT_SCAN_A, SLIP_INPUT_SCAN_N, SLIP_INPUT_SCAN_NONE};

static const char g_startupIntroCredits[] =
    "Copyright (C) 1995, Gremlin Interactive Ltd\rCopyright (C) 1995, The Software Refinery Ltd\0"
    "Game Design\rThe Software Refinery Ltd & Gremlin Interactive Ltd\0"
    "Game Programmers\rCiaran Gultnieks   Ian Martin\0"
    "Game Artist\rMark Griffiths\0"
    "Track Design\rThe Software Refinery Ltd\0"
    "Sound Technology\rSound Operating System by Human Machine Interface\0"
    "Lipsynch\rWai-Ming Yuen\0"
    "Music\rNeil Biggin   Chris Adams\0"
    "Sound Effects\rPatrick Phelan\0"
    "Speech Production\rRob Rackstraw, Melissa Sindex, The Voice Box, ...\0"
    "Speech Production\rNeil Biggin, Patrick Phelan\0"
    "Intro\rSydney Franklin\0"
    "Scripts\rPaul Green\0"
    "Producer\rTony Casson\0"
    "Creative Manager\rPatrick Phelan\0"
    "Software Manager\rTim Heaton\0"
    "Product Director\rJames North-Hearn\0\0";

static const char g_startupIntroPhelanCredits[] =
    "Copyright (C) 1995, Gremlin Interactive Ltd\rCopyright (C) 1995, The Software Refinery Ltd\0"
    "Game Design\rPatrick Phelan\0"
    "Game Programmers\rPatrick Phelan and Pat Phelan\0"
    "Game Artist\rPatrick Phelan\0"
    "Track Design\rP. Phelan\0"
    "Sound Technology\rPatrick Phelan Technologies Inc\0"
    "Lipsynch\rPatrick Phelan\0"
    "Music\rPatrick Phelan\0"
    "Sound Effects\rPatrick Phelan\0"
    "Speech Production\rPatrick Phelan\0"
    "Speech Production\rPatrick Phelan\0"
    "Intro\rPatrick Phelan\0"
    "Scripts\rPatrick Phelan\0"
    "Producer\rPatrick Phelan\0"
    "Creative Manager\rPatrick Phelan\0"
    "Software Manager\rPatrick Phelan\0"
    "Product Director\rPatrick Phelan\0\0";

static void TrackView_StartupIntroStartTimedValue(SlipStartupIntroTimedValue *value, int32_t from, int32_t to,
                                                  uint32_t ticks, uint32_t mode) {
	SlipTimedValues_Start(&startupTimedValues, SLIP_STARTUP_GLOBE_DISTANCE_DOS_IDENTITY, &value->fadeValue, from, to,
	                      ticks, mode);
}

static void TrackView_StartupIntroTickTimedValue(SlipStartupIntroTimedValue *value) {
	(void)value;
	SlipTimedValues_Tick(&startupTimedValues);
}

static SlipFont TrackView_StartupIntroLockFont(void *context, uint16_t resource) {
	(void)SlipResourceHost_Lock(context, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	SlipFont font;
	if (!SlipFont_FromPayload(&payload, &font))
		SlipRuntime_Fatal("Invalid startup font resource");
	return font;
}

static const SlipFontResourceCalls TrackView_startupFontCalls = {
    .lock = TrackView_StartupIntroLockFont,
    .unlock = SlipResourceHost_Unlock,
};

static void TrackView_StartupIntroSprite(uint16_t resource, bool palette) {
	(void)SlipResourceHost_Lock(NULL, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	SlipSprite sprite;
	if (!SlipSprite_FromPayload(&payload, &sprite))
		SlipRuntime_Fatal("Invalid startup sprite resource");
	if (palette)
		SlipSprite_ApplyPalette(&sprite);
	else
		SlipSprite_DrawClipped(&sprite, g_framebuffer, SLIPSTREAM_SCREEN_WIDTH, sprite.x, sprite.y);
	SlipResourceHost_Unlock(NULL, resource);
}

static bool TrackView_StartupIntroHostRunning(const SlipStartupIntroHost *host) {
	return host->isRunning == NULL || host->isRunning(host->context);
}

static bool TrackView_StartupIntroLockSound(const SlipStartupIntroHost *host) {
	return host->lockSound == NULL || host->lockSound(host->context);
}

static void TrackView_StartupIntroUnlockSound(const SlipStartupIntroHost *host) {
	if (host->unlockSound != NULL) {
		host->unlockSound(host->context);
	}
}

static void TrackView_StartupIntroStopSound(const SlipStartupIntroHost *host, uint16_t soundResource,
                                            uint32_t soundHandle) {
	if (soundResource == 0) {
		return;
	}
	if (host->sound != NULL && TrackView_StartupIntroLockSound(host)) {
		SlipGameSound_Stop(host->sound, soundHandle);
		TrackView_StartupIntroUnlockSound(host);
	}
	SlipResourceHost_Unlock(NULL, soundResource);
	SlipResourceHost_Release(NULL, soundResource);
}

void TrackView_MaterialBytes(uint8_t *bytes, const SlipDraw3DMaterialTable *table) {
	TrackView_WriteLE32(bytes, table->count);
	for (uint32_t index = 0; index < table->count; ++index) {
		const SlipDraw3DMaterialRecord *const record = &table->records[index];
		uint8_t *const destination =
		    bytes + SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES + index * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE;
		memcpy(destination, record->name, sizeof(record->name));
		TrackView_WriteLE32(destination + offsetof(SlipDraw3DMaterialRecord, rampStart), record->rampStart);
		TrackView_WriteLE32(destination + offsetof(SlipDraw3DMaterialRecord, rampEnd), record->rampEnd);
		destination[offsetof(SlipDraw3DMaterialRecord, textureTransparency)] = (uint8_t)record->textureTransparency;
		destination[offsetof(SlipDraw3DMaterialRecord, textureTransparency) + 1] =
		    (uint8_t)((uint16_t)record->textureTransparency >> 8);
		destination[offsetof(SlipDraw3DMaterialRecord, skipFlatPolygon)] = (uint8_t)record->skipFlatPolygon;
		destination[offsetof(SlipDraw3DMaterialRecord, skipFlatPolygon) + 1] =
		    (uint8_t)((uint16_t)record->skipFlatPolygon >> 8);
		TrackView_WriteLE32(destination + offsetof(SlipDraw3DMaterialRecord, fixedShade), record->fixedShade);
		TrackView_WriteLE32(destination + offsetof(SlipDraw3DMaterialRecord, ambientCoefficient),
		                    record->ambientCoefficient);
		TrackView_WriteLE32(destination + offsetof(SlipDraw3DMaterialRecord, diffuseCoefficient),
		                    record->diffuseCoefficient);
		TrackView_WriteLE32(destination + offsetof(SlipDraw3DMaterialRecord, specularCoefficient),
		                    record->specularCoefficient);
		TrackView_WriteLE32(destination + offsetof(SlipDraw3DMaterialRecord, ditherBits), record->ditherBits);
		TrackView_WriteLE32(destination + offsetof(SlipDraw3DMaterialRecord, vertexShading), record->vertexShading);
		memcpy(destination + offsetof(SlipDraw3DMaterialRecord, textureName), record->textureName,
		       sizeof(record->textureName));
		for (unsigned frame = 0; frame < SLIP_DRAW3D_MATERIAL_FRAME_COUNT; ++frame)
			TrackView_WriteLE32(destination + offsetof(SlipDraw3DMaterialRecord, textureHandles) +
			                        frame * SLIP_DRAW3D_TEXTURE_HANDLE_BYTES,
			                    record->textureHandles[frame]);
		TrackView_WriteLE32(destination + offsetof(SlipDraw3DMaterialRecord, importedMaterialByte),
		                    record->importedMaterialByte);
	}
}

static bool TrackView_StartupIntroMaterials(uint8_t **table, size_t *bytes, uint16_t *resource,
                                            TrackViewResourceHandleRegistry *registry) {
	uint16_t sourceResource;
	if (!SlipResourceHost_Load(NULL, "SPDTEST.MAT", &sourceResource))
		return false;
	const uint8_t *const source = SlipResourceHost_Lock(NULL, sourceResource);
	SlipMaterial_Install(&SlipMaterialHost_install, source, &SlipMaterialHost_installCalls);
	SlipMaterial_MakeResident(&SlipMaterialHost_residency, &SlipMaterialHost_residencyCalls);
	SlipResourceHost_Unlock(NULL, sourceResource);
	SlipResourceHost_Release(NULL, sourceResource);
	*resource = SlipMaterialHost_residency.resource;
	SlipResourcePayload payload = SlipResourceHost_Payload(*resource);
	*table = payload.data;
	*bytes = payload.size;
	TrackView_MaterialBytes(*table, SlipMaterialHost_residency.table);
	memset(registry, 0, sizeof(*registry));
	registry->hostResources = true;
	return true;
}

static bool SlipStartupIntro_RunGlobeCredits(const char *resPath, const SlipStartupIntroHost *host,
                                             bool phelanCredits) {
	char secondaryPath[SLIP_TRACK_RESOURCE_SECONDARY_PATH_BYTES];
	const char *archives[2];
	size_t archiveCount;
	uint16_t fontResource = 0;
	uint16_t logoResource = 0;
	uint16_t globeResource = 0;
	uint16_t soundResource = 0;
	SlipView3DMaths maths = {0};
	SlipView3DMatrix globeMatrix = {{SLIP_Q14_ONE, 0, 0, 0, SLIP_Q14_ONE, 0, 0, 0, SLIP_Q14_ONE}};
	SlipDraw3DProjectState projectState;
	SlipDraw3DRefreshMode0Projection refresh;
	TrackViewResourceHandleRegistry resourceRegistry;
	SlipDraw3DRecordPool *drawRecordPool;
	SlipDraw3DRecordPoolInit drawRecordPoolInit;
	TrackViewRawBspContext trackContext;
	SlipDraw3DStateRecord *drawStateRecords;
	SlipStartupIntroTimedValue globeDistanceAnimation = {0};
	uint8_t *materialTable = NULL;
	size_t materialTableBytes = 0;
	uint16_t materialGlobal = 0;
	const char *credits = phelanCredits ? g_startupIntroPhelanCredits : g_startupIntroCredits;
	int32_t creditsTime = SLIP_STARTUP_CREDITS_DURATION_MS;
	uint32_t textureScroll = 0;
	uint32_t soundHandle = 0;
	uint32_t timedValueTicks = 0;
	bool ok = false;

	archiveCount = SlipMenu_BuildArchiveList(resPath, secondaryPath, archives);

	if (archiveCount != 0 && host->sound != NULL && host->sound->digitalCard != 0 && SlipConfig_SoundEffects() != 0 &&
	    SlipResourceHost_Load(NULL, "REFINERY.SMP", &soundResource)) {
		uint32_t soundBytes;
		(void)SlipResourceHost_Size(NULL, soundResource, &soundBytes);
		const uint8_t *const soundData = SlipResourceHost_Lock(NULL, soundResource);
		if (TrackView_StartupIntroLockSound(host)) {
			soundHandle = SlipGameSound_Play(host->sound, soundData, soundBytes);
			TrackView_StartupIntroUnlockSound(host);
		}
	}
	if (archiveCount == 0)
		return false; /* Invalid host archive binding. */

	(void)SlipTimedValues_Initialize(&startupTimedValues, startupTimedValueOutputs, SLIP_STARTUP_TIMED_CLOCK_HZ,
	                                 SLIP_STARTUP_TIMED_OUTPUT_COUNT, &startupTimedValueTimer);
	if (!SlipResourceHost_Load(NULL, "SHADE1.FNT", &fontResource))
		SlipGame_ResourceFailure();
	SlipText_SelectResourceFont(&SlipText_state, fontResource, &TrackView_startupFontCalls);
	SlipText_SetColor(&SlipText_state, UINT16_MAX);
	SlipText_SetStyle(&SlipText_state, SLIP_TEXT_CENTERED, UINT16_MAX, 0, SLIPSTREAM_SCREEN_WIDTH - 1);
	if (!SlipResourceHost_Load(NULL, "SOFTLOGO.SPR", &logoResource))
		SlipGame_ResourceFailure();
	if (!TrackView_LoadMaths(&maths, archives, archiveCount))
		SlipRuntime_Fatal("Invalid host startup maths view");
	TrackView_StartupIntroSprite(logoResource, true);
	SlipRenderer_Initialize(&SlipRendererHost_state, SLIP_STARTUP_VERTEX_CAPACITY, &SlipRendererHost_lifecycleCalls);
	SlipDraw3D_SetMinimumDepth(SLIP_STARTUP_MINIMUM_DEPTH);
	SlipDraw3D_SetMaximumDepth(INT32_MAX);
	SlipDraw3D_ResetLighting();
	SlipDraw3D_SetAmbientLight(SLIP_GLOBE_AMBIENT_LIGHT_Q14);
	SlipDraw3D_SetLightVector(SLIP_STARTUP_LIGHT_DIAGONAL_Q14, -SLIP_STARTUP_LIGHT_DIAGONAL_Q14,
	                          SLIP_STARTUP_LIGHT_DIAGONAL_Q14, SLIP_GLOBE_DIRECT_LIGHT_Q14);
	SlipDraw3D_SetDepthFade(0, 0, 0);
	SlipShape3D_Initialize();
	if (!TrackView_StartupIntroMaterials(&materialTable, &materialTableBytes, &materialGlobal, &resourceRegistry) ||
	    !SlipResourceHost_Load(NULL, "GLOBE.SHP", &globeResource)) {
		SlipGame_ResourceFailure();
	}
	drawRecordPool = SlipResourceStorage_RecordPool(SlipResource_handles[SlipRendererHost_state.polygonResource].block);
	if (!SlipDraw3D_InitRecordPool(drawRecordPool, &drawRecordPoolInit))
		SlipRuntime_Fatal("Invalid startup polygon workspace");
	SlipDraw3D_InitDefaultProjectState(&projectState);
	projectState.minZ = SLIP_STARTUP_MINIMUM_DEPTH;
	projectState.maxZ = INT32_MAX;
	SlipDraw3D_SetViewport(&projectState, 0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1,
	                       SLIP_STARTUP_VIEWPORT_CENTRE_X, SLIP_STARTUP_VIEWPORT_CENTRE_Y);
	projectState.renderFlags = SLIP_RENDER_ALTERNATE_TEXTURE_RASTER;
	if (!SlipDraw3D_RefreshProjectFrustum(&projectState, 0, &refresh)) {
		SlipRuntime_Fatal("Invalid host startup projection view");
	}
	drawStateRecords = SlipResourceStorage_DrawStateRecords(
	    SlipResource_handles[SlipRendererHost_state.stateResource].block, SlipRendererHost_state.stateCount);
	memset(&trackContext, 0, sizeof(trackContext));
	trackContext.materialTable = materialTable;
	trackContext.materialTableBytes = materialTableBytes;
	trackContext.materialGlobal = materialGlobal;
	trackContext.drawRecordPool = drawRecordPool;
	trackContext.hostRenderer = &SlipRendererHost_state;
	trackContext.hostShapeResource = globeResource;
	trackContext.projectState = &projectState;
	trackContext.drawStateRecords = drawStateRecords;
	trackContext.drawStateRecordCount = SlipRendererHost_state.stateCount;
	trackContext.drawStateRecord = drawStateRecords;
	trackContext.vertexBufferBase = SlipRendererHost_state.vertexBase;
	trackContext.vertexBufferRecordCapacity = SlipRendererHost_state.vertexCapacity;
	trackContext.vertexBufferCursor = 0;
	trackContext.vertexBufferLimit = SlipRendererHost_state.vertexCapacity * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
	trackContext.resourceRegistry = &resourceRegistry;
	trackContext.frustum = (SlipTrackWorldProjectFrustum){
	    refresh.maxXStep,        refresh.minXStep,       refresh.minYStep,           refresh.maxYStep,
	    refresh.minXPlaneDepthQ, refresh.minXPlaneNegXQ, refresh.maxXPlaneNegDepthQ, refresh.maxXPlaneXQ,
	    refresh.maxYPlaneDepthQ, refresh.maxYPlaneYQ,    refresh.minYPlaneNegDepthQ, refresh.minYPlaneNegYQ,
	    projectState.minZ,       projectState.maxZ};
	trackContext.rendererFlags = SLIP_RENDER_ALTERNATE_TEXTURE_RASTER;
	trackContext.directLight = SLIP_GLOBE_DIRECT_LIGHT_Q14;
	trackContext.lightInput = (SlipView3DVec32){SLIP_STARTUP_LIGHT_DIAGONAL_Q14, -SLIP_STARTUP_LIGHT_DIAGONAL_Q14,
	                                            SLIP_STARTUP_LIGHT_DIAGONAL_Q14};
	trackContext.ambientLight = SLIP_GLOBE_AMBIENT_LIGHT_Q14;
	TrackView_StartupIntroStartTimedValue(&globeDistanceAnimation, SLIP_STARTUP_GLOBE_DISTANCE_START,
	                                      SLIP_STARTUP_GLOBE_DISTANCE_END, SLIP_STARTUP_GLOBE_DISTANCE_TICKS, 0);
	SlipFrameTimer_Reset();

	while (TrackView_StartupIntroHostRunning(host)) {
		SlipFrameTimerValues timerValues;
		uint16_t globeAngle;
		uint32_t globeDistance;
		SlipView3DVec32 translation;
		SlipView3DVec32 bspOrigin;
		SlipView3DVec32 lightVector;

		host->pollEvents(host->context);
		if (!TrackView_StartupIntroHostRunning(host)) {
			break;
		}
		SlipFrameTimer_Update((uint32_t)SlipSdl_TicksMs());
		timerValues = SlipFrameTimer_Values();
		timedValueTicks += timerValues.deltaMilliseconds * startupTimedClockRate;
		while (timedValueTicks >= SLIP_STARTUP_MILLISECONDS_PER_SECOND) {
			TrackView_StartupIntroTickTimedValue(&globeDistanceAnimation);
			timedValueTicks -= SLIP_STARTUP_MILLISECONDS_PER_SECOND;
		}
		globeAngle = (uint16_t)(((uint32_t)SLIP_STARTUP_GLOBE_ROTATION_STEP * (uint16_t)timerValues.stepQ14) >>
		                        SLIP_Q14_FRACTION_BITS);
		SlipView3D_ApplyColumn0Column2Rotation(&maths, (int16_t)globeAngle, &globeMatrix);
		SlipView3D_ApplyPitchMatrix(&maths, (int16_t)globeAngle, &globeMatrix);
		SlipView3D_OrthonormalizeForwardBasis(&globeMatrix);
		TrackView_StartupIntroSprite(logoResource, false);
		textureScroll += (uint32_t)(((uint64_t)SLIP_STARTUP_TEXTURE_SCROLL_STEP_Q14 * timerValues.stepQ14) >>
		                            SLIP_Q14_FRACTION_BITS);
		textureScroll &= SLIP_TRACK_TEXTURE_SCROLL_FRACTION_MASK;
		creditsTime -= (int32_t)timerValues.deltaMilliseconds;
		if (creditsTime < 0) {
			creditsTime = SLIP_STARTUP_CREDITS_DURATION_MS;
			while (*credits != '\0') {
				++credits;
			}
			++credits;
			if (*credits == '\0') {
				ok = true;
				break;
			}
		}
		trackContext.textureScrollPhase = textureScroll;
		globeDistance = (uint32_t)globeDistanceAnimation.fadeValue;
		if ((int32_t)globeDistance < SLIP_STARTUP_GLOBE_MINIMUM_DISTANCE) {
			globeDistance = SLIP_STARTUP_GLOBE_MINIMUM_DISTANCE;
		}
		translation = (SlipView3DVec32){0, 0, (int32_t)globeDistance};
		bspOrigin = SlipView3D_TransformPositionByRows(&globeMatrix, (SlipView3DVec32){0, 0, -(int32_t)globeDistance});
		lightVector = SlipView3D_TransformVector(&globeMatrix, (SlipView3DVec32){SLIP_STARTUP_LIGHT_DIAGONAL_Q14,
		                                                                         -SLIP_STARTUP_LIGHT_DIAGONAL_Q14,
		                                                                         SLIP_STARTUP_LIGHT_DIAGONAL_Q14});
		(void)TrackView_DrawVehicleViewShape(NULL, archives, archiveCount, "GLOBE.SHP", &globeMatrix, translation,
		                                     bspOrigin, lightVector, &projectState, NULL, 0, NULL,
		                                     SLIP_GLOBE_AMBIENT_LIGHT_Q14, SLIP_GLOBE_DIRECT_LIGHT_Q14, &trackContext);
		SlipTextPosition creditsPosition = {0, SLIP_STARTUP_CREDITS_Y};
		SlipText_Draw(&SlipText_state, credits, NULL, &creditsPosition);
		host->presentFrame(host->context);
		if (host->testAndClearInput(host->context, SLIP_INPUT_SCAN_ENTER) ||
		    host->testAndClearInput(host->context, SLIP_INPUT_MOUSE_LEFT)) {
			ok = true;
			break;
		}
	}

	if (logoResource != 0)
		SlipResourceHost_Release(NULL, logoResource);
	if (globeResource != 0)
		SlipResourceHost_Release(NULL, globeResource);
	if (fontResource != 0)
		SlipResourceHost_Release(NULL, fontResource);
	SlipShape3D_Shutdown();
	SlipRenderer_Shutdown(&SlipRendererHost_state, &SlipRendererHost_lifecycleCalls);
	SlipTimedValues_Shutdown(&startupTimedValues, &startupTimedValueTimer);
	TrackView_StartupIntroStopSound(host, soundResource, soundHandle);
	return ok || !TrackView_StartupIntroHostRunning(host);
}

bool SlipStartupIntro_Run(const char *resPath, const SlipStartupIntroHost *host) {
	char secondaryPath[SLIP_TRACK_RESOURCE_SECONDARY_PATH_BYTES];
	const char *archives[2];
	size_t archiveCount;
	uint16_t logoResource = 0;
	const SlipInputCode *hiddenSequenceCursor = g_startupIntroHiddenSequence;
	bool phelanCredits = false;
	bool ok = false;

	if (resPath == NULL || host == NULL || host->pollEvents == NULL || host->inputHeld == NULL ||
	    host->popInput == NULL || host->testAndClearInput == NULL || host->presentFrame == NULL) {
		return false;
	}
	archiveCount = SlipMenu_BuildArchiveList(resPath, secondaryPath, archives);

	if (archiveCount == 0)
		return false;
	SlipText_SelectResourceFont(&SlipText_state, SlipMenu_resources.smallFont, &TrackView_startupFontCalls);
	SlipText_SetColor(&SlipText_state, UINT8_MAX);
	SlipText_SetStyle(&SlipText_state, SLIP_TEXT_CENTERED, UINT16_MAX, 0, SLIPSTREAM_SCREEN_WIDTH - 1);
	if (!SlipResourceHost_Load(NULL, "GREMLOGO.SPR", &logoResource))
		SlipGame_ResourceFailure();
	Raster_SetScreenBufferRows(g_framebuffer, SLIPSTREAM_SCREEN_WIDTH);
	Raster_SetClipRect(0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1);
	TrackView_StartupIntroSprite(logoResource, true);
	SlipFrameTimer_Reset();
	while (TrackView_StartupIntroHostRunning(host)) {
		SlipInputCode inputEventCode;

		host->pollEvents(host->context);
		if (!TrackView_StartupIntroHostRunning(host)) {
			break;
		}
		SlipFrameTimer_Update((uint32_t)SlipSdl_TicksMs());
		TrackView_StartupIntroSprite(logoResource, false);
		if (host->inputHeld(host->context, SLIP_INPUT_SCAN_V)) {
			SlipTextPosition versionPosition = {0, SLIP_STARTUP_VERSION_Y};
			SlipText_Draw(&SlipText_state, "Version ID: 24/04/95@14:36:40/CD", NULL, &versionPosition);
		}
		host->presentFrame(host->context);
		inputEventCode = host->popInput(host->context);
		if (inputEventCode != SLIP_INPUT_SCAN_NONE) {
			if (*hiddenSequenceCursor == inputEventCode) {
				++hiddenSequenceCursor;
				if (*hiddenSequenceCursor == SLIP_INPUT_SCAN_NONE) {
					phelanCredits = !phelanCredits;
					hiddenSequenceCursor = g_startupIntroHiddenSequence;
				}
			} else {
				hiddenSequenceCursor = g_startupIntroHiddenSequence;
			}
		}
		if (inputEventCode == SLIP_INPUT_SCAN_ENTER || inputEventCode == SLIP_INPUT_MOUSE_LEFT) {
			SlipResourceHost_Release(NULL, logoResource);
			logoResource = 0;
			ok = SlipStartupIntro_RunGlobeCredits(resPath, host, phelanCredits);
			break;
		}
	}

	if (logoResource != 0)
		SlipResourceHost_Release(NULL, logoResource);
	Raster_Clear(0, sizeof(g_framebuffer));
	host->presentFrame(host->context);
	return ok || !TrackView_StartupIntroHostRunning(host);
}
