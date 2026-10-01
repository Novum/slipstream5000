#ifndef SLIPSTREAM5000_VGA_DAC_H
#define SLIPSTREAM5000_VGA_DAC_H

#include <stdint.h>

extern uint8_t g_vgaDacPalette[256][3];

void SlipVgaDac_InitializeHostBiosDefaults(void);
void SlipVgaDac_WriteRange(uint16_t firstIndex, uint16_t count, const uint8_t *rgb);
void SlipVgaDac_Commit(void);
void SlipVgaDac_ReadRange(uint16_t firstIndex, uint16_t count, uint8_t *rgb);
void SlipVgaDac_RefreshArgbPalette(uint32_t palette[256]);

#endif
