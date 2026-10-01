#ifndef SLIPSTREAM5000_HOST_FILE_H
#define SLIPSTREAM5000_HOST_FILE_H

#include <stdio.h>

FILE *SlipHostFile_OpenStream(const char *path, const char *mode);
int SlipHostFile_OpenDescriptor(const char *path, int flags, int permissions);

#endif
