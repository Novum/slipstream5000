#include "guided_projectile_creation.h"
#include "race_collision.h"

uint16_t SlipGuidedProjectile_superSeekerShooter;

void SlipGuidedProjectile_FireSuperSeeker(uint16_t shooter, uint16_t target, const SlipGuidedProjectileCalls *calls) {
	enum {
		TARGETED_VOICE = 0x3a,
		SUPER_SEEKER = SLIP_RACE_WEAPON_SUPER_SEEKER,
		PROJECTILE_OBJECT_FLAGS = 2,
		PROJECTILE_BODY_FLAGS = SLIP_COLLISION_BODY_PROJECTILE,
		SHAPE_TRACK_FLAGS = 1,
		SPEED_INCREMENT = 0x22e98,
		TRAIL_OFFSET_Z = -2151,
		TRAIL_LIFETIME = 5000,
		PROJECTILE_SMOKE_TYPE = 0,
		PROJECTILE_SOUND = 5,
		SOUND_FLAGS = 1
	};

	void *const context = calls->context;
	SlipGuidedProjectile_superSeekerShooter = shooter;
	if (target == *calls->playerObject)
		calls->voice(context, TARGETED_VOICE);
	const int32_t speed = calls->getSpeed(context, shooter);
	SlipView3DVec32 position = calls->weaponMountPosition(context, shooter, 0);
	const SlipView3DMatrix *matrixSource = calls->copyObjectMatrix(context, shooter);
	const uint16_t shape = *calls->shapeHandle;
	uint16_t projectile;

	while (
	    !calls->fill(context, &matrixSource, position, calls->drawCallback, shape, calls->eventCallback, &projectile)) {
		if (!calls->reclaim(context))
			return;
	}
	calls->setObjectFlags(context, projectile, PROJECTILE_OBJECT_FLAGS);
	SlipRacePlayerProjectileState *const state = calls->private(context, projectile);
	state->targetObject = target;
	state->weaponIndex = SUPER_SEEKER;
	state->shooterObject = SlipGuidedProjectile_superSeekerShooter;
	if (!calls->body(context, projectile, PROJECTILE_BODY_FLAGS)) {
		calls->free(context, projectile);
		return;
	}
	SlipView3DVec32 minimum, maximum;
	calls->shapeBounds(context, *calls->shapeHandle, &minimum, &maximum);
	calls->setBodyBounds(context, projectile, minimum, maximum);
	if (!calls->track(context, projectile, *calls->shapeHandle, SHAPE_TRACK_FLAGS)) {
		calls->free(context, projectile);
		return;
	}
	calls->setSpeed(context, projectile, (int32_t)((uint32_t)speed + SPEED_INCREMENT));
	calls->exclude(context, projectile, SlipGuidedProjectile_superSeekerShooter);
	calls->smoke(context, projectile, (SlipView3DVec32){0, 0, TRAIL_OFFSET_Z}, TRAIL_LIFETIME, PROJECTILE_SMOKE_TYPE);
	calls->camera(context, projectile, SlipGuidedProjectile_superSeekerShooter);
	calls->queueSound(context, 0, SlipGuidedProjectile_superSeekerShooter, (uint32_t)TRAIL_OFFSET_Z, PROJECTILE_SOUND,
	                  SlipGuidedProjectile_superSeekerShooter, SOUND_FLAGS);
}

uint16_t SlipGuidedProjectile_superFragShooter;

void SlipGuidedProjectile_FireSuperFrag(uint16_t shooter, uint16_t target, const SlipGuidedProjectileCalls *calls) {
	enum {
		TARGETED_VOICE = 0x38,
		SUPER_FRAG = SLIP_RACE_WEAPON_SUPER_FRAG,
		PROJECTILE_OBJECT_FLAGS = 2,
		PROJECTILE_BODY_FLAGS = SLIP_COLLISION_BODY_PROJECTILE,
		SHAPE_TRACK_FLAGS = 1,
		SPEED_INCREMENT = 0x22e98,
		TRAIL_OFFSET_Z = -2151,
		TRAIL_LIFETIME = 5000,
		PROJECTILE_SMOKE_TYPE = 0,
		PROJECTILE_SOUND = 5,
		SOUND_FLAGS = 1
	};

	void *const context = calls->context;
	SlipGuidedProjectile_superFragShooter = shooter;
	if (target == *calls->playerObject)
		calls->voice(context, TARGETED_VOICE);
	const int32_t speed = calls->getSpeed(context, shooter);
	SlipView3DVec32 position = calls->weaponMountPosition(context, shooter, 0);
	const SlipView3DMatrix *matrixSource = calls->copyObjectMatrix(context, shooter);
	const uint16_t shape = *calls->shapeHandle;
	uint16_t projectile;

	while (
	    !calls->fill(context, &matrixSource, position, calls->drawCallback, shape, calls->eventCallback, &projectile)) {
		if (!calls->reclaim(context))
			return;
	}
	calls->setObjectFlags(context, projectile, PROJECTILE_OBJECT_FLAGS);
	SlipRacePlayerProjectileState *const state = calls->private(context, projectile);
	state->targetObject = target;
	state->weaponIndex = SUPER_FRAG;
	state->shooterObject = SlipGuidedProjectile_superFragShooter;
	if (!calls->body(context, projectile, PROJECTILE_BODY_FLAGS)) {
		calls->free(context, projectile);
		return;
	}
	SlipView3DVec32 minimum, maximum;
	calls->shapeBounds(context, *calls->shapeHandle, &minimum, &maximum);
	calls->setBodyBounds(context, projectile, minimum, maximum);
	if (!calls->track(context, projectile, *calls->shapeHandle, SHAPE_TRACK_FLAGS)) {
		calls->free(context, projectile);
		return;
	}
	calls->setSpeed(context, projectile, (int32_t)((uint32_t)speed + SPEED_INCREMENT));
	calls->exclude(context, projectile, SlipGuidedProjectile_superFragShooter);
	calls->smoke(context, projectile, (SlipView3DVec32){0, 0, TRAIL_OFFSET_Z}, TRAIL_LIFETIME, PROJECTILE_SMOKE_TYPE);
	calls->camera(context, projectile, SlipGuidedProjectile_superFragShooter);
	calls->queueSound(context, 0, SlipGuidedProjectile_superFragShooter, (uint32_t)TRAIL_OFFSET_Z, PROJECTILE_SOUND,
	                  SlipGuidedProjectile_superFragShooter, SOUND_FLAGS);
}

uint16_t SlipGuidedProjectile_fragShooter;

void SlipGuidedProjectile_FireFrag(uint16_t shooter, uint16_t target, const SlipGuidedProjectileCalls *calls) {
	enum {
		TARGETED_VOICE = 0x37,
		FRAG = SLIP_RACE_WEAPON_FRAG,
		PROJECTILE_OBJECT_FLAGS = 2,
		PROJECTILE_BODY_FLAGS = SLIP_COLLISION_BODY_PROJECTILE,
		SHAPE_TRACK_FLAGS = 1,
		SPEED_INCREMENT = 0x22e98,
		TRAIL_OFFSET_Z = -2151,
		TRAIL_LIFETIME = 5000,
		PROJECTILE_SMOKE_TYPE = 0,
		PROJECTILE_SOUND = 5,
		SOUND_FLAGS = 1
	};

	void *const context = calls->context;
	SlipGuidedProjectile_fragShooter = shooter;
	if (target == *calls->playerObject)
		calls->voice(context, TARGETED_VOICE);
	const int32_t speed = calls->getSpeed(context, shooter);
	SlipView3DVec32 position = calls->weaponMountPosition(context, shooter, 0);
	const SlipView3DMatrix *matrixSource = calls->copyObjectMatrix(context, shooter);
	const uint16_t shape = *calls->shapeHandle;
	uint16_t projectile;

	while (
	    !calls->fill(context, &matrixSource, position, calls->drawCallback, shape, calls->eventCallback, &projectile)) {
		if (!calls->reclaim(context))
			return;
	}
	calls->setObjectFlags(context, projectile, PROJECTILE_OBJECT_FLAGS);
	SlipRacePlayerProjectileState *const state = calls->private(context, projectile);
	state->targetObject = target;
	state->weaponIndex = FRAG;
	state->shooterObject = SlipGuidedProjectile_fragShooter;
	if (!calls->body(context, projectile, PROJECTILE_BODY_FLAGS)) {
		calls->free(context, projectile);
		return;
	}
	SlipView3DVec32 minimum, maximum;
	calls->shapeBounds(context, *calls->shapeHandle, &minimum, &maximum);
	calls->setBodyBounds(context, projectile, minimum, maximum);
	if (!calls->track(context, projectile, *calls->shapeHandle, SHAPE_TRACK_FLAGS)) {
		calls->free(context, projectile);
		return;
	}
	calls->setSpeed(context, projectile, (int32_t)((uint32_t)speed + SPEED_INCREMENT));
	calls->exclude(context, projectile, SlipGuidedProjectile_fragShooter);
	calls->smoke(context, projectile, (SlipView3DVec32){0, 0, TRAIL_OFFSET_Z}, TRAIL_LIFETIME, PROJECTILE_SMOKE_TYPE);
	calls->camera(context, projectile, SlipGuidedProjectile_fragShooter);
	calls->queueSound(context, 0, SlipGuidedProjectile_fragShooter, (uint32_t)TRAIL_OFFSET_Z, PROJECTILE_SOUND,
	                  SlipGuidedProjectile_fragShooter, SOUND_FLAGS);
}

uint16_t SlipGuidedProjectile_scramblerShooter;

void SlipGuidedProjectile_FireScrambler(uint16_t shooter, uint16_t target, const SlipGuidedProjectileCalls *calls) {
	enum {
		TARGETED_VOICE = 0x3c,
		SCRAMBLER = SLIP_RACE_WEAPON_SCRAMBLER,
		PROJECTILE_OBJECT_FLAGS = 2,
		PROJECTILE_BODY_FLAGS = SLIP_COLLISION_BODY_PROJECTILE,
		SHAPE_TRACK_FLAGS = 1,
		SPEED_INCREMENT = 0x1174c,
		TRAIL_OFFSET_Z = -2151,
		TRAIL_LIFETIME = 5000,
		PROJECTILE_SMOKE_TYPE = 0,
		PROJECTILE_SOUND = 5,
		SOUND_FLAGS = 1
	};

	void *const context = calls->context;
	SlipGuidedProjectile_scramblerShooter = shooter;
	if (target == *calls->playerObject)
		calls->voice(context, TARGETED_VOICE);
	const int32_t speed = calls->getSpeed(context, shooter);
	SlipView3DVec32 position = calls->weaponMountPosition(context, shooter, 0);
	SlipView3DMatrix *const launchMatrix = calls->copyObjectMatrix(context, shooter);

	enum { MATRIX_FIRST_ROW_Y = 1 };

	launchMatrix->m[MATRIX_FIRST_ROW_Y] = 0;
	calls->orthonormalize(context, launchMatrix);
	const SlipView3DMatrix *matrixSource = launchMatrix;
	const uint16_t shape = *calls->shapeHandle;
	uint16_t projectile;

	while (
	    !calls->fill(context, &matrixSource, position, calls->drawCallback, shape, calls->eventCallback, &projectile)) {
		if (!calls->reclaim(context))
			return;
	}
	calls->setObjectFlags(context, projectile, PROJECTILE_OBJECT_FLAGS);
	SlipRacePlayerProjectileState *const state = calls->private(context, projectile);
	state->targetObject = target;
	state->weaponIndex = SCRAMBLER;
	state->shooterObject = SlipGuidedProjectile_scramblerShooter;
	if (!calls->body(context, projectile, PROJECTILE_BODY_FLAGS)) {
		calls->free(context, projectile);
		return;
	}
	SlipView3DVec32 minimum, maximum;
	calls->shapeBounds(context, *calls->shapeHandle, &minimum, &maximum);
	calls->setBodyBounds(context, projectile, minimum, maximum);
	if (!calls->track(context, projectile, *calls->shapeHandle, SHAPE_TRACK_FLAGS)) {
		calls->free(context, projectile);
		return;
	}
	calls->setSpeed(context, projectile, (int32_t)((uint32_t)speed + SPEED_INCREMENT));
	calls->exclude(context, projectile, SlipGuidedProjectile_scramblerShooter);
	calls->smoke(context, projectile, (SlipView3DVec32){0, 0, TRAIL_OFFSET_Z}, TRAIL_LIFETIME, PROJECTILE_SMOKE_TYPE);
	calls->camera(context, projectile, SlipGuidedProjectile_scramblerShooter);
	calls->queueSound(context, 0, SlipGuidedProjectile_scramblerShooter, (uint32_t)TRAIL_OFFSET_Z, PROJECTILE_SOUND,
	                  SlipGuidedProjectile_scramblerShooter, SOUND_FLAGS);
}

uint16_t SlipGuidedProjectile_seekerShooter;

void SlipGuidedProjectile_FireSeeker(uint16_t shooter, uint16_t target, const SlipGuidedProjectileCalls *calls) {
	enum {
		TARGETED_VOICE = 0x39,
		SEEKER = SLIP_RACE_WEAPON_SEEKER,
		PROJECTILE_OBJECT_FLAGS = 2,
		PROJECTILE_BODY_FLAGS = SLIP_COLLISION_BODY_PROJECTILE,
		SHAPE_TRACK_FLAGS = 1,
		SPEED_INCREMENT = 0x22e98,
		TRAIL_OFFSET_Z = -2151,
		TRAIL_LIFETIME = 5000,
		PROJECTILE_SMOKE_TYPE = 0,
		PROJECTILE_SOUND = 5,
		SOUND_FLAGS = 1
	};

	void *const context = calls->context;
	SlipGuidedProjectile_seekerShooter = shooter;
	if (target == *calls->playerObject)
		calls->voice(context, TARGETED_VOICE);
	const int32_t speed = calls->getSpeed(context, shooter);
	SlipView3DVec32 position = calls->weaponMountPosition(context, shooter, 0);
	const SlipView3DMatrix *matrixSource = calls->copyObjectMatrix(context, shooter);
	const uint16_t shape = *calls->shapeHandle;
	uint16_t projectile;

	while (
	    !calls->fill(context, &matrixSource, position, calls->drawCallback, shape, calls->eventCallback, &projectile)) {
		if (!calls->reclaim(context))
			return;
	}
	calls->setObjectFlags(context, projectile, PROJECTILE_OBJECT_FLAGS);
	SlipRacePlayerProjectileState *const state = calls->private(context, projectile);
	state->targetObject = target;
	state->weaponIndex = SEEKER;
	state->shooterObject = SlipGuidedProjectile_seekerShooter;
	if (!calls->body(context, projectile, PROJECTILE_BODY_FLAGS)) {
		calls->free(context, projectile);
		return;
	}
	SlipView3DVec32 minimum, maximum;
	calls->shapeBounds(context, *calls->shapeHandle, &minimum, &maximum);
	calls->setBodyBounds(context, projectile, minimum, maximum);
	if (!calls->track(context, projectile, *calls->shapeHandle, SHAPE_TRACK_FLAGS)) {
		calls->free(context, projectile);
		return;
	}
	calls->setSpeed(context, projectile, (int32_t)((uint32_t)speed + SPEED_INCREMENT));
	calls->exclude(context, projectile, SlipGuidedProjectile_seekerShooter);
	calls->smoke(context, projectile, (SlipView3DVec32){0, 0, TRAIL_OFFSET_Z}, TRAIL_LIFETIME, PROJECTILE_SMOKE_TYPE);
	calls->camera(context, projectile, SlipGuidedProjectile_seekerShooter);
	calls->queueSound(context, 0, SlipGuidedProjectile_seekerShooter, (uint32_t)TRAIL_OFFSET_Z, PROJECTILE_SOUND,
	                  SlipGuidedProjectile_seekerShooter, SOUND_FLAGS);
}

uint16_t SlipGuidedProjectile_bomberShooter;

void SlipGuidedProjectile_FireBomber(uint16_t shooter, uint16_t target, const SlipGuidedProjectileCalls *calls) {
	enum {
		TARGETED_VOICE = 0x3e,
		BOMBER = 10,
		PROJECTILE_OBJECT_FLAGS = 2,
		PROJECTILE_BODY_FLAGS = SLIP_COLLISION_BODY_PROJECTILE,
		SHAPE_TRACK_FLAGS = 1,
		SPEED_INCREMENT = 0x22e98,
		TRAIL_OFFSET_Z = -2151,
		TRAIL_LIFETIME = 5000,
		PROJECTILE_SMOKE_TYPE = 0,
		PROJECTILE_SOUND = 5,
		SOUND_FLAGS = 1
	};

	void *const context = calls->context;
	SlipGuidedProjectile_bomberShooter = shooter;
	if (target == *calls->playerObject)
		calls->voice(context, TARGETED_VOICE);
	const int32_t speed = calls->getSpeed(context, shooter);
	SlipView3DVec32 position = calls->weaponMountPosition(context, shooter, 0);
	const SlipView3DMatrix *matrixSource = calls->copyObjectMatrix(context, shooter);
	const uint16_t shape = *calls->shapeHandle;
	uint16_t projectile;

	while (
	    !calls->fill(context, &matrixSource, position, calls->drawCallback, shape, calls->eventCallback, &projectile)) {
		if (!calls->reclaim(context))
			return;
	}
	calls->setObjectFlags(context, projectile, PROJECTILE_OBJECT_FLAGS);
	SlipRacePlayerProjectileState *const state = calls->private(context, projectile);
	state->targetObject = target;
	state->weaponIndex = BOMBER;
	state->shooterObject = SlipGuidedProjectile_bomberShooter;
	if (!calls->body(context, projectile, PROJECTILE_BODY_FLAGS)) {
		calls->free(context, projectile);
		return;
	}
	SlipView3DVec32 minimum, maximum;
	calls->shapeBounds(context, *calls->shapeHandle, &minimum, &maximum);
	calls->setBodyBounds(context, projectile, minimum, maximum);
	if (!calls->track(context, projectile, *calls->shapeHandle, SHAPE_TRACK_FLAGS)) {
		calls->free(context, projectile);
		return;
	}
	calls->setSpeed(context, projectile, (int32_t)((uint32_t)speed + SPEED_INCREMENT));
	calls->exclude(context, projectile, SlipGuidedProjectile_bomberShooter);
	calls->smoke(context, projectile, (SlipView3DVec32){0, 0, TRAIL_OFFSET_Z}, TRAIL_LIFETIME, PROJECTILE_SMOKE_TYPE);
	calls->camera(context, projectile, SlipGuidedProjectile_bomberShooter);
	calls->queueSound(context, 0, SlipGuidedProjectile_bomberShooter, (uint32_t)TRAIL_OFFSET_Z, PROJECTILE_SOUND,
	                  SlipGuidedProjectile_bomberShooter, SOUND_FLAGS);
}

uint16_t SlipGuidedProjectile_disrupterShooter;
uint16_t SlipGuidedProjectile_disrupterTarget;

static void SlipGuidedProjectile_CreateDisrupter(uint16_t shooter, uint32_t side,
                                                 const SlipGuidedProjectileCalls *calls) {
	enum { PROJECTILE_OBJECT_FLAGS = 2, DISRUPTER = SLIP_RACE_WEAPON_DISRUPTER, PROJECTILE_LIFETIME = 5000 };

	void *const context = calls->context;
	SlipView3DVec32 position = calls->weaponMountPosition(context, shooter, side);
	const SlipView3DMatrix *matrixSource = calls->copyObjectMatrix(context, shooter);
	uint16_t projectile;

	while (!calls->fill(context, &matrixSource, position, NULL, 0, calls->eventCallback, &projectile)) {
		if (!calls->reclaim(context))
			return;
	}
	calls->setObjectFlags(context, projectile, PROJECTILE_OBJECT_FLAGS);
	SlipRacePlayerProjectileState *const state = calls->private(context, projectile);
	state->weaponIndex = DISRUPTER;
	state->remainingTime = PROJECTILE_LIFETIME;
	state->shooterObject = SlipGuidedProjectile_disrupterShooter;
	state->targetObject = SlipGuidedProjectile_disrupterTarget;
	if (state->targetObject != 0) {

		SlipView3DVec32 origin = calls->objectPosition(context, projectile);
		SlipView3DVec32 target = calls->objectPosition(context, state->targetObject);
		SlipView3DVec32 delta = {(int32_t)((uint32_t)target.x - (uint32_t)origin.x),
		                         (int32_t)((uint32_t)target.y - (uint32_t)origin.y),
		                         (int32_t)((uint32_t)target.z - (uint32_t)origin.z)};
		SlipView3DVec16 direction = calls->normalize(context, delta);
		calls->setDirection(context, projectile, direction);
	}
}

void SlipGuidedProjectile_FireDisrupter(uint16_t shooter, uint16_t target, const SlipGuidedProjectileCalls *calls) {
	enum {
		TARGETED_VOICE = 0x36,
		FIRST_LASER = SLIP_RACE_WEAPON_MUZZLE_RIGHT,
		SECOND_LASER = SLIP_RACE_WEAPON_MUZZLE_LEFT,
		SHOT_SOUND = 4,
		OBJECT_SOUND_MODE = 1
	};

	SlipGuidedProjectile_disrupterShooter = shooter;
	SlipGuidedProjectile_disrupterTarget = target;
	if (target == *calls->playerObject)
		calls->voice(calls->context, TARGETED_VOICE);
	SlipGuidedProjectile_CreateDisrupter(shooter, FIRST_LASER, calls);
	SlipGuidedProjectile_CreateDisrupter(shooter, SECOND_LASER, calls);
	calls->objectSound(calls->context, SHOT_SOUND, SlipGuidedProjectile_disrupterShooter, OBJECT_SOUND_MODE);
}

uint16_t SlipGuidedProjectile_amblerShooter;
uint16_t SlipGuidedProjectile_amblerTarget;

void SlipGuidedProjectile_FireAmbler(uint16_t shooter, uint16_t target, const SlipGuidedProjectileCalls *calls) {
	enum {
		WEAPON_INDEX = SLIP_RACE_WEAPON_AMBLER,
		OBJECT_FLAGS = 2,
		BODY_FLAGS = SLIP_COLLISION_BODY_PROJECTILE,
		TRACK_FLAGS = 1,
		INITIAL_SPEED_INCREMENT = 0x45d30,
		SHOT_SOUND = 5,
		OBJECT_SOUND_MODE = 1
	};

	void *const context = calls->context;
	SlipGuidedProjectile_amblerShooter = shooter;
	SlipGuidedProjectile_amblerTarget = target;

	enum { TARGETED_VOICE = 0x3b };

	if (target == *calls->playerObject)
		calls->voice(context, TARGETED_VOICE);
	uint32_t speed = (uint32_t)calls->getSpeed(context, shooter) + INITIAL_SPEED_INCREMENT;
	SlipView3DVec32 position = calls->weaponMountPosition(context, shooter, 0);
	const SlipView3DMatrix *matrixSource = calls->copyObjectMatrix(context, shooter);
	const uint16_t shape = *calls->shapeHandle;
	uint16_t projectile;
	while (
	    !calls->fill(context, &matrixSource, position, calls->drawCallback, shape, calls->eventCallback, &projectile)) {
		if (!calls->reclaim(context))
			return;
	}
	calls->setObjectFlags(context, projectile, OBJECT_FLAGS);
	if (!calls->body(context, projectile, BODY_FLAGS)) {
		calls->free(context, projectile);
		return;
	}
	SlipView3DVec32 minimum, maximum;
	calls->shapeBounds(context, *calls->shapeHandle, &minimum, &maximum);
	calls->setBodyBounds(context, projectile, minimum, maximum);
	if (!calls->track(context, projectile, *calls->shapeHandle, TRACK_FLAGS)) {
		calls->free(context, projectile);
		return;
	}

	enum { ADDITIONAL_SPEED_INCREMENT = 0x22e98 };

	speed += ADDITIONAL_SPEED_INCREMENT;
	calls->setSpeed(context, projectile, (int32_t)speed);
	SlipRacePlayerProjectileState *const state = calls->private(context, projectile);
	state->weaponIndex = WEAPON_INDEX;
	state->targetObject = SlipGuidedProjectile_amblerTarget;
	state->shooterObject = SlipGuidedProjectile_amblerShooter;
	calls->camera(context, projectile, SlipGuidedProjectile_amblerShooter);
	calls->objectSound(context, SHOT_SOUND, SlipGuidedProjectile_amblerShooter, OBJECT_SOUND_MODE);
}

uint16_t SlipGuidedProjectile_hyperNeuroShooter;
uint16_t SlipGuidedProjectile_hyperNeuroTarget;

void SlipGuidedProjectile_FireHyperNeuro(uint16_t shooter, uint16_t target, const SlipGuidedProjectileCalls *calls) {
	enum {
		WEAPON_INDEX = SLIP_RACE_WEAPON_HYPER_NEURO,
		OBJECT_FLAGS = 2,
		BODY_FLAGS = SLIP_COLLISION_BODY_PROJECTILE,
		TRACK_FLAGS = 1,
		INITIAL_SPEED_INCREMENT = 0x45d30,
		SHOT_SOUND = 5,
		OBJECT_SOUND_MODE = 1
	};

	void *const context = calls->context;
	SlipGuidedProjectile_hyperNeuroShooter = shooter;
	SlipGuidedProjectile_hyperNeuroTarget = target;
	const uint32_t speed = (uint32_t)calls->getSpeed(context, shooter) + INITIAL_SPEED_INCREMENT;
	SlipView3DVec32 position = calls->weaponMountPosition(context, shooter, 0);
	const SlipView3DMatrix *matrixSource = calls->copyObjectMatrix(context, shooter);
	const uint16_t shape = *calls->shapeHandle;
	uint16_t projectile;
	while (
	    !calls->fill(context, &matrixSource, position, calls->drawCallback, shape, calls->eventCallback, &projectile)) {
		if (!calls->reclaim(context))
			return;
	}
	calls->setObjectFlags(context, projectile, OBJECT_FLAGS);
	if (!calls->body(context, projectile, BODY_FLAGS)) {
		calls->free(context, projectile);
		return;
	}
	SlipView3DVec32 minimum, maximum;
	calls->shapeBounds(context, *calls->shapeHandle, &minimum, &maximum);
	calls->setBodyBounds(context, projectile, minimum, maximum);
	if (!calls->track(context, projectile, *calls->shapeHandle, TRACK_FLAGS)) {
		calls->free(context, projectile);
		return;
	}
	calls->setSpeed(context, projectile, (int32_t)speed);
	SlipRacePlayerProjectileState *const state = calls->private(context, projectile);
	state->weaponIndex = WEAPON_INDEX;
	state->targetObject = SlipGuidedProjectile_hyperNeuroTarget;
	state->shooterObject = SlipGuidedProjectile_hyperNeuroShooter;
	calls->camera(context, projectile, SlipGuidedProjectile_hyperNeuroShooter);
	calls->objectSound(context, SHOT_SOUND, SlipGuidedProjectile_hyperNeuroShooter, OBJECT_SOUND_MODE);
}

uint16_t SlipGuidedProjectile_miniMinesShooter;

SlipGuidedProjectileMiniMinesContinuation
SlipGuidedProjectile_FireMiniMines(uint16_t shooter, const SlipGuidedProjectileMiniMinesCalls *calls) {
	enum {
		MINE_COUNT = 4,
		MINE_SPREAD = 4880,
		OFFSET_PAIR_BYTES = 8,
		LAUNCH_OFFSET_Z = -9760,
		MATRIX_FIRST_ROW_Y = 1,
		COLLIDE_WITH_TRACK = 1,
		COLLIDE_WITH_BODIES = 2,
		COLLIDE_WITH_TRACK_AND_BODIES = COLLIDE_WITH_TRACK | COLLIDE_WITH_BODIES,
		SHAPE_BOUNDS_TRACK_SLOT = 1,
		MINI_MINES = 11,
		MINE_LIFETIME = 10000,
		MINE_SOUND = 7,
		OBJECT_POSITION_SOUND = 1,
		MINES_DEPLOYED_VOICE = 1
	};

	static const int32_t offsets[MINE_COUNT][2] = {{MINE_SPREAD, MINE_SPREAD},
	                                               {-MINE_SPREAD, -MINE_SPREAD},
	                                               {-MINE_SPREAD, MINE_SPREAD},
	                                               {MINE_SPREAD, -MINE_SPREAD}};
	const SlipGuidedProjectileCalls *const p = &calls->projectile;
	void *const context = p->context;
	SlipGuidedProjectile_miniMinesShooter = shooter;
	uint32_t remaining = MINE_COUNT;
	uint32_t tableOffset = 0;
	while (remaining != 0) {
		SlipView3DMatrix *const matrix = p->copyObjectMatrix(context, shooter);
		SlipView3DVec32 delta =
		    calls->transform(context, matrix,
		                     (SlipView3DVec32){offsets[tableOffset / OFFSET_PAIR_BYTES][0],
		                                       offsets[tableOffset / OFFSET_PAIR_BYTES][1], LAUNCH_OFFSET_Z});
		SlipView3DVec32 position = p->objectPosition(context, shooter);
		position.z = (int32_t)((uint32_t)position.z + (uint32_t)delta.z);
		position.y = (int32_t)((uint32_t)position.y + (uint32_t)delta.y);
		position.x = (int32_t)((uint32_t)position.x + (uint32_t)delta.x);
		matrix->m[MATRIX_FIRST_ROW_Y] = 0;
		p->orthonormalize(context, matrix);
		const SlipView3DMatrix *matrixSource = matrix;
		const uint16_t shape = *p->shapeHandle;
		uint16_t object;
		while (!p->fill(context, &matrixSource, position, p->drawCallback, shape, p->eventCallback, &object)) {
			if (!p->reclaim(context))
				return (SlipGuidedProjectileMiniMinesContinuation){true, remaining, tableOffset};
		}
		p->setObjectFlags(context, object, SLIP_OBJECT_FLAG_PROJECTILE);
		SlipRacePlayerProjectileState *const state = p->private(context, object);
		state->targetObject = 0;
		state->weaponIndex = MINI_MINES;
		state->remainingTime = MINE_LIFETIME;
		state->shooterObject = SlipGuidedProjectile_miniMinesShooter;
		if (!p->body(context, object, COLLIDE_WITH_TRACK_AND_BODIES)) {
			p->free(context, object);
			return (SlipGuidedProjectileMiniMinesContinuation){true, remaining, tableOffset};
		}
		SlipView3DVec32 minimum, maximum;
		p->shapeBounds(context, *p->shapeHandle, &minimum, &maximum);
		p->setBodyBounds(context, object, minimum, maximum);
		if (!p->track(context, object, *p->shapeHandle, SHAPE_BOUNDS_TRACK_SLOT)) {
			p->free(context, object);
			return (SlipGuidedProjectileMiniMinesContinuation){true, remaining, tableOffset};
		}
		p->exclude(context, object, SlipGuidedProjectile_miniMinesShooter);
		tableOffset += OFFSET_PAIR_BYTES;
		--remaining;
	}

	enum { FIRE_ROUTINE_DOS_OFFSET = 0x5d4c6, OFFSET_TABLE_END_DOS_OFFSET = 0x5d5f6 };

	p->queueSound(context, FIRE_ROUTINE_DOS_OFFSET, OFFSET_TABLE_END_DOS_OFFSET, 0, MINE_SOUND,
	              SlipGuidedProjectile_miniMinesShooter, OBJECT_POSITION_SOUND);
	if (SlipGuidedProjectile_miniMinesShooter == *p->playerObject)
		p->voice(context, MINES_DEPLOYED_VOICE);
	return (SlipGuidedProjectileMiniMinesContinuation){false, remaining, tableOffset};
}
