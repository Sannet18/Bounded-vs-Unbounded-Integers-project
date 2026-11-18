/*
 * Writes a CSV:  impl,bits,bytes,payload_bytes,allocator_bytes
 *
 *   bytes            what the representation itself accounts for: the handle
 *                    or struct plus the digit array it asked for
 *   payload_bytes    just the digits actually in use (the "useful" part)
 *   allocator_bytes  bytes plus whatever rounding malloc added, as reported
 *                    by glibc's malloc_usable_size (blank if not applicable)
 *
 * Rows (impl):
 *   c_uint64     the bounded case: a fixed 8 bytes whatever the value
 *                (only meaningful up to 64 bits; beyond that it overflows)
 *   c_bigint_t   the first design of this project: a fixed 4096-limb struct.
 *                Constant size, whatever the value -- which is exactly why it
 *                was slow and wasteful.
 *   c_bn         the limb-array core (bn.h): a pointer + length handle (16
 *                bytes) and a malloc'd array of 8 * ceil(bits/64) bytes
 *   gmp_mpz      GMP's mpz_t: a 16-byte struct plus its limb array
 *
 * The number measured at each bit length b is 2^b - 1 (all ones), so its bit
 * length is exactly b. Footprint depends only on the bit length, not the value.
 *
 * Why measure at EVERY bit length from 1 to 320? Memory is a staircase: it
 * only steps up when another limb is needed. The width of the steps reveals
 * the limb size directly (64 bits here), a stronger check on the theory than
 * a fitted slope alone.
 *
 * To run: make memory, then
 *  ./memory ../data/memory/memory_c.csv
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <malloc.h>
#include <errno.h>
#include <sys/stat.h>
#include <gnu/libc-version.h>
#include <gmp.h>
#include "bn.h"
#include "bigint.h"

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
            { /* fopen will report it */
            }
            *p = '/';
        }
    }
}

/* Same bit lengths in every language's memory tool, so the CSVs line up. */
static size_t *make_bit_list(size_t *count)
{
    static size_t list[2048];
    size_t n = 0;
    for (size_t b = 1; b <= 320; b++)
        list[n++] = b;
    size_t ladder[] = {384, 448, 512, 640, 768, 1024, 1536, 2048, 3072, 4096, 6144,
                       8192, 12288, 16384, 32768, 65536, 131072, 262144, 524288, 1048576};
    for (size_t i = 0; i < sizeof ladder / sizeof *ladder; i++)
        list[n++] = ladder[i];
    *count = n;
    return list;
}

/* n little-endian limbs holding exactly 2^bits - 1. */
static void all_ones(limb_t *a, size_t bits)
{
    size_t n = (bits + 63) / 64;
    for (size_t i = 0; i < n; i++)
        a[i] = ~0ULL;
    size_t rem = bits % 64;
    if (rem)
        a[n - 1] = (1ULL << rem) - 1;
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <out.csv>\n", argv[0]);
        return 1;
    }
    ensure_parent_dir(argv[1]);
    FILE *csv = fopen(argv[1], "w");
    if (!csv)
    {
        perror("fopen");
        return 1;
    }

    fprintf(csv, "# C / GMP memory footprint. gmp %s, glibc %s, sizeof(mpz_t)=%zu, "
                 "sizeof(bigint_t)=%zu, sizeof(limb)=%zu\n",
            gmp_version, gnu_get_libc_version(), sizeof(mpz_t), sizeof(bigint_t), sizeof(limb_t));
    fprintf(csv, "impl,bits,bytes,payload_bytes,allocator_bytes\n");

    size_t count;
    size_t *bits_list = make_bit_list(&count);

    for (size_t i = 0; i < count; i++)
    {
        size_t bits = bits_list[i];
        size_t n = (bits + 63) / 64;

        if (bits <= 64)
            fprintf(csv, "c_uint64,%zu,%zu,%zu,\n", bits, sizeof(uint64_t), sizeof(uint64_t));

        if (n <= BIGINT_MAX_LIMBS)
            fprintf(csv, "c_bigint_t,%zu,%zu,%zu,\n", bits, sizeof(bigint_t), n * 8);

        /* limb-array core: handle (pointer + length) + malloc'd digits */
        limb_t *p = malloc(n * sizeof(limb_t));
        if (!p)
        {
            fprintf(stderr, "out of memory\n");
            return 1;
        }
        all_ones(p, bits);
        size_t handle = sizeof(limb_t *) + sizeof(size_t);
        fprintf(csv, "c_bn,%zu,%zu,%zu,%zu\n", bits, handle + n * 8, n * 8,
                handle + malloc_usable_size(p));

        /* GMP: build the same number through the public API and ask it how
         * many limbs it actually allocated. */
        mpz_t z;
        mpz_init(z);
        mpz_import(z, n, -1, sizeof(limb_t), 0, 0, p);
        if (mpz_sizeinbase(z, 2) != bits)
        {
            fprintf(stderr, "FATAL: bit length mismatch at %zu\n", bits);
            return 2;
        }
        fprintf(csv, "gmp_mpz,%zu,%zu,%zu,%zu\n", bits,
                sizeof(mpz_t) + (size_t)z->_mp_alloc * 8,
                (size_t)(z->_mp_size < 0 ? -z->_mp_size : z->_mp_size) * 8,
                sizeof(mpz_t) + malloc_usable_size(z->_mp_d));
        mpz_clear(z);
        free(p);
    }
    fclose(csv);
    fprintf(stderr, "wrote %s\n", argv[1]);
    return 0;
}