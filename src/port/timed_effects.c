#include "timed_effects.h"

/* Form a Q32 quotient, retain its low word pair, then extract the Q16 fraction. */
enum { SLIP_TIMED_EFFECT_PHASE_NUMERATOR_SHIFT = 2 * SLIP_TIMED_EFFECT_FRACTION_BITS };

SlipTimedEffect *SlipTimedEffects_active;
SlipTimedEffect *SlipTimedEffects_free;
uint16_t SlipTimedEffects_count;
uint16_t SlipTimedEffects_objectCount;

void SlipTimedEffects_Reset(void) {
	SlipTimedEffect *entry = SlipTimedEffects_active;
	entry->next = entry;
	entry->previous = entry;
	entry = SlipTimedEffects_free;
	uint32_t remaining = SlipTimedEffects_count;
	do {
		SlipTimedEffect *const next = entry + 1;
		entry->next = next;
		next->previous = entry;
		entry = next;
	} while (--remaining != 0);
	entry->next = SlipTimedEffects_free;
	SlipTimedEffects_free->previous = entry;
}

void SlipTimedEffects_Release(SlipTimedEffect *entry) {
	SlipTimedEffect *previous = entry->previous;
	SlipTimedEffect *next = entry->next;
	next->previous = previous;
	previous->next = next;
	previous = SlipTimedEffects_free;
	next = previous->next;
	next->previous = entry;
	entry->next = next;
	entry->previous = previous;
	previous->next = entry;
}

void SlipTimedEffects_RemoveParent(uint16_t parentObject) {
	SlipTimedEffect *entry = SlipTimedEffects_active->next;
	while (entry != SlipTimedEffects_active) {
		SlipTimedEffect *const next = entry->next;
		if (entry->parentObject == parentObject) {
			SlipTimedEffects_Release(entry);
		}
		entry = next;
	}
}

SlipTimedEffect *SlipTimedEffects_Allocate(void) {
	SlipTimedEffect *entry = SlipTimedEffects_free->next;
	if (entry != SlipTimedEffects_free) {
		SlipTimedEffect *next = entry->next;
		next->previous = SlipTimedEffects_free;
		SlipTimedEffects_free->next = next;

		SlipTimedEffect *const previous = SlipTimedEffects_active->next;
		next = previous->next;
		next->previous = entry;
		entry->next = next;
		entry->previous = previous;
		previous->next = entry;
	} else {
		int32_t minimum = 0;
		SlipTimedEffect *selected = SlipTimedEffects_active->next;
		entry = SlipTimedEffects_active->next;
		while (entry != SlipTimedEffects_active) {
			if (minimum > entry->lifetime) {
				minimum = entry->lifetime;
				selected = entry;
			}
			entry = entry->next;
		}
		entry = selected;
	}
	return entry;
}

SlipTimedEffect *SlipTimedEffects_Create(uint16_t parentObject, int32_t x, int32_t y, int32_t z, int32_t lifetime,
                                         uint16_t speedLimit, const struct SlipTimedEffectDescriptor *descriptor) {
	if (parentObject != 0) {
		SlipTimedEffects_RemoveParent(parentObject);
	}
	SlipTimedEffect *const entry = SlipTimedEffects_Allocate();
	entry->parentObject = parentObject;
	entry->x = x;
	entry->y = y;
	entry->z = z;
	entry->displacementX = 0;
	entry->displacementY = 0;
	entry->lifetime = lifetime;
	entry->periodCountdown = 0;
	entry->speedLimit = speedLimit;
	entry->descriptor = descriptor;
	entry->currentObject = 0;
	entry->precedingObject = 0;
	return entry;
}

void SlipTimedEffects_ObjectDeleted(uint16_t object) {
	--SlipTimedEffects_objectCount;
	SlipTimedEffect *entry = SlipTimedEffects_active->next;
	while (entry != SlipTimedEffects_active) {
		if (entry->currentObject == object) {
			entry->currentObject = 0;
		}
		if (entry->precedingObject == object) {
			entry->precedingObject = 0;
		}
		entry = entry->next;
	}
}

uint32_t SlipTimedEffects_fraction;

void SlipTimedEffects_Phase(SlipTimedEffectObjectState *state, uint32_t elapsed, SlipTimedEffectPhase *result) {
	const SlipTimedEffectDescriptor *const descriptor = state->descriptor;
	const int32_t step = (int16_t)(uint16_t)elapsed;
	const uint32_t age = state->age + (uint32_t)step;
	state->age = age;
	*result = (SlipTimedEffectPhase){.step = step, .age = age};
	if (state->phase == SLIP_TIMED_EFFECT_GROWTH_PHASE) {
		if (age < descriptor->growthDuration) {
			const uint32_t fraction =
			    (uint32_t)(((uint64_t)age << SLIP_TIMED_EFFECT_PHASE_NUMERATOR_SHIFT) / descriptor->growthDuration) >>
			    SLIP_TIMED_EFFECT_FRACTION_BITS;
			const int32_t difference = (int32_t)((uint32_t)state->holdExtent - (uint32_t)state->initialExtent);
			const uint32_t scaled =
			    (uint32_t)((uint64_t)((int64_t)(int32_t)fraction * difference) >> SLIP_TIMED_EFFECT_FRACTION_BITS);
			result->extent = scaled + (uint32_t)state->initialExtent;
		} else {
			state->age = descriptor->growthDuration;
			result->extent = (uint32_t)state->holdExtent;
			state->phase = SLIP_TIMED_EFFECT_HOLD_PHASE;
		}
		result->writeExtent = true;
		result->callbackValue = result->extent;
	} else if (state->phase == SLIP_TIMED_EFFECT_HOLD_PHASE) {
		result->callbackValue = age;
		if (age >= descriptor->holdDuration + descriptor->growthDuration) {
			state->phase = SLIP_TIMED_EFFECT_FINAL_PHASE;
			SlipTimedEffects_fraction = 0;
		}
	} else {
		const uint32_t elapsed = age - descriptor->holdDuration - descriptor->growthDuration;
		if (elapsed >= descriptor->finalDuration) {
			result->freeObject = true;
			result->callbackValue = (age & SLIP_TIMED_EFFECT_VALUE_UPPER_WORD_MASK) | state->phase;
		} else {
			const uint32_t fraction = (uint32_t)(((uint64_t)elapsed << SLIP_TIMED_EFFECT_PHASE_NUMERATOR_SHIFT) /
			                                     descriptor->finalDuration) >>
			                          SLIP_TIMED_EFFECT_FRACTION_BITS;
			SlipTimedEffects_fraction = fraction;
			const int32_t difference = (int32_t)((uint32_t)descriptor->finalExtent - (uint32_t)state->holdExtent);
			const uint32_t scaled =
			    (uint32_t)((uint64_t)((int64_t)(int32_t)fraction * difference) >> SLIP_TIMED_EFFECT_FRACTION_BITS);
			result->extent = scaled + (uint32_t)state->holdExtent;
			result->writeExtent = true;
			result->callbackValue = result->extent;
		}
	}
}
