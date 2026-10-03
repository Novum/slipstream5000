#include "race_results_host.h"
#include "config_menu_host.h"
#include "config_settings.h"
#include "frame_timer.h"
#include "menu.h"
#include "menu_music.h"
#include "port_app_bridge.h"
#include "race_voice_host.h"
#include "string_table.h"

static SlipInputNavigationTable resultsNavigation = {
    2, 0, {-1, -1}, {-1, -1}, {-1, 0}, {1, -1}, {{83, 183}, {233, 183}}};

void SlipRaceResults_Begin(SlipRaceResultsScreen *screen, uint16_t track, SlipRaceRacerTable *racers,
                           uint16_t playerOne, const char *archive, SlipGameSoundState *sound) {
	enum { LAST_PROGRESS_QUALIFYING_POSITION = 4 };

	screen->track = track;
	screen->racers = racers;
	if (SlipRace_type == SLIP_RACE_TYPE_CHAMPIONSHIP)
		goto display;
	if (SlipRace_gameMode != SLIP_RACE_GAME_SINGLE_PLAYER)
		goto display;
	SlipRaceFindRacer found = SlipRace_FindRacer(screen->racers, playerOne);
	if (found.racerNotFound)
		goto display;
	if (found.record->finished == 0)
		goto display;
	if ((int16_t)found.record->racePosition > LAST_PROGRESS_QUALIFYING_POSITION)
		goto display;
	uint32_t progress = SlipConfig_TrackProgress();
	if (progress == SLIP_CONFIG_TRACK_COUNT)
		goto display;
	static const uint8_t trackProgress[SLIP_CONFIG_TRACK_COUNT + 1] = {255, 2, 8, 9, 5, 7, 1, 3, 4, 6, 10};
	if (progress != trackProgress[screen->track])
		goto display;
	++progress;
	SlipConfig_SetTrackProgress((uint16_t)progress);
	SlipConfigHost_calls.save(SlipConfigHost_calls.context);
display:
	SlipRaceResults_BeginDisplay(screen, archive, sound);
}

enum { SLIP_RACE_RESULTS_SPEECH_RESERVE_BYTES = 0x80000 };

void SlipRaceResults_BeginDisplay(SlipRaceResultsScreen *screen, const char *archive, SlipGameSoundState *sound) {
	SlipRaceVoiceCalls voices = SlipRaceVoiceHost_Calls(sound);
	if (SlipRace_gameMode != SLIP_RACE_GAME_SPLIT_SCREEN)
		SlipRaceVoice_Setup(sound->digitalCard, SLIP_RACE_VOICE_BANK_RESULTS, 1, 0,
		                    SLIP_RACE_RESULTS_SPEECH_RESERVE_BYTES, &voices);
	if (!SlipMenuMusic_ResultsStart(screen->racers) && sound->musicCard != 0)
		SlipRuntime_Fatal("Could not load DOS results music.");
	char secondaryPath[SLIP_MENU_ARCHIVE_PATH_BYTES];
	const char *archives[SLIP_MENU_ARCHIVE_CAPACITY];
	const size_t count = SlipMenu_BuildArchiveList(archive, secondaryPath, archives);
	SlipRaceResults_LoadAssets(&screen->assets, archives, count, (uint16_t)SlipConfig_Language());
	SlipInput_SetNavigation(&resultsNavigation);
	if (SlipRace_gameMode != SLIP_RACE_GAME_SPLIT_SCREEN) {
		for (uint16_t i = 0; i < screen->racers->racerCount; ++i) {
			const SlipRaceRacerState *const racer = &screen->racers->records[i];
			if (racer->racerType == SLIP_RACER_PLAYER_ONE) {
				enum { SLIP_RESULTS_WINNING_VOICE_COUNT = 2 };

				static const uint32_t winningVoices[SLIP_RESULTS_WINNING_VOICE_COUNT] = {0, 1};
				static const uint32_t positionVoices[SLIP_RACE_RACER_COUNT + 1] = {2, 0, 3, 4, 5, 6, 7, 8, 9, 10, 11};
				const uint32_t voice = racer->racePosition == 1
				                           ? winningVoices[SlipRandom_Range(SLIP_RESULTS_WINNING_VOICE_COUNT)]
				                           : positionVoices[racer->racePosition];
				SlipRaceVoice_Play(sound->digitalCard, voice, &voices);
				break;
			}
		}
	}
	SlipFrameTimer_Reset();
}

SlipRaceResultsAction SlipRaceResults_Frame(SlipRaceResultsScreen *screen) {
	SlipFrameTimer_Update((uint32_t)SlipSdl_TicksMs());
	SlipInputPointerPosition pointer = SlipInput_Pointer();
	screen->hoveredButton = SlipRaceResults_HitTest(pointer.x, pointer.y);
	SlipRaceResults_DrawResources(screen->racers, &screen->assets, screen->track, screen->hoveredButton, g_framebuffer,
	                              SLIPSTREAM_SCREEN_WIDTH);
	SlipMenu_PresentFrame();
	SlipMenu_PollInput();
	return SlipRaceResults_ReadInput(screen->hoveredButton, SlipInput_pressed);
}

void SlipRaceResults_EndDisplay(SlipRaceResultsScreen *screen, SlipGameSoundState *sound) {
	SlipInput_ClearNavigation();
	SlipStringTable_Release(screen->assets.stringSlot, &SlipRaceResults_stringResources);
	SlipRaceVoice_Shutdown(sound);
	SlipMenuMusic_ResultsStop();
	SlipRaceResults_ReleaseSprites(&screen->assets);
}
