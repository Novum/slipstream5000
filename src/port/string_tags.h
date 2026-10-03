#ifndef SLIPSTREAM5000_STRING_TAGS_H
#define SLIPSTREAM5000_STRING_TAGS_H

/* Four-character keys in the game's string tables. Numbered keys are
 * consecutive, so callers can add a button, player, or setting index. */
#define SLIP_STRING_TRACK_TITLE_ZERO 0x54495430u           /* TIT0; tracks use one-based indices */
#define SLIP_STRING_TITLE 0x5449544cu                      /* TITL */
#define SLIP_STRING_FIRST_BUTTON 0x42555431u               /* BUT1 */
#define SLIP_STRING_FIRST_OPTION 0x4f505431u               /* OPT1 */
#define SLIP_STRING_DIFFICULTY_LEVEL_ZERO 0x4c455630u      /* LEV0 */
#define SLIP_STRING_SETTING_OFF 0x4f464630u                /* OFF0 */
#define SLIP_STRING_FIRST_CATEGORY 0x43415431u             /* CAT1 */
#define SLIP_STRING_SHADING_ZERO 0x53484130u               /* SHA0 */
#define SLIP_STRING_TEXTURE_COARSE 0x54455843u             /* TEXC */
#define SLIP_STRING_TEXTURE_FINE 0x54455846u               /* TEXF */
#define SLIP_STRING_WINDOW_ZERO 0x57494e30u                /* WIN0 */
#define SLIP_STRING_SOUND_LEVEL_ZERO 0x454e4730u           /* ENG0 */
#define SLIP_STRING_FIRST_PLAYER 0x504c5931u               /* PLY1 */
#define SLIP_STRING_SECOND_PLAYER 0x504c5932u              /* PLY2 */
#define SLIP_STRING_FIRST_CALIBRATION 0x43414c31u          /* CAL1 */
#define SLIP_STRING_SECOND_CALIBRATION 0x43414c32u         /* CAL2 */
#define SLIP_STRING_REVERSE_ACCELERATOR 0x52455641u        /* REVA */
#define SLIP_STRING_CALIBRATION_BUTTON 0x4a434231u         /* JCB1 */
#define SLIP_STRING_CONTROL_CONFLICT_TITLE 0x434e4654u     /* CNFT */
#define SLIP_STRING_CONTROL_CONFLICT 0x434f4e46u           /* CONF */
#define SLIP_STRING_JOYSTICK_DETECTION_ZERO 0x4a435431u    /* JCT1 */
#define SLIP_STRING_CONTROL_UP 0x44454630u                 /* DEF0 */
#define SLIP_STRING_CONTROL_DOWN 0x44454631u               /* DEF1 */
#define SLIP_STRING_CONTROL_LEFT 0x44454632u               /* DEF2 */
#define SLIP_STRING_CONTROL_RIGHT 0x44454633u              /* DEF3 */
#define SLIP_STRING_CONTROL_SELECT 0x44454634u             /* DEF4 */
#define SLIP_STRING_CONTROL_FIRE 0x44454635u               /* DEF5 */
#define SLIP_STRING_CONTROL_ACCELERATE 0x44454636u         /* DEF6 */
#define SLIP_STRING_CONTROL_FOOTER 0x4445464fu             /* DEFO */
#define SLIP_STRING_CONTROL_HEADER 0x4445464du             /* DEFM */
#define SLIP_STRING_VEHICLE_DESCRIPTION_PREFIX 0x43415200u /* CAR plus a vehicle digit */

#endif
