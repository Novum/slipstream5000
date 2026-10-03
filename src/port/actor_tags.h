#ifndef SLIPSTREAM5000_ACTOR_TAGS_H
#define SLIPSTREAM5000_ACTOR_TAGS_H

/* Four-character part and attachment-point keys in ART resources. */
#define SLIP_ACTOR_PART_MAIN 0x6d61696eu         /* main */
#define SLIP_ACTOR_POINT_HEAD 0x68656164u        /* head */
#define SLIP_ACTOR_PART_DRIVER 0x64727631u       /* drv1 */
#define SLIP_ACTOR_POINT_SMOKE 0x736d6f6bu       /* smok */
#define SLIP_ACTOR_POINT_WEAPON 0x77656170u      /* weap */
#define SLIP_ACTOR_POINT_LEFT_LASER 0x6c61736cu  /* lasl */
#define SLIP_ACTOR_POINT_RIGHT_LASER 0x6c617372u /* lasr */
#define SLIP_ACTOR_FIRST_FAN 0x66616e31u         /* fan1 */
#define SLIP_ACTOR_FIRST_JET 0x6a657431u         /* jet1 */

enum { SLIP_ACTOR_FAN_COUNT = 4, SLIP_ACTOR_JET_COUNT = 4 };

#endif
