#include "file_read.h"
#include "runtime.h"
#include <string.h>

enum {
	SLIP_ARCHIVE_NAME_LENGTH = 8,
	SLIP_ARCHIVE_EXTENSION_LENGTH = 3,
	SLIP_ARCHIVE_PACKED_NAME_BYTES = SLIP_ARCHIVE_NAME_LENGTH + 1 + SLIP_ARCHIVE_EXTENSION_LENGTH,
	SLIP_ARCHIVE_MAXIMUM_ENTRY_COUNT = 5000
};

SlipArchive SlipFile_archives[SLIP_FILE_ARCHIVE_CAPACITY];
SlipArchive *SlipFile_primaryArchive, *SlipFile_secondaryArchive;
uint8_t SlipArchive_readName[SLIP_ARCHIVE_PACKED_NAME_BYTES], SlipArchive_sizeName[SLIP_ARCHIVE_PACKED_NAME_BYTES];
uint8_t *SlipArchive_readDestination;
uint32_t SlipArchive_countWord, SlipArchive_indexOffset;

void SlipFile_SetPrimaryArchive(SlipArchive *archive) { SlipFile_primaryArchive = archive; }

SlipArchive *SlipFile_GetPrimaryArchive(void) { return SlipFile_primaryArchive; }

static uint8_t SlipArchive_UppercaseReadName(uint8_t character) {
	if ((int8_t)character >= 'a' && (int8_t)character <= 'z')
		character -= 'a' - 'A';
	return character;
}

static uint8_t SlipArchive_UppercaseSizeName(uint8_t character) {
	if ((int8_t)character >= 'a' && (int8_t)character <= 'z')
		character -= 'a' - 'A';
	return character;
}

bool SlipArchive_Read(SlipArchive *archive, const char *name, uint8_t *destination, const SlipFileReadCalls *calls) {
	SlipArchive_readDestination = destination;
	const uint8_t *source = (const uint8_t *)name;
	uint8_t *target = SlipArchive_readName;
	for (unsigned characterIndex = 0; characterIndex < SLIP_ARCHIVE_NAME_LENGTH; ++characterIndex) {
		const uint8_t character = *source;
		if (character == 0)
			break;
		++source;
		if (character == '.')
			break;
		*target++ = SlipArchive_UppercaseReadName(character);
	}
	if (target == SlipArchive_readName + SLIP_ARCHIVE_NAME_LENGTH)
		++source;
	while (target < SlipArchive_readName + SLIP_ARCHIVE_NAME_LENGTH)
		*target++ = ' ';
	*target++ = '.';
	for (unsigned characterIndex = 0; characterIndex < SLIP_ARCHIVE_EXTENSION_LENGTH; ++characterIndex) {
		const uint8_t character = *source;
		if (character == 0)
			break;
		++source;
		*target++ = SlipArchive_UppercaseReadName(character);
	}
	while (target < SlipArchive_readName + SLIP_ARCHIVE_PACKED_NAME_BYTES)
		*target++ = ' ';
	const uint16_t resource = archive->indexResource;
	const SlipArchiveEntry *entry = (const SlipArchiveEntry *)calls->lock(calls->context, resource);
	uint32_t remaining = archive->count;
	do {
		if (entry->kind == SLIP_ARCHIVE_ENTRY_FILE_KIND &&
		    memcmp(entry->name, SlipArchive_readName, SLIP_ARCHIVE_PACKED_NAME_BYTES) == 0) {
			if (!calls->read(calls->context, archive->file, (int32_t)entry->offset, SlipArchive_readDestination,
			                 entry->length)) {
				SlipRuntime_error = SLIP_RUNTIME_ERROR_READ_FAILED;
				calls->unlock(calls->context, resource);
				return false;
			}
			calls->unlock(calls->context, resource);
			return true;
		}
		++entry;
	} while (--remaining != 0);
	SlipRuntime_error = SLIP_RUNTIME_ERROR_FILE_UNAVAILABLE;
	calls->unlock(calls->context, resource);
	return false;
}

bool SlipArchive_Size(SlipArchive *archive, const char *name, uint32_t *size, const SlipFileReadCalls *calls) {
	const uint8_t *source = (const uint8_t *)name;
	uint8_t *target = SlipArchive_sizeName;
	for (unsigned characterIndex = 0; characterIndex < SLIP_ARCHIVE_NAME_LENGTH; ++characterIndex) {
		const uint8_t character = *source;
		if (character == 0)
			break;
		++source;
		if (character == '.')
			break;
		*target++ = SlipArchive_UppercaseSizeName(character);
	}
	if (target == SlipArchive_sizeName + SLIP_ARCHIVE_NAME_LENGTH)
		++source;
	while (target < SlipArchive_sizeName + SLIP_ARCHIVE_NAME_LENGTH)
		*target++ = ' ';
	*target++ = '.';
	for (unsigned characterIndex = 0; characterIndex < SLIP_ARCHIVE_EXTENSION_LENGTH; ++characterIndex) {
		const uint8_t character = *source;
		if (character == 0)
			break;
		++source;
		*target++ = SlipArchive_UppercaseSizeName(character);
	}
	while (target < SlipArchive_sizeName + SLIP_ARCHIVE_PACKED_NAME_BYTES)
		*target++ = ' ';
	const uint16_t resource = archive->indexResource;
	const SlipArchiveEntry *entry = (const SlipArchiveEntry *)calls->lock(calls->context, resource);
	uint32_t remaining = archive->count;
	do {
		if (entry->kind == SLIP_ARCHIVE_ENTRY_FILE_KIND &&
		    memcmp(entry->name, SlipArchive_sizeName, SLIP_ARCHIVE_PACKED_NAME_BYTES) == 0) {
			*size = entry->length;
			calls->unlock(calls->context, resource);
			return true;
		}
		++entry;
	} while (--remaining != 0);
	SlipRuntime_error = SLIP_RUNTIME_ERROR_FILE_UNAVAILABLE;
	calls->unlock(calls->context, resource);
	return false;
}

bool SlipFile_Read(const char *name, uint8_t *destination, const SlipFileReadCalls *calls) {
	if (SlipFile_primaryArchive != NULL) {
		if (SlipArchive_Read(SlipFile_primaryArchive, name, destination, calls))
			return true;
		if (SlipFile_secondaryArchive != NULL && SlipArchive_Read(SlipFile_secondaryArchive, name, destination, calls))
			return true;
	}
	int32_t file;
	if (!calls->open(calls->context, name, &file)) {
		SlipRuntime_error = SLIP_RUNTIME_ERROR_FILE_UNAVAILABLE;
		return false;
	}
	uint32_t size;
	if (!calls->size(calls->context, file, &size) || !calls->read(calls->context, file, 0, destination, size) ||
	    !calls->close(calls->context, file)) {
		SlipRuntime_error = SLIP_RUNTIME_ERROR_READ_FAILED;
		calls->close(calls->context, file);
		return false;
	}
	return true;
}

bool SlipFile_Size(const char *name, uint32_t *size, const SlipFileReadCalls *calls) {
	if (SlipFile_primaryArchive != NULL) {
		if (SlipArchive_Size(SlipFile_primaryArchive, name, size, calls))
			return true;
		if (SlipFile_secondaryArchive != NULL && SlipArchive_Size(SlipFile_secondaryArchive, name, size, calls))
			return true;
	}
	int32_t file;
	if (calls->open(calls->context, name, &file) && calls->size(calls->context, file, size) &&
	    calls->close(calls->context, file))
		return true;
	SlipRuntime_error = SLIP_RUNTIME_ERROR_FILE_UNAVAILABLE;
	return false;
}

bool SlipArchive_Open(const char *path, SlipArchive **result, const SlipFileReadCalls *calls) {
	SlipArchive *archive = SlipFile_archives;
	uint32_t remaining = SLIP_FILE_ARCHIVE_CAPACITY;
	while (archive->file != 0) {
		++archive;
		if (--remaining == 0) {
			*result = archive;
			SlipRuntime_error = SLIP_RUNTIME_ERROR_CAPACITY_EXHAUSTED;
			return false;
		}
	}
	*result = archive;
	int32_t file;
	if (!calls->open(calls->context, path, &file)) {
		SlipRuntime_error = SLIP_RUNTIME_ERROR_FILE_UNAVAILABLE;
		return false;
	}
	archive->file = file;
	calls->seekEnd(calls->context, file, -SLIP_ARCHIVE_INDEX_OFFSET_BYTES);
	uint8_t diskWord[sizeof(uint32_t)] = {(uint8_t)SlipArchive_indexOffset, (uint8_t)(SlipArchive_indexOffset >> 8),
	                                      (uint8_t)(SlipArchive_indexOffset >> 16),
	                                      (uint8_t)(SlipArchive_indexOffset >> 24)};
	bool success = calls->read(calls->context, file, -1, diskWord, sizeof(diskWord));

	SlipArchive_indexOffset =
	    (uint32_t)diskWord[0] | (uint32_t)diskWord[1] << 8 | (uint32_t)diskWord[2] << 16 | (uint32_t)diskWord[3] << 24;
	if (success) {
		for (uint32_t byte = 0; byte < sizeof(diskWord); ++byte)
			diskWord[byte] = (uint8_t)(archive->count >> (byte * 8));
		success = calls->read(calls->context, file, (int32_t)SlipArchive_indexOffset, diskWord, sizeof(diskWord));
		archive->count = (uint32_t)diskWord[0] | (uint32_t)diskWord[1] << 8 | (uint32_t)diskWord[2] << 16 |
		                 (uint32_t)diskWord[3] << 24;
	}
	if (!success) {
		SlipRuntime_error = SLIP_RUNTIME_ERROR_READ_FAILED;
	} else {
		SlipArchive_countWord = archive->count;
		archive->count = SlipArchive_countWord & ~SLIP_ARCHIVE_COUNT_ENCRYPTED;
		if (archive->count > SLIP_ARCHIVE_MAXIMUM_ENTRY_COUNT) {
			SlipRuntime_error = SLIP_RUNTIME_ERROR_FILE_UNAVAILABLE;
			success = false;
		} else {
			const uint32_t bytes = archive->count * SLIP_ARCHIVE_ENTRY_BYTES;
			uint16_t indexResource;
			success = calls->allocate(calls->context, bytes, 0, &indexResource);
			if (!success) {
				SlipRuntime_error = SLIP_RUNTIME_ERROR_MEMORY_EXHAUSTED;
			} else {
				archive->indexResource = indexResource;
				uint8_t *index = calls->lock(calls->context, archive->indexResource);
				success = calls->read(calls->context, file, -1, index, bytes);
				if (!success) {
					SlipRuntime_error = SLIP_RUNTIME_ERROR_READ_FAILED;
				} else {
					if ((int32_t)SlipArchive_countWord < 0) {
						static const uint8_t key[SLIP_ARCHIVE_INDEX_NAME_BYTES] = "SOFTWAREREFINERY";
						remaining = archive->count;
						do {
							for (uint32_t character = 0; character < SLIP_ARCHIVE_INDEX_NAME_BYTES; ++character)
								index[SLIP_ARCHIVE_ENTRY_NAME_OFFSET + character] ^= key[character];
							index += SLIP_ARCHIVE_ENTRY_BYTES;
						} while (--remaining != 0);
					}
					calls->unlock(calls->context, archive->indexResource);
					return true;
				}
			}
		}
	}

	calls->close(calls->context, file);
	archive->file = 0;
	return false;
}

void SlipArchive_Close(SlipArchive *archive, const SlipFileReadCalls *calls) {
	calls->close(calls->context, archive->file);
	calls->release(calls->context, archive->indexResource);
	if (SlipFile_primaryArchive == archive)
		SlipFile_primaryArchive = NULL;
	if (SlipFile_secondaryArchive == archive)
		SlipFile_secondaryArchive = NULL;
}
