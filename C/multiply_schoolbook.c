#include <stdint.h>
#include "bigint.h"

void bigint_add(bigint_t *result, const bigint_t *a, const bigint_t *b)
{
    uint64_t carry = 0;
    for (size_t i = 0; i < BIGINT_MAX_LIMBS; i++)
    {
        uint64_t sum1 = a->limbs[i] + b->limbs[i];
        uint64_t carry1 = (sum1 < a->limbs[i]) ? 1 : 0;
        uint64_t sum2 = sum1 + carry;
        uint64_t carry2 = (sum2 < sum1) ? 1 : 0;
        result->limbs[i] = sum2;
        carry = carry1 | carry2;
    }
    // Don't trust the loop's final carry value as a signal: the loop runs
    // all the way to BIGINT_MAX_LIMBS, so by the last iteration any real
    // carry has already propagated into its correct limb and the carry
    // *out* of iteration 4095 is always 0. bigint_actual_len does an
    // honest scan instead -- this was the source of the Karatsuba bug.
    (void)carry;
    result->len = bigint_actual_len(result); //scans to get the total number of limbs used
    result->is_negative = a->is_negative; // simplification: same-sign add only, for now
}

// This IS the O(n^2) step your Pillar 3 / RQ3 crossover measurement
// is built around. Every multiplication does a->len * b->len individual
// 64x64->128 widening multiplies -- the exact CPU primitive described
// in your hardware section -- summed into the right position of the
// result array with carry propagation. 

//Later, multiply_karatsuba.c
// will call THIS function as its base case once operands get small
// enough, and the threshold where it becomes faster to switch is
// your centerpiece result.


void bigint_mul_schoolbook(bigint_t *result, const bigint_t *a, const bigint_t *b)
{
    uint64_t temp[BIGINT_MAX_LIMBS] = {0};

    for (size_t i = 0; i < a->len; i++)
    {
        uint64_t carry = 0;
        for (size_t j = 0; j < b->len && (i + j) < BIGINT_MAX_LIMBS; j++)
        {
            __uint128_t product = (__uint128_t)a->limbs[i] * b->limbs[j] + temp[i + j] + carry;
            temp[i + j] = (uint64_t)product;
            carry = (uint64_t)(product >> 64);
        }
        if (i + b->len < BIGINT_MAX_LIMBS)
        {
            // propagates any remaining carry forward,carries the thing into the next limb
            size_t k = i + b->len;
            while (carry && k < BIGINT_MAX_LIMBS)
            {
                __uint128_t s = (__uint128_t)temp[k] + carry;
                temp[k] = (uint64_t)s;
                carry = (uint64_t)(s >> 64);
                k++;
            }
        }
    }

    //fills the result with the temp
    for (size_t k = 0; k < BIGINT_MAX_LIMBS; k++)
        result->limbs[k] = temp[k];
    result->is_negative = a->is_negative != b->is_negative; //xor logic(same sings means positive)
    result->len = bigint_actual_len(result);
}