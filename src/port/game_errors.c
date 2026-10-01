#include "game_errors.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static void SlipGame_WriteDosMessage(const char *message) {
#ifdef _WIN32
	_setmode(_fileno(stdout), _O_BINARY);
#endif
	fputs(message, stdout);
}

SLIP_RUNTIME_NORETURN void SlipGame_FileFailure(void) {
	SlipRuntime_Shutdown();
	SlipGame_WriteDosMessage(
	    "\r\n ------------------------------------------------------------------------------\r\n  ERROR: One of the "
	    "files required to run Slipstream is missing or damaged.\r\n         Reinstalling from your original disks may "
	    "correct the problem.\r\n ------------------------------------------------------------------------------\r\n");
	exit(1);
}

SLIP_RUNTIME_NORETURN void SlipGame_MemoryFailure(void) {
	SlipRuntime_Shutdown();
	SlipGame_WriteDosMessage(
	    "\r\n ------------------------------------------------------------------------------\r\n  ERROR: Slipstream "
	    "ran out of memory.\r\n ------------------------------------------------------------------------------\r\n");
	exit(1);
}

SLIP_RUNTIME_NORETURN void SlipGame_ConfigurationFailure(void) {
	SlipRuntime_Shutdown();
	SlipGame_WriteDosMessage(
	    "\r\n ------------------------------------------------------------------------------\r\n  ERROR: The "
	    "configuration file SLIPSTRM.CFG is invalid. Please delete it\r\n         and try again.\r\n "
	    "------------------------------------------------------------------------------\r\n");
	exit(1);
}

SLIP_RUNTIME_NORETURN void SlipGame_UnexpectedFailure(void) {
	SlipRuntime_Shutdown();
	SlipGame_WriteDosMessage(
	    "\r\n ------------------------------------------------------------------------------\r\n  ERROR: An unforseen "
	    "problem has been encountered - please restart Slipstream.\r\n "
	    "------------------------------------------------------------------------------\r\n");
	exit(1);
}

SLIP_RUNTIME_NORETURN void SlipGame_ResourceFailure(void) {
	const uint32_t error = SlipRuntime_error;
	if (error == 2 || error == 3 || error == 4 || error == 5)
		SlipGame_FileFailure();
	if (error == 6)
		SlipGame_MemoryFailure();
	SlipRuntime_Shutdown();
	SlipGame_WriteDosMessage(
	    "\r\n -------------------------------------------------------------------\r\n  ERROR: An internal error has "
	    "occurred. Please restart Slipstream.\r\n "
	    "-------------------------------------------------------------------\r\n");
	exit(1);
}
