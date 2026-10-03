#ifndef SLIPSTREAM5000_FIXED_POINT_H
#define SLIPSTREAM5000_FIXED_POINT_H

/* Matrices, unit vectors, palette blends and sprite scales use Q14. Angles
 * use a separate 16-bit full-turn representation with the same quarter-turn value. */
enum {
	SLIP_Q14_FRACTION_BITS = 14,
	SLIP_Q14_ONE = 1 << SLIP_Q14_FRACTION_BITS,
	SLIP_Q14_HALF = SLIP_Q14_ONE / 2,
	SLIP_Q14_DWORD_HIGH_SHIFT = 32 - SLIP_Q14_FRACTION_BITS,
	SLIP_Q14_WORD_HIGH_SHIFT = 16 - SLIP_Q14_FRACTION_BITS,
	SLIP_ANGLE_EIGHTH_TURN = 0x2000,
	SLIP_ANGLE_QUARTER_TURN = 0x4000,
	SLIP_ANGLE_HALF_TURN = 0x8000,
	SLIP_ANGLE_FULL_TURN = 0x10000
};

#endif
