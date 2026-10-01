#ifndef SLIPSTREAM5000_ACTOR_OBJECT_HOST_H
#define SLIPSTREAM5000_ACTOR_OBJECT_HOST_H
#include "actor_transform.h"
extern SlipView3DMatrix SlipObject_matrixCopy;

void SlipActorObject_Attach(void *, uint16_t object, SlipActorRecord *);
SlipActorRecord *SlipActorObject_Get(void *, uint16_t object);
SlipView3DVec32 SlipActorObject_Position(void *, uint16_t object);
SlipView3DVec32 SlipActorObject_ViewPosition(void *, uint16_t object);
const SlipView3DMatrix *SlipActorObject_Matrix(void *, uint16_t object);
extern const SlipActorAccessCalls SlipActorObject_access;
extern const SlipActorTransformCalls SlipActorObject_transform;
#endif
