#ifdef SLIP_REPLAY_HARNESS
#include "../testing/ui_capture.h"
#endif
#include "config_menu_host.h"
#include "controller_sdl.h"
#include "debug.h"
#include "frame_timer.h"
#include "game_data.h"
#include "hmi_digital.h"
#include "hmi_mixer_1000.h"
#include "hmi_sdl_output.h"
#include "input.h"
#include "menu.h"
#include "menu_music.h"
#include "port_app_bridge.h"
#include "race_session.h"
#include "raster.h"
#include "saved_games.h"
#include "sound_effects.h"
#include "startup_intro.h"
#include "vga_dac.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t g_presentPixels[SLIPSTREAM_SCREEN_WIDTH * SLIPSTREAM_SCREEN_HEIGHT];
bool g_presentPixelsReady;
static const SlipStartupIntroHost *SlipSdl_startupIntroHost;

static bool SlipSdl_Init(void) {
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD))
		return false;
	SlipControllerSdl_Initialize();
	return true;
}

uint64_t SlipSdl_TicksMs(void) { return SlipDebug_fixedClock ? SlipDebug_clockMilliseconds : (uint64_t)SDL_GetTicks(); }

void SlipSdl_DelayMs(uint32_t ms) { SDL_Delay(ms); }

bool SlipSdl_PollEvent(SDL_Event *event) {
	static bool fullscreenEnterHeld[2];
	SlipControllerSdl_Poll();
	while (SDL_PollEvent(event)) {
		SlipControllerSdl_ObserveEvent(event);
		if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
			if (event->key.key == SDLK_RETURN || event->key.key == SDLK_KP_ENTER) {
				const unsigned index = event->key.key == SDLK_KP_ENTER;
				if (fullscreenEnterHeld[index]) {
					if (event->type == SDL_EVENT_KEY_UP)
						fullscreenEnterHeld[index] = false;
					continue;
				}
				if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat && (event->key.mod & SDL_KMOD_ALT)) {
					SDL_Window *const window = SDL_GetWindowFromID(event->key.windowID);
					fullscreenEnterHeld[index] = true;
					if (window != NULL &&
					    !SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)))
						fprintf(stderr, "SDL_SetWindowFullscreen failed: %s\n", SDL_GetError());
					continue;
				}
			}
		}
		return true;
	}
	return false;
}

/*
 * Pseudo-bandlimited pixel-art upsampling (Maister, 2018): each source
 * pixel stays a sharp rectangle, but its edges are blended over exactly
 * one destination pixel with a smoothstep profile. The scale is fixed
 * (320x200 into the 960x720 4:3 CRT space, 3.0 x and 3.6 y), so the
 * separable filter reduces to per-column / per-row lookup tables: each
 * destination pixel blends at most four source texels.
 */
static uint32_t g_outPixels[SLIP_OUT_WIDTH * SLIP_OUT_HEIGHT];
static uint16_t g_filterXIndex[SLIP_OUT_WIDTH];
static uint16_t g_filterXWeight[SLIP_OUT_WIDTH];
static uint16_t g_filterYIndex[SLIP_OUT_HEIGHT];
static uint16_t g_filterYWeight[SLIP_OUT_HEIGHT];
static bool g_filterTablesReady;

static void SlipSdl_BuildFilterAxis(uint16_t *indices, uint16_t *weights, int outSize, int srcSize) {
	const double scale = (double)srcSize / (double)outSize;
	const double halfWidth = 0.5 * scale; /* half a destination pixel, in texels */
	int outIndex;

	for (outIndex = 0; outIndex < outSize; ++outIndex) {
		const double u = ((double)outIndex + 0.5) * scale;
		const double base = u - 0.5;
		const double cell = base >= 0.0 ? (double)(int)base : -1.0;
		const double d = base - cell; /* distance past the lower texel centre */
		double t = (d - (0.5 - halfWidth)) / (2.0 * halfWidth);
		int lower;

		if (t < 0.0) {
			t = 0.0;
		}
		if (t > 1.0) {
			t = 1.0;
		}
		t = t * t * (3.0 - 2.0 * t); /* smoothstep edge profile */
		lower = (int)cell;
		if (lower < 0) {
			lower = 0;
			t = 0.0;
		}
		if (lower >= srcSize - 1) {
			lower = srcSize - 1;
			t = 0.0;
		}
		indices[outIndex] = (uint16_t)lower;
		weights[outIndex] = (uint16_t)(t * 256.0 + 0.5);
	}
}

static uint32_t SlipSdl_LerpArgb(uint32_t a, uint32_t b, uint32_t w) {
	const uint32_t rb = ((a & 0x00ff00ffu) * (256u - w) + (b & 0x00ff00ffu) * w) >> 8;
	const uint32_t g = ((a & 0x0000ff00u) * (256u - w) + (b & 0x0000ff00u) * w) >> 8;

	return 0xff000000u | (rb & 0x00ff00ffu) | (g & 0x0000ff00u);
}

static void SlipSdl_UpscaleBandlimited(void) {
	int outY;

	if (!g_filterTablesReady) {
		SlipSdl_BuildFilterAxis(g_filterXIndex, g_filterXWeight, SLIP_OUT_WIDTH, SLIPSTREAM_SCREEN_WIDTH);
		SlipSdl_BuildFilterAxis(g_filterYIndex, g_filterYWeight, SLIP_OUT_HEIGHT, SLIPSTREAM_SCREEN_HEIGHT);
		g_filterTablesReady = true;
	}
	for (outY = 0; outY < SLIP_OUT_HEIGHT; ++outY) {
		const uint32_t *const row0 = g_presentPixels + (size_t)g_filterYIndex[outY] * SLIPSTREAM_SCREEN_WIDTH;
		const uint32_t *const row1 =
		    g_filterYIndex[outY] + 1 < SLIPSTREAM_SCREEN_HEIGHT ? row0 + SLIPSTREAM_SCREEN_WIDTH : row0;
		const uint32_t wy = g_filterYWeight[outY];
		uint32_t *const out = g_outPixels + (size_t)outY * SLIP_OUT_WIDTH;
		int outX;

		if (wy == 0) {
			for (outX = 0; outX < SLIP_OUT_WIDTH; ++outX) {
				const int sx = g_filterXIndex[outX];
				const uint32_t wx = g_filterXWeight[outX];

				out[outX] = wx == 0 ? row0[sx] : SlipSdl_LerpArgb(row0[sx], row0[sx + 1], wx);
			}
			continue;
		}
		for (outX = 0; outX < SLIP_OUT_WIDTH; ++outX) {
			const int sx = g_filterXIndex[outX];
			const uint32_t wx = g_filterXWeight[outX];
			uint32_t top;
			uint32_t bottom;

			if (wx == 0) {
				top = row0[sx];
				bottom = row1[sx];
			} else {
				top = SlipSdl_LerpArgb(row0[sx], row0[sx + 1], wx);
				bottom = SlipSdl_LerpArgb(row1[sx], row1[sx + 1], wx);
			}
			out[outX] = SlipSdl_LerpArgb(top, bottom, wy);
		}
	}
}

typedef struct SlipSdlPresentContext {
	SDL_Texture *texture;
	SDL_Renderer *renderer;
} SlipSdlPresentContext;

typedef struct SlipSdlStartupIntroContext {
	SlipSdlPresentContext *presentContext;
	bool presenterInstalled;
	HmiSdlOutput *soundOutput;
	bool inputHeld[256];
	bool inputPressed[256];
	bool running;
} SlipSdlStartupIntroContext;

static void SlipSdl_PresentFrame(void *context) {
	SlipSdlPresentContext *const presentContext = (SlipSdlPresentContext *)context;
	SDL_Texture *const texture = presentContext->texture;
	SDL_Renderer *const renderer = presentContext->renderer;
	size_t i;

	SlipVgaDac_Commit();
	SlipVgaDac_RefreshArgbPalette(g_palette);
#ifdef SLIP_REPLAY_HARNESS
	if (SlipUiCapture_Present())
		return;
#endif
	/* The host display replaces the 70 Hz VGA presentation boundary. */
	if (!SlipDebug_fixedClock) {
		static uint64_t previousPresentation;
		const uint64_t frameNanoseconds = SDL_NS_PER_SECOND / 70;
		const uint64_t now = SDL_GetTicksNS();
		if (previousPresentation != 0 && now - previousPresentation < frameNanoseconds)
			SDL_DelayPrecise(frameNanoseconds - (now - previousPresentation));
		previousPresentation = SDL_GetTicksNS();
	}
	if (!g_presentPixelsReady) {
		for (i = 0; i < SLIPSTREAM_SCREEN_WIDTH * SLIPSTREAM_SCREEN_HEIGHT; ++i) {
			g_presentPixels[i] = g_palette[g_displayFramebuffer[i]];
		}
	}
	g_presentPixelsReady = false;

	SlipSdl_UpscaleBandlimited();
	SDL_UpdateTexture(texture, NULL, g_outPixels, SLIP_OUT_WIDTH * (int)sizeof(uint32_t));
	SDL_RenderClear(renderer);
	SDL_RenderTexture(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}

static SlipInputCode SlipSdl_StartupIntroSdlKeyToDosScan(int key) {
	switch (key) {
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		return SLIP_INPUT_SCAN_ENTER;
	case 'a':
	case 'A':
		return SLIP_INPUT_SCAN_A;
	case 'e':
	case 'E':
		return SLIP_INPUT_SCAN_E;
	case 'h':
	case 'H':
		return SLIP_INPUT_SCAN_H;
	case 'l':
	case 'L':
		return SLIP_INPUT_SCAN_L;
	case 'n':
	case 'N':
		return SLIP_INPUT_SCAN_N;
	case 'p':
	case 'P':
		return SLIP_INPUT_SCAN_P;
	case 't':
	case 'T':
		return SLIP_INPUT_SCAN_T;
	case 'v':
	case 'V':
		return SLIP_INPUT_SCAN_V;
	default:
		return SLIP_INPUT_SCAN_NONE;
	}
}

static void SlipSdl_StartupIntroPollSdlEvents(void *context) {
	SlipSdlStartupIntroContext *const introContext = (SlipSdlStartupIntroContext *)context;
	SDL_Event event;

	while (SlipSdl_PollEvent(&event)) {
		SlipInputCode inputCode = SLIP_INPUT_SCAN_NONE;
		bool inputDown = false;

		if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
			introContext->running = false;
			continue;
		}
		if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
			inputCode = SlipSdl_StartupIntroSdlKeyToDosScan(event.key.key);
			inputDown = event.type == SDL_EVENT_KEY_DOWN;
		} else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
			if (event.button.button == SDL_BUTTON_LEFT) {
				inputCode = SLIP_INPUT_MOUSE_LEFT;
			}
			inputDown = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
		}
		if (inputCode >= 0 && inputCode < 256) {
			if (inputDown && !introContext->inputHeld[inputCode]) {
				introContext->inputPressed[inputCode] = true;
			}
			introContext->inputHeld[inputCode] = inputDown;
		}
	}
}

static bool SlipSdl_StartupIntroInputHeld(void *context, SlipInputCode inputCode) {
	SlipSdlStartupIntroContext *const introContext = (SlipSdlStartupIntroContext *)context;

	return inputCode >= 0 && inputCode < 256 && introContext->inputHeld[inputCode];
}

static SlipInputCode SlipSdl_StartupIntroPopInput(void *context) {
	SlipSdlStartupIntroContext *const introContext = (SlipSdlStartupIntroContext *)context;
	int inputCode;

	for (inputCode = 0; inputCode < 256; ++inputCode) {
		if (introContext->inputPressed[inputCode]) {
			introContext->inputPressed[inputCode] = false;
			return (SlipInputCode)inputCode;
		}
	}
	return SLIP_INPUT_SCAN_NONE;
}

static bool SlipSdl_StartupIntroTestAndClearInput(void *context, SlipInputCode inputCode) {
	SlipSdlStartupIntroContext *const introContext = (SlipSdlStartupIntroContext *)context;
	bool pressed;

	if (inputCode < 0 || inputCode >= 256) {
		return false;
	}
	pressed = introContext->inputPressed[inputCode];
	introContext->inputPressed[inputCode] = false;
	return pressed;
}

static bool SlipSdl_StartupIntroIsRunning(void *context) { return ((SlipSdlStartupIntroContext *)context)->running; }

static bool SlipSdl_StartupIntroLockSound(void *context) {
	return HmiSdlOutput_Lock(((SlipSdlStartupIntroContext *)context)->soundOutput);
}

static void SlipSdl_StartupIntroUnlockSound(void *context) {
	(void)HmiSdlOutput_Unlock(((SlipSdlStartupIntroContext *)context)->soundOutput);
}

static bool SlipSdl_RaceLockSound(void *context) { return HmiSdlOutput_Lock((HmiSdlOutput *)context); }

static void SlipSdl_RaceUnlockSound(void *context) { (void)HmiSdlOutput_Unlock((HmiSdlOutput *)context); }

static void SlipSdl_StartupIntroPresentFrame(void *context) {
	SlipSdlStartupIntroContext *const introContext = (SlipSdlStartupIntroContext *)context;

	/* Startup precedes installation of the menu's SDL presentation callback. */
	SlipMenu_PresentFrame();
	if (!introContext->presenterInstalled)
		SlipSdl_PresentFrame(introContext->presentContext);
}

bool SlipSdl_RunStartupIntro(const char *resourcePath) {
	SlipSdlStartupIntroContext *const context = SlipSdl_startupIntroHost->context;
	memset(context->inputHeld, 0, sizeof(context->inputHeld));
	memset(context->inputPressed, 0, sizeof(context->inputPressed));
	return SlipStartupIntro_Run(resourcePath, SlipSdl_startupIntroHost) && context->running;
}

static int SlipSdl_Run(int argc, char **argv) {
	SDL_Window *window = NULL;
	SDL_Renderer *renderer = NULL;
	SDL_Texture *texture = NULL;
	SDL_PropertiesID rendererProperties = 0;
	const char *resPath;
	bool running = true;
	int rendererVSync = SDL_RENDERER_VSYNC_DISABLED;
	int dumpResult;
	SlipSdlPresentContext presentContext;
	SlipSdlStartupIntroContext introContext = {0};
	SlipStartupIntroHost introHost;
	HmiDigitalDriver digitalDriver;
	HmiMixer1000State mixer1000;
	HmiSdlOutput soundOutput;
	SlipGameSoundState gameSound;
	uint8_t digitalDmaBuffer[0x400];

	SlipFrameTimer_InitializeHostRate(0x46u);
	dumpResult = SlipDebug_RunDumpCommand(argc, argv);
	if (dumpResult >= 0) {
		return dumpResult;
	}
#ifdef SLIP_REPLAY_HARNESS
	SlipUiCapture_Configure();
#endif

	if (!SlipSdl_Init()) {
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}

	/*
	 * Mode 13h's 320x200 was displayed on a 4:3 CRT with non-square
	 * pixels (1.2x tall), so the window is 4:3 and the framebuffer is
	 * stretched into a 320x240 logical space, nearest filtered.
	 */
	window = SDL_CreateWindow("Slipstream 5000 SDL Port", SLIP_OUT_WIDTH, SLIP_OUT_HEIGHT, SDL_WINDOW_RESIZABLE);
	if (window == NULL) {
		fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		SlipControllerSdl_Shutdown();
		SDL_Quit();
		return 1;
	}

	rendererProperties = SDL_CreateProperties();
	if (rendererProperties != 0 &&
	    SDL_SetPointerProperty(rendererProperties, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window) &&
	    SDL_SetNumberProperty(rendererProperties, SDL_PROP_RENDERER_CREATE_PRESENT_VSYNC_NUMBER, 1)) {
		renderer = SDL_CreateRendererWithProperties(rendererProperties);
	}
	if (rendererProperties != 0) {
		SDL_DestroyProperties(rendererProperties);
	}
	if (renderer == NULL) {
		renderer = SDL_CreateRenderer(window, NULL);
	}
	if (renderer == NULL) {
		fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
		SDL_DestroyWindow(window);
		SlipControllerSdl_Shutdown();
		SDL_Quit();
		return 1;
	}

	if ((!SDL_GetRenderVSync(renderer, &rendererVSync) || rendererVSync != 1) && !SDL_SetRenderVSync(renderer, 1)) {
		fprintf(stderr, "SDL_SetRenderVSync failed: %s\n", SDL_GetError());
	}
	if (SDL_GetRenderVSync(renderer, &rendererVSync)) {
		fprintf(stderr, "SDL renderer=%s vsync=%d\n", SDL_GetRendererName(renderer), rendererVSync);
	}
	SDL_SetRenderLogicalPresentation(renderer, SLIP_OUT_WIDTH, SLIP_OUT_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SLIP_OUT_WIDTH,
	                            SLIP_OUT_HEIGHT);
	if (texture != NULL) {
		SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
	}
	if (texture == NULL) {
		fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SlipControllerSdl_Shutdown();
		SDL_Quit();
		return 1;
	}

	HmiDigitalDriver_Reset(&digitalDriver, 0xe015u);
	HmiMixer1000_Initialize(&mixer1000, &digitalDriver, digitalDmaBuffer, sizeof(digitalDmaBuffer), 1);
	if (!HmiSdlOutput_Open(&soundOutput, &mixer1000, 0x2b11u, 0x3cu)) {
		fprintf(stderr, "SDL audio output failed: %s\n", SDL_GetError());
		SDL_DestroyTexture(texture);
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SlipControllerSdl_Shutdown();
		SDL_Quit();
		return 1;
	}
	SlipGameSound_Reset(&gameSound, &digitalDriver);
	gameSound.initialized = 1;
	SlipSoundEffects_Install();
	gameSound.digitalCard = 0xe015u;
	SlipRaceSession_BindSoundHost(&gameSound, 0u, SlipSdl_RaceLockSound, SlipSdl_RaceUnlockSound, &soundOutput);
	SlipMenu_BindSoundHost(&gameSound, SlipSdl_RaceLockSound, SlipSdl_RaceUnlockSound, &soundOutput);

	resPath = SlipMenu_FindResPath(argc, argv);
	if (resPath == NULL)
		resPath = SlipGameData_SelectFile(window);
	if (resPath == NULL) {
		HmiSdlOutput_Close(&soundOutput);
		SDL_DestroyTexture(texture);
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SlipControllerSdl_Shutdown();
		SDL_Quit();
		return 0;
	}
	SlipSavedGamesHost_ImportLegacySave(resPath);
	SlipVgaDac_InitializeHostBiosDefaults();
	SlipVgaDac_RefreshArgbPalette(g_palette);

	if (resPath != NULL) {
		if (!SlipConfigHost_Install(resPath))
			return 1;

		if (!SlipMenuMusic_Open(resPath, &gameSound))
			fprintf(stderr, "Menu music initialization failed: %s\n", SDL_GetError());
		SlipConfigHost_LoadConfiguration();
	}
	presentContext.texture = texture;
	presentContext.renderer = renderer;
	introContext.presentContext = &presentContext;
	introContext.soundOutput = &soundOutput;
	introContext.running = true;
	introHost.pollEvents = SlipSdl_StartupIntroPollSdlEvents;
	introHost.inputHeld = SlipSdl_StartupIntroInputHeld;
	introHost.popInput = SlipSdl_StartupIntroPopInput;
	introHost.testAndClearInput = SlipSdl_StartupIntroTestAndClearInput;
	introHost.presentFrame = SlipSdl_StartupIntroPresentFrame;
	introHost.isRunning = SlipSdl_StartupIntroIsRunning;
	introHost.sound = &gameSound;
	introHost.lockSound = SlipSdl_StartupIntroLockSound;
	introHost.unlockSound = SlipSdl_StartupIntroUnlockSound;
	introHost.context = &introContext;
	SlipSdl_startupIntroHost = &introHost;
	if (!SlipStartupIntro_Run(resPath, &introHost) || !introContext.running) {
		HmiSdlOutput_Close(&soundOutput);
		SDL_DestroyTexture(texture);
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SlipControllerSdl_Shutdown();
		SDL_Quit();
		return introContext.running ? 1 : 0;
	}
	SlipMenu_Init(resPath, window, renderer, SlipSdl_PresentFrame, &presentContext);
	introContext.presenterInstalled = true;

	while (running) {
		SDL_Event event;

		while (SlipSdl_PollEvent(&event)) {
			SlipMenu_HandleEvent(resPath, window, renderer, &event, &running);
		}
		if (!running) {
			break;
		}

		if (!SlipMenu_UpdateAndDraw(resPath, window)) {
			break;
		}
	}

	HmiSdlOutput_Close(&soundOutput);
	SlipMenuMusic_Close();
	SlipRaceSession_BindSoundHost(NULL, 0u, NULL, NULL, NULL);
	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SlipControllerSdl_Shutdown();
	SDL_Quit();
	return 0;
}

int main(int argc, char **argv) { return SlipSdl_Run(argc, argv); }
