#ifndef BIGINT_H
#define BIGINT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// if Limb size = 8 (this will cap every bignumber at 512 bit)
//  Therefore 8 limbs * 64 bits = 512 bits of total storage

#define BIGINT_MAX_LIMBS 4096
// Over here it will be (64 * 4096) so it will have 262,144 bits to use
typedef struct
{
    uint64_t limbs[BIGINT_MAX_LIMBS];
    size_t len; // This will be the number of limbs in use
    bool is_negative;
}bigint_t;


// MEMORY AND INITIALIZATION
void bigint_init(bigint_t *n);
void bigint_from_int(bigint_t *n, int64_t value);
void bigint_copy(bigint_t *dest, const bigint_t *src);


// COMPARISONS(signed and unsigned)
int bigint_compare_absolute(const bigint_t *a, const bigint_t *b);
int bigint_compare(const bigint_t *a, const bigint_t *b);

// ARITHMETIC OPERATIONS
void bigint_add(bigint_t *result, const bigint_t *a, const bigint_t *b);
void bigint_sub(bigint_t *result, const bigint_t *a, const bigint_t *b);
void bigint_mul_schoolbook(bigint_t *result, const bigint_t *a, const bigint_t *b);


// threshold is = the max limb count at which Karatsuba falls back to schoolbook.
void bigint_mul_karatsuba(bigint_t *result, const bigint_t *a, const bigint_t *b, size_t threshold);

// To print out the number
void bigint_print(const bigint_t *n);
size_t bigint_length(const bigint_t *n);



#endif