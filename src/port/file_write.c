#include "file_write.h"
#include "runtime.h"
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

static int SlipFile_Create(const char *path) {
#ifdef _WIN32
	return _open(path, _O_BINARY | _O_CREAT | _O_TRUNC | _O_RDWR, _S_IREAD | _S_IWRITE);
#else
	return open(path, O_CREAT | O_TRUNC | O_RDWR, 0666);
#endif
}

static bool SlipFile_Close(int handle) {
#ifdef _WIN32
	return _close(handle) < 0;
#else
	return close(handle) < 0;
#endif
}

static bool SlipFile_WriteAt(int handle, int32_t offset, const uint8_t *data, uint16_t length) {
	if (offset >= 0) {
#ifdef _WIN32
		if (_lseek(handle, offset, SEEK_SET) < 0)
#else
		if (lseek(handle, offset, SEEK_SET) < 0)
#endif
			return true;
	}
#ifdef _WIN32
	return _write(handle, data, length) < 0;
#else
	return write(handle, data, length) < 0;
#endif
}

bool SlipFile_Write(const char *path, const uint8_t *data, uint32_t length) {
	const int handle = SlipFile_Create(path);
	if (handle < 0) {
		SlipRuntime_error = 4;
		return true;
	}
	if (SlipFile_WriteAt(handle, 0, data, (uint16_t)length)) {
		SlipRuntime_error = 5;
		SlipFile_Close(handle);
		return true;
	}
	return SlipFile_Close(handle);
}
