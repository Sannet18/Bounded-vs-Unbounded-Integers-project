// test_correctness.c
//
// Why this file exists, in terms of the research:
// Every number in your Pillar 3 crossover sweep comes from
// bigint_mul_schoolbook (and later, bigint_mul_karatsuba). If either
// has a silent bug -- a dropped carry, an off-by-one in a loop bound --
// your timing data is still "valid" in the sense that it measures
// something real, but it is no longer measuring correct multiplication,
// and any crossover point you report would be unfalsifiable: a reviewer
// (or your committee) can't tell you measured the right algorithm.
// GMP is the trusted reference implementation in this whole project
// (it's also one of your Tier A benchmarks), so using it to verify your
// own bignum is the correct and standard move, not a shortcut.

#include <stdio.h>
#include <gmp.h>
#include "bigint.h"

// Import a bigint_t's magnitude directly into an mpz_t.
// mpz_import's -1 order means "least-significant limb first",
// which matches how you've laid out bigint_t.limbs[0] as the
// lowest limb -- the same question you asked earlier about
// bigint_from_int. The sign is applied separately afterward,
// exactly because mpz_import only reads magnitude, same as your
// own struct.
static void bigint_to_mpz(mpz_t out, const bigint_t *n)
{
    mpz_import(out, n->len, -1, sizeof(uint64_t), 0, 0, n->limbs);
    if (n->is_negative)
        mpz_neg(out, out);
}

static int run_one_test(size_t limbs_a, size_t limbs_b, unsigned int *seed)
{
    bigint_t a, b, result;
    bigint_random(&a, limbs_a, seed);
    bigint_random(&b, limbs_b, seed);
    bigint_mul_schoolbook(&result, &a, &b);

    mpz_t ga, gb, expected, got;
    mpz_inits(ga, gb, expected, got, NULL);

    bigint_to_mpz(ga, &a);
    bigint_to_mpz(gb, &b);
    mpz_mul(expected, ga, gb);
    bigint_to_mpz(got, &result);

    int ok = (mpz_cmp(expected, got) == 0);

    if (!ok)
    {
        gmp_printf("  MISMATCH (a=%zu limbs, b=%zu limbs)\n"
                   "    expected: %Zx\n"
                   "    got:      %Zx\n",
                   limbs_a, limbs_b, expected, got);
    }

    mpz_clears(ga, gb, expected, got, NULL);
    return ok;
}

int main()
{
    unsigned int seed = 42; // fixed seed: reproducible across runs/machines
    size_t sizes[] = {1, 2, 3, 4, 8, 16, 32, 64, 128, 256};
    size_t n_sizes = sizeof(sizes) / sizeof(sizes[0]);

    int total = 0, passed = 0;

    // Square sizes (a and b the same size) -- the common case
    // your crossover sweep will actually use.
    for (size_t i = 0; i < n_sizes; i++)
    {
        for (int trial = 0; trial < 20; trial++)
        {
            total++;
            passed += run_one_test(sizes[i], sizes[i], &seed);
        }
    }

    // A handful of unequal sizes, since real Karatsuba
    // implementations must handle a->len != b->len correctly,
    // and that's the case most likely to hide a bug.
    size_t mixed[][2] = {{1, 4}, {3, 7}, {16, 3}, {64, 9}, {100, 40}};
    for (size_t i = 0; i < 5; i++)
    {
        for (int trial = 0; trial < 10; trial++)
        {
            total++;
            passed += run_one_test(mixed[i][0], mixed[i][1], &seed);
        }
    }

    printf("%d / %d tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}