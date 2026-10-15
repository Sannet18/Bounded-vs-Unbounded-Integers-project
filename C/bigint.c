#include <string.h>
#include "bigint.h"


//populates each limb with 0 for now
void bigint_init(bigint_t *n) {
    memset(n->limbs, 0, sizeof(n->limbs));
    n->len = 1;
    n->is_negative = false;
}

// converts bigint into a int64
void bigint_from_int(bigint_t *n, int64_t value) {
    bigint_init(n);
    n->is_negative = value < 0;
    n->limbs[0] = n->is_negative ? (uint64_t)(-value) : (uint64_t)value;
}


// copies entire arry limb array(so 512 or 4096)
void bigint_copy(bigint_t *dest, const bigint_t *src) {
    memcpy(dest->limbs, src->limbs, sizeof(src->limbs));
    dest->len = src->len;
    dest->is_negative = src->is_negative;
}