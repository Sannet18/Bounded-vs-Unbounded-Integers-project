// Same purpose as test_correctness.c, with one addition: Karatsuba has
// a parameter the schoolbook function doesn't -- `threshold`. A bug
// could easily hide in one threshold value and not another (e.g. an
// off-by-one at the exact base-case boundary), so this sweeps several
// thresholds against the same size set, not just one.

#include <stdio.h>
#include <gmp.h>
#include "bigint.h"

static void bigint_to_mpz(mpz_t out, const bigint_t *n)
{
    mpz_import(out, n->len, -1, sizeof(uint64_t), 0, 0, n->limbs);
    if (n->is_negative)
        mpz_neg(out, out);
}

static int run_one_test(size_t limbs_a, size_t limbs_b, size_t threshold, unsigned int *seed)
{
    bigint_t a, b, result;
    bigint_random(&a, limbs_a, seed);
    bigint_random(&b, limbs_b, seed);
    bigint_mul_karatsuba(&result, &a, &b, threshold);

    mpz_t ga, gb, expected, got;
    mpz_inits(ga, gb, expected, got, NULL);

    bigint_to_mpz(ga, &a);
    bigint_to_mpz(gb, &b);
    mpz_mul(expected, ga, gb);
    bigint_to_mpz(got, &result);

    int ok = (mpz_cmp(expected, got) == 0);

    if (!ok)
    {
        gmp_printf("  MISMATCH (a=%zu limbs, b=%zu limbs, threshold=%zu)\n"
                   "    expected: %Zx\n"
                   "    got:      %Zx\n",
                   limbs_a, limbs_b, threshold, expected, got);
    }

    mpz_clears(ga, gb, expected, got, NULL);
    return ok;
}

int main(void)
{
    unsigned int seed = 42;
    size_t sizes[] = {1, 2, 3, 4, 8, 16, 32, 64, 128, 256};
    size_t n_sizes = sizeof(sizes) / sizeof(sizes[0]);
    size_t thresholds[] = {1, 2, 4, 8, 17, 32};
    size_t n_thresholds = sizeof(thresholds) / sizeof(thresholds[0]);

    int total = 0, passed = 0;

    for (size_t t = 0; t < n_thresholds; t++)
    {
        for (size_t i = 0; i < n_sizes; i++)
        {
            for (int trial = 0; trial < 5; trial++)
            {
                total++;
                passed += run_one_test(sizes[i], sizes[i], thresholds[t], &seed);
            }
        }
        // unequal sizes at each threshold -- the case most likely to
        // expose a splitting bug
        size_t mixed[][2] = {{1, 4}, {3, 7}, {16, 3}, {64, 9}, {100, 40}};
        for (size_t i = 0; i < 5; i++)
        {
            total++;
            passed += run_one_test(mixed[i][0], mixed[i][1], thresholds[t], &seed);
        }
    }

    printf("%d / %d tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}