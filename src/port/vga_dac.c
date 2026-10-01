#include "vga_dac.h"

#include <stddef.h>
#include <string.h>

uint8_t g_vgaDacPalette[256][3];
static uint8_t g_vgaDacPendingPalette[256][3];
static uint8_t g_vgaDacDirtyEntries[256];

static uint8_t SlipVgaDac_ExpandDacChannel(uint8_t channel) { return (uint8_t)((channel << 2) | (channel >> 4)); }

void SlipVgaDac_InitializeHostBiosDefaults(void) {

	static const uint8_t biosReservedColors[8][3] = {
	    {0x00, 0x00, 0x00}, {0x00, 0x00, 0x00}, {0x00, 0x00, 0x00}, {0x20, 0x20, 0x20},
	    {0x00, 0x3f, 0x00}, {0x3f, 0x00, 0x00}, {0x3f, 0x3f, 0x00}, {0x3f, 0x3f, 0x3f},
	};
	static const uint8_t biosBaseColors[32][3] = {
	    {0x00, 0x00, 0x00}, {0x00, 0x00, 0x2a}, {0x00, 0x2a, 0x00}, {0x00, 0x2a, 0x2a}, {0x2a, 0x00, 0x00},
	    {0x2a, 0x00, 0x2a}, {0x2a, 0x15, 0x00}, {0x2a, 0x2a, 0x2a}, {0x15, 0x15, 0x15}, {0x15, 0x15, 0x3f},
	    {0x15, 0x3f, 0x15}, {0x15, 0x3f, 0x3f}, {0x3f, 0x15, 0x15}, {0x3f, 0x15, 0x3f}, {0x3f, 0x3f, 0x15},
	    {0x3f, 0x3f, 0x3f}, {0x00, 0x00, 0x00}, {0x05, 0x05, 0x05}, {0x08, 0x08, 0x08}, {0x0b, 0x0b, 0x0b},
	    {0x0e, 0x0e, 0x0e}, {0x11, 0x11, 0x11}, {0x14, 0x14, 0x14}, {0x18, 0x18, 0x18}, {0x1c, 0x1c, 0x1c},
	    {0x20, 0x20, 0x20}, {0x24, 0x24, 0x24}, {0x28, 0x28, 0x28}, {0x2d, 0x2d, 0x2d}, {0x32, 0x32, 0x32},
	    {0x38, 0x38, 0x38}, {0x3f, 0x3f, 0x3f},
	};
	static const uint8_t ringRamps[9][5] = {
	    {0x00, 0x10, 0x1f, 0x2f, 0x3f}, {0x1f, 0x27, 0x2f, 0x37, 0x3f}, {0x2d, 0x31, 0x36, 0x3a, 0x3f},
	    {0x00, 0x07, 0x0e, 0x15, 0x1c}, {0x0e, 0x11, 0x15, 0x18, 0x1c}, {0x14, 0x16, 0x18, 0x1a, 0x1c},
	    {0x00, 0x04, 0x08, 0x0c, 0x10}, {0x08, 0x0a, 0x0c, 0x0e, 0x10}, {0x0b, 0x0c, 0x0d, 0x0f, 0x10},
	};
	int ring;
	int step;

	memset(g_vgaDacPalette, 0, sizeof(g_vgaDacPalette));
	memcpy(g_vgaDacPalette, biosBaseColors, sizeof(biosBaseColors));
	for (ring = 0; ring < 9; ++ring) {
		const uint8_t *const ramp = ringRamps[ring];
		const uint8_t low = ramp[0];
		const uint8_t high = ramp[4];

		for (step = 0; step < 24; ++step) {
			const int side = step / 4;
			const int offset = step % 4;
			const uint8_t rising = ramp[offset];
			const uint8_t falling = ramp[4 - offset];
			uint8_t *const color = g_vgaDacPalette[32 + ring * 24 + step];

			switch (side) {
			case 0:
				color[0] = rising;
				color[1] = low;
				color[2] = high;
				break;
			case 1:
				color[0] = high;
				color[1] = low;
				color[2] = falling;
				break;
			case 2:
				color[0] = high;
				color[1] = rising;
				color[2] = low;
				break;
			case 3:
				color[0] = falling;
				color[1] = high;
				color[2] = low;
				break;
			case 4:
				color[0] = low;
				color[1] = high;
				color[2] = rising;
				break;
			default:
				color[0] = low;
				color[1] = falling;
				color[2] = high;
				break;
			}
		}
	}
	memcpy(g_vgaDacPalette + 248, biosReservedColors, sizeof(biosReservedColors));
	memcpy(g_vgaDacPendingPalette, g_vgaDacPalette, sizeof(g_vgaDacPalette));
	memset(g_vgaDacDirtyEntries, 0, sizeof(g_vgaDacDirtyEntries));
}

void SlipVgaDac_WriteRange(uint16_t firstIndex, uint16_t count, const uint8_t *rgb) {
	uint16_t colorIndex;

	if (rgb == NULL || firstIndex >= 256u || count > 256u - firstIndex) {
		return;
	}
	for (colorIndex = 0; colorIndex < count; ++colorIndex) {
		g_vgaDacPendingPalette[firstIndex + colorIndex][0] = rgb[(size_t)colorIndex * 3u + 0u] & 0x3fu;
		g_vgaDacPendingPalette[firstIndex + colorIndex][1] = rgb[(size_t)colorIndex * 3u + 1u] & 0x3fu;
		g_vgaDacPendingPalette[firstIndex + colorIndex][2] = rgb[(size_t)colorIndex * 3u + 2u] & 0x3fu;
		g_vgaDacDirtyEntries[firstIndex + colorIndex] = 0xffu;
	}
}

void SlipVgaDac_Commit(void) {
	int colorIndex;

	for (colorIndex = 0; colorIndex < 256; ++colorIndex) {
		if (g_vgaDacDirtyEntries[colorIndex] != 0u) {
			g_vgaDacDirtyEntries[colorIndex] = 0u;
			memcpy(g_vgaDacPalette[colorIndex], g_vgaDacPendingPalette[colorIndex], 3u);
		}
	}
}

void SlipVgaDac_ReadRange(uint16_t firstIndex, uint16_t count, uint8_t *rgb) {
	if (rgb == NULL || firstIndex >= 256u || count > 256u - firstIndex) {
		return;
	}
	memcpy(rgb, g_vgaDacPalette[firstIndex], (size_t)count * 3u);
}

void SlipVgaDac_RefreshArgbPalette(uint32_t palette[256]) {
	int colorIndex;

	for (colorIndex = 0; colorIndex < 256; ++colorIndex) {
		const uint8_t red = SlipVgaDac_ExpandDacChannel(g_vgaDacPalette[colorIndex][0]);
		const uint8_t green = SlipVgaDac_ExpandDacChannel(g_vgaDacPalette[colorIndex][1]);
		const uint8_t blue = SlipVgaDac_ExpandDacChannel(g_vgaDacPalette[colorIndex][2]);

		palette[colorIndex] = 0xff000000u | ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
	}
}
