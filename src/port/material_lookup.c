#include "material_lookup.h"
#include "runtime.h"
#include <string.h>

bool SlipMaterial_Find(const SlipMaterialResidency *state, const char *name, char key[SLIP_DRAW3D_MATERIAL_KEY_BYTES],
                       uint16_t *index) {
	if (state->resource == 0)
		SlipRuntime_Fatal("Draw3DGetMaterialNumber - No Materials are set");
	unsigned copied = 0;
	while (copied < SLIP_DRAW3D_MATERIAL_KEY_BYTES) {
		uint8_t character = (uint8_t)*name++;
		if (character >= 'a' && character <= 'z')
			character -= 'a' - 'A';
		if (character == 0)
			break;
		key[copied++] = (char)character;
	}
	while (copied < SLIP_DRAW3D_MATERIAL_KEY_BYTES)
		key[copied++] = ' ';
	const SlipDraw3DMaterialRecord *record = state->table->records;
	uint16_t remaining = (uint16_t)state->table->count;
	*index = 0;
	do {
		if (memcmp(record->name, key, SLIP_DRAW3D_MATERIAL_KEY_BYTES) == 0)
			return true;
		++record;
		*index = (uint16_t)(*index + 1u);
	} while (--remaining != 0);
	return false;
}
