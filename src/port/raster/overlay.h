#ifndef SLIP_INDEXED_OVERLAY_H
#define SLIP_INDEXED_OVERLAY_H

#include <stddef.h>
#include <stdint.h>

/* Select indexed HUD drawing with a separate coverage mask for GPU composition. */
void RasterOverlay_Begin(uint8_t *pixels, uint8_t *coverage);
void RasterOverlay_End(void);
void RasterOverlay_MarkWritten(const uint8_t *pixels, size_t count);

#endif
