#include "input_bios_host.h"

static unsigned SlipInputBiosHost_NextSlot(unsigned slot) {
	++slot;
	if (slot >= SLIP_BIOS_KEY_SLOTS)
		slot = 0;
	return slot;
}

bool SlipInputBiosHost_Push(SlipInputBiosHostQueue *queue, SlipInputBiosKey key) {
	const unsigned tail = SlipInputBiosHost_NextSlot(queue->tail);
	if (tail == queue->head)
		return false;
	queue->keys[queue->tail] = key;
	queue->tail = tail;
	return true;
}

/* PC BIOS legacy INT 16h filtering. Named byte values are BIOS key markers,
 * not bit masks or offsets into native storage. */
static bool SlipInputBiosHost_EnhancedKey(SlipInputBiosKey *key) {
	enum {
		KEYPAD_SCAN = 0xe0,
		KEYPAD_ENTER_SCAN = 0x1c,
		KEYPAD_SLASH_SCAN = 0x35,
		LAST_LEGACY_SCAN = 0x84,
		ENHANCED_CHARACTER = 0xf0,
		EXTENDED_CHARACTER = 0xe0
	};

	if (key->scanCode == KEYPAD_SCAN) {
		key->scanCode = key->character == '\n' || key->character == '\r' ? KEYPAD_ENTER_SCAN : KEYPAD_SLASH_SCAN;
		return false;
	}
	if (key->scanCode > LAST_LEGACY_SCAN || (key->scanCode != 0 && key->character == ENHANCED_CHARACTER))
		return true;
	if (key->scanCode != 0 && key->character == EXTENDED_CHARACTER)
		key->character = 0;
	return false;
}

bool SlipInputBiosHost_Available(void *context) {
	SlipInputBiosHostQueue *const queue = context;
	for (;;) {
		if (queue->head == queue->tail)
			return false;
		SlipInputBiosKey key = queue->keys[queue->head];
		if (!SlipInputBiosHost_EnhancedKey(&key))
			return true;
		queue->head = SlipInputBiosHost_NextSlot(queue->head);
	}
}

SlipInputBiosKey SlipInputBiosHost_ReadReady(void *context) {
	SlipInputBiosHostQueue *const queue = context;
	SlipInputBiosKey key = queue->keys[queue->head];
	queue->head = SlipInputBiosHost_NextSlot(queue->head);
	(void)SlipInputBiosHost_EnhancedKey(&key);
	return key;
}

void SlipInputBiosHost_Clear(void *context) {
	SlipInputBiosHostQueue *const queue = context;
	queue->tail = queue->head;
}

/* Connect the original game INT 15h interceptor to the reference BIOS key
 * branches. Host events supply make/break and E0 explicitly, without packing
 * native event structures into a guest memory image. */
static bool SlipInputBiosHost_InterceptKey(SlipInputBiosHostQueue *queue, uint8_t scan, bool down, bool extended,
                                           bool pressed[256], bool held[256]) {
	enum { SCAN_RELEASE = 0x80, SCAN_EXTENDED = 0xe0 };

	const SlipInputBiosQueueCalls bios = {queue, SlipInputBiosHost_Clear};
	if (extended)
		(void)SlipInput_InterceptScan(SCAN_EXTENDED, pressed, held, &bios);
	return SlipInput_InterceptScan(down ? scan : (uint8_t)(scan | SCAN_RELEASE), pressed, held, &bios);
}

static void SlipInputBiosHost_DeliverKey(SlipInputBiosHostQueue *queue, uint8_t scan, bool down, bool extended,
                                         SlipInputBiosModifiers modifiers) {
	enum { ALT_SCAN = 0x38, NAVIGATION_FIRST = 0x47, NAVIGATION_LAST = 0x53, KEYPAD_MINUS = 0x4a, KEYPAD_PLUS = 0x4e };

	if (!down) {
		/* Reference BIOS Alt release emits the accumulated decimal token once
		 * both Alt keys are released; all other translated key breaks emit none. */
		if (scan == ALT_SCAN && !modifiers.alt && queue->altDigits != 0) {
			(void)SlipInputBiosHost_Push(queue, (SlipInputBiosKey){queue->altDigits, 0});
			queue->altDigits = 0;
		}
		return;
	}
	SlipInputBiosKey key;
	if (scan >= NAVIGATION_FIRST && scan <= NAVIGATION_LAST && scan != KEYPAD_MINUS && scan != KEYPAD_PLUS)
		key = SlipInputBiosHost_TranslateNavigation(scan, modifiers, extended, &queue->altDigits);
	else
		key = SlipInputBiosHost_TranslateNormal(scan, modifiers, extended);
	/* Reference add_key suppresses the zero table entries. */
	if (key.character != 0 || key.scanCode != 0)
		(void)SlipInputBiosHost_Push(queue, key);
}

void SlipInputBiosHost_KeyEvent(SlipInputBiosHostQueue *queue, uint8_t scan, bool down, bool extended,
                                SlipInputBiosModifiers modifiers, bool pressed[256], bool held[256]) {
	if (SlipInputBiosHost_InterceptKey(queue, scan, down, extended, pressed, held))
		SlipInputBiosHost_DeliverKey(queue, scan, down, extended, modifiers);
}

static SlipInputBiosHostQueue hostQueue;
static SlipInputBiosModifierState hostModifiers;
const SlipInputBiosCalls SlipInputBiosHost_calls = {SlipInputBiosHost_Available, SlipInputBiosHost_ReadReady,
                                                    &hostQueue};

void SlipInputBiosHost_ApplyKey(uint8_t scan, bool down, bool extended, bool pressed[256], bool held[256]) {
	if (!SlipInputBiosHost_InterceptKey(&hostQueue, scan, down, extended, pressed, held))
		return;
	SlipInputBiosModifiers modifiers = SlipInputBiosHost_UpdateModifiers(&hostModifiers, scan, down, extended);
	SlipInputBiosHost_DeliverKey(&hostQueue, scan, down, extended, modifiers);
}
