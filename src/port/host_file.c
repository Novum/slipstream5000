#include "host_file.h"
#include <SDL3/SDL.h>
#include <fcntl.h>

#ifdef _WIN32
#include <errno.h>
#include <io.h>

static wchar_t *SlipHostFile_WidePath(const char *path) {

	wchar_t *const wide = (wchar_t *)SDL_iconv_string("WCHAR_T", "UTF-8", path, SDL_strlen(path) + 1);
	if (wide == NULL)
		errno = EINVAL;
	return wide;
}
#else
#include <unistd.h>
#endif

char *SlipHostFile_PreferencePath(const char *name) {

	char *const directory = SDL_GetPrefPath(NULL, "slipstream5000");
	if (directory == NULL)
		return NULL;
	char *path = NULL;
	SDL_asprintf(&path, "%s%s", directory, name);
	SDL_free(directory);
	return path;
}

FILE *SlipHostFile_OpenStream(const char *path, const char *mode) {

#ifdef _WIN32
	wchar_t *const widePath = SlipHostFile_WidePath(path);
	if (widePath == NULL)
		return NULL;
	wchar_t *const wideMode = SlipHostFile_WidePath(mode);
	FILE *const file = wideMode != NULL ? _wfopen(widePath, wideMode) : NULL;
	SDL_free(wideMode);
	SDL_free(widePath);
	return file;
#else
	return fopen(path, mode);
#endif
}

int SlipHostFile_OpenDescriptor(const char *path, int flags, int permissions) {

#ifdef _WIN32
	wchar_t *const widePath = SlipHostFile_WidePath(path);
	if (widePath == NULL)
		return -1;
	const int file = _wopen(widePath, flags, permissions);
	SDL_free(widePath);
	return file;
#else
	return open(path, flags, permissions);
#endif
}
