#include "vga_dac.h"

#include <stddef.h>
#include <string.h>

/* Six RGB transitions around each BIOS colour ring. */
enum {
	SLIP_VGA_RING_BLUE_TO_MAGENTA,
	SLIP_VGA_RING_MAGENTA_TO_RED,
	SLIP_VGA_RING_RED_TO_YELLOW,
	SLIP_VGA_RING_YELLOW_TO_GREEN,
	SLIP_VGA_RING_GREEN_TO_CYAN,
	SLIP_VGA_RING_CYAN_TO_BLUE,
	SLIP_VGA_RING_SIDE_COUNT
};

enum {
	SLIP_VGA_BIOS_BASE_COLOUR_COUNT = 32,
	SLIP_VGA_BIOS_RESERVED_COLOUR_COUNT = 8,
	SLIP_VGA_BIOS_RESERVED_COLOUR_START = SLIP_VGA_DAC_PALETTE_COUNT - SLIP_VGA_BIOS_RESERVED_COLOUR_COUNT,
	SLIP_VGA_BIOS_COLOUR_RING_COUNT = 9,
	SLIP_VGA_BIOS_RING_RAMP_LENGTH = 5,
	SLIP_VGA_BIOS_RING_SIDE_STEPS = SLIP_VGA_BIOS_RING_RAMP_LENGTH - 1,
	SLIP_VGA_BIOS_RING_COLOUR_COUNT = SLIP_VGA_RING_SIDE_COUNT * SLIP_VGA_BIOS_RING_SIDE_STEPS
};

static const uint32_t SLIP_VGA_ARGB_OPAQUE_ALPHA = UINT32_C(0xff000000);

uint8_t g_vgaDacPalette[SLIP_VGA_DAC_PALETTE_COUNT][SLIP_VGA_DAC_CHANNEL_COUNT];
static uint8_t g_vgaDacPendingPalette[SLIP_VGA_DAC_PALETTE_COUNT][SLIP_VGA_DAC_CHANNEL_COUNT];
static uint8_t g_vgaDacDirtyEntries[SLIP_VGA_DAC_PALETTE_COUNT];

static uint8_t SlipVgaDac_ExpandDacChannel(uint8_t channel) {
	return (uint8_t)((channel << (SLIP_VGA_OUTPUT_CHANNEL_BITS - SLIP_VGA_DAC_CHANNEL_BITS)) |
	                 (channel >> (2 * SLIP_VGA_DAC_CHANNEL_BITS - SLIP_VGA_OUTPUT_CHANNEL_BITS)));
}

void SlipVgaDac_InitializeHostBiosDefaults(void) {

	static const uint8_t biosReservedColors[SLIP_VGA_BIOS_RESERVED_COLOUR_COUNT][SLIP_VGA_DAC_CHANNEL_COUNT] = {
	    {0x00, 0x00, 0x00}, {0x00, 0x00, 0x00}, {0x00, 0x00, 0x00}, {0x20, 0x20, 0x20},
	    {0x00, 0x3f, 0x00}, {0x3f, 0x00, 0x00}, {0x3f, 0x3f, 0x00}, {0x3f, 0x3f, 0x3f},
	};
	static const uint8_t biosBaseColors[SLIP_VGA_BIOS_BASE_COLOUR_COUNT][SLIP_VGA_DAC_CHANNEL_COUNT] = {
	    {0x00, 0x00, 0x00}, {0x00, 0x00, 0x2a}, {0x00, 0x2a, 0x00}, {0x00, 0x2a, 0x2a}, {0x2a, 0x00, 0x00},
	    {0x2a, 0x00, 0x2a}, {0x2a, 0x15, 0x00}, {0x2a, 0x2a, 0x2a}, {0x15, 0x15, 0x15}, {0x15, 0x15, 0x3f},
	    {0x15, 0x3f, 0x15}, {0x15, 0x3f, 0x3f}, {0x3f, 0x15, 0x15}, {0x3f, 0x15, 0x3f}, {0x3f, 0x3f, 0x15},
	    {0x3f, 0x3f, 0x3f}, {0x00, 0x00, 0x00}, {0x05, 0x05, 0x05}, {0x08, 0x08, 0x08}, {0x0b, 0x0b, 0x0b},
	    {0x0e, 0x0e, 0x0e}, {0x11, 0x11, 0x11}, {0x14, 0x14, 0x14}, {0x18, 0x18, 0x18}, {0x1c, 0x1c, 0x1c},
	    {0x20, 0x20, 0x20}, {0x24, 0x24, 0x24}, {0x28, 0x28, 0x28}, {0x2d, 0x2d, 0x2d}, {0x32, 0x32, 0x32},
	    {0x38, 0x38, 0x38}, {0x3f, 0x3f, 0x3f},
	};
	static const uint8_t ringRamps[SLIP_VGA_BIOS_COLOUR_RING_COUNT][SLIP_VGA_BIOS_RING_RAMP_LENGTH] = {
	    {0x00, 0x10, 0x1f, 0x2f, 0x3f}, {0x1f, 0x27, 0x2f, 0x37, 0x3f}, {0x2d, 0x31, 0x36, 0x3a, 0x3f},
	    {0x00, 0x07, 0x0e, 0x15, 0x1c}, {0x0e, 0x11, 0x15, 0x18, 0x1c}, {0x14, 0x16, 0x18, 0x1a, 0x1c},
	    {0x00, 0x04, 0x08, 0x0c, 0x10}, {0x08, 0x0a, 0x0c, 0x0e, 0x10}, {0x0b, 0x0c, 0x0d, 0x0f, 0x10},
	};
	int ring;
	int step;

	memset(g_vgaDacPalette, 0, sizeof(g_vgaDacPalette));
	memcpy(g_vgaDacPalette, biosBaseColors, sizeof(biosBaseColors));
	for (ring = 0; ring < SLIP_VGA_BIOS_COLOUR_RING_COUNT; ++ring) {
		const uint8_t *const ramp = ringRamps[ring];
		const uint8_t low = ramp[0];
		const uint8_t high = ramp[SLIP_VGA_BIOS_RING_SIDE_STEPS];

		for (step = 0; step < SLIP_VGA_BIOS_RING_COLOUR_COUNT; ++step) {
			const int side = step / SLIP_VGA_BIOS_RING_SIDE_STEPS;
			const int offset = step % SLIP_VGA_BIOS_RING_SIDE_STEPS;
			const uint8_t rising = ramp[offset];
			const uint8_t falling = ramp[SLIP_VGA_BIOS_RING_SIDE_STEPS - offset];
			uint8_t *const color =
			    g_vgaDacPalette[SLIP_VGA_BIOS_BASE_COLOUR_COUNT + ring * SLIP_VGA_BIOS_RING_COLOUR_COUNT + step];

			switch (side) {
			case SLIP_VGA_RING_BLUE_TO_MAGENTA:
				color[0] = rising;
				color[1] = low;
				color[2] = high;
				break;
			case SLIP_VGA_RING_MAGENTA_TO_RED:
				color[0] = high;
				color[1] = low;
				color[2] = falling;
				break;
			case SLIP_VGA_RING_RED_TO_YELLOW:
				color[0] = high;
				color[1] = rising;
				color[2] = low;
				break;
			case SLIP_VGA_RING_YELLOW_TO_GREEN:
				color[0] = falling;
				color[1] = high;
				color[2] = low;
				break;
			case SLIP_VGA_RING_GREEN_TO_CYAN:
				color[0] = low;
				color[1] = high;
				color[2] = rising;
				break;
			default: /* SLIP_VGA_RING_CYAN_TO_BLUE */
				color[0] = low;
				color[1] = falling;
				color[2] = high;
				break;
			}
		}
	}
	memcpy(g_vgaDacPalette + SLIP_VGA_BIOS_RESERVED_COLOUR_START, biosReservedColors, sizeof(biosReservedColors));
	memcpy(g_vgaDacPendingPalette, g_vgaDacPalette, sizeof(g_vgaDacPalette));
	memset(g_vgaDacDirtyEntries, 0, sizeof(g_vgaDacDirtyEntries));
}

void SlipVgaDac_WriteRange(uint16_t firstIndex, uint16_t count, const uint8_t *rgb) {
	uint16_t colorIndex;

	if (rgb == NULL || firstIndex >= SLIP_VGA_DAC_PALETTE_COUNT || count > SLIP_VGA_DAC_PALETTE_COUNT - firstIndex) {
		return;
	}
	for (colorIndex = 0; colorIndex < count; ++colorIndex) {
		g_vgaDacPendingPalette[firstIndex + colorIndex][0] =
		    rgb[(size_t)colorIndex * SLIP_VGA_DAC_CHANNEL_COUNT + 0u] & SLIP_VGA_DAC_CHANNEL_MASK;
		g_vgaDacPendingPalette[firstIndex + colorIndex][1] =
		    rgb[(size_t)colorIndex * SLIP_VGA_DAC_CHANNEL_COUNT + 1u] & SLIP_VGA_DAC_CHANNEL_MASK;
		g_vgaDacPendingPalette[firstIndex + colorIndex][2] =
		    rgb[(size_t)colorIndex * SLIP_VGA_DAC_CHANNEL_COUNT + 2u] & SLIP_VGA_DAC_CHANNEL_MASK;
		g_vgaDacDirtyEntries[firstIndex + colorIndex] = UINT8_MAX;
	}
}

void SlipVgaDac_Commit(void) {
	int colorIndex;

	for (colorIndex = 0; colorIndex < SLIP_VGA_DAC_PALETTE_COUNT; ++colorIndex) {
		if (g_vgaDacDirtyEntries[colorIndex] != 0u) {
			g_vgaDacDirtyEntries[colorIndex] = 0u;
			memcpy(g_vgaDacPalette[colorIndex], g_vgaDacPendingPalette[colorIndex], SLIP_VGA_DAC_CHANNEL_COUNT);
		}
	}
}

void SlipVgaDac_ReadRange(uint16_t firstIndex, uint16_t count, uint8_t *rgb) {
	if (rgb == NULL || firstIndex >= SLIP_VGA_DAC_PALETTE_COUNT || count > SLIP_VGA_DAC_PALETTE_COUNT - firstIndex) {
		return;
	}
	memcpy(rgb, g_vgaDacPalette[firstIndex], (size_t)count * SLIP_VGA_DAC_CHANNEL_COUNT);
}

void SlipVgaDac_RefreshArgbPalette(uint32_t palette[SLIP_VGA_DAC_PALETTE_COUNT]) {
	int colorIndex;

	for (colorIndex = 0; colorIndex < SLIP_VGA_DAC_PALETTE_COUNT; ++colorIndex) {
		const uint8_t red = SlipVgaDac_ExpandDacChannel(g_vgaDacPalette[colorIndex][0]);
		const uint8_t green = SlipVgaDac_ExpandDacChannel(g_vgaDacPalette[colorIndex][1]);
		const uint8_t blue = SlipVgaDac_ExpandDacChannel(g_vgaDacPalette[colorIndex][2]);

		palette[colorIndex] = SLIP_VGA_ARGB_OPAQUE_ALPHA | ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
	}
}
