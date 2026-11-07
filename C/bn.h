#ifndef BN_H
#define BN_H

/*
 * The multiplication core, written the way GMP's mpn layer is:
 * plain arrays of 64-bit limbs plus an explicit length. No struct, no
 * capacity, no per-call allocation.
 *
 * Why this exists: the first version of this project stored every number
 * in a fixed 32 KB struct (4096 limbs). Every call zeroed and copied that
 * struct, so each multiply paid(4 microseconds of fixed cost no matter how
 * small the operands were, and each Karatsuba recursion level paid)100
 * microseconds. That drowned the arithmetic Karatsuba saves, so Karatsuba
 * could never win in any reachable size range. Here the cost of every
 * call scales with the actual operand length.
 *
 *
 * Limbs are little-endian: p[0] is the least significant limb.
 */

#include <stddef.h>
#include <stdint.h>

typedef uint64_t limb_t;

/* rp = ap + bp over n limbs; returns the carry out (0 or 1).
 * rp may equal ap or bp. */
limb_t bn_add_n(limb_t *rp, const limb_t *ap, const limb_t *bp, size_t n);

/* rp = ap - bp over n limbs; returns the borrow out (0 or 1).
 * rp may equal ap or bp. */
limb_t bn_sub_n(limb_t *rp, const limb_t *ap, const limb_t *bp, size_t n);

/* Compare two n-limb numbers: -1, 0, or +1. */
int bn_cmp_n(const limb_t *ap, const limb_t *bp, size_t n);

/* p[0..n) += v (v is a small value); returns the carry out of limb n-1. */
limb_t bn_incr(limb_t *p, size_t n, limb_t v);

/* rp[0..n) = ap[0..n) * b; returns the high limb that doesn't fit. */
limb_t bn_mul_1(limb_t *rp, const limb_t *ap, size_t n, limb_t b);

/* rp[0..n) += ap[0..n) * b; returns the carry limb. */
limb_t bn_addmul_1(limb_t *rp, const limb_t *ap, size_t n, limb_t b);

/* Schoolbook: rp[0..an+bn) = ap[0..an) * bp[0..bn). an, bn >= 1.
 * rp must not overlap ap or bp. This is the O(n^2) algorithm. */
void bn_mul_basecase(limb_t *rp, const limb_t *ap, size_t an,
                     const limb_t *bp, size_t bn);

/* Number of scratch limbs bn_mul_karatsuba needs for n-limb operands. */
size_t bn_karatsuba_scratch(size_t n, size_t threshold);

/* Karatsuba: rp[0..2n) = ap[0..n) * bp[0..n).
 * Falls back to bn_mul_basecase once n <= threshold. `threshold` is the
 * value your crossover sweep varies. scratch must hold at least
 * bn_karatsuba_scratch(n, threshold) limbs. rp must not overlap anything. */
void bn_mul_karatsuba(limb_t *rp, const limb_t *ap, const limb_t *bp,
                      size_t n, limb_t *scratch, size_t threshold);

#endif