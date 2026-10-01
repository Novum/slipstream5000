#ifndef SLIPSTREAM5000_HOST_FILE_H
#define SLIPSTREAM5000_HOST_FILE_H

#include <stdio.h>

char *SlipHostFile_PreferencePath(const char *name);

FILE *SlipHostFile_OpenStream(const char *path, const char *mode);
int SlipHostFile_OpenDescriptor(const char *path, int flags, int permissions);

#endif
