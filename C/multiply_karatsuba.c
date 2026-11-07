/*
 * Karatsuba on raw limb arrays, with the crossover threshold exposed as a
 * runtime parameter. The sweep over `threshold` is the experiment.
 *
 * Even n = 2h:   a = a1*B^h + a0,   b = b1*B^h + b0,   B = 2^64
 *   z0 = a0*b0                       (h x h)
 *   z2 = a1*b1                       (h x h)
 *   P  = |a0 - a1| * |b0 - b1|       (h x h)
 *   middle = a0*b1 + a1*b0 = z0 + z2 - (a0-a1)(b0-b1)
 *          = z0 + z2 - P   if the two differences have the same sign
 *          = z0 + z2 + P   otherwise
 *   a*b = z2*B^2h + middle*B^h + z0
 * Three half-size multiplies instead of four. Using the DIFFERENCE (not the
 * sum a0+a1) means every intermediate stays h limbs wide: a sum can carry
 * out an extra limb, which is what makes the textbook version fiddly.
 *
 * Odd n: peel off the top limb so the rest is even, then fix up the two
 * cross terms with one-limb multiplies. That costs O(n) extra, which is
 * negligible next to the recursion.
 */

#include <stdlib.h>
#include <string.h>
#include "bn.h"
#include "bigint.h"

/* Scratch layout for an even call with half-size h:
 *   [0, 2h)        P
 *   [2h, 3h)       |a0 - a1|
 *   [3h, 4h)       |b0 - b1|
 *   [4h]           one spare limb (the combine step needs 2h+1 limbs)
 *   [4h+1, ...)    scratch for the three recursive calls (reused in turn)
 */
size_t bn_karatsuba_scratch(size_t n, size_t threshold) {
    if (threshold < 1) threshold = 1;
    if (n <= threshold || n < 2) return 0;
    if (n & 1) return bn_karatsuba_scratch(n - 1, threshold);
    size_t h = n / 2;
    return 4 * h + 1 + bn_karatsuba_scratch(h, threshold);
}

void bn_mul_karatsuba(limb_t *rp, const limb_t *ap, const limb_t *bp,
                      size_t n, limb_t *scratch, size_t threshold) {
    if (threshold < 1) threshold = 1;

    /* Base case: this comparison is the knob the sweep turns. */
    if (n <= threshold || n < 2) {
        bn_mul_basecase(rp, ap, n, bp, n);
        return;
    }

    if (n & 1) {
        size_t m = n - 1;                        //even
        /* a = a_lo + ap[m]*B^m, b = b_lo + bp[m]*B^m
         * a*b = a_lo*b_lo + (a_lo*bp[m])*B^m + (ap[m]*b)*B^m */
        bn_mul_karatsuba(rp, ap, bp, m, scratch, threshold);  /* rp[0, 2m) */
        rp[2 * m] = 0;
        rp[2 * m + 1] = 0;
        limb_t c = bn_addmul_1(rp + m, ap, m, bp[m]);
        bn_incr(rp + 2 * m, 2, c);
        c = bn_addmul_1(rp + m, bp, n, ap[m]);
        bn_incr(rp + m + n, 1, c);
        return;
    }

    size_t h = n / 2;
    limb_t *P    = scratch;
    limb_t *da   = scratch + 2 * h;
    limb_t *db   = scratch + 3 * h;
    limb_t *rest = scratch + 4 * h + 1;
    const limb_t *a0 = ap, *a1 = ap + h;
    const limb_t *b0 = bp, *b1 = bp + h;

    /* z0 and z2 go straight into their final positions in rp. */
    bn_mul_karatsuba(rp, a0, b0, h, rest, threshold);
    bn_mul_karatsuba(rp + 2 * h, a1, b1, h, rest, threshold);

    /* |a0 - a1| and |b0 - b1|, remembering which way each went. */
    int a_ge = bn_cmp_n(a0, a1, h) >= 0;
    if (a_ge) bn_sub_n(da, a0, a1, h); else bn_sub_n(da, a1, a0, h);
    int b_ge = bn_cmp_n(b0, b1, h) >= 0;
    if (b_ge) bn_sub_n(db, b0, b1, h); else bn_sub_n(db, b1, b0, h);

    bn_mul_karatsuba(P, da, db, h, rest, threshold);

    /* middle = z0 + z2 -/+ P, built in the (now free) da/db area. It needs
     * 2h+1 limbs: the sum can carry one limb past 2h. */
    limb_t *mid = scratch + 2 * h;
    limb_t cy = bn_add_n(mid, rp, rp + 2 * h, 2 * h);
    if (a_ge == b_ge) {
        limb_t bw = bn_sub_n(mid, mid, P, 2 * h);
        mid[2 * h] = cy - bw;      /* cy >= bw because the true middle is >= 0 */
    } else {
        limb_t c2 = bn_add_n(mid, mid, P, 2 * h);
        mid[2 * h] = cy + c2;
    }

    /* Add middle into the result at offset h. */
    limb_t c = bn_add_n(rp + h, rp + h, mid, 2 * h);
    bn_incr(rp + 3 * h, h, mid[2 * h] + c);
}

//  bigint_t wrapper (used by test_correctness_karatsuba.c) 

void bigint_mul_karatsuba(bigint_t *result, const bigint_t *a, const bigint_t *b,
                          size_t threshold) {
    size_t an = a->len, bn = b->len;
    size_t n = an > bn ? an : bn;
    size_t mn = an < bn ? an : bn;
    if (threshold < 1) threshold = 1;

    limb_t *out = calloc(2 * n, sizeof(limb_t));
    if (!out) abort();

    if (mn <= threshold) {
        /* small enough that schoolbook is the right algorithm anyway */
        bn_mul_basecase(out, a->limbs, an, b->limbs, bn);
        bigint_set_limbs(result, out, an + bn);
    } else {
        /* bn_mul_karatsuba wants equal lengths: zero-pad the shorter one.
         * Correct for any sizes, though not optimal for lopsided operands
         * (the benchmarks only use equal sizes). */
        limb_t *pa = calloc(n, sizeof(limb_t));
        limb_t *pb = calloc(n, sizeof(limb_t));
        limb_t *sc = calloc(bn_karatsuba_scratch(n, threshold) + 1, sizeof(limb_t));
        if (!pa || !pb || !sc) abort();
        memcpy(pa, a->limbs, an * sizeof(limb_t));
        memcpy(pb, b->limbs, bn * sizeof(limb_t));
        bn_mul_karatsuba(out, pa, pb, n, sc, threshold);
        bigint_set_limbs(result, out, 2 * n);
        free(pa); free(pb); free(sc);
    }
    result->is_negative = a->is_negative != b->is_negative;
    free(out);
}