#ifndef SLIPSTREAM5000_RANDOM_SEQUENCE_H
#define SLIPSTREAM5000_RANDOM_SEQUENCE_H

/* Feedback bits shared by the original 16-bit effect and dither sequences.
 * Callers retain their own increment and narrowing order. */
enum { SLIP_RANDOM_LFSR_FEEDBACK_MASK = 0xb400u };

#endif
