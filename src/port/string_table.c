#include "string_table.h"
#include "byte_order.h"
#include "runtime.h"

#include <stdint.h>
#include <string.h>

SlipStringTableState SlipStringTable_state = {.filename = "PAUSED  .ST0"};

void SlipStringTable_SetLanguage(SlipStringTableState *state, uint8_t language) { state->language = language; }

bool SlipStringTable_Load(SlipStringTableState *state, const char name[SLIP_RESOURCE_BASE_NAME_BYTES],
                          const SlipStringTableResources *resources, SlipStringTableSlot **slot) {
	memcpy(state->filename, name, SLIP_RESOURCE_BASE_NAME_BYTES);
	state->filename[SLIP_RESOURCE_NAME_BYTES - 1] = (char)(uint8_t)(state->language + '0');
	uint16_t resource;
	if (!resources->load(resources->context, state->filename, &resource))
		return false;

	for (unsigned i = 0; i < SLIP_STRING_TABLE_SLOT_COUNT; ++i) {
		if (state->slots[i].resource == 0) {
			state->slots[i].resource = resource;
			state->slots[i].locks = 0;
			*slot = &state->slots[i];
			return true;
		}
	}
	SlipRuntime_error = SLIP_RUNTIME_ERROR_CAPACITY_EXHAUSTED;
	return false;
}

void SlipStringTable_Release(SlipStringTableSlot *slot, const SlipStringTableResources *resources) {
	const uint16_t resource = slot->resource;
	slot->resource = 0;
	if (slot->locks != 0)
		resources->unlock(resources->context, resource);
	resources->release(resources->context, resource);
}

const char *SlipStringTable_Get(SlipStringTableSlot *slot, uint32_t tag, const SlipStringTableResources *resources) {
	if (slot->locks == 0)
		slot->data = resources->lock(resources->context, slot->resource);
	slot->locks = (uint16_t)(slot->locks + 1);
	const uint8_t *entry = slot->data;
	for (;;) {
		/* Serialized ST records contain four tag bytes, a little-endian
		 * length word, and the text. Runtime slot fields remain typed. */
		const uint32_t entryTag =
		    (uint32_t)entry[0] << 24 | (uint32_t)entry[1] << 16 | (uint32_t)entry[2] << 8 | entry[3];
		if (entryTag == UINT32_MAX)
			SlipRuntime_Fatal("StrTabGet: String not found.");
		if (entryTag == tag)
			return (const char *)(entry + SLIP_STRING_TABLE_TEXT_OFFSET);
		entry += SlipBytes_ReadLE16(entry + SLIP_STRING_TABLE_LENGTH_OFFSET) + SLIP_STRING_TABLE_TEXT_OFFSET;
	}
}

void SlipStringTable_Unlock(SlipStringTableSlot *slot, const SlipStringTableResources *resources) {
	slot->locks = (uint16_t)(slot->locks - 1);
	if (slot->locks == 0)
		resources->unlock(resources->context, slot->resource);
}

int SlipStringTable_FindText(const SlipResourcePayload *payload, const char tag[SLIP_STRING_TABLE_TAG_BYTES], char *dst,
                             size_t dstSize) {
	size_t pos = 0;

	if (payload == NULL || payload->data == NULL || dst == NULL || dstSize == 0) {
		return 0;
	}

	while (pos + SLIP_STRING_TABLE_TEXT_OFFSET <= payload->size) {
		uint16_t len;
		size_t copyLen;

		if (payload->data[pos + 0] == UINT8_MAX && payload->data[pos + 1] == UINT8_MAX &&
		    payload->data[pos + 2] == UINT8_MAX && payload->data[pos + 3] == UINT8_MAX) {
			break;
		}

		len = SlipBytes_ReadLE16(payload->data + pos + SLIP_STRING_TABLE_LENGTH_OFFSET);
		pos += SLIP_STRING_TABLE_TEXT_OFFSET;
		if (len == 0 || pos + len > payload->size) {
			break;
		}

		if (memcmp(payload->data + pos - SLIP_STRING_TABLE_TEXT_OFFSET, tag, SLIP_STRING_TABLE_TAG_BYTES) == 0) {
			copyLen = len;
			if (payload->data[pos + copyLen - 1] == '\0') {
				--copyLen;
			}
			if (copyLen >= dstSize) {
				copyLen = dstSize - 1;
			}
			memcpy(dst, payload->data + pos, copyLen);
			dst[copyLen] = '\0';
			return 1;
		}

		pos += len;
	}

	return 0;
}
