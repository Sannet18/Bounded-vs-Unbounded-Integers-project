/*
 * Every higher-level routine (schoolbook, Karatsuba) is built from these,
 * and the multiply ones are the software side of the hardware story in the
 * paper: the CPU gives a fixed 64x64 -> 128 bit multiply, and everything
 * larger is built by calling it repeatedly and propagating carries.
 */

#include "bn.h"

limb_t bn_add_n(limb_t *rp, const limb_t *ap, const limb_t *bp, size_t n) {
    limb_t carry = 0;
    for (size_t i = 0; i < n; i++) {
        limb_t a = ap[i];
        limb_t s = a + bp[i];
        limb_t c1 = s < a;          /* overflow from a + b */
        limb_t s2 = s + carry;
        limb_t c2 = s2 < s;         /* overflow from adding the incoming carry */
        rp[i] = s2;
        carry = c1 | c2;            /* at most one of the two can be set */
    }
    return carry;
}

limb_t bn_sub_n(limb_t *rp, const limb_t *ap, const limb_t *bp, size_t n) {
    limb_t borrow = 0;
    for (size_t i = 0; i < n; i++) {
        limb_t a = ap[i];
        limb_t b = bp[i];
        limb_t d = a - b;
        limb_t b1 = a < b;
        limb_t d2 = d - borrow;
        limb_t b2 = d < borrow;
        rp[i] = d2;
        borrow = b1 | b2;
    }
    return borrow;
}

int bn_cmp_n(const limb_t *ap, const limb_t *bp, size_t n) {
    for (size_t i = n; i > 0; i--) {
        if (ap[i - 1] != bp[i - 1]) {
            return ap[i - 1] > bp[i - 1] ? 1 : -1;
        }
    }
    return 0;
}

limb_t bn_incr(limb_t *p, size_t n, limb_t v) {
    for (size_t i = 0; i < n && v; i++) {
        limb_t s = p[i] + v;
        v = s < v;                  /* carry into the next limb */
        p[i] = s;
    }
    return v;
}

limb_t bn_mul_1(limb_t *rp, const limb_t *ap, size_t n, limb_t b) {
    limb_t carry = 0;
    for (size_t i = 0; i < n; i++) {
        /* The fixed hardware primitive: one 64x64 -> 128 bit multiply. */
        unsigned __int128 p = (unsigned __int128)ap[i] * b + carry;
        rp[i] = (limb_t)p;
        carry = (limb_t)(p >> 64);
    }
    return carry;
}

limb_t bn_addmul_1(limb_t *rp, const limb_t *ap, size_t n, limb_t b) {
    limb_t carry = 0;
    for (size_t i = 0; i < n; i++) {
        /* (2^64-1)^2 + 2*(2^64-1) = 2^128 - 1, so this never overflows. */
        unsigned __int128 p = (unsigned __int128)ap[i] * b + rp[i] + carry;
        rp[i] = (limb_t)p;
        carry = (limb_t)(p >> 64);
    }
    return carry;
}