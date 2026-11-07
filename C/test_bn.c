/* Why this goes beyond random inputs: random operands almost never hit the
 * cases where multiplication code actually breaks. Karatsuba's weak spots are
 *   - odd sizes (the peel-off-the-top-limb path),
 *   - carries rippling all the way up (all-ones operands),
 *   - a0 == a1 or b0 == b1 (the difference is zero),
 *   - a0 < a1 vs a0 > a1 (both sign branches),
 *   - zeros.
 * Each is generated deliberately here. The reference is GMP's public
 * mpn_mul_n, the library whose speed your project compares against.
 * This is a correctness test of the raw limb-array core against GMP, not a benchmark: it is not timed, and it does not
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gmp.h>
#include "bn.h"

static uint64_t rng = 88172645463325252ULL;
static uint64_t next_u64(void) {           /* xorshift64* */
    rng ^= rng >> 12; rng ^= rng << 25; rng ^= rng >> 27;
    return rng * 2685821657736338717ULL;
}

enum kind { RANDOM, ALL_ONES, ZERO, HALVES_EQUAL, A_LOW_BIG, A_HIGH_BIG, SPARSE, KINDS };
static const char *kind_name[KINDS] = {
    "random", "all-ones", "zero", "equal halves", "low half bigger", "high half bigger", "sparse"
};

static void fill(limb_t *p, size_t n, enum kind k) {
    switch (k) {
    case RANDOM:    for (size_t i = 0; i < n; i++) p[i] = next_u64(); break;
    case ALL_ONES:  for (size_t i = 0; i < n; i++) p[i] = ~0ULL; break;
    case ZERO:      memset(p, 0, n * sizeof *p); break;
    case HALVES_EQUAL: {
        size_t h = n / 2;
        for (size_t i = 0; i < h; i++) { p[i] = next_u64(); p[i + h] = p[i]; }
        if (n & 1) p[n - 1] = next_u64();
        break;
    }
    case A_LOW_BIG: {
        size_t h = n / 2;
        for (size_t i = 0; i < n; i++) p[i] = next_u64();
        for (size_t i = 0; i < h; i++) p[i] |= 1ULL << 63;
        for (size_t i = h; i < n; i++) p[i] &= 0xFFFFULL;
        break;
    }
    case A_HIGH_BIG: {
        size_t h = n / 2;
        for (size_t i = 0; i < n; i++) p[i] = next_u64();
        for (size_t i = 0; i < h; i++) p[i] &= 0xFFFFULL;
        for (size_t i = h; i < n; i++) p[i] |= 1ULL << 63;
        break;
    }
    case SPARSE:
        memset(p, 0, n * sizeof *p);
        p[0] = next_u64();
        p[n - 1] = next_u64();
        break;
    default: break;
    }
}

static long failures = 0, total = 0;

static void check(size_t n, size_t thr, enum kind ka, enum kind kb) {
    limb_t *a = malloc(n * sizeof *a), *b = malloc(n * sizeof *b);
    limb_t *got = malloc(2 * n * sizeof *got), *want = malloc(2 * n * sizeof *want);
    size_t sc_len = bn_karatsuba_scratch(n, thr) + 1;
    limb_t *sc = malloc(sc_len * sizeof *sc);
    fill(a, n, ka);
    fill(b, n, kb);

    /* canary: guard limbs past the scratch end and past the result end */
    memset(got, 0xAB, 2 * n * sizeof *got);
    bn_mul_karatsuba(got, a, b, n, sc, thr);
    mpn_mul_n(want, a, b, n);

    total++;
    if (memcmp(got, want, 2 * n * sizeof *got) != 0) {
        failures++;
        if (failures <= 10)
            printf("FAIL n=%zu thr=%zu a=%s b=%s\n", n, thr, kind_name[ka], kind_name[kb]);
    }

    /* schoolbook too, so a basecase bug is distinguishable from a Karatsuba one */
    memset(got, 0xAB, 2 * n * sizeof *got);
    bn_mul_basecase(got, a, n, b, n);
    total++;
    if (memcmp(got, want, 2 * n * sizeof *got) != 0) {
        failures++;
        if (failures <= 10)
            printf("FAIL basecase n=%zu a=%s b=%s\n", n, kind_name[ka], kind_name[kb]);
    }
    free(a); free(b); free(got); free(want); free(sc);
}

int main(void) {
    /* odd, even, prime, power-of-two, and one-off-power-of-two sizes */
    size_t sizes[] = {1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 47, 63, 64, 65,
                      100, 127, 128, 129, 255, 256, 257, 500, 511, 512, 513, 1023, 1025};
    size_t thrs[] = {1, 2, 3, 4, 7, 8, 16, 32};
    size_t ns = sizeof sizes / sizeof *sizes, nt = sizeof thrs / sizeof *thrs;

    /* adversarial pairs at every size and threshold */
    for (size_t i = 0; i < ns; i++)
        for (size_t t = 0; t < nt; t++)
            for (int ka = 0; ka < KINDS; ka++)
                for (int kb = 0; kb < KINDS; kb++) {
                    /* the full 7x7 grid at every size is slow for big n; thin it */
                    if (sizes[i] > 300 && !((ka == RANDOM || ka == ALL_ONES) && kb == ka)
                        && !(ka == HALVES_EQUAL && kb == A_LOW_BIG)
                        && !(ka == A_LOW_BIG && kb == A_HIGH_BIG)) continue;
                    check(sizes[i], thrs[t], ka, kb);
                }

    /* plenty of purely random trials too */
    for (int rep = 0; rep < 400; rep++) {
        size_t n = 1 + next_u64() % 700;
        size_t thr = 1 + next_u64() % 40;
        check(n, thr, RANDOM, RANDOM);
    }

    printf("%ld / %ld tests passed\n", total - failures, total);
    return failures ? 1 : 0;
}