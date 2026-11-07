#include <stdio.h>
#include <gmp.h>
#include "bigint.h"

// mpz_import's -1 order means "least-significant limb first",
// which matches bigint_t.limbs[0] as the
// lowest limb
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
    unsigned int seed = 42; // fixed seed: so you can run across different machines
    size_t sizes[] = {1, 2, 3, 4, 8, 16, 32, 64, 128, 256};
    size_t n_sizes = sizeof(sizes) / sizeof(sizes[0]);

    int total = 0, passed = 0;

    // Square sizes (a and b the same size) -- the common case
    // the crossover sweep will actually use.
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