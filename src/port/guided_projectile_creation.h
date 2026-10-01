#ifndef SLIP_GUIDED_PROJECTILE_CREATION_H
#define SLIP_GUIDED_PROJECTILE_CREATION_H

#include "race_player.h"

typedef struct SlipGuidedProjectileCalls {
	void *context;
	const uint16_t *playerObject;
	const uint16_t *shapeHandle;
	void (*voice)(void *, uint32_t selection);
	int32_t (*getSpeed)(void *, uint16_t object);
	SlipView3DVec32 (*weaponMountPosition)(void *, uint16_t object, uint32_t side);
	SlipView3DMatrix *(*copyObjectMatrix)(void *, uint16_t object);
	void (*orthonormalize)(void *, SlipView3DMatrix *matrix);
	bool (*fill)(void *, const SlipView3DMatrix **matrixSource, SlipView3DVec32 position, SlipObjectDrawCallback draw,
	             uint16_t shape, SlipObjectEventCallback event, uint16_t *object);
	bool (*reclaim)(void *);
	void (*setObjectFlags)(void *, uint16_t object, uint32_t flags);
	SlipRacePlayerProjectileState *(*private)(void *, uint16_t object);
	bool (*body)(void *, uint16_t object, uint16_t flags);
	void (*shapeBounds)(void *, uint16_t shape, SlipView3DVec32 *minimum, SlipView3DVec32 *maximum);
	void (*setBodyBounds)(void *, uint16_t object, SlipView3DVec32 minimum, SlipView3DVec32 maximum);
	bool (*track)(void *, uint16_t object, uint16_t shape, uint32_t flags);
	void (*setSpeed)(void *, uint16_t object, int32_t speed);
	void (*exclude)(void *, uint16_t object, uint16_t shooter);
	void (*smoke)(void *, uint16_t object, SlipView3DVec32 position, int32_t lifetime, uint32_t type);
	void (*camera)(void *, uint16_t object, uint16_t shooter);
	void (*queueSound)(void *, uint32_t positionX, uint32_t positionY, uint32_t positionZ, uint32_t sound,
	                   uint16_t object, uint32_t flags);
	void (*free)(void *, uint16_t object);
	SlipObjectDrawCallback drawCallback;
	SlipObjectEventCallback eventCallback;
	SlipView3DVec32 (*objectPosition)(void *, uint16_t object);
	SlipView3DVec16 (*normalize)(void *, SlipView3DVec32 vector);
	void (*setDirection)(void *, uint16_t object, SlipView3DVec16 direction);
	void (*objectSound)(void *, uint32_t effect, uint16_t object, uint16_t mode);
} SlipGuidedProjectileCalls;

extern uint16_t SlipGuidedProjectile_superSeekerShooter;
extern uint16_t SlipGuidedProjectile_superFragShooter;
void SlipGuidedProjectile_FireSuperFrag(uint16_t shooter, uint16_t target, const SlipGuidedProjectileCalls *calls);
void SlipGuidedProjectile_FireSuperSeeker(uint16_t shooter, uint16_t target, const SlipGuidedProjectileCalls *calls);
extern uint16_t SlipGuidedProjectile_fragShooter;
void SlipGuidedProjectile_FireFrag(uint16_t shooter, uint16_t target, const SlipGuidedProjectileCalls *calls);
extern uint16_t SlipGuidedProjectile_scramblerShooter;
void SlipGuidedProjectile_FireScrambler(uint16_t shooter, uint16_t target, const SlipGuidedProjectileCalls *calls);
extern uint16_t SlipGuidedProjectile_seekerShooter;
void SlipGuidedProjectile_FireSeeker(uint16_t shooter, uint16_t target, const SlipGuidedProjectileCalls *calls);
extern uint16_t SlipGuidedProjectile_bomberShooter;
void SlipGuidedProjectile_FireBomber(uint16_t shooter, uint16_t target, const SlipGuidedProjectileCalls *calls);
extern uint16_t SlipGuidedProjectile_disrupterShooter;
extern uint16_t SlipGuidedProjectile_disrupterTarget;
void SlipGuidedProjectile_FireDisrupter(uint16_t shooter, uint16_t target, const SlipGuidedProjectileCalls *calls);
extern uint16_t SlipGuidedProjectile_amblerShooter;
extern uint16_t SlipGuidedProjectile_amblerTarget;
void SlipGuidedProjectile_FireAmbler(uint16_t shooter, uint16_t target, const SlipGuidedProjectileCalls *calls);
extern uint16_t SlipGuidedProjectile_hyperNeuroShooter;
extern uint16_t SlipGuidedProjectile_hyperNeuroTarget;
void SlipGuidedProjectile_FireHyperNeuro(uint16_t shooter, uint16_t target, const SlipGuidedProjectileCalls *calls);

typedef struct SlipGuidedProjectileMiniMinesCalls {
	SlipGuidedProjectileCalls projectile;
	SlipView3DVec32 (*transform)(void *, const SlipView3DMatrix *, SlipView3DVec32);
} SlipGuidedProjectileMiniMinesCalls;

typedef struct SlipGuidedProjectileMiniMinesContinuation {
	bool displacedStack;
	uint32_t remainingIterations;
	uint32_t offsetTableByteOffset;
} SlipGuidedProjectileMiniMinesContinuation;

extern uint16_t SlipGuidedProjectile_miniMinesShooter;
SlipGuidedProjectileMiniMinesContinuation
SlipGuidedProjectile_FireMiniMines(uint16_t shooter, const SlipGuidedProjectileMiniMinesCalls *calls);

#endif
