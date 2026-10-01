#ifndef SLIPSTREAM5000_GAME_ERRORS_H
#define SLIPSTREAM5000_GAME_ERRORS_H
#include "runtime.h"
SLIP_RUNTIME_NORETURN void SlipGame_ResourceFailure(void);
SLIP_RUNTIME_NORETURN void SlipGame_FileFailure(void);
SLIP_RUNTIME_NORETURN void SlipGame_MemoryFailure(void);
SLIP_RUNTIME_NORETURN void SlipGame_ConfigurationFailure(void);
SLIP_RUNTIME_NORETURN void SlipGame_UnexpectedFailure(void);
#endif
