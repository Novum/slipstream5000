#ifndef SLIPSTREAM5000_HMI_MUSIC_H
#define SLIPSTREAM5000_HMI_MUSIC_H

#include "hmi_music_format.h"
#include "hmi_timer.h"
#include <stdint.h>

/* Error table lookup; caller supplies an HMI error code in 0..19. */
const char *HmiMusic_ErrorString(uint32_t code);

typedef const uint8_t *(*HmiMusicDriverTableQuery)(void *context, uint32_t command, uint16_t dataSelector);
void HmiMusic_GetDriverFunctions(HmiMusicDriverTableQuery entry, void *context, uint16_t codeSelector,
                                 uint16_t dataSelector, uint8_t destination[HMI_DRIVER_FUNCTION_TABLE_BYTES]);

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
	uint8_t *songs[HMI_MUSIC_SONG_COUNT];
	uint32_t playing[HMI_MUSIC_SONG_COUNT];
	uint32_t paused[HMI_MUSIC_SONG_COUNT];
	uint8_t callbackPending;
	void (*completionCallbacks[HMI_MUSIC_SONG_COUNT])(HmiMusicState *);
	uint32_t muted[HMI_MUSIC_SONG_COUNT];
	HmiMusicDriverSend driverSend[HMI_MUSIC_DRIVER_COUNT];
	HmiMusicDriverBank driverBank[HMI_MUSIC_DRIVER_COUNT];
	uint32_t (*driverInit[HMI_MUSIC_DRIVER_COUNT])(HmiMusicState *, uint32_t driver, uint16_t port);
	uint32_t (*driverShutdown[HMI_MUSIC_DRIVER_COUNT])(HmiMusicState *, uint32_t driver);
	uint32_t (*driverReset[HMI_MUSIC_DRIVER_COUNT])(HmiMusicState *, uint32_t driver);

	void *driverContexts[HMI_MUSIC_DRIVER_COUNT];
	uint32_t driverIds[HMI_MUSIC_DRIVER_COUNT];
	uint32_t instrumentBankEnabled;

	uint8_t **instrumentPointers;

	uint8_t dispatchGlobals[HMI_DISPATCH_STATE_BYTES];
	uint32_t timerHandles[HMI_MUSIC_SONG_COUNT];
	uint32_t fadeMode[HMI_MUSIC_SONG_COUNT];
	uint32_t fadeStep[HMI_MUSIC_SONG_COUNT];
	uint32_t fadeVolume[HMI_MUSIC_SONG_COUNT];
	uint32_t fadeRemaining[HMI_MUSIC_SONG_COUNT];
	uint8_t fadeCountdown[HMI_MUSIC_SONG_COUNT];
	uint32_t activeTracks[HMI_MUSIC_SONG_COUNT];
	uint32_t totalTracks[HMI_MUSIC_SONG_COUNT];
	uint32_t elapsed[HMI_MUSIC_SONG_COUNT][HMI_MUSIC_TRACK_COUNT];
	uint32_t delta[HMI_MUSIC_SONG_COUNT][HMI_MUSIC_TRACK_COUNT];
	const uint8_t *trackHeaders[HMI_MUSIC_SONG_COUNT][HMI_MUSIC_TRACK_COUNT];
	const uint8_t *cursors[HMI_MUSIC_SONG_COUNT][HMI_MUSIC_TRACK_COUNT];
	const uint32_t *routing[HMI_MUSIC_SONG_COUNT];
	HmiMusicBranchRecord *branches[HMI_MUSIC_SONG_COUNT][HMI_MUSIC_TRACK_COUNT];
	HmiMusicBranchCallback branchCallbacks[HMI_MUSIC_SONG_COUNT];
	HmiMusicLoopCallback loopCallbacks[HMI_MUSIC_SONG_COUNT];
	HmiMusicTriggerCallback triggerCallbacks[HMI_MUSIC_SONG_COUNT][HMI_MUSIC_TRIGGER_COUNT];
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
	uint8_t portInput[HMI_SERIALIZED_DWORD_BYTES];
	uint8_t busy;       /* Send-entry busy byte; native functions are statically bound. */
	uint8_t *bankInput; /* Native offset/selector pair at +5fc/+600. */
	uint32_t bankDriverIndex, bankByteLength;

	union {
		uint8_t operatorRelease[HMI_A002_OPERATOR_CACHE_BYTES];

		struct {
			uint8_t reserved[HMI_MIDI_CHANNEL_COUNT];
			uint8_t frequencyLow[HMI_A002_VOICE_COUNT], frequencyHigh[HMI_A002_VOICE_COUNT];
		} frequency;
	} cache;

	uint8_t activeNotes[HMI_A002_VOICE_COUNT];
	uint8_t operatorLevels[HMI_A002_OPERATOR_CACHE_BYTES];
	/* Reset routines clear eleven bytes, ending immediately before active notes. */
	uint8_t instrumentSet[HMI_A002_INSTRUMENT_FLAGS_BYTES];
	uint8_t rhythm, chipEnabled, mode;
	uint8_t operatorOffsets[HMI_A002_VOICE_COUNT * HMI_A002_OPERATOR_COUNT];
	uint8_t volumeCurve[HMI_MIDI_NOTE_COUNT];
	/* Contiguous pitch tables include the lower/upper block views. */
	uint32_t pitchTables[HMI_A002_PITCH_TABLE_COUNT];
	uint32_t voiceChannels[HMI_A002_VOICE_COUNT];
	uint32_t sustain[HMI_MIDI_CHANNEL_COUNT], deferredCount[HMI_MIDI_CHANNEL_COUNT];
	/* Contiguous dwords preserve overlapping velocity/volume-change entries. */
	uint32_t controllerGlobals[HMI_A002_CONTROLLER_WORD_COUNT];
	uint8_t deferred[HMI_MIDI_CHANNEL_COUNT][HMI_A002_DEFERRED_CAPACITY][HMI_A002_OFF_PARAMETER_BYTES];
	uint32_t bankToggle;
	uint32_t pitchEnabled;
	uint32_t noteFrequencies[HMI_MIDI_NOTE_COUNT];
	uint8_t *melodicBank, *melodicIndex, *melodicData;
	int32_t melodicCount;
	uint32_t melodicLoaded;
	uint8_t *percussionBank, *percussionIndex, *percussionData;
	uint32_t percussionLoaded;
	uint32_t programs[HMI_MIDI_CHANNEL_COUNT];
	uint8_t initialProgramMessage[HMI_A002_PROGRAM_PARAMETER_BYTES];
	uint8_t offMessage[HMI_A002_OFF_PARAMETER_BYTES], programMessage[HMI_A002_PROGRAM_PARAMETER_BYTES];
	uint8_t controlMessage[HMI_A002_CONTROL_PARAMETER_BYTES], pitchMessage[HMI_A002_PITCH_PARAMETER_BYTES];
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
