#ifndef SLIPSTREAM5000_DEBUG_H
#define SLIPSTREAM5000_DEBUG_H
#include <stdbool.h>
#include <stdint.h>

/* Host replay clock, enabled only by capture diagnostics. */
extern bool SlipDebug_fixedClock;
extern uint64_t SlipDebug_clockMilliseconds;
extern uint64_t SlipDebug_biosClockOrigin;

#ifdef SLIP_DEBUG
int SlipDebug_RunDumpCommand(int argc, char **argv);
#endif

#endif
