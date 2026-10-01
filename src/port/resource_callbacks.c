#include "resource.h"
#include "runtime.h"

void SlipResource_RegisterCallback(uint32_t extension, SlipResourceLoadedCallback callback) {
	if (SlipResource_callbackCount == 32) {
		SlipRuntime_Fatal("ResAddType - insufficient space for this new type");
	}
	const uint32_t key = SlipResource_ExtensionKey(extension);
	for (uint32_t i = 0; i < SlipResource_callbackCount; ++i) {
		if (SlipResource_callbacks[i].extensionKey == key) {
			return;
		}
	}
	SlipResourceCallback *const entry = &SlipResource_callbacks[SlipResource_callbackCount];
	++SlipResource_callbackCount;
	entry->extensionKey = key;
	entry->loadedCallback = callback;

	entry->zeroInitializedWord = 0;
	return;
}
