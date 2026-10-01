#include "race_records.h"
#include "text_edit.h"

void SlipLapRecords_BeginNameInput(SlipLapRecordNameInput *input) {
	input->cursor = 0;
	input->cursorVisible = 1;
	input->blinkRemaining = 600;
}

void SlipLapRecords_UpdateNameBlink(SlipLapRecordNameInput *input, uint16_t elapsed) {
	bool borrow = input->blinkRemaining < elapsed;
	input->blinkRemaining = (uint16_t)(input->blinkRemaining - elapsed);
	if (borrow) {
		input->blinkRemaining = (uint16_t)(input->blinkRemaining + 600);
		input->cursorVisible ^= 1;
	}
}

bool SlipLapRecords_EditName(SlipLapRecordNameInput *input, SlipLapRecord *record, uint8_t character) {
	if (character == SLIP_CHARACTER_NONE)
		return false;
	if (character == SLIP_CHARACTER_ENTER)
		return true;
	input->cursor = (uint16_t)SlipText_Edit(record->name, input->cursor, 31, character);
	return false;
}

void SlipLapRecords_Update(SlipLapRecordTable *records, uint32_t track, const SlipRaceRacerTable *racers,
                           const SlipLapRecordsHost *host) {
	const uint32_t trackIndex = track - 1;
	SlipLapRecord *const entries = records->tracks[trackIndex];
	for (uint32_t racerIndex = 0; racerIndex < racers->racerCount; ++racerIndex) {
		const SlipRaceRacerState *const racer = &racers->records[racerIndex];
		if (racer->racerType != 0 && racer->racerType != 1)
			continue;
		const uint32_t lapTime = racer->bestLapTime;
		if (lapTime == 0)
			continue;
		for (uint32_t position = 0; position < 3; ++position) {
			if ((int32_t)lapTime >= (int32_t)entries[position].lapTime)
				continue;

			for (uint32_t displaced = 2; displaced > position; --displaced)
				entries[displaced] = entries[displaced - 1];
			SlipLapRecord *const entry = &entries[position];
			entry->lapTime = lapTime;
			entry->driverIndex = (uint16_t)(racer->tuningIndex - 1);
			entry->name[0] = '\0';
			entry->editing = 1;
			host->enterName(host->context, trackIndex, entry);
			entry->editing = 0;
			break;
		}
	}
	host->saveConfiguration(host->context);
}
