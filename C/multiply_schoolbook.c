/*
 * bn_mul_basecase is the O(n^2) step the crossover study is built around:
 * an * bn individual 64x64 -> 128 bit multiplies, each summed into the right
 * position of the result with carry propagation. It is also the base case
 * Karatsuba falls back to once operands get small enough.
 *
 * Structure (same as GMP's mpn_mul_basecase): one mul_1 pass for the first
 * limb of b, then one addmul_1 pass for each remaining limb of b.
 */

#include <stdlib.h>
#include "bn.h"
#include "bigint.h"

void bn_mul_basecase(limb_t *rp, const limb_t *ap, size_t an,
                     const limb_t *bp, size_t bn) {
    rp[an] = bn_mul_1(rp, ap, an, bp[0]);
    for (size_t j = 1; j < bn; j++) {
        rp[an + j] = bn_addmul_1(rp + j, ap, an, bp[j]);
    }
}

// bigint_t wrapper (used by test_correctness.c)

void bigint_mul_schoolbook(bigint_t *result, const bigint_t *a, const bigint_t *b) {
    size_t an = a->len, bn = b->len;
    limb_t *tmp = calloc(an + bn, sizeof(limb_t));
    if (!tmp) abort();
    bn_mul_basecase(tmp, a->limbs, an, b->limbs, bn);
    bigint_set_limbs(result, tmp, an + bn);
    result->is_negative = a->is_negative != b->is_negative;
    free(tmp);
}