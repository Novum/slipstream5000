#ifndef SLIPSTREAM5000_GAME_DATA_H
#define SLIPSTREAM5000_GAME_DATA_H

#include <SDL3/SDL.h>
#include <stddef.h>

bool SlipGameData_FindInSteam(const char *steamDirectory, char *path, size_t capacity);
const char *SlipGameData_FindInstalled(void);
const char *SlipGameData_SelectFile(SDL_Window *window);

#endif
