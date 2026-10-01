#include "game_data.h"

#include <ctype.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

enum { GAME_DATA_PATH_CAPACITY = 4096, FILE_DIALOG_POLL_INTERVAL_MS = 10 };

static const char *const steamManifest = "steamapps/appmanifest_306350.acf";
static const char *const resourceFile = "SLIPSTRM.RES";
static char resourcePath[GAME_DATA_PATH_CAPACITY];

typedef enum GameDataToken {
	GAME_DATA_END,
	GAME_DATA_STRING,
	GAME_DATA_OPEN,
	GAME_DATA_CLOSE,
	GAME_DATA_INVALID
} GameDataToken;

/* Steam's text files use quoted KeyValues strings, braces and // comments. */
static GameDataToken SlipGameData_ReadToken(const char **cursor, char *text, size_t capacity) {

	const char *p = *cursor;
	for (;;) {
		while (isspace((unsigned char)*p))
			++p;
		if (p[0] != '/' || p[1] != '/')
			break;
		while (*p != '\0' && *p != '\n')
			++p;
	}
	if (*p == '\0') {
		*cursor = p;
		return GAME_DATA_END;
	}
	if (*p == '{' || *p == '}') {
		const GameDataToken token = *p == '{' ? GAME_DATA_OPEN : GAME_DATA_CLOSE;
		*cursor = p + 1;
		return token;
	}
	const bool quoted = *p == '"';
	if (quoted)
		++p;
	size_t length = 0;
	while (*p != '\0' && (quoted ? *p != '"' : !isspace((unsigned char)*p) && *p != '{' && *p != '}')) {
		char value = *p++;
		if (quoted && value == '\\' && (*p == '\\' || *p == '"'))
			value = *p++;
		if (length + 1 >= capacity)
			return GAME_DATA_INVALID;
		text[length++] = value;
	}
	if (quoted) {
		if (*p != '"')
			return GAME_DATA_INVALID;
		++p;
	}
	text[length] = '\0';
	*cursor = p;
	return GAME_DATA_STRING;
}

static bool SlipGameData_Join(char *path, size_t capacity, const char *directory, const char *relative) {

	const int length = SDL_snprintf(path, capacity, "%s/%s", directory, relative);
	return length >= 0 && (size_t)length < capacity;
}

static bool SlipGameData_FileExists(const char *path) {

	SDL_IOStream *const file = SDL_IOFromFile(path, "rb");
	if (file == NULL)
		return false;
	SDL_CloseIO(file);
	return true;
}

static bool SlipGameData_FindInDirectory(const char *directory, char *path, size_t capacity) {

	return SlipGameData_Join(path, capacity, directory, resourceFile) && SlipGameData_FileExists(path);
}

static bool SlipGameData_TrySteamLibrary(const char *library, char *path, size_t capacity) {

	char manifestPath[GAME_DATA_PATH_CAPACITY];
	if (!SlipGameData_Join(manifestPath, sizeof(manifestPath), library, steamManifest))
		return false;
	char *const manifest = SDL_LoadFile(manifestPath, NULL);
	if (manifest == NULL)
		return false;
	const char *cursor = manifest;
	char key[GAME_DATA_PATH_CAPACITY], value[GAME_DATA_PATH_CAPACITY];
	char installDirectory[GAME_DATA_PATH_CAPACITY] = "";
	bool validApp = false;
	bool valid = SlipGameData_ReadToken(&cursor, key, sizeof(key)) == GAME_DATA_STRING &&
	             SDL_strcasecmp(key, "AppState") == 0 &&
	             SlipGameData_ReadToken(&cursor, value, sizeof(value)) == GAME_DATA_OPEN;
	unsigned depth = 1;
	while (valid && depth != 0) {
		const GameDataToken token = SlipGameData_ReadToken(&cursor, key, sizeof(key));
		if (token == GAME_DATA_CLOSE) {
			--depth;
			continue;
		}
		if (token != GAME_DATA_STRING) {
			valid = false;
			break;
		}
		const GameDataToken next = SlipGameData_ReadToken(&cursor, value, sizeof(value));
		if (next == GAME_DATA_OPEN) {
			++depth;
			continue;
		}
		if (next != GAME_DATA_STRING) {
			valid = false;
			break;
		}
		if (depth == 1 && SDL_strcasecmp(key, "appid") == 0)
			validApp = strcmp(value, "306350") == 0;
		if (depth == 1 && SDL_strcasecmp(key, "installdir") == 0)
			SDL_strlcpy(installDirectory, value, sizeof(installDirectory));
	}
	SDL_free(manifest);
	if (!valid || !validApp || installDirectory[0] == '\0')
		return false;
	char commonPath[GAME_DATA_PATH_CAPACITY], gamePath[GAME_DATA_PATH_CAPACITY];
	return SlipGameData_Join(commonPath, sizeof(commonPath), library, "steamapps/common") &&
	       SlipGameData_Join(gamePath, sizeof(gamePath), commonPath, installDirectory) &&
	       SlipGameData_FindInDirectory(gamePath, path, capacity);
}

bool SlipGameData_FindInSteam(const char *steamDirectory, char *path, size_t capacity) {

	if (SlipGameData_TrySteamLibrary(steamDirectory, path, capacity))
		return true;
	const char *const configurations[] = {"config/libraryfolders.vdf", "steamapps/libraryfolders.vdf"};
	for (size_t config = 0; config < SDL_arraysize(configurations); ++config) {
		char configPath[GAME_DATA_PATH_CAPACITY];
		if (!SlipGameData_Join(configPath, sizeof(configPath), steamDirectory, configurations[config]))
			continue;
		char *const contents = SDL_LoadFile(configPath, NULL);
		if (contents == NULL)
			continue;
		const char *cursor = contents;
		char key[GAME_DATA_PATH_CAPACITY], value[GAME_DATA_PATH_CAPACITY];
		bool found = false;
		bool valid = SlipGameData_ReadToken(&cursor, key, sizeof(key)) == GAME_DATA_STRING &&
		             SDL_strcasecmp(key, "libraryfolders") == 0 &&
		             SlipGameData_ReadToken(&cursor, value, sizeof(value)) == GAME_DATA_OPEN;
		unsigned depth = 1;
		while (valid && depth != 0 && !found) {
			const GameDataToken token = SlipGameData_ReadToken(&cursor, key, sizeof(key));
			if (token == GAME_DATA_CLOSE) {
				--depth;
				continue;
			}
			if (token != GAME_DATA_STRING)
				break;
			const GameDataToken next = SlipGameData_ReadToken(&cursor, value, sizeof(value));
			if (next == GAME_DATA_OPEN) {
				++depth;
				continue;
			}
			if (next != GAME_DATA_STRING)
				break;
			if ((depth == 2 && SDL_strcasecmp(key, "path") == 0) || (depth == 1 && isdigit((unsigned char)key[0])))
				found = SlipGameData_TrySteamLibrary(value, path, capacity);
		}
		SDL_free(contents);
		if (found)
			return true;
	}
	return false;
}

#ifdef _WIN32
static bool SlipGameData_ReadRegistry(HKEY root, const wchar_t *keyPath, const wchar_t *valueName, REGSAM view,
                                      char *path, size_t capacity) {

	HKEY key;
	if (RegOpenKeyExW(root, keyPath, 0, KEY_QUERY_VALUE | view, &key) != ERROR_SUCCESS)
		return false;
	wchar_t value[GAME_DATA_PATH_CAPACITY];
	DWORD bytes = sizeof(value) - sizeof(value[0]);
	DWORD type = 0;
	const LSTATUS result = RegQueryValueExW(key, valueName, NULL, &type, (BYTE *)value, &bytes);
	RegCloseKey(key);
	if (result != ERROR_SUCCESS || type != REG_SZ || bytes % sizeof(value[0]) != 0)
		return false;
	value[bytes / sizeof(value[0])] = L'\0';
	return WideCharToMultiByte(CP_UTF8, 0, value, -1, path, (int)capacity, NULL, NULL) != 0;
}
#endif

const char *SlipGameData_FindInstalled(void) {

#ifdef _WIN32
	char installPath[GAME_DATA_PATH_CAPACITY];
	if (SlipGameData_ReadRegistry(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", 0, installPath,
	                              sizeof(installPath)) &&
	    SlipGameData_FindInSteam(installPath, resourcePath, sizeof(resourcePath)))
		return resourcePath;
	const REGSAM registryViews[] = {KEY_WOW64_32KEY, KEY_WOW64_64KEY};
	for (size_t view = 0; view < SDL_arraysize(registryViews); ++view) {
		if (SlipGameData_ReadRegistry(HKEY_LOCAL_MACHINE, L"SOFTWARE\\GOG.com\\Games\\1207658953", L"path",
		                              registryViews[view], installPath, sizeof(installPath)) &&
		    SlipGameData_FindInDirectory(installPath, resourcePath, sizeof(resourcePath)))
			return resourcePath;
	}
#else
	const struct passwd *const user = getpwuid(getuid());
	const char *const home = user != NULL ? user->pw_dir : SDL_getenv("HOME");
	if (home != NULL) {
		const char *const steamLocations[] = {
#ifdef __APPLE__
		    "Library/Application Support/Steam",
#else
		    ".steam/steam",
		    ".local/share/Steam",
		    ".var/app/com.valvesoftware.Steam/.steam/steam",
		    ".var/app/com.valvesoftware.Steam/.local/share/Steam",
#endif
		};
		for (size_t location = 0; location < SDL_arraysize(steamLocations); ++location) {
			char steamPath[GAME_DATA_PATH_CAPACITY];
			if (SlipGameData_Join(steamPath, sizeof(steamPath), home, steamLocations[location]) &&
			    SlipGameData_FindInSteam(steamPath, resourcePath, sizeof(resourcePath)))
				return resourcePath;
		}
	}
#endif
	return NULL;
}

typedef struct GameDataSelection {
	SDL_AtomicInt complete;
	bool failed;
	char path[GAME_DATA_PATH_CAPACITY];
} GameDataSelection;

static void SDLCALL SlipGameData_FileSelected(void *context, const char *const *files, int filter) {

	(void)filter;
	GameDataSelection *const selection = context;
	selection->failed = files == NULL;
	if (files != NULL && files[0] != NULL) {
		if (SDL_strlcpy(selection->path, files[0], sizeof(selection->path)) >= sizeof(selection->path)) {
			selection->path[0] = '\0';
			selection->failed = true;
		}
	}
	SDL_SetAtomicInt(&selection->complete, 1);
}

const char *SlipGameData_SelectFile(SDL_Window *window) {

	static const SDL_DialogFileFilter filters[] = {{"Slipstream 5000 resources", "res;RES"}, {"All files", "*"}};
	GameDataSelection selection = {0};
	SDL_ShowOpenFileDialog(SlipGameData_FileSelected, &selection, window, filters, SDL_arraysize(filters), NULL, false);
	while (!SDL_GetAtomicInt(&selection.complete)) {
		SDL_PumpEvents();
		SDL_Delay(FILE_DIALOG_POLL_INTERVAL_MS);
	}
	if (selection.failed) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Slipstream 5000", "Could not select the game data file.",
		                         window);
		return NULL;
	}
	if (selection.path[0] == '\0')
		return NULL;
	if (!SlipGameData_FileExists(selection.path)) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Slipstream 5000", "The selected file could not be opened.",
		                         window);
		return NULL;
	}
	SDL_strlcpy(resourcePath, selection.path, sizeof(resourcePath));
	return resourcePath;
}
