#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <process.h>
#endif

/* Project-wide target of the assert macro selected by SLIP_ASSERT_TO_STDERR. */
void SlipAssertFail(const char *message, const char *file, unsigned line) {
	fprintf(stderr, "Assertion failed: %s, file %s, line %u\n", message, file, line);
	fflush(stderr);
#ifdef _WIN32
	_exit(3);
#else
	_Exit(3);
#endif
}
