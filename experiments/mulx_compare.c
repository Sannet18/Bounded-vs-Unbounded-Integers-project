/* mulx_compare.c -- times bn_mul_basecase (schoolbook) at 64, 256, 1024 limbs.
 * Built twice by run_pending.sh: default flags, and with -mbmi2. Only the
 * compiler flag differs, so any timing difference is the instruction choice.
 * Prints: <tag> <limbs> <median_ns> <min_ns>   (30 trials, random buffer offsets) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "bn.h"

static double now_ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e9 + t.tv_nsec;
}
static uint64_t s = 88172645463325252ULL;
static uint64_t rnd(void) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
static int cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y);
}
#define SLACK 16384

int main(int argc, char **argv) {
    const char *tag = argc > 1 ? argv[1] : "run";
    size_t sizes[] = {64, 256, 1024};
    for (int k = 0; k < 3; k++) {
        size_t n = sizes[k];
        char *a0 = malloc(n * 8 + SLACK), *b0 = malloc(n * 8 + SLACK), *r0 = malloc(2 * n * 8 + SLACK);
        size_t reps = (size_t)(2.0e6 / (0.7 * n * n + 30.0)); if (reps < 1) reps = 1;
        double t[30];
        for (int trial = 0; trial < 30; trial++) {
            limb_t *a = (limb_t *)(a0 + (rnd() % (SLACK / 64)) * 64);
            limb_t *b = (limb_t *)(b0 + (rnd() % (SLACK / 64)) * 64);
            limb_t *r = (limb_t *)(r0 + (rnd() % (SLACK / 64)) * 64);
            for (size_t i = 0; i < n; i++) { a[i] = rnd(); b[i] = rnd(); }
            bn_mul_basecase(r, a, n, b, n);
            double t0 = now_ns();
            for (size_t i = 0; i < reps; i++) { bn_mul_basecase(r, a, n, b, n); __asm__ volatile("" ::: "memory"); }
            t[trial] = (now_ns() - t0) / reps;
        }
        qsort(t, 30, sizeof t[0], cmp);
        printf("%s %zu %.1f %.1f\n", tag, n, (t[14] + t[15]) / 2, t[0]);
        free(a0); free(b0); free(r0);
    }
    return 0;
}
