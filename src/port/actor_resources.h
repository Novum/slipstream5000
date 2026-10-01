#ifndef SLIPSTREAM5000_ACTOR_RESOURCES_H
#define SLIPSTREAM5000_ACTOR_RESOURCES_H
#include <stdbool.h>
#include <stdint.h>

typedef struct SlipActorResourceCalls {
	void *context;
	const uint8_t *(*lock)(void *, uint16_t);
	void (*unlock)(void *, uint16_t);
	int (*load)(void *, const char *, uint32_t *);
	int (*find)(void *, const char *, uint32_t *);
	void (*release)(void *, uint32_t);
} SlipActorResourceCalls;

extern const uint8_t *SlipActorResources_preloadPayload;
extern const uint8_t *SlipActorResources_releasePayload;
void SlipActor_PreloadResources(uint16_t, const SlipActorResourceCalls *);
void SlipActor_ReleaseResources(uint16_t, const SlipActorResourceCalls *);
void SlipActor_PreloadNode(const uint8_t *, const SlipActorResourceCalls *);
void SlipActor_ReleaseNode(const uint8_t *, const SlipActorResourceCalls *);
extern const SlipActorResourceCalls SlipActorHost_resourceCalls;
#endif
