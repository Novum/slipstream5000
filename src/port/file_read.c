#include "file_read.h"
#include "runtime.h"
#include <string.h>

SlipArchive SlipFile_archives[3];
SlipArchive *SlipFile_primaryArchive, *SlipFile_secondaryArchive;
uint8_t SlipArchive_readName[12], SlipArchive_sizeName[12];
uint8_t *SlipArchive_readDestination;
uint32_t SlipArchive_countWord, SlipArchive_indexOffset;

void SlipFile_SetPrimaryArchive(SlipArchive *archive) { SlipFile_primaryArchive = archive; }

SlipArchive *SlipFile_GetPrimaryArchive(void) { return SlipFile_primaryArchive; }

static uint8_t SlipArchive_UppercaseReadName(uint8_t character) {
	if ((int8_t)character >= 'a' && (int8_t)character <= 'z')
		character -= 0x20;
	return character;
}

static uint8_t SlipArchive_UppercaseSizeName(uint8_t character) {
	if ((int8_t)character >= 'a' && (int8_t)character <= 'z')
		character -= 0x20;
	return character;
}

bool SlipArchive_Read(SlipArchive *archive, const char *name, uint8_t *destination, const SlipFileReadCalls *calls) {
	SlipArchive_readDestination = destination;
	const uint8_t *source = (const uint8_t *)name;
	uint8_t *target = SlipArchive_readName;
	uint32_t remaining = 8;
	do {
		const uint8_t character = *source;
		if (character == 0)
			break;
		++source;
		if (character == '.')
			break;
		*target++ = SlipArchive_UppercaseReadName(character);
	} while (--remaining != 0);
	if (remaining == 0)
		++source;
	while (remaining != 0) {
		*target++ = ' ';
		--remaining;
	}
	*target++ = '.';
	remaining = 3;
	do {
		const uint8_t character = *source;
		if (character == 0)
			break;
		++source;
		*target++ = SlipArchive_UppercaseReadName(character);
	} while (--remaining != 0);
	while (remaining != 0) {
		*target++ = ' ';
		--remaining;
	}
	const uint16_t resource = archive->indexResource;
	const SlipArchiveEntry *entry = (const SlipArchiveEntry *)calls->lock(calls->context, resource);
	remaining = archive->count;
	do {
		if (entry->kind == 2 && memcmp(entry->name, SlipArchive_readName, 12) == 0) {
			if (!calls->read(calls->context, archive->file, (int32_t)entry->offset, SlipArchive_readDestination,
			                 entry->length)) {
				SlipRuntime_error = 3;
				calls->unlock(calls->context, resource);
				return false;
			}
			calls->unlock(calls->context, resource);
			return true;
		}
		++entry;
	} while (--remaining != 0);
	SlipRuntime_error = 2;
	calls->unlock(calls->context, resource);
	return false;
}

bool SlipArchive_Size(SlipArchive *archive, const char *name, uint32_t *size, const SlipFileReadCalls *calls) {
	const uint8_t *source = (const uint8_t *)name;
	uint8_t *target = SlipArchive_sizeName;
	uint32_t remaining = 8;
	do {
		const uint8_t character = *source;
		if (character == 0)
			break;
		++source;
		if (character == '.')
			break;
		*target++ = SlipArchive_UppercaseSizeName(character);
	} while (--remaining != 0);
	if (remaining == 0)
		++source;
	while (remaining != 0) {
		*target++ = ' ';
		--remaining;
	}
	*target++ = '.';
	remaining = 3;
	do {
		const uint8_t character = *source;
		if (character == 0)
			break;
		++source;
		*target++ = SlipArchive_UppercaseSizeName(character);
	} while (--remaining != 0);
	while (remaining != 0) {
		*target++ = ' ';
		--remaining;
	}
	const uint16_t resource = archive->indexResource;
	const SlipArchiveEntry *entry = (const SlipArchiveEntry *)calls->lock(calls->context, resource);
	remaining = archive->count;
	do {
		if (entry->kind == 2 && memcmp(entry->name, SlipArchive_sizeName, 12) == 0) {
			*size = entry->length;
			calls->unlock(calls->context, resource);
			return true;
		}
		++entry;
	} while (--remaining != 0);
	SlipRuntime_error = 2;
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
		SlipRuntime_error = 2;
		return false;
	}
	uint32_t size;
	if (!calls->size(calls->context, file, &size) || !calls->read(calls->context, file, 0, destination, size) ||
	    !calls->close(calls->context, file)) {
		SlipRuntime_error = 3;
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
	SlipRuntime_error = 2;
	return false;
}

bool SlipArchive_Open(const char *path, SlipArchive **result, const SlipFileReadCalls *calls) {
	SlipArchive *archive = SlipFile_archives;
	uint32_t remaining = 3;
	while (archive->file != 0) {
		++archive;
		if (--remaining == 0) {
			*result = archive;
			SlipRuntime_error = 7;
			return false;
		}
	}
	*result = archive;
	int32_t file;
	if (!calls->open(calls->context, path, &file)) {
		SlipRuntime_error = 2;
		return false;
	}
	archive->file = file;
	calls->seekEnd(calls->context, file, -4);
	uint8_t diskWord[4] = {(uint8_t)SlipArchive_indexOffset, (uint8_t)(SlipArchive_indexOffset >> 8),
	                       (uint8_t)(SlipArchive_indexOffset >> 16), (uint8_t)(SlipArchive_indexOffset >> 24)};
	bool success = calls->read(calls->context, file, -1, diskWord, 4);

	SlipArchive_indexOffset =
	    (uint32_t)diskWord[0] | (uint32_t)diskWord[1] << 8 | (uint32_t)diskWord[2] << 16 | (uint32_t)diskWord[3] << 24;
	if (success) {
		for (uint32_t byte = 0; byte < 4; ++byte)
			diskWord[byte] = (uint8_t)(archive->count >> (byte * 8));
		success = calls->read(calls->context, file, (int32_t)SlipArchive_indexOffset, diskWord, 4);
		archive->count = (uint32_t)diskWord[0] | (uint32_t)diskWord[1] << 8 | (uint32_t)diskWord[2] << 16 |
		                 (uint32_t)diskWord[3] << 24;
	}
	if (!success) {
		SlipRuntime_error = 3;
	} else {
		SlipArchive_countWord = archive->count;
		archive->count = SlipArchive_countWord & 0x7fffffffu;
		if (archive->count > 5000) {
			SlipRuntime_error = 2;
			success = false;
		} else {
			const uint32_t bytes = archive->count * 28u;
			uint16_t indexResource;
			success = calls->allocate(calls->context, bytes, 0, &indexResource);
			if (!success) {
				SlipRuntime_error = 6;
			} else {
				archive->indexResource = indexResource;
				uint8_t *index = calls->lock(calls->context, archive->indexResource);
				success = calls->read(calls->context, file, -1, index, bytes);
				if (!success) {
					SlipRuntime_error = 3;
				} else {
					if ((int32_t)SlipArchive_countWord < 0) {
						static const uint8_t key[16] = "SOFTWAREREFINERY";
						remaining = archive->count;
						do {
							for (uint32_t character = 0; character < 16; ++character)
								index[4 + character] ^= key[character];
							index += 28;
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
