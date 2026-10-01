#include "material_residency.h"

enum { SLIP_MATERIAL_MINIMUM_MEMORY_BUDGET = 0x1f40u };

void SlipMaterial_SetLimits(SlipMaterialResidency *state, uint32_t memoryBudget, uint32_t maximumTextureSize,
                            uint32_t maximumTextureFrame) {
	if (memoryBudget < SLIP_MATERIAL_MINIMUM_MEMORY_BUDGET)
		memoryBudget = SLIP_MATERIAL_MINIMUM_MEMORY_BUDGET;
	state->memoryBudget = memoryBudget;
	state->maximumTextureSize = maximumTextureSize;
	state->maximumTextureFrame = maximumTextureFrame;
}

void SlipMaterial_Shutdown(SlipMaterialResidency *state, const SlipMaterialResidencyCalls *calls) {
	if (state->resource != 0) {
		SlipMaterial_ReleaseTextures(state, calls);
		const uint16_t resource = state->resource;
		calls->unlock(calls->context, resource);
		calls->release(calls->context, resource);
		state->resource = 0;
	}
}

void SlipMaterial_ReleaseTextures(SlipMaterialResidency *state, const SlipMaterialResidencyCalls *calls) {
	if (state->table != NULL) {
		uint32_t remaining = state->table->count;
		SlipDraw3DMaterialRecord *record = state->table->records;
		do {
			for (unsigned frame = 0; frame < 4; ++frame) {
				const uint16_t handle = (uint16_t)record->textureHandles[frame];
				if (handle != 0) {
					calls->release(calls->context, handle);
					record->textureHandles[frame] = 0;
				}
			}
			++record;
		} while (--remaining != 0);
	}
}

void SlipMaterial_LargestResident(SlipMaterialResidency *state, const SlipMaterialResidencyCalls *calls,
                                  uint16_t *handle, uint32_t *size) {
	uint32_t remaining = state->table->count;
	uint32_t index = 0;
	*size = 0;
	do {
		const uint16_t candidate = calls->frame(calls->context, &index);
		if (candidate != 0 && calls->resident(calls->context, candidate)) {
			const uint32_t bytes = calls->resourceSize(calls->context, candidate);
			if (bytes >= *size) {
				*handle = candidate;
				*size = bytes;
			}
		}
		++index;
	} while (--remaining != 0);
}

uint16_t SlipMaterial_LargestUnloaded(SlipMaterialResidency *state, const SlipMaterialResidencyCalls *calls) {
	uint32_t remaining = state->table->count;
	uint32_t index = 0, largest = 0;
	uint16_t handle = 0;
	do {
		const uint16_t candidate = calls->frame(calls->context, &index);
		if (candidate != 0 && !calls->resident(calls->context, candidate)) {
			const uint32_t bytes = calls->allocationSize(calls->context, candidate);
			if (bytes >= largest) {
				handle = candidate;
				largest = bytes;
				state->selectedIndex = (uint16_t)index;
			}
		}
		++index;
	} while (--remaining != 0);
	return handle;
}

void SlipMaterial_MakeResident(SlipMaterialResidency *state, const SlipMaterialResidencyCalls *calls) {
	const uint32_t savedReclaim = calls->getReclaim(calls->context);
	calls->setReclaim(calls->context, 0);
	SlipMaterial_ReleaseTextures(state, calls);
	calls->loadFrames(calls->context);
	state->scale = 0x40000000;
	if (state->table != NULL) {
		uint32_t largeBytes = 0, smallBytes = 0;
		uint32_t remaining = state->table->count, index = 0;
		do {
			const uint16_t handle = calls->frame(calls->context, &index);
			if (handle != 0) {
				const uint32_t bytes = calls->allocationSize(calls->context, handle);
				if (bytes <= 2000)
					smallBytes += bytes;
				else if (bytes <= state->maximumTextureSize)
					largeBytes += bytes;
			}
			++index;
		} while (--remaining != 0);
		const uint32_t available = state->memoryBudget - smallBytes;
		if (available < largeBytes) {
			const uint32_t ratio = (uint32_t)(((uint64_t)available << 30) / largeBytes);
			state->scale = calls->squareRoot(calls->context, (uint64_t)ratio << 32) >> 1;
			calls->resetFrames(calls->context);
		}
		for (;;) {
			const uint16_t handle = SlipMaterial_LargestUnloaded(state, calls);
			if (handle == 0)
				break;

			const uint32_t materialIndex = state->selectedIndex;
			bool loaded = false;
			for (;;) {
				if (calls->resize(calls->context, handle, state->scale)) {
					loaded = true;
					break;
				}
				uint16_t candidate = 0;
				uint32_t bytes;
				SlipMaterial_LargestResident(state, calls, &candidate, &bytes);
				if (bytes == 0 || bytes < 2000) {
					calls->release(calls->context, handle);
					calls->setFrame(calls->context, materialIndex, 0);
					break;
				}
				(void)calls->resize(calls->context, candidate, 0x20000000);
			}
			if (loaded && state->scale != 0x40000000)
				calls->protect(calls->context, handle);
		}
	}
	calls->setReclaim(calls->context, savedReclaim);
}
