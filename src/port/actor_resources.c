#include "actor_resources.h"
#include "actor_format.h"
#include "byte_order.h"
const uint8_t *SlipActorResources_preloadPayload;
const uint8_t *SlipActorResources_releasePayload;

/* Serialized ART node offsets. */

void SlipActor_PreloadResources(uint16_t resource, const SlipActorResourceCalls *calls) {
	const uint8_t *const payload = calls->lock(calls->context, resource);
	SlipActorResources_preloadPayload = payload;
	SlipActor_PreloadNode(payload + SlipBytes_ReadLE32(payload + SLIP_ART_ROOT_PART_OFFSET), calls);
	calls->unlock(calls->context, resource);
}

void SlipActor_PreloadNode(const uint8_t *node, const SlipActorResourceCalls *calls) {
	const uint8_t *const first = node;
	for (;;) {
		const uint32_t child = SlipBytes_ReadLE32(node + SLIP_ART_PART_CHILD_OFFSET);
		for (unsigned index = 0; index < SLIP_ART_LOD_COUNT; ++index) {
			const char *const name =
			    (const char *)(node + SLIP_ART_PART_SHAPE_NAMES_OFFSET + index * SLIP_ART_SHAPE_NAME_BYTES);
			uint32_t handle;
			if (*name != 0)
				(void)calls->load(calls->context, name, &handle);
		}
		for (unsigned index = 0; index < SLIP_ART_LOD_COUNT; ++index) {
			const char *const name =
			    (const char *)(node + SLIP_ART_PART_REPLAY_SHAPE_NAMES_OFFSET + index * SLIP_ART_SHAPE_NAME_BYTES);
			uint32_t handle;
			if (*name != 0)
				(void)calls->load(calls->context, name, &handle);
		}
		for (unsigned index = 0; index < SLIP_ART_DESTRUCTION_COUNT; ++index) {
			const char *const name =
			    (const char *)(node + SLIP_ART_PART_DESTRUCTION_OFFSET + index * SLIP_ART_NAMED_SHAPE_BYTES);
			uint32_t handle;
			if (*name != 0)
				(void)calls->load(calls->context, name, &handle);
		}
		for (unsigned index = 0; index < SLIP_ART_DEBRIS_COUNT; ++index) {
			const char *const name =
			    (const char *)(node + SLIP_ART_PART_DEBRIS_OFFSET + index * SLIP_ART_NAMED_SHAPE_BYTES);
			uint32_t handle;
			if (*name != 0)
				(void)calls->load(calls->context, name, &handle);
		}
		if (child != 0)
			SlipActor_PreloadNode(SlipActorResources_preloadPayload + child, calls);
		const uint32_t next = SlipBytes_ReadLE32(node + SLIP_ART_PART_SIBLING_OFFSET);
		if (next == 0)
			break;
		node = SlipActorResources_preloadPayload + next;
		if (node == first)
			break;
	}
}

void SlipActor_ReleaseResources(uint16_t resource, const SlipActorResourceCalls *calls) {
	const uint8_t *const payload = calls->lock(calls->context, resource);
	SlipActorResources_releasePayload = payload;
	SlipActor_ReleaseNode(payload + SlipBytes_ReadLE32(payload + SLIP_ART_ROOT_PART_OFFSET), calls);
	calls->unlock(calls->context, resource);
}

void SlipActor_ReleaseNode(const uint8_t *node, const SlipActorResourceCalls *calls) {
	const uint8_t *const first = node;
	for (;;) {
		const uint32_t child = SlipBytes_ReadLE32(node + SLIP_ART_PART_CHILD_OFFSET);
		for (unsigned index = 0; index < SLIP_ART_LOD_COUNT; ++index) {
			const char *const name =
			    (const char *)(node + SLIP_ART_PART_SHAPE_NAMES_OFFSET + index * SLIP_ART_SHAPE_NAME_BYTES);
			uint32_t handle;
			if (*name != 0 && calls->find(calls->context, name, &handle))
				calls->release(calls->context, handle);
		}
		bool repeatDestructionGroups;
		do {
			for (unsigned index = 0; index < SLIP_ART_DESTRUCTION_COUNT; ++index) {
				const char *const name =
				    (const char *)(node + SLIP_ART_PART_DESTRUCTION_OFFSET + index * SLIP_ART_NAMED_SHAPE_BYTES);
				uint32_t handle;
				if (*name != 0 && calls->find(calls->context, name, &handle))
					calls->release(calls->context, handle);
			}
			for (unsigned index = 0; index < SLIP_ART_DEBRIS_COUNT; ++index) {
				const char *const name =
				    (const char *)(node + SLIP_ART_PART_DEBRIS_OFFSET + index * SLIP_ART_NAMED_SHAPE_BYTES);
				uint32_t handle;
				if (*name != 0 && calls->find(calls->context, name, &handle))
					calls->release(calls->context, handle);
			}
			repeatDestructionGroups = false;
			for (unsigned index = 0; index < SLIP_ART_LOD_COUNT; ++index) {
				const char *const name =
				    (const char *)(node + SLIP_ART_PART_REPLAY_SHAPE_NAMES_OFFSET + index * SLIP_ART_SHAPE_NAME_BYTES);
				uint32_t handle;
				if (*name != 0) {
					if (calls->find(calls->context, name, &handle))
						calls->release(calls->context, handle);
					else
						repeatDestructionGroups = true;
				}
			}
		} while (repeatDestructionGroups);
		if (child != 0)
			SlipActor_ReleaseNode(SlipActorResources_releasePayload + child, calls);
		const uint32_t next = SlipBytes_ReadLE32(node + SLIP_ART_PART_SIBLING_OFFSET);
		if (next == 0)
			break;
		node = SlipActorResources_releasePayload + next;
		if (node == first)
			break;
	}
}
