#include "track_assets.h"
#include "byte_order.h"
#include "runtime.h"
#include "track_world.h"

#include <string.h>

void SlipTrackAssets_LoadScenery(SlipTrackAssetBundle *bundle) {
	uint8_t *const trd = bundle->trdPayload.data;
	const uint16_t tableOffset = SlipBytes_ReadLE16(trd + 2);
	if (tableOffset == 0)
		return;
	uint8_t *entry = trd + tableOffset;
	uint16_t remaining = SlipBytes_ReadLE16(entry);
	entry += 2;
	do {
		const uint16_t sceneryOffset = SlipBytes_ReadLE16(entry + 8);
		if (sceneryOffset != 0) {
			uint8_t *scenery = trd + sceneryOffset;
			uint16_t sceneryRemaining = SlipBytes_ReadLE16(scenery);
			scenery += 2;
			do {
				scenery[0x0c] = 0;
				scenery[0x0d] = 0;
				uint16_t resource;
				if (!bundle->resourceCalls->load(NULL, (const char *)scenery, &resource))
					SlipRuntime_Fatal("TrackLoad - missing shape");
				scenery[0x0c] = (uint8_t)resource;
				scenery[0x0d] = (uint8_t)(resource >> 8);
				const uint8_t *shape = bundle->resourceCalls->lock(NULL, resource);
				uint32_t minimumY = SlipBytes_ReadLE32(shape + 0x28);
				bundle->resourceCalls->unlock(NULL, resource);
				shape = bundle->resourceCalls->lock(NULL, resource);
				const uint32_t radius = SlipBytes_ReadLE32(shape + 0x1c);
				bundle->resourceCalls->unlock(NULL, resource);
				minimumY = 0u - minimumY;
				for (unsigned byte = 0; byte < 4; ++byte) {
					scenery[0x1c + byte] = (uint8_t)(radius >> (byte * 8u));
					scenery[0x20 + byte] = (uint8_t)(minimumY >> (byte * 8u));
				}
				scenery += 0x46;
			} while (--sceneryRemaining != 0);
		}
		entry += SlipBytes_ReadLE16(entry);
	} while (--remaining != 0);
}

static void SlipTrackAssets_CopyTrackName(char dst[13], const char *src) {
	size_t i;

	memset(dst, 0, 13u);
	if (src == NULL) {
		return;
	}
	for (i = 0; i < 12u && src[i] != '\0'; ++i) {
		dst[i] = src[i];
	}
}

static bool SlipTrackAssets_MutateExtensionTail(char name[13], char extensionTail) {
	const size_t len = strlen(name);

	if (len == 0 || len >= 13u) {
		return false;
	}
	name[len - 1u] = extensionTail;
	return true;
}

static void SlipTrackAssets_ReleaseActors(SlipResourcePayload *trd, SlipArticSlotReleaseResource releaseResource,
                                          void *user) {
	if (trd->data != NULL) {
		const uint16_t tableOffset = SlipBytes_ReadLE16(trd->data + 2);
		if (tableOffset != 0) {
			uint8_t *entry = trd->data + tableOffset;
			uint16_t remaining = SlipBytes_ReadLE16(entry);
			entry += 2;
			do {
				const uint16_t actorsOffset = SlipBytes_ReadLE16(entry + 8);
				if (actorsOffset != 0) {
					uint8_t *actor = trd->data + actorsOffset;
					uint16_t actorsRemaining = SlipBytes_ReadLE16(actor);
					actor += 2;
					do {
						if (SlipBytes_ReadLE16(actor + 0xc) != 0) {
							if (releaseResource != NULL)
								releaseResource(user, SlipBytes_ReadLE16(actor + 0xc));
							actor[0xc] = 0;
							actor[0xd] = 0;
						}
						actor += 0x46;
					} while (--actorsRemaining != 0);
				}
				entry += SlipBytes_ReadLE16(entry);
			} while (--remaining != 0);
		}
	}
}

void SlipTrackAssets_FreeBundle(SlipTrackAssetBundle *bundle, SlipArticSlotReleaseResource releaseResource,
                                void *user) {

	if (bundle->trdHandle != 0)
		SlipTrackAssets_ReleaseActors(&bundle->trdPayload, releaseResource, user);
	uint16_t *handles[] = {&bundle->trkHandle, &bundle->trdHandle, &bundle->trcHandle};
	for (unsigned i = 0; i < 3; ++i) {
		if (*handles[i] != 0) {
			bundle->resourceCalls->unlock(NULL, *handles[i]);
			bundle->resourceCalls->release(NULL, *handles[i]);
			*handles[i] = 0;
		}
	}
}

static bool SlipTrackAssets_LoadPayload(const char *name, SlipResourcePayload *payload, uint16_t *handle,
                                        const SlipTrackAssetCalls *calls) {
	uint16_t foundHandle;
	if (!calls->find(NULL, name, &foundHandle))
		return false;
	*handle = foundHandle;
	calls->lock(NULL, *handle);
	*payload = calls->payload(*handle);
	return true;
}

bool SlipTrackAssets_LoadBundle(const char *trkName, SlipTrackAssetBundle *bundle, const SlipTrackAssetCalls *calls) {
	if (trkName == NULL || bundle == NULL || calls == NULL) {
		return false;
	}

	bundle->resourceCalls = calls;
	SlipTrackAssets_CopyTrackName(bundle->trkName, trkName);
	if (bundle->trkName[0] == '\0') {
		return false;
	}

	if (!SlipTrackAssets_LoadPayload(bundle->trkName, &bundle->trkPayload, &bundle->trkHandle, calls)) {
		return false;
	}
	if (bundle->trkPayload.size < 0xa4u)
		return false;

	bundle->defaultTraversalGate = (int16_t)SlipBytes_ReadLE16(bundle->trkPayload.data + 0x9eu);
	bundle->affineDepthThreshold = 0x000830e0u;
	bundle->useFullObjectViewport = 0;
	if (SlipBytes_ReadLE16(bundle->trkPayload.data + 0xa2u) != 0) {
		bundle->affineDepthThreshold = 0x000bea00u;
		bundle->useFullObjectViewport = 0xffffffffu;
	}

	if (SlipBytes_ReadLE16(bundle->trkPayload.data + 2u) != 0x2bu)
		SlipRuntime_Fatal("TrackLoad - wrong data version");
	if (SlipBytes_ReadLE16(bundle->trkPayload.data + 4u) == 0)
		SlipRuntime_Fatal("TrackLoad - this data is unlinked");

	SlipTrackAssets_CopyTrackName(bundle->trdName, bundle->trkName);
	if (!SlipTrackAssets_MutateExtensionTail(bundle->trdName, 'D') ||
	    !SlipTrackAssets_LoadPayload(bundle->trdName, &bundle->trdPayload, &bundle->trdHandle, calls)) {
		return false;
	}

	bundle->trdBaseToken = bundle->trdPayload.address;
	SlipTrackAssets_CopyTrackName(bundle->trcName, bundle->trkName);
	if (!SlipTrackAssets_MutateExtensionTail(bundle->trcName, 'C') ||
	    !SlipTrackAssets_LoadPayload(bundle->trcName, &bundle->trcPayload, &bundle->trcHandle, calls)) {
		return false;
	}

	bundle->trcBaseToken = bundle->trcPayload.address;

	SlipTrackWorld_refuelInitialized = 0;
	return true;
}
