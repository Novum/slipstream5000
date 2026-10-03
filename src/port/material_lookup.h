#ifndef SLIPSTREAM5000_MATERIAL_LOOKUP_H
#define SLIPSTREAM5000_MATERIAL_LOOKUP_H
#include "material_residency.h"
bool SlipMaterial_Find(const SlipMaterialResidency *, const char *, char key[SLIP_DRAW3D_MATERIAL_KEY_BYTES],
                       uint16_t *index);
#endif
