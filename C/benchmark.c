/* All timed calls use plain limb arrays (bn.h). Nothing allocated or zeroed
 * inside a timed region, so what is measured is the multiplication itself.
 *
 * Modes (all write raw per-trial rows: impl,machine,x,trial,time_ns):
 *
 *   sizes <machine> <out.csv> [threshold=32] [core]
 *     Equal-size operands across a size ladder (1..2048 limbs). Rows:
 *       schoolbook   our bn_mul_basecase
 *       karatsuba    our bn_mul_karatsuba at the given threshold
 *       gmp          mpz_mul    (what a GMP user normally calls)
 *       gmp_mpn      mpn_mul_n  (GMP's limb-array layer; the like-for-like
 *                                comparison with our code)
 *     x = operand size in BITS.
 *
 *   threshold <machine> <out.csv> [core]
 *     One fixed size (1024 limbs), sweeping our Karatsuba threshold. If
 *     Karatsuba helps there is an interior minimum. Rows: karatsuba_threshold,
 *     x = the threshold in LIMBS. A threshold >= 1024 is pure schoolbook.
 *
 *   crossover <machine> <out.csv> [core]
 *     Fine sweep of even sizes 4..192 limbs comparing schoolbook against
 *     exactly ONE level of Karatsuba (threshold = n/2), for our code and for
 *     GMP's own internal routines. The size where the Karatsuba curve drops
 *     below the schoolbook curve IS the crossover. Rows (x = bits):
 *       cross_schoolbook, cross_karatsuba,
 *       cross_gmp_basecase, cross_gmp_toom22   (the last two only if this
 *                                               libgmp exports them)
 *
 * Every size is verified against GMP's mpn_mul_n before it is timed: a fast
 * wrong answer is worthless.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sched.h>
#include <dlfcn.h>
#include <errno.h>
#include <sys/stat.h>
#include <gmp.h>
#include "bn.h"

#define TRIALS 30
#define THRESHOLD_SWEEP_LIMBS 1024

static double now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static void pin_to_core(int core)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0)
        fprintf(stderr, "warning: could not pin to core %d (continuing unpinned)\n", core);
}

/* Create the parent directory of `path` if it is missing (fopen will not). */
static void ensure_parent_dir(const char *path)
{
    char buf[4096];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            if (mkdir(buf, 0777) != 0 && errno != EEXIST)
            { /* ignore, fopen will report */
            }
            *p = '/';
        }
    }
}

// random operands

static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;
static uint64_t next_u64(void)
{ /* xorshift64* */
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * 2685821657736338717ULL;
}
/* n random limbs with the top bit forced on, so the operand really is n limbs. */
static void fill(limb_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        p[i] = next_u64();
    p[n - 1] |= 1ULL << 63;
}

static void *xalloc(size_t bytes)
{
    size_t rounded = (bytes + 63) & ~(size_t)63;
    void *p = aligned_alloc(64, rounded ? rounded : 64);
    if (!p)
    {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return p;
}

/* Buffer placement matters more than you would think. When a load and a
 * recent store have addresses that differ by a multiple of 4096 bytes the CPU
 * can briefly treat them as dependent ("4K aliasing"), and where the allocator
 * happens to place buffers depends on n. In testing this moved the measured
 * Karatsuba/schoolbook ratio by 10-20 percentage points at particular sizes,
 * a systematic error that looked like a real step in the curve. So every
 * trial draws fresh random offsets (multiples of 64 bytes) for each buffer,
 * identical for all implementations within that trial so comparisons stay
 * paired. Layout effects then show up as trial-to-trial variance, which the
 * confidence intervals see, instead of as a bias baked into one size. */
#define SLACK 16384
static size_t rand_off(void) { return (next_u64() % (SLACK / 64)) * 64; }
#define AT(base) ((limb_t *)((char *)(base) + rand_off()))

/* Aim for ~2 ms of work per timed sample so the clock's resolution is
 * irrelevant, whatever the size. Schoolbook cost is roughly 0.7 ns * n^2. */
static size_t reps_for(size_t n)
{
    double est_ns = 0.7 * (double)n * (double)n + 30.0;
    size_t r = (size_t)(2.0e6 / est_ns);
    if (r < 1)
        r = 1;
    if (r > 200000)
        r = 200000;
    return r;
}

/* Time `reps` calls of CALL and give back ns per call. The empty asm with a
 * memory clobber stops the compiler from hoisting or dropping the call. */
#define BENCH(reps_, CALL) ({                                              \
    double t0_ = now_ns();                                                 \
    for (size_t r_ = 0; r_ < (reps_); r_++) {                              \
        CALL;                                                              \
        __asm__ volatile("" ::: "memory");                                 \
    }                                                                      \
    (now_ns() - t0_) / (double)(reps_); })

#define WARM(CALL)                     \
    do                                 \
    {                                  \
        for (int w_ = 0; w_ < 3; w_++) \
        {                              \
            CALL;                      \
        }                              \
    } while (0)

//  GMP's internal routines, looked up at runtime 

typedef void (*gmp_basecase_fn)(mp_limb_t *, const mp_limb_t *, mp_size_t,
                                const mp_limb_t *, mp_size_t);
typedef void (*gmp_toom22_fn)(mp_limb_t *, const mp_limb_t *, mp_size_t,
                              const mp_limb_t *, mp_size_t, mp_limb_t *);
static gmp_basecase_fn gmp_basecase;
static gmp_toom22_fn gmp_toom22;

static void lookup_gmp_internals(void)
{
    gmp_basecase = (gmp_basecase_fn)dlsym(RTLD_DEFAULT, "__gmpn_mul_basecase");
    gmp_toom22 = (gmp_toom22_fn)dlsym(RTLD_DEFAULT, "__gmpn_toom22_mul");
    fprintf(stderr, "GMP %s | internal basecase: %s | internal toom22: %s\n",
            gmp_version, gmp_basecase ? "found" : "NOT found",
            gmp_toom22 ? "found" : "NOT found");
}

static void die_mismatch(const char *what, size_t n)
{
    fprintf(stderr, "FATAL: %s gives a wrong product at n=%zu limbs; refusing to time it\n", what, n);
    exit(2);
}

static void emit(FILE *csv, const char *impl, const char *machine, size_t x,
                 int trial, double ns)
{
    fprintf(csv, "%s,%s,%zu,%d,%.3f\n", impl, machine, x, trial, ns);
}

// mode: sizes 
static void run_sizes(FILE *csv, const char *machine, size_t threshold)
{
    size_t ladder[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048};
    mpz_t ga, gb, gr;
    mpz_inits(ga, gb, gr, NULL);

    for (size_t s = 0; s < sizeof ladder / sizeof *ladder; s++)
    {
        size_t n = ladder[s], reps = reps_for(n);
        limb_t *a0 = xalloc(n * 8 + SLACK), *b0 = xalloc(n * 8 + SLACK);
        limb_t *r0 = xalloc(2 * n * 8 + SLACK), *ref0 = xalloc(2 * n * 8 + SLACK);
        limb_t *sc0 = xalloc((bn_karatsuba_scratch(n, threshold) + 1) * 8 + SLACK);
        limb_t *a = a0, *b = b0, *r = r0, *ref = ref0, *sc = sc0;

        /* correctness gate */
        fill(a, n);
        fill(b, n);
        mpn_mul_n(ref, a, b, n);
        bn_mul_basecase(r, a, n, b, n);
        if (memcmp(r, ref, 2 * n * 8))
            die_mismatch("schoolbook", n);
        bn_mul_karatsuba(r, a, b, n, sc, threshold);
        if (memcmp(r, ref, 2 * n * 8))
            die_mismatch("karatsuba", n);

        for (int trial = 0; trial < TRIALS; trial++)
        {
            a = AT(a0);
            b = AT(b0);
            r = AT(r0);
            ref = AT(ref0);
            sc = AT(sc0);
            fill(a, n);
            fill(b, n);
            mpz_import(ga, n, -1, 8, 0, 0, a);
            mpz_import(gb, n, -1, 8, 0, 0, b);

            WARM(bn_mul_basecase(r, a, n, b, n));
            emit(csv, "schoolbook", machine, n * 64, trial,
                 BENCH(reps, bn_mul_basecase(r, a, n, b, n)));

            WARM(bn_mul_karatsuba(r, a, b, n, sc, threshold));
            emit(csv, "karatsuba", machine, n * 64, trial,
                 BENCH(reps, bn_mul_karatsuba(r, a, b, n, sc, threshold)));

            WARM(mpz_mul(gr, ga, gb));
            emit(csv, "gmp", machine, n * 64, trial, BENCH(reps, mpz_mul(gr, ga, gb)));

            WARM(mpn_mul_n(ref, a, b, n));
            emit(csv, "gmp_mpn", machine, n * 64, trial, BENCH(reps, mpn_mul_n(ref, a, b, n)));
        }
        fflush(csv);
        fprintf(stderr, "sizes: done %zu limbs (%zu bits)\n", n, n * 64);
        free(a0);
        free(b0);
        free(r0);
        free(ref0);
        free(sc0);
    }
    mpz_clears(ga, gb, gr, NULL);
}

// mode: threshold 

static void run_threshold(FILE *csv, const char *machine)
{
    size_t n = THRESHOLD_SWEEP_LIMBS, reps = reps_for(n);
    size_t thrs[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 512, 1024};
    limb_t *a0 = xalloc(n * 8 + SLACK), *b0 = xalloc(n * 8 + SLACK);
    limb_t *r0 = xalloc(2 * n * 8 + SLACK), *ref = xalloc(2 * n * 8);
    limb_t *a = a0, *b = b0, *r = r0;

    for (size_t t = 0; t < sizeof thrs / sizeof *thrs; t++)
    {
        size_t thr = thrs[t];
        limb_t *sc0 = xalloc((bn_karatsuba_scratch(n, thr) + 1) * 8 + SLACK);
        limb_t *sc = sc0;
        a = a0;
        b = b0;
        r = r0;

        fill(a, n);
        fill(b, n);
        mpn_mul_n(ref, a, b, n);
        bn_mul_karatsuba(r, a, b, n, sc, thr);
        if (memcmp(r, ref, 2 * n * 8))
            die_mismatch("karatsuba", n);

        for (int trial = 0; trial < TRIALS; trial++)
        {
            a = AT(a0);
            b = AT(b0);
            r = AT(r0);
            sc = AT(sc0);
            fill(a, n);
            fill(b, n);
            WARM(bn_mul_karatsuba(r, a, b, n, sc, thr));
            emit(csv, "karatsuba_threshold", machine, thr, trial,
                 BENCH(reps, bn_mul_karatsuba(r, a, b, n, sc, thr)));
        }
        fflush(csv);
        fprintf(stderr, "threshold: done %zu\n", thr);
        free(sc0);
    }
    free(a0);
    free(b0);
    free(r0);
    free(ref);
}

// mode: crossover 

static void run_crossover(FILE *csv, const char *machine)
{
    /* even sizes only: avoids the odd-size peel path so the one-level
     * comparison is clean. */
    size_t ns[256], count = 0;
    for (size_t n = 4; n <= 64; n += 2)
        ns[count++] = n;
    for (size_t n = 68; n <= 192; n += 4)
        ns[count++] = n;

    for (size_t i = 0; i < count; i++)
    {
        size_t n = ns[i], reps = reps_for(n), thr = n / 2; /* exactly one split */
        limb_t *a0 = xalloc(n * 8 + SLACK), *b0 = xalloc(n * 8 + SLACK);
        limb_t *r0 = xalloc(2 * n * 8 + SLACK), *ref = xalloc(2 * n * 8);
        limb_t *sc0 = xalloc((bn_karatsuba_scratch(n, thr) + 1) * 8 + SLACK);
        limb_t *gsc0 = xalloc((4 * n + 512) * 8 + SLACK);
        limb_t *a = a0, *b = b0, *r = r0, *sc = sc0, *gsc = gsc0;

        fill(a, n);
        fill(b, n);
        mpn_mul_n(ref, a, b, n);
        bn_mul_basecase(r, a, n, b, n);
        if (memcmp(r, ref, 2 * n * 8))
            die_mismatch("schoolbook", n);
        bn_mul_karatsuba(r, a, b, n, sc, thr);
        if (memcmp(r, ref, 2 * n * 8))
            die_mismatch("karatsuba (1 level)", n);
        if (gmp_basecase)
        {
            gmp_basecase(r, a, n, b, n);
            if (memcmp(r, ref, 2 * n * 8))
                die_mismatch("GMP basecase", n);
        }
        if (gmp_toom22)
        {
            gmp_toom22(r, a, n, b, n, gsc);
            if (memcmp(r, ref, 2 * n * 8))
                die_mismatch("GMP toom22", n);
        }

        for (int trial = 0; trial < TRIALS; trial++)
        {
            a = AT(a0);
            b = AT(b0);
            r = AT(r0);
            sc = AT(sc0);
            gsc = AT(gsc0);
            fill(a, n);
            fill(b, n);
            WARM(bn_mul_basecase(r, a, n, b, n));
            emit(csv, "cross_schoolbook", machine, n * 64, trial,
                 BENCH(reps, bn_mul_basecase(r, a, n, b, n)));
            WARM(bn_mul_karatsuba(r, a, b, n, sc, thr));
            emit(csv, "cross_karatsuba", machine, n * 64, trial,
                 BENCH(reps, bn_mul_karatsuba(r, a, b, n, sc, thr)));
            if (gmp_basecase)
            {
                WARM(gmp_basecase(r, a, n, b, n));
                emit(csv, "cross_gmp_basecase", machine, n * 64, trial,
                     BENCH(reps, gmp_basecase(r, a, n, b, n)));
            }
            if (gmp_toom22)
            {
                WARM(gmp_toom22(r, a, n, b, n, gsc));
                emit(csv, "cross_gmp_toom22", machine, n * 64, trial,
                     BENCH(reps, gmp_toom22(r, a, n, b, n, gsc)));
            }
        }
        fflush(csv);
        if (i % 8 == 0)
            fprintf(stderr, "crossover: done %zu limbs\n", n);
        free(a0);
        free(b0);
        free(r0);
        free(ref);
        free(sc0);
        free(gsc0);
    }
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        fprintf(stderr,
                "usage:\n"
                "  %s sizes     <machine_label> <out.csv> [karatsuba_threshold=32] [core]\n"
                "  %s threshold <machine_label> <out.csv> [core]\n"
                "  %s crossover <machine_label> <out.csv> [core]\n",
                argv[0], argv[0], argv[0]);
        return 1;
    }
    const char *mode = argv[1], *machine = argv[2], *outpath = argv[3];

    ensure_parent_dir(outpath);
    FILE *csv = fopen(outpath, "w");
    if (!csv)
    {
        perror("fopen");
        return 1;
    }
    fprintf(csv, "impl,machine,x,trial,time_ns\n");
    lookup_gmp_internals();

    if (strcmp(mode, "sizes") == 0)
    {
        size_t thr = (argc > 4) ? (size_t)atol(argv[4]) : 32;
        int core = (argc > 5) ? atoi(argv[5]) : -1;
        if (core >= 0)
            pin_to_core(core);
        fprintf(stderr, "sizes mode: karatsuba threshold = %zu limbs\n", thr);
        run_sizes(csv, machine, thr);
    }
    else if (strcmp(mode, "threshold") == 0)
    {
        int core = (argc > 4) ? atoi(argv[4]) : -1;
        if (core >= 0)
            pin_to_core(core);
        run_threshold(csv, machine);
    }
    else if (strcmp(mode, "crossover") == 0)
    {
        int core = (argc > 4) ? atoi(argv[4]) : -1;
        if (core >= 0)
            pin_to_core(core);
        run_crossover(csv, machine);
    }
    else
    {
        fprintf(stderr, "unknown mode '%s'\n", mode);
        fclose(csv);
        return 1;
    }
    fclose(csv);
    return 0;
}