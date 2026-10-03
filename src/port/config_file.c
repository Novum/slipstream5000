#include "config_file.h"
#include "checksum.h"
#include "file_write.h"

void SlipConfig_SaveImage(const char *path, uint8_t image[SLIP_CONFIG_FILE_BYTES]) {
	uint16_t table[SLIP_CHECKSUM_TABLE_COUNT];
	SlipChecksum_Initialize(table);
	const uint16_t checksum = SlipChecksum_Calculate(table, image, SLIP_CONFIG_PAYLOAD_BYTES);
	image[SLIP_CONFIG_PAYLOAD_BYTES] = (uint8_t)checksum;
	image[SLIP_CONFIG_PAYLOAD_BYTES + 1] = (uint8_t)(checksum >> 8);
	SlipFile_Write(path, image, SLIP_CONFIG_FILE_BYTES);
}
