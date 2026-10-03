#include "track_assets.h"
#include "byte_order.h"
#include "runtime.h"
#include "shape_format.h"
#include "track_format.h"
#include "track_world.h"

#include <string.h>

enum {
	SLIP_TRACK_ASSET_NAME_BUFFER_BYTES = SLIP_RESOURCE_NAME_BUFFER_BYTES,
	SLIP_TRACK_AFFINE_DEPTH_THRESHOLD = 536800,
	SLIP_TRACK_FULL_VIEWPORT_AFFINE_DEPTH_THRESHOLD = 780800
};

void SlipTrackAssets_LoadScenery(SlipTrackAssetBundle *bundle) {
	uint8_t *const trd = bundle->trdPayload.data;
	const uint16_t tableOffset = SlipBytes_ReadLE16(trd + SLIP_TRD_GROUP_TABLE_OFFSET);
	if (tableOffset == 0)
		return;
	uint8_t *entry = trd + tableOffset;
	uint16_t remaining = SlipBytes_ReadLE16(entry);
	entry += SLIP_TRD_TABLE_COUNT_BYTES;
	do {
		const uint16_t sceneryOffset = SlipBytes_ReadLE16(entry + SLIP_TRD_GROUP_SCENERY_LIST_OFFSET);
		if (sceneryOffset != 0) {
			uint8_t *scenery = trd + sceneryOffset;
			uint16_t sceneryRemaining = SlipBytes_ReadLE16(scenery);
			scenery += SLIP_TRD_TABLE_COUNT_BYTES;
			do {
				scenery[SLIP_TRK_SHAPE_HANDLE_OFFSET] = 0;
				scenery[SLIP_TRK_SHAPE_HANDLE_OFFSET + 1] = 0;
				uint16_t resource;
				if (!bundle->resourceCalls->load(NULL, (const char *)scenery, &resource))
					SlipRuntime_Fatal("TrackLoad - missing shape");
				scenery[SLIP_TRK_SHAPE_HANDLE_OFFSET] = (uint8_t)resource;
				scenery[SLIP_TRK_SHAPE_HANDLE_OFFSET + 1] = (uint8_t)(resource >> 8);
				const uint8_t *shape = bundle->resourceCalls->lock(NULL, resource);
				uint32_t minimumY = SlipBytes_ReadLE32(shape + SLIP_SHAPE_MINIMUM_Y_OFFSET);
				bundle->resourceCalls->unlock(NULL, resource);
				shape = bundle->resourceCalls->lock(NULL, resource);
				const uint32_t radius = SlipBytes_ReadLE32(shape + SLIP_SHAPE_RADIUS_OFFSET);
				bundle->resourceCalls->unlock(NULL, resource);
				minimumY = 0u - minimumY;
				for (unsigned byte = 0; byte < 4; ++byte) {
					scenery[SLIP_TRK_SHAPE_RADIUS_OFFSET + byte] = (uint8_t)(radius >> (byte * 8u));
					scenery[SLIP_TRK_SHAPE_CENTER_Y_OFFSET + byte] = (uint8_t)(minimumY >> (byte * 8u));
				}
				scenery += SLIP_TRD_SCENERY_RECORD_BYTES;
			} while (--sceneryRemaining != 0);
		}
		entry += SlipBytes_ReadLE16(entry);
	} while (--remaining != 0);
}

static void SlipTrackAssets_CopyTrackName(char dst[SLIP_TRACK_ASSET_NAME_BUFFER_BYTES], const char *src) {
	size_t i;

	memset(dst, 0, SLIP_TRACK_ASSET_NAME_BUFFER_BYTES);
	if (src == NULL) {
		return;
	}
	for (i = 0; i < SLIP_TRACK_ASSET_NAME_BUFFER_BYTES - 1 && src[i] != '\0'; ++i) {
		dst[i] = src[i];
	}
}

static bool SlipTrackAssets_MutateExtensionTail(char name[SLIP_TRACK_ASSET_NAME_BUFFER_BYTES], char extensionTail) {
	const size_t len = strlen(name);

	if (len == 0 || len >= SLIP_TRACK_ASSET_NAME_BUFFER_BYTES) {
		return false;
	}
	name[len - 1u] = extensionTail;
	return true;
}

static void SlipTrackAssets_ReleaseActors(SlipResourcePayload *trd, SlipArticSlotReleaseResource releaseResource,
                                          void *user) {
	if (trd->data != NULL) {
		const uint16_t tableOffset = SlipBytes_ReadLE16(trd->data + SLIP_TRD_GROUP_TABLE_OFFSET);
		if (tableOffset != 0) {
			uint8_t *entry = trd->data + tableOffset;
			uint16_t remaining = SlipBytes_ReadLE16(entry);
			entry += SLIP_TRD_TABLE_COUNT_BYTES;
			do {
				const uint16_t actorsOffset = SlipBytes_ReadLE16(entry + SLIP_TRD_GROUP_SCENERY_LIST_OFFSET);
				if (actorsOffset != 0) {
					uint8_t *actor = trd->data + actorsOffset;
					uint16_t actorsRemaining = SlipBytes_ReadLE16(actor);
					actor += SLIP_TRD_TABLE_COUNT_BYTES;
					do {
						if (SlipBytes_ReadLE16(actor + SLIP_TRK_SHAPE_HANDLE_OFFSET) != 0) {
							if (releaseResource != NULL)
								releaseResource(user, SlipBytes_ReadLE16(actor + SLIP_TRK_SHAPE_HANDLE_OFFSET));
							actor[SLIP_TRK_SHAPE_HANDLE_OFFSET] = 0;
							actor[SLIP_TRK_SHAPE_HANDLE_OFFSET + 1] = 0;
						}
						actor += SLIP_TRD_SCENERY_RECORD_BYTES;
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
	for (unsigned i = 0; i < sizeof(handles) / sizeof(handles[0]); ++i) {
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
	if (bundle->trkPayload.size < SLIP_TRK_VIEWPORT_SETTINGS_END)
		return false;

	bundle->defaultTraversalGate =
	    (int16_t)SlipBytes_ReadLE16(bundle->trkPayload.data + SLIP_TRK_DEFAULT_TRAVERSAL_GATE_OFFSET);
	bundle->affineDepthThreshold = SLIP_TRACK_AFFINE_DEPTH_THRESHOLD;
	bundle->useFullObjectViewport = 0;
	if (SlipBytes_ReadLE16(bundle->trkPayload.data + SLIP_TRK_FULL_OBJECT_VIEWPORT_OFFSET) != 0) {
		bundle->affineDepthThreshold = SLIP_TRACK_FULL_VIEWPORT_AFFINE_DEPTH_THRESHOLD;
		bundle->useFullObjectViewport = UINT32_MAX;
	}

	if (SlipBytes_ReadLE16(bundle->trkPayload.data + SLIP_TRK_VERSION_OFFSET) != SLIP_TRK_VERSION)
		SlipRuntime_Fatal("TrackLoad - wrong data version");
	if (SlipBytes_ReadLE16(bundle->trkPayload.data + SLIP_TRACK_LINKED_OFFSET) == 0)
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
