#include "race_records_host.h"
#include "config_menu_draw.h"
#include "config_settings.h"
#include "fixed_point.h"
#include "game_errors.h"
#include "material_host.h"
#include "race_hud.h"
#include "race_records_draw.h"
#include "raster/raster.h"
#include "renderer_host.h"
#include "renderer_projection.h"
#include "renderer_state.h"
#include "resource_host.h"
#include "shape3d.h"
#include "shape_host.h"
#include "shape_vertices.h"

SlipLapRecordsScreen SlipLapRecordsHost_screen = {
    .animation = {.matrix = {{SLIP_Q14_ONE, 0, 0, 0, SLIP_Q14_ONE, 0, 0, 0, SLIP_Q14_ONE}}}};
static const SlipView3DMatrix cameraMatrix = {{SLIP_Q14_ONE, 0, 0, 0, SLIP_Q14_ONE, 0, 0, 0, SLIP_Q14_ONE}};

static void SlipLapRecordsHost_RowViewport(void *context, int32_t left, int32_t top, int32_t right, int32_t bottom,
                                           int32_t centerX, int32_t centerY) {
	(void)context;
	SlipDraw3D_SetViewport(&SlipRendererHost_state.projection, left, top, right, bottom, centerX, centerY);
}

static void SlipLapRecordsHost_RowPosition(void *context, SlipView3DVec32 view, SlipView3DVec32 world) {
	(void)context;
	SlipShapeVertices_Setup(&SlipShapeHost_state, view, world);
}

static void SlipLapRecordsHost_RowShape(void *context, uint16_t resource, const SlipView3DMatrix *world,
                                        const SlipView3DMatrix *draw) {
	(void)context;
	SlipShape_Draw(&SlipShapeHost_state, resource, world, draw, &SlipShapeHost_drawCalls);
}

const SlipLapRecordShapeCalls SlipLapRecordsHost_shapeCalls = {.viewport = SlipLapRecordsHost_RowViewport,
                                                               .position = SlipLapRecordsHost_RowPosition,
                                                               .drawShape = SlipLapRecordsHost_RowShape};

static void SlipLapRecordsHost_Language(void *context) {
	(void)context;
	SlipStringTable_SetLanguage(&SlipStringTable_state, (uint8_t)SlipConfig_Language());
}

static void SlipLapRecordsHost_Renderer(void *context, uint32_t vertices, uint32_t flags) {
	(void)context;
	(void)flags;
	SlipRenderer_Initialize(&SlipRendererHost_state, (uint16_t)vertices, &SlipRendererHost_lifecycleCalls);
}

static void SlipLapRecordsHost_MinimumDepth(void *context, uint32_t value) {
	(void)context;
	SlipDraw3D_SetMinimumDepth(value);
	SlipRendererHost_state.projection.minZ = (int32_t)value;
}

static void SlipLapRecordsHost_MaximumDepth(void *context, uint32_t value) {
	(void)context;
	SlipDraw3D_SetMaximumDepth(value);
	SlipRendererHost_state.projection.maxZ = (int32_t)value;
}

static void SlipLapRecordsHost_ResetLighting(void *context) {
	(void)context;
	SlipDraw3D_ResetLighting();
}

static void SlipLapRecordsHost_Ambient(void *context, uint16_t value) {
	(void)context;
	SlipDraw3D_SetAmbientLight(value);
}

static void SlipLapRecordsHost_Light(void *context, int16_t x, int16_t y, int16_t z, uint16_t strength) {
	(void)context;
	SlipDraw3D_SetLightVector(x, y, z, strength);
}

static void SlipLapRecordsHost_DepthFade(void *context, uint32_t enabled) {
	(void)context;

	SlipDraw3D_SetDepthFade(enabled, 0, 0);
}

static void SlipLapRecordsHost_Flags(void *context, uint32_t value) {
	(void)context;
	SlipRenderer_SetFlags(&SlipRendererHost_state, (uint16_t)value);
}

static void SlipLapRecordsHost_Camera(void *context, SlipView3DVec32 position, const SlipView3DMatrix *matrix) {
	(void)context;
	SlipRenderer_SetCamera(&SlipRendererHost_state, position, matrix);
}

static void SlipLapRecordsHost_Shapes(void *context) {
	(void)context;
	SlipShape3D_Initialize();
}

static void SlipLapRecordsHost_Materials(void *context, const uint8_t *asset) {
	(void)context;
	SlipMaterial_Install(&SlipMaterialHost_install, asset, &SlipMaterialHost_installCalls);
}

static void SlipLapRecordsHost_Palette(void *context, uint16_t resource) {
	SlipResourceHost_Lock(context, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	SlipSprite sprite;
	if (!SlipSprite_FromPayload(&payload, &sprite))
		SlipGame_ResourceFailure();
	SlipSprite_ApplyPalette(&sprite);
	SlipResourceHost_Unlock(context, resource);
}

static void SlipLapRecordsHost_CloseShapes(void *context) {
	(void)context;
	SlipShape3D_Shutdown();
}

static void SlipLapRecordsHost_CloseRenderer(void *context) {
	(void)context;
	SlipRenderer_Shutdown(&SlipRendererHost_state, &SlipRendererHost_lifecycleCalls);
}

static void SlipLapRecordsHost_ResourceFailure(void *context) {
	(void)context;
	SlipGame_ResourceFailure();
}

const SlipLapRecordsScreenCalls SlipLapRecordsHost_lifecycle = {.resources = {.load = SlipResourceHost_Load,
                                                                              .lock = SlipResourceHost_Lock,
                                                                              .unlock = SlipResourceHost_Unlock,
                                                                              .release = SlipResourceHost_Release},
                                                                .language = SlipLapRecordsHost_Language,
                                                                .renderer = SlipLapRecordsHost_Renderer,
                                                                .minimumDepth = SlipLapRecordsHost_MinimumDepth,
                                                                .maximumDepth = SlipLapRecordsHost_MaximumDepth,
                                                                .resetLighting = SlipLapRecordsHost_ResetLighting,
                                                                .ambient = SlipLapRecordsHost_Ambient,
                                                                .light = SlipLapRecordsHost_Light,
                                                                .depthFade = SlipLapRecordsHost_DepthFade,
                                                                .rendererFlags = SlipLapRecordsHost_Flags,
                                                                .camera = SlipLapRecordsHost_Camera,
                                                                .cameraMatrix = &cameraMatrix,
                                                                .shapes = SlipLapRecordsHost_Shapes,
                                                                .materials = SlipLapRecordsHost_Materials,
                                                                .palette = SlipLapRecordsHost_Palette,
                                                                .sequence = SlipResourceHost_LoadSequence,
                                                                .allocate = SlipResourceHost_Allocate,
                                                                .lockSprite = SlipResourceHost_LockGeneratedSprite,
                                                                .releaseSequence = SlipResourceHost_ReleaseSequence,
                                                                .closeShapes = SlipLapRecordsHost_CloseShapes,
                                                                .closeRenderer = SlipLapRecordsHost_CloseRenderer,
                                                                .resourceFailure = SlipLapRecordsHost_ResourceFailure};

static SlipFont SlipLapRecordsHost_LockFont(void *context, uint16_t resource) {
	SlipResourceHost_Lock(context, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	SlipFont font;
	if (!SlipFont_FromPayload(&payload, &font))
		SlipGame_ResourceFailure();
	return font;
}

static const SlipFontResourceCalls recordFontCalls = {.lock = SlipLapRecordsHost_LockFont,
                                                      .unlock = SlipResourceHost_Unlock};

static SlipSprite SlipLapRecordsHost_LockSprite(void *context, uint16_t resource) {
	SlipResourceHost_Lock(context, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	SlipSprite sprite;
	if (!SlipSprite_FromPayload(&payload, &sprite))
		SlipGame_ResourceFailure();
	return sprite;
}

void SlipLapRecordsHost_DrawRow(void *context, SlipLapRecordsScreen *screen, uint16_t resource, SlipLapRecord *record) {
	SlipSprite *row = SlipResourceHost_LockGeneratedSprite(context, resource);
	uint16_t width = row->width, height = row->height;
	SlipResourceHost_Unlock(context, resource);
	row = SlipResourceHost_LockGeneratedSprite(context, resource);
	RasterSurfaceBinding saved;
	Raster_BindSprite((uint8_t *)row->pixels, width, height, &saved);
	row = SlipResourceHost_LockGeneratedSprite(context, resource);
	int16_t x = row->x, y = row->y;
	SlipResourceHost_Unlock(context, resource);
	SlipSprite sprite = SlipLapRecordsHost_LockSprite(context, screen->inactive);
	SlipSprite_DrawClipped(&sprite, g_screenBufferBase, g_screenPitch, (int16_t)(0u - (uint16_t)x),
	                       (int16_t)(0u - (uint16_t)y));
	SlipResourceHost_Unlock(context, screen->inactive);
	const uint16_t shape = screen->shapes[record->driverIndex];
	sprite = SlipLapRecordsHost_LockSprite(context, screen->portraits[record->driverIndex]);
	SlipSprite_Draw(&sprite, g_screenBufferBase, g_screenPitch, 1, 1);
	SlipResourceHost_Unlock(context, screen->portraits[record->driverIndex]);
	sprite = SlipLapRecordsHost_LockSprite(context, screen->shapeBackground);
	SlipSprite_Draw(&sprite, g_screenBufferBase, g_screenPitch, SLIP_LAP_RECORDS_ROW_SHAPE_LEFT,
	                SLIP_LAP_RECORDS_ROW_SHAPE_TOP);
	SlipResourceHost_Unlock(context, screen->shapeBackground);
	SlipText_SelectResourceFont(&SlipText_state, screen->rowFont, &recordFontCalls);
	SlipText_SetStyle(&SlipText_state, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_LAP_RECORDS_ROW_TEXT_LEFT,
	                  SLIP_LAP_RECORDS_ROW_TEXT_RIGHT);
	SlipText_SetColor(&SlipText_state, SLIP_LAP_RECORDS_ROW_TEXT_COLOUR);
	SlipTextPosition position = {0, SLIP_LAP_RECORDS_ROW_NAME_Y};
	SlipText_Draw(&SlipText_state, record->name, NULL, &position);
	if (screen->input.cursorVisible != 0 && record->editing != 0) {

		const char character = record->name[screen->input.cursor];
		record->name[screen->input.cursor] = '\0';
		const uint16_t prefixWidth = SlipFont_MeasureResource(screen->rowFont, record->name, &recordFontCalls);
		record->name[screen->input.cursor] = character;
		const int16_t wholeWidth = (int16_t)SlipFont_MeasureResource(screen->rowFont, record->name, &recordFontCalls);
		const int16_t cursorX = (int16_t)(uint16_t)(prefixWidth + SLIP_LAP_RECORDS_ROW_TEXT_CENTER - (wholeWidth >> 1));
		Raster_DrawLineClipped(SLIP_LAP_RECORDS_ROW_TEXT_COLOUR, cursorX, position.y, cursorX,
		                       (int16_t)(position.y + SLIP_LAP_RECORDS_ROW_CURSOR_HEIGHT));
	}
	char time[SLIP_RACE_TIME_TEXT_BYTES];
	SlipRaceHud_FormatTime(record->lapTime, time);
	time[SLIP_RACE_TIME_MINUTES_SEPARATOR] = '\'';
	time[SLIP_RACE_TIME_SECONDS_SEPARATOR] = '"';
	position = (SlipTextPosition){0, SLIP_LAP_RECORDS_ROW_TIME_Y};
	SlipText_Draw(&SlipText_state, time + SLIP_RACE_TIME_MINUTES_OFFSET, NULL, &position);
	SlipLapRecordsHost_RowViewport(context, SLIP_LAP_RECORDS_ROW_SHAPE_LEFT, SLIP_LAP_RECORDS_ROW_SHAPE_TOP,
	                               SLIP_LAP_RECORDS_ROW_SHAPE_RIGHT, SLIP_LAP_RECORDS_ROW_SHAPE_BOTTOM,
	                               SLIP_LAP_RECORDS_ROW_SHAPE_CENTER_X, SLIP_LAP_RECORDS_ROW_SHAPE_CENTER_Y);
	SlipView3DVec32 position3D = {0, 0, SLIP_LAP_RECORDS_ROW_SHAPE_DEPTH};
	SlipLapRecordsHost_RowPosition(context, position3D, position3D);
	SlipLapRecordsHost_RowShape(context, shape, &screen->animation.matrix, &screen->animation.matrix);
	SlipResourceHost_Unlock(context, resource);
	Raster_RestoreScreen(&saved);
}

static void SlipLapRecordsHost_FrameSprite(void *context, uint16_t resource, int16_t x, int16_t y) {
	SlipSprite sprite = SlipLapRecordsHost_LockSprite(context, resource);
	SlipSprite_Draw(&sprite, g_screenBufferBase, g_screenPitch, x, y);
	SlipResourceHost_Unlock(context, resource);
}

static void SlipLapRecordsHost_FrameFont(void *context, uint16_t resource) {
	(void)context;
	SlipText_SelectResourceFont(&SlipText_state, resource, &recordFontCalls);
}

static void SlipLapRecordsHost_FrameColor(void *context, uint16_t color) {
	(void)context;
	SlipText_SetColor(&SlipText_state, color);
}

static void SlipLapRecordsHost_PanelClip(void *context, SlipConfigMenuRectangle bounds) {
	(void)context;
	Raster_SetClipRect(bounds.left, bounds.top, bounds.right, bounds.bottom);
}

static void SlipLapRecordsHost_PanelLine(void *context, uint16_t color, int16_t left, int16_t top, int16_t right,
                                         int16_t bottom) {
	(void)context;
	Raster_DrawLineSolid(color, left, top, right, bottom);
}

static void SlipLapRecordsHost_PanelSprite(void *context, uint16_t resource, int16_t x, int16_t y) {
	SlipSprite sprite = SlipLapRecordsHost_LockSprite(context, resource);
	SlipSprite_DrawClipped(&sprite, g_screenBufferBase, g_screenPitch, x, y);
	SlipResourceHost_Unlock(context, resource);
}

static void SlipLapRecordsHost_PanelStyle(void *context, uint16_t mode, uint16_t spacing, int16_t left, int16_t right) {
	(void)context;
	SlipText_SetStyle(&SlipText_state, mode, spacing, left, right);
}

static const char *SlipLapRecordsHost_PanelString(void *context, SlipStringTableSlot *slot, uint32_t tag) {
	(void)context;
	return SlipStringTable_Get(slot, tag, &SlipLapRecordsHost_lifecycle.resources);
}

static void SlipLapRecordsHost_PanelUnlock(void *context, SlipStringTableSlot *slot) {
	(void)context;
	SlipStringTable_Unlock(slot, &SlipLapRecordsHost_lifecycle.resources);
}

static void SlipLapRecordsHost_PanelText(void *context, const char *text, const SlipTextArgument *arguments,
                                         SlipTextPosition position) {
	(void)context;
	SlipText_Draw(&SlipText_state, text, arguments, &position);
}

static void SlipLapRecordsHost_FramePanel(void *context, SlipInputRectangle rectangle, uint16_t resource,
                                          SlipStringTableSlot *strings, uint32_t tag) {
	const SlipConfigMenuDrawCalls calls = {.context = context,
	                                       .clip = SlipLapRecordsHost_PanelClip,
	                                       .line = SlipLapRecordsHost_PanelLine,
	                                       .clippedSprite = SlipLapRecordsHost_PanelSprite,
	                                       .style = SlipLapRecordsHost_PanelStyle,
	                                       .string = SlipLapRecordsHost_PanelString,
	                                       .unlockStrings = SlipLapRecordsHost_PanelUnlock,
	                                       .text = SlipLapRecordsHost_PanelText};
	SlipMenu_DrawPanel(rectangle, resource, strings, tag, &calls);
}

static void SlipLapRecordsHost_FrameDissolve(void *context, uint16_t resource, int16_t x, uint16_t level) {

	SlipSprite *const sprite = SlipResourceHost_LockGeneratedSprite(context, resource);
	SlipSprite_DrawDissolve(sprite, g_screenBufferBase, g_screenPitch, x, sprite->y, level);
	SlipResourceHost_Unlock(context, resource);
}

void SlipLapRecordsHost_BindDrawing(SlipLapRecordsFrameCalls *calls) {
	calls->drawSprite = SlipLapRecordsHost_FrameSprite;
	calls->font = SlipLapRecordsHost_FrameFont;
	calls->textColorOrMode = SlipLapRecordsHost_FrameColor;
	calls->panel = SlipLapRecordsHost_FramePanel;
	calls->row = SlipLapRecordsHost_DrawRow;
	calls->dissolve = SlipLapRecordsHost_FrameDissolve;
}
