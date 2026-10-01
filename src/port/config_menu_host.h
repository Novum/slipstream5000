#ifndef SLIPSTREAM5000_CONFIG_MENU_HOST_H
#define SLIPSTREAM5000_CONFIG_MENU_HOST_H
#include "config_menu.h"
/* Native bindings for the translated configuration call tree. */
bool SlipConfigHost_Install(const char *resourcePath);
void SlipConfigHost_LoadConfiguration(void);
void SlipConfigHost_Initialize(const char *resourcePath);
extern const SlipConfigMenuCalls SlipConfigHost_calls;
#endif
