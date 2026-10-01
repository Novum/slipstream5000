/* Host-only differential-test transport, shared by DOSBox and the SDL diagnostic.
 * This is instrumentation, not a translation of game logic. Never writes images
 * to files: SLIP_HARNESS_PIPE must name a Windows pipe owned by the harness. */
#ifndef SLIP_CAPTURE_STREAM_H
#define SLIP_CAPTURE_STREAM_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int SlipHarness_WriteFrame(uint32_t frame, uint32_t milliseconds, uint32_t players, uint32_t randomA,
                                  uint32_t randomB, const uint32_t state[16], const uint8_t pixels[64000],
                                  const uint8_t palette[768]) {
	static FILE *pipe = NULL;
	const char *path = getenv("SLIP_HARNESS_PIPE");
	if (!path || !*path)
		return 0;
	if (!pipe) {
		if (strncmp(path, "\\\\.\\pipe\\", 9) != 0 || !(pipe = fopen(path, "wb"))) {
			fprintf(stderr, "Could not open harness named pipe\n");
			exit(90);
		}
	}
	uint32_t words[22] = {0x31464c53u, frame, milliseconds, players, randomA, randomB};
	for (unsigned i = 0; i < 16; ++i)
		words[6 + i] = state[i];
	uint8_t header[88];
	for (unsigned i = 0; i < 22; ++i)
		for (unsigned byte = 0; byte < 4; ++byte)
			header[i * 4 + byte] = (uint8_t)(words[i] >> (byte * 8));
	if (fwrite(header, 1, sizeof(header), pipe) != sizeof(header) || fwrite(pixels, 1, 64000, pipe) != 64000 ||
	    fwrite(palette, 1, 768, pipe) != 768 || fflush(pipe) != 0) {
		fprintf(stderr, "Harness pipe write failed\n");
		exit(91);
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
