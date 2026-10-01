#ifndef SLIPSTREAM5000_CONTROLLER_SDL_H
#define SLIPSTREAM5000_CONTROLLER_SDL_H
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>
void SlipControllerSdl_Initialize(void);
void SlipControllerSdl_Poll(void);
void SlipControllerSdl_Shutdown(void);
uint16_t SlipControllerSdl_Presence(void);
bool SlipControllerSdl_Read(void *, uint32_t joystick, uint16_t *x, uint16_t *y);
void SlipControllerSdl_ObserveEvent(const SDL_Event *event);
#endif
