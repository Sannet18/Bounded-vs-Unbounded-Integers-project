#ifndef BIGINT_H
#define BIGINT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// if Limb size = 8 (this will cap every bignumber at 512 bit)
//  Therefore 8 limbs * 64 bits = 512 bits of total storage
/*
 * bigint_t is a FIXED-CAPACITY struct (4096 limbs). That is fine for the
 * correctness tests, but it is exactly the design that made the first
 * version of this project slow: every call zeroes and copies the whole
 * 32 KB struct. The benchmarks therefore call the bn_* functions on plain
 * arrays and never use this type, so the fixed cost cannot contaminate
 * a timing.
 */

#define BIGINT_MAX_LIMBS 4096
// Over here it will be (64 * 4096) so it will have 262,144 bits to use
typedef struct
{
    uint64_t limbs[BIGINT_MAX_LIMBS];
    size_t len; // This will be the number of limbs in use, >=1
    bool is_negative;
} bigint_t;

// MEMORY AND INITIALIZATION
void bigint_init(bigint_t *n);
void bigint_from_int(bigint_t *n, int64_t value);
void bigint_copy(bigint_t *dest, const bigint_t *src);
void bigint_random(bigint_t *n, size_t num_limbs, unsigned int *seed);

/* Load n limbs into r (truncating at BIGINT_MAX_LIMBS) and set len honestly. */
void bigint_set_limbs(bigint_t *r, const uint64_t *src, size_t n);

void bigint_mul_schoolbook(bigint_t *result, const bigint_t *a, const bigint_t *b);
/* `threshold` = limb count at or below which Karatsuba falls back to schoolbook. */
void bigint_mul_karatsuba(bigint_t *result, const bigint_t *a, const bigint_t *b,
                          size_t threshold);

// To print out the number
void bigint_print(const bigint_t *n);
size_t bigint_actual_len(const bigint_t *n);

#endif