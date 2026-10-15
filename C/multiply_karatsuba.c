// multiply_karatsuba.c
//
// This is your one fully original, sweep-able result (per the roadmap).
// `threshold` controls where recursion bottoms out into schoolbook --
// expose it as a parameter (not a #define) so benchmark.c can sweep it
// without recompiling, which is exactly what the crossover-point study
// needs.

#include <stdint.h>
#include "bigint.h"

// Subtraction ASSUMES a >= b. Karatsuba's identity
// z1 = (a_low+a_high)(b_low+b_high) - z0 - z2 is always non-negative
// for correctly computed z0/z1/z2, so this internal-use restriction
// never actually gets violated by the algorithm itself.
void bigint_sub(bigint_t *result, const bigint_t *a, const bigint_t *b)
{
    uint64_t borrow = 0;
    for (size_t i = 0; i < BIGINT_MAX_LIMBS; i++)
    {
        uint64_t ai = a->limbs[i];
        uint64_t bi = b->limbs[i];
        uint64_t diff1 = ai - bi;
        uint64_t borrow1 = (ai < bi) ? 1 : 0;
        uint64_t diff2 = diff1 - borrow;
        uint64_t borrow2 = (diff1 < borrow) ? 1 : 0;
        result->limbs[i] = diff2;
        borrow = borrow1 | borrow2;
    }
    result->is_negative = false;
    result->len = bigint_actual_len(result);

    //   1000
    // -    1
    // ------
    //    999
    // this is what the loop does
}
// This one does result = a-b where a>=b

// Low m limbs of "in" the "low half" split.

// in = the big integer you're splitting.
// in->len = how many limbs are currently used.
// m = how many limbs go into the low half.
// out = a new bigint_t containing either the
// low or high part, depending on which function you call.
static void bigint_slice_low(bigint_t *out, const bigint_t *in, size_t m)
{
    bigint_init(out);
    size_t count = (in->len < m) ? in->len : m;
    for (size_t i = 0; i < count; i++)
    {
        out->limbs[i] = in->limbs[i];
    }
    out->len = bigint_actual_len(out);
}
// For example: in limb3, limb2, limb1, limb0 and m=2
// out:  limb1, limb0
// then copies limbs 0 through m-1
// this is what gives a_low or b_low

// Everything above limb m, shifted down to start at index 0 - the
// "high half" split.
static void bigint_slice_high(bigint_t *out, const bigint_t *in, size_t m)
{
    bigint_init(out);
    if (in->len <= m)
    {
        out->len = 1;
        return;
    } // nothing above m: zero
    size_t count = in->len - m;
    for (size_t i = 0; i < count; i++)
    {
        out->limbs[i] = in->limbs[m + i];
    }
    out->len = bigint_actual_len(out);
}
// does the opposite of bigint_slice_low but shifts the high half
// to limb 1 and 0
// So if m = 2;
// limb3 and limb2 remain but get shifted into


// Multiply `in` by B^shift (B = 2^64), i.e. move every limb up by
// `shift` positions. This is how z1 and z2's partial products get
// placed back into their correct position in the final sum.
// assuming limbs[0] is the least significant limb
static void bigint_shift_limbs(bigint_t *out, const bigint_t *in, size_t shift)
{
    bigint_init(out);
    if (shift >= BIGINT_MAX_LIMBS)
    {
        out->len = 1;
        return;
    }
    size_t max_copy = BIGINT_MAX_LIMBS - shift;
    size_t count = (in->len < max_copy) ? in->len : max_copy; // truncates the high limbs if the shifted number doesn't fit
    for (size_t i = 0; i < count; i++)
    {
        out->limbs[shift + i] = in->limbs[i];
    }
    out->len = bigint_actual_len(out);
}


void bigint_mul_karatsuba(bigint_t *result, const bigint_t *a, const bigint_t *b, size_t threshold)
{
    // threshold is the max number of limbs at which you stop using the karatsuba 
    // and switch to the schoolbook method(like a cutoff point)
    if (threshold < 1)
        threshold = 1;

    // Base case, this is the exact line your sweep varies. Below
    // threshold, do the O(n^2) schoolbook step; above it, keep splitting.
    if (a->len <= threshold && b->len <= threshold)
    {
        bigint_mul_schoolbook(result, a, b);
        result->is_negative = a->is_negative != b->is_negative;
        return;
    }
    //splitting the operands
    size_t max_len = a->len > b->len ? a->len : b->len;
    size_t m = (max_len + 1) / 2;

    bigint_t a_low, a_high, b_low, b_high;
    bigint_slice_low(&a_low, a, m);
    bigint_slice_high(&a_high, a, m);
    bigint_slice_low(&b_low, b, m);
    bigint_slice_high(&b_high, b, m);

    // The three recursive multiplications this is how Karatsuba
    // works, 3 multiplications of half-size operands instead of 4,
    // dropping the exponent from O(n^2) toward O(n^1.585).

    bigint_t z0, z2, sum_a, sum_b, cross;
    bigint_mul_karatsuba(&z0, &a_low, &b_low, threshold);
    // commputes z0 = a_low × b_low
    bigint_mul_karatsuba(&z2, &a_high, &b_high, threshold);
    // computes z2 = a_high × b_high
    bigint_add(&sum_a, &a_low, &a_high);
    bigint_add(&sum_b, &b_low, &b_high);
    // sum_a = a_low + a_high
    // sum_b = b_low + b_high

    bigint_mul_karatsuba(&cross, &sum_a, &sum_b, threshold);
    //  this gives close to the final step,/ cross - z0 - z2



    bigint_t z1, temp;
    bigint_sub(&temp, &cross, &z0);
    bigint_sub(&z1, &temp, &z2);

    // result = z2 * B^(2m) + z1 * B^m + z0
    bigint_t z2_shifted, z1_shifted, partial;
    bigint_shift_limbs(&z2_shifted, &z2, 2 * m);
    bigint_shift_limbs(&z1_shifted, &z1, m);

    bigint_add(&partial, &z2_shifted, &z1_shifted);
    bigint_add(result, &partial, &z0);

    result->is_negative = a->is_negative != b->is_negative;
    result->len = bigint_actual_len(result);
}