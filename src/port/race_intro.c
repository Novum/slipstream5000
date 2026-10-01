#include "race_intro.h"
#include "byte_order.h"
#include "resource_host.h"

#include <string.h>

/* ANN commands use unaligned little-endian readers; caption tags follow the
 * original byte-swap sequence below. */
static uint32_t SlipRaceIntro_CaptionTag(const uint8_t *p) {

	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static bool SlipRaceIntro_CommandSize(const uint8_t *script, size_t bytes, size_t cursor, size_t *size) {
	static const uint8_t sizes[] = {10, 4, 8, 0, 4, 6, 6};
	uint16_t opcode;
	if (cursor > bytes || bytes - cursor < 2)
		return false;
	opcode = SlipBytes_ReadLE16(script + cursor);
	if (opcode >= sizeof(sizes))
		return false;
	*size = sizes[opcode];
	if (bytes - cursor < *size)
		return false;
	if (opcode == SLIP_INTRO_INLINE) {
		*size += SlipBytes_ReadLE16(script + cursor + 2);
		if (bytes - cursor < *size)
			return false;
	}
	return true;
}

static bool SlipRaceIntro_PreloadInternal(uint8_t *script, size_t scriptBytes, uint16_t languageIndex,
                                          uint32_t availableMemory, const SlipRaceIntroResources *resources,
                                          uint16_t scriptResource) {
	static const char speechLanguagePrefixes[] = {'E', 'F', 'G'};
	uint32_t total = 0;
	if (script == NULL || resources == NULL || languageIndex >= 3 || resources->sampleSize == NULL ||
	    resources->load == NULL)
		return false;

	for (unsigned pass = 0; pass < 2; ++pass) {
		if (scriptResource != 0)
			script = SlipResourceHost_LockWritable(NULL, scriptResource);
		size_t cursor = 16;
		for (;;) {
			size_t size;
			if (!SlipRaceIntro_CommandSize(script, scriptBytes, cursor, &size))
				return false;
			if (size == 0)
				break;
			if (SlipBytes_ReadLE16(script + cursor) == SLIP_INTRO_SPEECH) {

				char name[9] = "????.SMP";
				memcpy(name, script + cursor + 2, 4);
				name[0] = speechLanguagePrefixes[languageIndex];
				if (pass == 0) {
					uint32_t sampleBytes;
					if (!resources->sampleSize(resources->context, name, &sampleBytes))
						return false;
					total += sampleBytes;
					script[cursor + 6] = script[cursor + 7] = 0;
				} else {
					uint16_t handle;
					if (!resources->load(resources->context, name, &handle))
						return false;
					script[cursor + 6] = (uint8_t)handle;
					script[cursor + 7] = (uint8_t)(handle >> 8);
				}
			}
			cursor += size;
		}
		if (scriptResource != 0) {
			SlipResourceHost_Unlock(NULL, scriptResource);
			if (pass == 0)
				availableMemory = SlipResource_freeBytes + SlipResource_cachedBytes;
		}

		if (pass == 0 && availableMemory < (uint32_t)(total + 0x110000u))
			return true;
	}
	return true;
}

bool SlipRaceIntro_Preload(uint8_t *script, size_t bytes, uint16_t language, uint32_t availableMemory,
                           const SlipRaceIntroResources *resources) {
	return SlipRaceIntro_PreloadInternal(script, bytes, language, availableMemory, resources, 0);
}

bool SlipRaceIntro_PreloadResource(uint16_t resource, uint16_t language, const SlipRaceIntroResources *resources) {
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	return SlipRaceIntro_PreloadInternal(payload.data, payload.size, language, 0, resources, resource);
}

bool SlipRaceIntro_Release(const uint8_t *script, size_t scriptBytes, const SlipRaceIntroResources *resources) {
	size_t cursor = 16;
	if (script == NULL || resources == NULL || resources->release == NULL)
		return false;
	for (;;) {
		size_t size;
		if (!SlipRaceIntro_CommandSize(script, scriptBytes, cursor, &size))
			return false;
		if (size == 0)
			return true;

		if (SlipBytes_ReadLE16(script + cursor) == SLIP_INTRO_SPEECH && SlipBytes_ReadLE16(script + cursor + 6) != 0)
			resources->release(resources->context, SlipBytes_ReadLE16(script + cursor + 6));
		cursor += size;
	}
}

bool SlipRaceIntro_ReleaseResource(uint16_t resource, const SlipRaceIntroResources *resources) {
	const uint8_t *const bytes = SlipResourceHost_Lock(NULL, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	if (!SlipRaceIntro_Release(bytes, payload.size, resources))
		return false;
	SlipResourceHost_Unlock(NULL, resource);
	return true;
}

SlipRaceIntroScriptResult SlipRaceIntro_Step(SlipRaceIntroScript *state, const uint8_t *script, size_t scriptBytes,
                                             uint16_t delta, const SlipRaceIntroScriptHost *host) {
	size_t cursor;
	if (state == NULL || script == NULL || host == NULL)
		return SLIP_RACE_INTRO_SCRIPT_INVALID;

	if (state->delay != 0) {
		const uint16_t remaining = (uint16_t)(state->delay - delta);
		state->delay = (int16_t)remaining < 0 ? 0 : remaining;
		return SLIP_RACE_INTRO_SCRIPT_YIELD;
	}
	cursor = state->cursor;
	for (;;) {
		const uint8_t *command;
		uint16_t opcode;
		size_t size;
		if (cursor > scriptBytes || scriptBytes - cursor < 2)
			return SLIP_RACE_INTRO_SCRIPT_INVALID;
		command = script + cursor;
		opcode = SlipBytes_ReadLE16(command);

		switch (opcode) {
		case SLIP_INTRO_WAIT_TRACK:
			size = 10;
			break;
		case SLIP_INTRO_DELAY:
			size = 4;
			break;
		case SLIP_INTRO_SPEECH:
			size = 8;
			break;
		case SLIP_INTRO_END:
			size = 2;
			break;
		case SLIP_INTRO_INLINE:
			size = 4;
			break;
		case SLIP_INTRO_CAPTION:
			size = 6;
			break;
		case SLIP_INTRO_WAIT_PROGRESS:
			size = 6;
			break;
		default:
			return SLIP_RACE_INTRO_SCRIPT_FINISHED;
		}
		if (scriptBytes - cursor < size)
			return SLIP_RACE_INTRO_SCRIPT_INVALID;
		switch (opcode) {
		case SLIP_INTRO_INLINE:

			size += SlipBytes_ReadLE16(command + 2);
			if (scriptBytes - cursor < size)
				return SLIP_RACE_INTRO_SCRIPT_INVALID;
			cursor += size;
			continue;
		case SLIP_INTRO_SPEECH:
		case SLIP_INTRO_END:

			if (state->voice != 0) {
				if (host->isStopped == NULL)
					return SLIP_RACE_INTRO_SCRIPT_INVALID;
				if (host->isStopped(host->context, state->voice) == 0) {
					state->cursor = cursor;
					return SLIP_RACE_INTRO_SCRIPT_YIELD;
				}
				state->voice = 0;
			}
			if (opcode == SLIP_INTRO_END)
				return SLIP_RACE_INTRO_SCRIPT_FINISHED;
			/* Fall through: both 2 and 5 publish a caption and reset the HUD. */
		case SLIP_INTRO_CAPTION:
			if (host->resetConsole == NULL)
				return SLIP_RACE_INTRO_SCRIPT_INVALID;
			state->caption = SlipRaceIntro_CaptionTag(command + 2);
			host->resetConsole(host->context);
			if (opcode == SLIP_INTRO_SPEECH && SlipBytes_ReadLE16(command + 6) != 0) {

				if (host->play == NULL)
					return SLIP_RACE_INTRO_SCRIPT_INVALID;
				state->voice = host->play(host->context, SlipBytes_ReadLE16(command + 6), NULL);
			}
			cursor += size;
			continue;
		case SLIP_INTRO_DELAY:

			state->delay = SlipBytes_ReadLE16(command + 2);
			cursor += 4;
			break;
		case SLIP_INTRO_WAIT_PROGRESS: {
			int32_t progress;

			if (host->raceProgress == NULL || !host->raceProgress(host->context, &progress))
				return SLIP_RACE_INTRO_SCRIPT_INVALID;
			if (progress <= (int32_t)SlipBytes_ReadLE32(command + 2))
				cursor += 6;
			break;
		}
		case SLIP_INTRO_WAIT_TRACK: {
			uint8_t name[8];

			if (host->trackName == NULL || !host->trackName(host->context, name))
				return SLIP_RACE_INTRO_SCRIPT_INVALID;
			if (memcmp(command + 2, name, sizeof(name)) == 0)
				cursor += 10;
			break;
		}
		}

		state->cursor = cursor;
		return SLIP_RACE_INTRO_SCRIPT_YIELD;
	}
}

SlipRaceIntroScriptResult SlipRaceIntro_StepPresenter(SlipRaceIntroScript *state, const uint8_t *script, size_t bytes,
                                                      uint16_t delta, const SlipRaceIntroScriptHost *host,
                                                      uint16_t language, void (*queue)(void *, const uint8_t *, size_t),
                                                      uint32_t *speechBytes) {
	if (state == NULL || script == NULL || host == NULL)
		return SLIP_RACE_INTRO_SCRIPT_INVALID;

	if (state->delay != 0) {
		const uint16_t delay = (uint16_t)(state->delay - delta);
		state->delay = (int16_t)delay < 0 ? 0 : delay;
		return SLIP_RACE_INTRO_SCRIPT_YIELD;
	}
	size_t cursor = state->cursor;
	for (;;) {
		size_t size;
		if (cursor > bytes || bytes - cursor < 2)
			return SLIP_RACE_INTRO_SCRIPT_INVALID;
		const uint16_t opcode = SlipBytes_ReadLE16(script + cursor);
		if (opcode > SLIP_INTRO_WAIT_PROGRESS)
			return SLIP_RACE_INTRO_SCRIPT_FINISHED;
		if (!SlipRaceIntro_CommandSize(script, bytes, cursor, &size))
			return SLIP_RACE_INTRO_SCRIPT_INVALID;
		const uint8_t *const command = script + cursor;
		switch (opcode) {
		case SLIP_INTRO_END:
			if (state->voice != 0) {
				if (host->isStopped == NULL)
					return SLIP_RACE_INTRO_SCRIPT_INVALID;
				if (!host->isStopped(host->context, state->voice)) {
					state->cursor = cursor;
					return SLIP_RACE_INTRO_SCRIPT_YIELD;
				}
				state->voice = 0;
			}
			return SLIP_RACE_INTRO_SCRIPT_FINISHED;
		case SLIP_INTRO_INLINE:
			if (language == 0) {
				if (queue == NULL)
					return SLIP_RACE_INTRO_SCRIPT_INVALID;
				queue(host->context, command + 4, SlipBytes_ReadLE16(command + 2));
			}
			cursor += size;
			break;
		case SLIP_INTRO_CAPTION:
		case SLIP_INTRO_SPEECH:
			state->caption = SlipRaceIntro_CaptionTag(command + 2);
			if (host->resetConsole == NULL)
				return SLIP_RACE_INTRO_SCRIPT_INVALID;
			host->resetConsole(host->context);
			if (opcode == SLIP_INTRO_SPEECH && SlipBytes_ReadLE16(command + 6) != 0) {
				if (host->play == NULL)
					return SLIP_RACE_INTRO_SCRIPT_INVALID;
				state->voice = host->play(host->context, SlipBytes_ReadLE16(command + 6), speechBytes);
			}
			cursor += size;
			break;
		case SLIP_INTRO_DELAY:
			state->delay = SlipBytes_ReadLE16(command + 2);
			state->cursor = cursor + 4;
			return SLIP_RACE_INTRO_SCRIPT_YIELD;
		case SLIP_INTRO_WAIT_TRACK:
		case SLIP_INTRO_WAIT_PROGRESS:
			return SLIP_RACE_INTRO_SCRIPT_INVALID;
		}
	}
}
