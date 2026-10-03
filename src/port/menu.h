#ifndef SLIPSTREAM5000_MENU_H
#define SLIPSTREAM5000_MENU_H

#include <SDL3/SDL.h>

#include "sound_effects.h"
#include <stdbool.h>
#include <stdint.h>

void SlipMenu_BindSoundHost(SlipGameSoundState *sound, SlipSoundEffectLock lock, SlipSoundEffectUnlock unlock,
                            void *context);

typedef void (*SlipMenuSdlPresentFrame)(void *context);

bool SlipMenu_DebugAcceptChampionship(void);
bool SlipMenu_DebugAcceptSplitDrivers(void);
bool SlipMenu_DebugSplitGarage(const char *resPath);
bool SlipMenu_PollInput(void);
void SlipMenu_PresentFrame(void);
void SlipMenu_UpdateSystemCursor(void);
bool SlipMenu_CampaignPresenter(uint16_t track, uint32_t afterPreview);

const char *SlipMenu_FindResPath(int argc, char **argv);
bool SlipMenu_LoadMainMenuModel(const char *resPath);
void SlipMenu_Init(const char *resPath, SDL_Window *window, SDL_Renderer *renderer,
                   SlipMenuSdlPresentFrame presentFrame, void *presentFrameContext);
void SlipMenu_HandleEvent(const char *resPath, SDL_Window *window, SDL_Renderer *renderer, const SDL_Event *event,
                          bool *running);
bool SlipMenu_UpdateAndDraw(const char *resPath, SDL_Window *window);
int SlipConfig_TrackMapEnabled(void);
int SlipConfig_CloudsEnabled(void);
int SlipConfig_SpeedDisplay(void);
int SlipConfig_RearMonitor(void);
void SlipConfig_ToggleRearMonitor(void);
int SlipConfig_WeaponsMonitor(void);
void SlipConfig_ToggleWeaponsMonitor(void);

#endif
