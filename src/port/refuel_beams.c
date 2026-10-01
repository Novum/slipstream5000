#include "refuel_beams.h"

void SlipRefuel_BuildBeams(SlipRefuelBeamState *state, uint32_t section, uint16_t firstDrawOffset,
                           const SlipTrackDrawRecord *drawRecords, uint32_t drawRecordsAddress,
                           const SlipView3DMaths *maths, uint32_t incomingX, uint32_t incomingY,
                           const SlipRefuelBeamCalls *calls) {

	enum { SLIP_REFUEL_SLOT_EMITS_BEAMS = 4u };

	const uint32_t mainPartTag = 0x6d61696e;
	const uint32_t leftLaserTag = 0x6c61736c;
	const uint32_t rightLaserTag = 0x6c617372;
	if (section != state->section || state->built != 0)
		return;
	state->built = UINT32_MAX;
	state->active = 0;
	if (firstDrawOffset == 0)
		return;
	const uint32_t firstDraw = drawRecordsAddress + firstDrawOffset;
	uint32_t currentDraw = firstDraw;
	do {
		const SlipTrackDrawRecord *const draw = &drawRecords[(currentDraw - drawRecordsAddress) / sizeof(*drawRecords)];
		const uint16_t object = (uint16_t)draw->objectOffset;
		const SlipTrackSlotRecord *const slot = calls->slot(calls->context, object);
		if ((slot->flags & SLIP_REFUEL_SLOT_EMITS_BEAMS) != 0) {
			state->active = UINT32_MAX;
			bool rightSide = false;
			for (uint32_t remaining = 8; remaining != 0; --remaining) {
				rightSide = !rightSide;
				SlipView3DVec32 start = {(int32_t)incomingX, (int32_t)incomingY, (int32_t)remaining};
				calls->position(calls->context, object, mainPartTag, rightSide ? rightLaserTag : leftLaserTag, &start);
				const SlipView3DMatrix *const objectMatrix = calls->objectMatrix(calls->context, object);
				const int16_t roll = SlipView3D_RollFromMatrix(maths, objectMatrix);
				SlipView3DMatrix beamMatrix;
				SlipView3D_BuildYawMatrix(maths, roll, &beamMatrix);

				(void)calls->random(calls->context);
				(void)calls->random(calls->context);
				const int16_t pitch = (int16_t)calls->random(calls->context);
				SlipView3D_ApplyPitchMatrix(maths, pitch, &beamMatrix);
				int16_t rotation = (int16_t)(calls->random(calls->context) >> 4);
				const int16_t rotationSign = (int16_t)calls->random(calls->context);
				if (rotationSign < 0)
					rotation = (int16_t)-rotation;
				SlipView3D_ApplyRow0Row2Rotation(maths, rotation, &beamMatrix);
				SlipView3D_OrthonormalizeForwardBasis(&beamMatrix);
				SlipView3DVec32 displacement =
				    SlipView3D_ScaleVector(beamMatrix.m[6], beamMatrix.m[7], beamMatrix.m[8], 0xee480);
				SlipView3DVec32 end = {(int32_t)((uint32_t)start.x + (uint32_t)displacement.x),
				                       (int32_t)((uint32_t)start.y + (uint32_t)displacement.y),
				                       (int32_t)((uint32_t)start.z + (uint32_t)displacement.z)};
				calls->clip(calls->context, start, &end);

				incomingX = (uint32_t)start.x;
				incomingY = (uint32_t)start.y;
				SlipTrackBeamRecord *const beam = calls->allocate(calls->context);
				if (beam == NULL)
					break;
				beam->start = start;
				beam->end = end;
				beam->midpoint = (SlipView3DVec32){(int32_t)((uint32_t)start.x + (uint32_t)end.x) >> 1,
				                                   (int32_t)((uint32_t)start.y + (uint32_t)end.y) >> 1,
				                                   (int32_t)((uint32_t)start.z + (uint32_t)end.z) >> 1};
				beam->type = SLIP_TRACK_BEAM_REFUEL;
				beam->section = state->section;
				incomingX = state->section;
				incomingY = (uint32_t)beam->midpoint.y;
			}
		}
		currentDraw = draw->nextAddress;
	} while (currentDraw != firstDraw);
}
