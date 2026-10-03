#ifndef SLIPSTREAM5000_VGA_DAC_H
#define SLIPSTREAM5000_VGA_DAC_H

#include <stdint.h>

enum {
	SLIP_VGA_DAC_PALETTE_COUNT = 256,
	SLIP_VGA_DAC_CHANNEL_COUNT = 3,
	SLIP_VGA_DAC_CHANNEL_BITS = 6,
	SLIP_VGA_DAC_CHANNEL_MASK = (1u << SLIP_VGA_DAC_CHANNEL_BITS) - 1,
	SLIP_VGA_OUTPUT_CHANNEL_BITS = 8
};

extern uint8_t g_vgaDacPalette[SLIP_VGA_DAC_PALETTE_COUNT][SLIP_VGA_DAC_CHANNEL_COUNT];

void SlipVgaDac_InitializeHostBiosDefaults(void);
void SlipVgaDac_WriteRange(uint16_t firstIndex, uint16_t count, const uint8_t *rgb);
void SlipVgaDac_Commit(void);
void SlipVgaDac_ReadRange(uint16_t firstIndex, uint16_t count, uint8_t *rgb);
void SlipVgaDac_RefreshArgbPalette(uint32_t palette[SLIP_VGA_DAC_PALETTE_COUNT]);

#endif
