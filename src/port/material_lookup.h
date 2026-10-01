#ifndef SLIPSTREAM5000_MATERIAL_LOOKUP_H
#define SLIPSTREAM5000_MATERIAL_LOOKUP_H
#include "material_residency.h"
bool SlipMaterial_Find(const SlipMaterialResidency *, const char *, char key[16], uint16_t *index);
#endif
