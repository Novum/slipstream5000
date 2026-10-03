/* Host-only differential-test transport, shared by DOSBox and the SDL diagnostic.
 * This is instrumentation, not a translation of game logic. Never writes images
 * to files: SLIP_HARNESS_PIPE must name a Windows pipe owned by the harness. */
#ifndef SLIP_CAPTURE_STREAM_H
#define SLIP_CAPTURE_STREAM_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SLF1 frames contain six metadata words, sixteen state words, a 320x200
 * indexed image, and 256 RGB palette entries. All header words are little-endian. */
enum {
	SLIP_CAPTURE_SIGNATURE = 0x31464c53u,
	SLIP_CAPTURE_METADATA_WORD_COUNT = 6,
	SLIP_CAPTURE_STATE_WORD_COUNT = 16,
	SLIP_CAPTURE_HEADER_WORD_COUNT = SLIP_CAPTURE_METADATA_WORD_COUNT + SLIP_CAPTURE_STATE_WORD_COUNT,
	SLIP_CAPTURE_PIXEL_BYTES = 320 * 200,
	SLIP_CAPTURE_PALETTE_BYTES = 256 * 3,
	SLIP_CAPTURE_OPEN_FAILURE_EXIT = 90,
	SLIP_CAPTURE_WRITE_FAILURE_EXIT = 91
};

static int SlipHarness_WriteFrame(uint32_t frame, uint32_t milliseconds, uint32_t players, uint32_t randomA,
                                  uint32_t randomB, const uint32_t state[SLIP_CAPTURE_STATE_WORD_COUNT],
                                  const uint8_t pixels[SLIP_CAPTURE_PIXEL_BYTES],
                                  const uint8_t palette[SLIP_CAPTURE_PALETTE_BYTES]) {
	static FILE *pipe = NULL;
	const char *path = getenv("SLIP_HARNESS_PIPE");
	if (!path || !*path)
		return 0;
	if (!pipe) {
		if (strncmp(path, "\\\\.\\pipe\\", sizeof("\\\\.\\pipe\\") - 1) != 0 || !(pipe = fopen(path, "wb"))) {
			fprintf(stderr, "Could not open harness named pipe\n");
			exit(SLIP_CAPTURE_OPEN_FAILURE_EXIT);
		}
	}
	uint32_t words[SLIP_CAPTURE_HEADER_WORD_COUNT] = {
	    SLIP_CAPTURE_SIGNATURE, frame, milliseconds, players, randomA, randomB};
	for (unsigned i = 0; i < SLIP_CAPTURE_STATE_WORD_COUNT; ++i)
		words[SLIP_CAPTURE_METADATA_WORD_COUNT + i] = state[i];
	uint8_t header[SLIP_CAPTURE_HEADER_WORD_COUNT * sizeof(uint32_t)];
	for (unsigned i = 0; i < SLIP_CAPTURE_HEADER_WORD_COUNT; ++i)
		for (unsigned byte = 0; byte < sizeof(uint32_t); ++byte)
			header[i * sizeof(uint32_t) + byte] = (uint8_t)(words[i] >> (byte * 8));
	if (fwrite(header, 1, sizeof(header), pipe) != sizeof(header) ||
	    fwrite(pixels, 1, SLIP_CAPTURE_PIXEL_BYTES, pipe) != SLIP_CAPTURE_PIXEL_BYTES ||
	    fwrite(palette, 1, SLIP_CAPTURE_PALETTE_BYTES, pipe) != SLIP_CAPTURE_PALETTE_BYTES || fflush(pipe) != 0) {
		fprintf(stderr, "Harness pipe write failed\n");
		exit(SLIP_CAPTURE_WRITE_FAILURE_EXIT);
	}
	const char *limit = getenv("SLIP_HARNESS_FRAMES");
	if (limit && frame + 1 == strtoul(limit, NULL, 10)) {
		fclose(pipe);
		pipe = NULL;
		return 1;
	}
	return 0;
}
#endif
