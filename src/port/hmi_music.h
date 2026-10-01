#ifndef SLIPSTREAM5000_HMI_MUSIC_H
#define SLIPSTREAM5000_HMI_MUSIC_H

#include "hmi_timer.h"
#include <stdint.h>

/* Error table lookup; caller supplies an HMI error code in 0..19. */
const char *HmiMusic_ErrorString(uint32_t code);

typedef const uint8_t *(*HmiMusicDriverTableQuery)(void *context, uint32_t command, uint16_t dataSelector);
void HmiMusic_GetDriverFunctions(HmiMusicDriverTableQuery entry, void *context, uint16_t codeSelector,
                                 uint16_t dataSelector, uint8_t destination[72]);

typedef struct HmiMusicState HmiMusicState;
typedef void (*HmiMusicDriverSend)(HmiMusicState *, uint8_t *message, uint32_t length, uint32_t driver);
typedef uint32_t (*HmiMusicDriverBank)(HmiMusicState *, uint8_t *, uint32_t driver, uint32_t length);

typedef struct HmiMusicBranchRecord {
	/* Live 24-byte record in the mutable song, not a copied record. */
	uint8_t *dosBytes;
	/* Native equivalent of the relocated DS-relative pointer at +8. */
	uint8_t *controllerPairs;
} HmiMusicBranchRecord;

typedef uint32_t (*HmiMusicLoopCallback)(HmiMusicState *, uint32_t song, uint8_t track, uint8_t branch,
                                         uint8_t remaining);
typedef uint32_t (*HmiMusicBranchCallback)(HmiMusicState *, uint32_t song, uint8_t track, uint8_t branch);
typedef void (*HmiMusicTriggerCallback)(HmiMusicState *, uint32_t song, uint8_t track, uint8_t trigger);

struct HmiMusicState {
	/* Native game-global context for callbacks installed by the game. */
	struct SlipGameSoundState *gameSound;
	/* Static host dispatch for far targets exposed by byte aliases.
	 * No executable loading: the host must map a verified target to C code. */
	void (*callTriggerFar)(HmiMusicState *, uint32_t offset, uint16_t selector, uint32_t song, uint8_t track,
	                       uint8_t trigger);
	uint8_t *songs[8];
	uint32_t playing[8];
	uint32_t paused[8];
	uint8_t callbackPending;
	void (*completionCallbacks[8])(HmiMusicState *);
	uint32_t muted[8];
	HmiMusicDriverSend driverSend[5];
	HmiMusicDriverBank driverBank[5];
	uint32_t (*driverInit[5])(HmiMusicState *, uint32_t driver, uint16_t port);
	uint32_t (*driverShutdown[5])(HmiMusicState *, uint32_t driver);
	uint32_t (*driverReset[5])(HmiMusicState *, uint32_t driver);

	void *driverContexts[5];
	uint32_t driverIds[5];
	uint32_t instrumentBankEnabled;

	uint8_t **instrumentPointers;

	uint8_t dispatchGlobals[0x86df8 - 0x85e05];
	uint32_t timerHandles[8];
	uint32_t fadeMode[8];
	uint32_t fadeStep[8];
	uint32_t fadeVolume[8];
	uint32_t fadeRemaining[8];
	uint8_t fadeCountdown[8];
	uint32_t activeTracks[8];
	uint32_t totalTracks[8];
	uint32_t elapsed[8][32];
	uint32_t delta[8][32];
	const uint8_t *trackHeaders[8][32];
	const uint8_t *cursors[8][32];
	const uint32_t *routing[8];
	HmiMusicBranchRecord *branches[8][32];
	HmiMusicBranchCallback branchCallbacks[8];
	HmiMusicLoopCallback loopCallbacks[8];
	HmiMusicTriggerCallback triggerCallbacks[8][127];
};

/* Fresh executable-image globals; not the middleware initialization routine. */
void HmiMusic_Construct(HmiMusicState *);
/* A002 module-relative address; in-place AdLib bank conversion. */
void HmiA002_Convert(uint8_t *bank);

typedef struct HmiA002State {
	/* Required hardware-model interface, not SDL or optional no-op hooks. */
	void *portContext;
	uint8_t (*portRead)(void *, uint16_t);
	void (*portWrite)(void *, uint16_t, uint8_t);
	/* Stored as dwords; port instructions consume only the low word. */
	uint32_t portBase, portCopy, portArgument;
	uint32_t initialized;
	uint8_t portInput[4];
	uint8_t busy;       /* Send-entry busy byte; native functions are statically bound. */
	uint8_t *bankInput; /* Native offset/selector pair at +5fc/+600. */
	uint32_t bankDriverIndex, bankByteLength;

	union {
		uint8_t operatorRelease[32];

		struct {
			uint8_t reserved[16];
			uint8_t frequencyLow[9], frequencyHigh[9];
		} frequency;
	} cache;

	uint8_t activeNotes[9];
	uint8_t operatorLevels[32];
	/* Reset routines clear eleven bytes, ending immediately before active notes. */
	uint8_t instrumentSet[11];
	uint8_t rhythm, chipEnabled, mode;
	uint8_t operatorOffsets[18];
	uint8_t volumeCurve[128];
	/* Contiguous pitch tables, including the +30e4/+30e8 views. */
	uint32_t pitchTables[(0x318c - 0x2f4c) / 4];
	uint32_t voiceChannels[9];
	uint32_t sustain[16], deferredCount[16];
	/* Contiguous dwords retain the +274c/+2770 channel-index alias. */
	uint32_t controllerGlobals[(0x27b0 - 0x2648) / 4];
	uint8_t deferred[16][32][3];
	uint32_t bankToggle;
	uint32_t pitchEnabled;
	uint32_t noteFrequencies[128];
	uint8_t *melodicBank, *melodicIndex, *melodicData;
	int32_t melodicCount;
	uint32_t melodicLoaded;
	uint8_t *percussionBank, *percussionIndex, *percussionData;
	uint32_t percussionLoaded;
	uint32_t programs[16];
	uint8_t initialProgramMessage[2];
	uint8_t offMessage[3], programMessage[2];
	uint8_t controlMessage[3], pitchMessage[2];
	uint8_t *message;
} HmiA002State;

/* Statically linked initial data; no driver binary required. */
void HmiA002_ConstructStatic(HmiA002State *);

uint32_t HmiA002_SendEntry(HmiA002State *, const uint8_t *, uint32_t preservedReturnValue);
/* Native ABI adapter for the middleware send callback. */
void HmiA002_MiddlewareSend(HmiMusicState *, uint8_t *, uint32_t, uint32_t);
uint32_t HmiA002_MiddlewareBank(HmiMusicState *, uint8_t *, uint32_t, uint32_t);
/* Static callback binding for the driver. */
void HmiA002_BindNativeFunctions(HmiMusicState *, uint32_t driver, HmiA002State *);
uint32_t HmiMusic_SetBank(HmiMusicState *, uint32_t driver, uint32_t length, uint8_t *);
uint32_t HmiA002_InitEntry(HmiA002State *, uint16_t);
uint32_t HmiA002_ShutdownEntry(HmiA002State *);
uint32_t HmiA002_BankEntry(HmiA002State *, uint8_t *, uint32_t, uint32_t);
uint32_t HmiA002_BankWrapper(HmiA002State *, uint8_t *, uint32_t, uint32_t);
uint32_t HmiA002_SendWrapper(HmiA002State *, const uint8_t *, uint32_t, uint32_t);
uint32_t HmiA002_InitWrapper(HmiA002State *, uint32_t);
uint32_t HmiA002_ShutdownWrapper(HmiA002State *);
uint8_t HmiA002_SetPort(HmiA002State *, const uint32_t *);
uint32_t HmiA002_Initialize(HmiA002State *, uint32_t, uint32_t, const uint8_t *);
void HmiA002_Shutdown(HmiA002State *);
uint8_t HmiA002_ResetChip(HmiA002State *);
uint8_t HmiA002_ClearChip(HmiA002State *);
uint8_t HmiA002_DisableChip(HmiA002State *);
void HmiA002_NoteOn(HmiA002State *, uint8_t, uint8_t, uint8_t);
void HmiA002_Send(HmiA002State *, uint32_t, uint32_t, const uint8_t *);
uint8_t HmiA002_Write(HmiA002State *, uint8_t, uint8_t);
uint8_t HmiA002_KeyOff(HmiA002State *, uint8_t);
uint8_t HmiA002_Select(HmiA002State *, uint8_t);
uint8_t HmiA002_KeyOn(HmiA002State *, uint8_t, uint32_t);
uint8_t HmiA002_Instrument(HmiA002State *, const uint8_t *, uint8_t);
void HmiA002_Off(HmiA002State *, uint32_t, uint32_t, const uint8_t *);
void HmiA002_AllOff(HmiA002State *, uint32_t);
void HmiA002_Reset(HmiA002State *, uint32_t);
void HmiA002_Volume(HmiA002State *, uint32_t, uint8_t);
void HmiA002_Control(HmiA002State *, uint32_t, uint32_t, const uint8_t *);
uint32_t HmiA002_PitchCalc(HmiA002State *, uint32_t, uint32_t, uint32_t);
void HmiA002_Pitch(HmiA002State *, uint32_t, uint32_t, const uint8_t *);
uint8_t HmiA002_Frequency(HmiA002State *, uint8_t, uint32_t);
void HmiA002_Channel(HmiA002State *, uint32_t, uint32_t, const uint8_t *);
uint32_t HmiA002_Bank(HmiA002State *, uint32_t, uint32_t, uint8_t *);
uint8_t *HmiMusic_Instrument(HmiMusicState *, uint32_t instrument, uint8_t note);
uint32_t HmiMusic_SetLoopCallback(HmiMusicState *state, uint32_t song, HmiMusicLoopCallback callback);
uint32_t HmiMusic_IsStopped(const HmiMusicState *, uint32_t song);
/* Middleware dispatch, ending at the per-driver send callback. */
uint32_t HmiMusic_Dispatch(HmiMusicState *, uint32_t song, uint32_t driver, uint8_t *message, uint32_t length);
uint32_t HmiMusic_SetSongVolume(HmiMusicState *, uint32_t song, uint8_t volume);
uint32_t HmiMusic_SetMasterVolume(HmiMusicState *, uint8_t volume);
uint32_t HmiMusic_Cleanup(HmiMusicState *, uint32_t song);
uint32_t HmiMusic_StopSong(HmiMusicState *, HmiTimerState *, uint32_t song);
void HmiMusic_Tick(HmiMusicState *, HmiTimerState *);
uint32_t HmiMusic_BranchSong(HmiMusicState *, uint32_t song, uint8_t branchId);
uint32_t HmiMusic_StartSong(HmiMusicState *, HmiTimerState *, uint32_t song);
uint32_t HmiMusic_Fade(HmiMusicState *, HmiTimerState *, uint32_t song, uint32_t mode, uint32_t duration, uint8_t from,
                       uint8_t to, uint32_t divisor);
void HmiMusic_RestoreBranch(HmiMusicState *, uint32_t song, uint32_t track, uint32_t branch);
uint32_t HmiMusic_SetTempo(HmiMusicState *, HmiTimerState *, uint32_t song, uint32_t percent);
uint32_t HmiMusic_SetBranchCallback(HmiMusicState *state, uint32_t song, HmiMusicBranchCallback callback);
uint32_t HmiMusic_SetTriggerCallback(HmiMusicState *state, uint32_t song, uint8_t trigger,
                                     HmiMusicTriggerCallback callback);

void HmiMusic_WriteTriggerAlias(HmiMusicState *, uint32_t offset, uint16_t selector);

typedef struct HmiMusicFarPointer {
	uint32_t offset;
	uint16_t selector;
} HmiMusicFarPointer;

/* Far-pointer inputs alongside their native callable equivalent. */
uint32_t HmiMusic_SetTriggerPointer(HmiMusicState *, uint32_t song, uint8_t trigger, HmiMusicTriggerCallback,
                                    HmiMusicFarPointer);
HmiMusicFarPointer HmiMusic_ReadTriggerAlias(const HmiMusicState *);

typedef struct HmiMusicSongDescriptor {
	uint8_t *songData;

	uint32_t completionCallbackOffset;
	uint16_t completionCallbackSelector;
} HmiMusicSongDescriptor;

uint32_t HmiMusic_ResetSong(HmiMusicState *state, uint32_t songIndex, const HmiMusicSongDescriptor *descriptor);

/* Native branchStorage must hold the sum of the song's branch counts.
 * completion is the native binding of descriptor's preserved far pointer. */
uint32_t HmiMusic_Register(HmiMusicState *, const HmiMusicSongDescriptor *, uint32_t *routing, uint32_t *songIndex,
                           HmiMusicBranchRecord *branchStorage, void (*completion)(HmiMusicState *));
uint32_t HmiMusic_Unregister(HmiMusicState *, uint32_t song);

uint32_t HmiMusic_ReadDelta(const uint8_t *stream, uint32_t *value);

#endif
