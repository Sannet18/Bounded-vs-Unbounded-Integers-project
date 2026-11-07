#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "bigint.h"

void bigint_init(bigint_t *n)
{
    memset(n->limbs, 0, sizeof(n->limbs));
    n->len = 1;
    n->is_negative = false;
}

void bigint_from_int(bigint_t *n, int64_t value)
{
    bigint_init(n);
    n->is_negative = value < 0;
    /* Magnitude only: the sign is metadata, never baked into a limb. The
     * (value + 1) form avoids overflow when value == INT64_MIN. */
    n->limbs[0] = n->is_negative ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value;
    n->len = 1;

    // Fills `num_limbs` limbs with pseudo-random 64-bit values, then
    // forces the top limb's high bit on. That guarantees the number
    // is genuinely num_limbs limbs wide (no accidental leading zero
    // limb shrinking it), which matters because the benchmark sweeps
    // are indexed by limb count -- a silently-shorter operand would
    // quietly put a data point in the wrong place on your crossover plot.
}

void bigint_copy(bigint_t *dest, const bigint_t *src)
{
    memcpy(dest->limbs, src->limbs, sizeof(src->limbs));
    dest->len = src->len;
    dest->is_negative = src->is_negative;
}

/* Random n-limb number with the top bit forced on, so it really is
 * num_limbs wide and a sweep indexed by limb count means what it says. */
void bigint_random(bigint_t *n, size_t num_limbs, unsigned int *seed)
{
    bigint_init(n);
    if (num_limbs > BIGINT_MAX_LIMBS)
        num_limbs = BIGINT_MAX_LIMBS;
    for (size_t i = 0; i < num_limbs; i++)
    {
        uint64_t hi = (uint64_t)rand_r(seed);
        uint64_t lo = (uint64_t)rand_r(seed);
        n->limbs[i] = (hi << 32) | lo;
    }
    n->limbs[num_limbs - 1] |= (1ULL << 63);
    n->len = num_limbs;
    n->is_negative = false;
}

size_t bigint_actual_len(const bigint_t *n)
{
    for (size_t i = BIGINT_MAX_LIMBS; i > 1; i--)
    {
        if (n->limbs[i - 1] != 0)
            return i;
    }
    return 1;
}

// actual number of limbs being used in the bigint_t struct, not the max number of limbs
void bigint_set_limbs(bigint_t *r, const uint64_t *src, size_t n)
{
    bigint_init(r);
    if (n > BIGINT_MAX_LIMBS)
        n = BIGINT_MAX_LIMBS;
    memcpy(r->limbs, src, n * sizeof(uint64_t));
    r->len = bigint_actual_len(r);
}

void bigint_print(const bigint_t *n)
{
    if (n->is_negative)
        printf("-");
    printf("0x");
    for (size_t i = n->len; i > 0; i--)
    {
        printf("%016llx", (unsigned long long)n->limbs[i - 1]);
    }
    printf("\n");
}