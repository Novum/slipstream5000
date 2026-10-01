#include "material_lookup.h"
#include "runtime.h"
#include <string.h>

bool SlipMaterial_Find(const SlipMaterialResidency *state, const char *name, char key[16], uint16_t *index) {
	if (state->resource == 0)
		SlipRuntime_Fatal("Draw3DGetMaterialNumber - No Materials are set");
	unsigned copied = 0;
	while (copied < 16) {
		uint8_t character = (uint8_t)*name++;
		if (character >= 'a' && character <= 'z')
			character -= 0x20;
		if (character == 0)
			break;
		key[copied++] = (char)character;
	}
	while (copied < 16)
		key[copied++] = ' ';
	const SlipDraw3DMaterialRecord *record = state->table->records;
	uint16_t remaining = (uint16_t)state->table->count;
	*index = 0;
	do {
		if (memcmp(record->name, key, 16) == 0)
			return true;
		++record;
		*index = (uint16_t)(*index + 1u);
	} while (--remaining != 0);
	return false;
}
