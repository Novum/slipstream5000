#include "race_bonus.h"

#include "frame_timer.h"
#include "race.h"
#include "race_collision.h"
#include "runtime.h"
#include "track_view_render.h"

#include <string.h>

typedef struct SlipRaceBonusPrivate {
	int32_t bonusType;
	int32_t remainingMilliseconds;
} SlipRaceBonusPrivate;

uint32_t SlipRaceBonus_Type(uint16_t objectOffset) {
	const SlipRaceBonusPrivate *const state =
	    (const SlipRaceBonusPrivate *)(const void *)SlipObject_PrivateState(objectOffset);
	return (uint32_t)state->bonusType;
}

static SlipRaceBonusHostBindings *SlipRaceBonus_hostBindings;

static const SlipView3DMatrix SlipRaceBonus_identity = {
    .m = {0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000},
};

static const int32_t SlipRaceBonus_normalTypes[] = {
    SLIP_RACE_BONUS_ENGINE_REPAIR,    SLIP_RACE_BONUS_CONTROL_REPAIR, SLIP_RACE_BONUS_REVERSE_CONTROLS,
    SLIP_RACE_BONUS_POWERUP_RECHARGE, SLIP_RACE_BONUS_SPEED_BOOST,
};

static const int32_t SlipRaceBonus_championshipTypes[] = {
    SLIP_RACE_BONUS_ENGINE_REPAIR,    SLIP_RACE_BONUS_CONTROL_REPAIR, SLIP_RACE_BONUS_REVERSE_CONTROLS,
    SLIP_RACE_BONUS_POWERUP_RECHARGE, SLIP_RACE_BONUS_CREDITS,        SLIP_RACE_BONUS_SPEED_BOOST,
};

static const SlipRaceBonusSpawn SlipRaceBonus_track1[] = {
    {4582847, 1012426, 1567396, -1}, {2842388, 993677, 734734, -1},  {1813481, 988183, 2599021, -1},
    {2408512, 977750, 3944936, -1},  {2845075, 984386, 5707992, -1}, {4417376, 954380, 2886744, -1},
    {2469555, 951449, 2562524, -1},
};
static const SlipRaceBonusSpawn SlipRaceBonus_track2[] = {
    {3682271, 1080070, 4728334, -1}, {4060299, 1005057, 3043198, -1}, {3441767, 1214313, 1610055, -1},
    {3095478, 972225, 2750483, -1},  {2769546, 971896, 5082829, -1},  {1734520, 1004657, 6459824, -1},
    {3624824, 1082331, 7037998, -1}, {3948361, 1105160, 6195724, -1},
};
static const SlipRaceBonusSpawn SlipRaceBonus_track3[] = {
    {4937029, 8281, 4844685, -1},  {4785724, 76933, 6281818, -1}, {8422638, 22925, 8007163, -1},
    {8407634, 7827, 8075775, -1},  {9817137, 2958, 7042977, -1},  {525926, 12885, 5739363, -1},
    {6870460, 12864, 4293594, -1}, {4890133, 49095, 4647822, 2},  {4797340, 57997, 4580666, 0},
    {7214012, 19708, 4223930, 1},  {7108765, 20690, 4224009, 4},
};
static const SlipRaceBonusSpawn SlipRaceBonus_track4[] = {
    {1708968, 12087, 4985545, -1}, {1601176, 10382, 6581162, -1}, {3801515, 19909, 6064694, -1},
    {2603447, 22665, 6881581, -1}, {4899414, 15681, 7516487, -1}, {4644993, 16873, 4911576, -1},
    {3329826, 78636, 4816613, -1}, {3250051, 88608, 4618699, -1},
};
static const SlipRaceBonusSpawn SlipRaceBonus_track5[] = {
    {4695002, 1079880, 4558001, -1}, {4362735, 504685, 6406296, -1},  {4764864, 60871, 5249106, -1},
    {1769935, 26612, 4809029, -1},   {1299238, 78864, 3258038, -1},   {2808305, 401445, 2069330, -1},
    {4114084, 831561, 448444, -1},   {4781952, 1088911, 2609963, -1}, {2693139, 52096, 4350207, -1},
    {2507314, 85715, 3839517, -1},
};
static const SlipRaceBonusSpawn SlipRaceBonus_track6[] = {
    {4364242, 1086182, 6066072, 2},  {2205974, 1127033, 1433416, 3}, {5339988, 940000, 235984, 4},
    {4650370, 1099342, 2229710, -1}, {4885935, 1099373, 2376241, 2}, {1460863, 1121571, 2077513, -1},
    {3127209, 1070170, 1379775, -1}, {2982470, 1067951, 5417395, 1}, {1733979, 1114937, 3890654, 0},
    {3899994, 1106362, 1112087, 1},  {4956321, 1128417, 1707861, 4}, {2008889, 1121674, 3978762, 1},
    {1941755, 1125563, 3700861, 0},  {1906952, 1127055, 3577365, 2},
};
static const SlipRaceBonusSpawn SlipRaceBonus_track7[] = {
    {3599152, 27783, 5704161, -1}, {2916256, 7139, 6106248, -1}, {45992088, 17449, 6204030, -1},
    {1822408, 5435, 5466282, -1},  {3157370, 5940, 7635544, -1}, {3940135, 10183, 6358948, -1},
    {4443223, 9705, 6453732, -1},  {4837207, 9972, 4002313, -1}, {3661287, 13198, 2604843, -1},
};
static const SlipRaceBonusSpawn SlipRaceBonus_track8[] = {
    {3204053, 1582791, 8776302, -1}, {2099858, 1578623, 9858409, -1}, {1421598, 1503799, 8669736, -1},
    {2195147, 1300687, 7007816, -1}, {2552395, 1313064, 7074450, -1}, {4055975, 1581129, 3489124, -1},
    {3730387, 1561816, 2659065, -1}, {3672386, 1561224, 2678540, -1}, {4404006, 1545906, 1719949, -1},
    {4797748, 1469660, 3773734, -1}, {4590645, 1518660, 6472006, -1}, {2869593, 1604604, 7340133, -1},
};
static const SlipRaceBonusSpawn SlipRaceBonus_track9[] = {
    {2858526, 857513, 4863366, -1},  {4055112, 866039, 5130264, -1},  {5597440, 915041, 4963300, -1},
    {5766439, 937256, 3485513, -1},  {4379537, 1002123, 3110810, -1}, {3413818, 1059378, 2355117, -1},
    {1823596, 1055808, 2181361, -1}, {242709, 907047, 1883390, -1},   {1177016, 919511, 529280, -1},
    {2898320, 860205, 1181613, -1},  {5348449, 610858, 6136333, -1},  {3922194, 785526, 4758994, -1},
};
static const SlipRaceBonusSpawn SlipRaceBonus_track10[] = {
    {4754313, 14503, 8991532, -1}, {5509914, 4232, 7516298, -1},   {6933906, 21064, 7020445, -1},
    {6918405, 28964, 9462829, -1}, {4917817, 16792, 10035679, -1},
};

#define BONUS_TABLE(records) {records, sizeof(records) / sizeof((records)[0])}
const SlipRaceBonusSpawnTable SlipRaceBonus_trackTables[] = {
    BONUS_TABLE(SlipRaceBonus_track1),  BONUS_TABLE(SlipRaceBonus_track2), BONUS_TABLE(SlipRaceBonus_track3),
    BONUS_TABLE(SlipRaceBonus_track4),  BONUS_TABLE(SlipRaceBonus_track5), BONUS_TABLE(SlipRaceBonus_track6),
    BONUS_TABLE(SlipRaceBonus_track7),  BONUS_TABLE(SlipRaceBonus_track8), BONUS_TABLE(SlipRaceBonus_track9),
    BONUS_TABLE(SlipRaceBonus_track10),
};
#undef BONUS_TABLE

void SlipRaceBonus_BindHostContext(SlipRaceBonusHostBindings *bindings) { SlipRaceBonus_hostBindings = bindings; }

uint32_t SlipRaceBonus_Event(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                             uint16_t objectOffset, uintptr_t dispatchData, uint32_t dispatchFrame) {
	SlipRaceBonusPrivate *state;

	(void)eventPayload;
	(void)eventValue;
	(void)eventFlags;
	(void)dispatchData;
	(void)dispatchFrame;
	state = (SlipRaceBonusPrivate *)(void *)SlipObject_PrivateState(objectOffset);
	switch ((SlipObjectEvent)(eventCode & 0xffffu)) {
	case SLIP_OBJECT_EVENT_COLLISION_STOP:
		(void)SlipRaceCollision_RemoveBody(objectOffset);
		SlipObject_Free(objectOffset, 0, 0, 0, 0, 0, 0);
		return 0;
	case SLIP_OBJECT_EVENT_UPDATE:
		if (state->remainingMilliseconds >= 0) {
			state->remainingMilliseconds -= (int32_t)SlipFrameTimer_Values().deltaMilliseconds;
			if (state->remainingMilliseconds < 0) {
				SlipObject_Free(objectOffset, 0, 0, 0, 0, 0, 0);
			}
		}
		return 0;
	default:
		return 1;
	}
}

void SlipRaceBonus_Create(SlipView3DVec32 position, int32_t remainingTime, int32_t bonusType) {
	SlipRaceBonusHostBindings *const bindings = SlipRaceBonus_hostBindings;
	SlipRacePlayerHostBindings *const player = bindings->playerBindings;
	SlipObjectSlotFill fill;
	SlipObjectActorHandleWriteResult actorHandleWrite;
	SlipObjectSlotDataWriteResult setActor;
	SlipObjectExtentWriteResult setRadius;
	SlipTrackWorldAddSlot addSlot;
	SlipRaceBonusPrivate *state;
	uint16_t objectOffset;

	if (bonusType == SLIP_RACE_BONUS_CREDITS && SlipRace_type != SLIP_RACE_TYPE_CHAMPIONSHIP) {
		bonusType = SLIP_RACE_BONUS_POWERUP_RECHARGE;
	}
	(void)SlipObject_SlotFill(&SlipRaceBonus_identity, (uint32_t)position.x, (uint32_t)position.y, (uint32_t)position.z,
	                          TrackView_ExecuteBonusDrawCallback, 0, SlipRaceBonus_Event, &fill);
	if (fill.carryOut) {
		return;
	}
	objectOffset = (uint16_t)fill.objectOffset;
	state = (SlipRaceBonusPrivate *)(void *)SlipObject_PrivateState(objectOffset);
	if (bonusType < 0) {
		if (SlipRace_type == SLIP_RACE_TYPE_CHAMPIONSHIP) {
			bonusType = SlipRaceBonus_championshipTypes[SlipRandom_Range(
			    (uint16_t)(sizeof(SlipRaceBonus_championshipTypes) / sizeof(SlipRaceBonus_championshipTypes[0]) - 1u))];
		} else {
			bonusType = SlipRaceBonus_normalTypes[SlipRandom_Range(
			    (uint16_t)(sizeof(SlipRaceBonus_normalTypes) / sizeof(SlipRaceBonus_normalTypes[0]) - 1u))];
		}
	}
	if (SlipRace_gameMode != 0 &&
	    (bonusType == SLIP_RACE_BONUS_ENGINE_REPAIR || bonusType == SLIP_RACE_BONUS_CONTROL_REPAIR)) {
		bonusType = SLIP_RACE_BONUS_POWERUP_RECHARGE;
	}
	state->bonusType = bonusType;
	state->remainingMilliseconds = remainingTime;
	(void)SlipObject_SetDrawData(player->objectTable, player->objectTableBytes, objectOffset,
	                             bindings->resourceHandles[bonusType], &setActor);
	(void)SlipObject_SetActorHandle(objectOffset, 1u, &actorHandleWrite);
	(void)SlipObject_SetDrawExtent(player->objectTable, player->objectTableBytes, objectOffset, 0x1c98u, &setRadius);
	if (SlipRaceCollision_CreateBody(objectOffset, 2u)) {
		SlipObject_Free(objectOffset, 0, 0, 0, 0, 0, 0);
		return;
	}
	SlipRaceCollision_SetBodyBounds(objectOffset, -0x2620, -0x2620, -0x2620, 0x2620, 0x2620, 0x2620);
	(void)SlipTrackWorld_AddSlot(
	    objectOffset, 2u, 1u, bindings->slotDrawBase, bindings->slotDrawBytes, bindings->slotDrawCallbacks,
	    bindings->slotDrawCallbackCount, bindings->slotDrawBaseAddress, bindings->slotDrawFreeListAddress,
	    player->slotListBase, player->slotListBytes, player->slotListBaseOffset, player->slotListSentinelOffset,
	    bindings->slotListFreeListAddress, player->objectTable, player->objectTableBytes, player->articSlotPool,
	    player->articSlotPoolBytes, player->articSlotPoolOffset, player->trdBase, player->trackDataSize,
	    player->trackDataOffset, player->componentBase, player->componentBaseBytes, player->componentBaseOffset,
	    player->trackTable, player->trackTableBytes, &addSlot);
	if (addSlot.carryOut || SlipRaceCollision_Query(objectOffset)) {
		SlipObject_Free(objectOffset, 0, 0, 0, 0, 0, 0);
		return;
	}
}
