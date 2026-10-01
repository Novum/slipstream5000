#ifndef SLIPSTREAM5000_TRACK_ASSETS_H
#define SLIPSTREAM5000_TRACK_ASSETS_H

#include "artic_slot.h"
#include "resource.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct SlipTrackAssetCalls {
	bool (*load)(void *, const char *, uint16_t *);
	bool (*find)(void *, const char *, uint16_t *);
	const uint8_t *(*lock)(void *, uint16_t);
	SlipResourcePayload (*payload)(uint16_t);
	void (*unlock)(void *, uint16_t);
	void (*release)(void *, uint16_t);
} SlipTrackAssetCalls;

typedef struct SlipTrackAssetBundle {
	const SlipTrackAssetCalls *resourceCalls;
	uint16_t trkHandle, trdHandle, trcHandle;
	uint32_t trdBaseToken, trcBaseToken;
	char trkName[13];
	char trdName[13];
	char trcName[13];
	SlipResourcePayload trkPayload;
	SlipResourcePayload trdPayload;
	SlipResourcePayload trcPayload;
	int16_t defaultTraversalGate;
	uint32_t affineDepthThreshold;
	uint32_t useFullObjectViewport;
} SlipTrackAssetBundle;

bool SlipTrackAssets_LoadBundle(const char *trkName, SlipTrackAssetBundle *bundle, const SlipTrackAssetCalls *calls);

void SlipTrackAssets_FreeBundle(SlipTrackAssetBundle *bundle, SlipArticSlotReleaseResource releaseResource, void *user);
void SlipTrackAssets_LoadScenery(SlipTrackAssetBundle *bundle);

#endif
