# Times CPython's native int * int across a size ladder matching
# c/benchmark.c's "sizes" mode (same bit widths, same 30-trial count),
# and writes the same CSV schema: impl,machine,x,trial,time_ns
# 
# There's no hand-rolled multiply here -> the thing under test here is
# CPython's built-in arbitrary-precision multiplication (base-2^30
# digits). This script's whole job is to measure it fairly.
# 
# Usage:
#     python3 benchmark.py <machine_label> <out.csv>

# To run:
# python3 benchmark.py amd-8940hx ../data/raw/sizes_cpython_amd-8940hx.csv 

import gc
import random
import sys
import time

# Same bit widths as the C harness's limb ladder (limbs * 64),
# so the two datasets line up on the same x-axis later.
SIZES_BITS = [64, 128, 256, 512, 1024, 2048, 4096, 8192,
              16384, 32768, 65536, 131072]

TRIALS = 30
SEED = 42


def reps_for_bits(bits):
    # Mirrors C's benchmark reps_for_size: small operands need many
    # repeats batched together to get above clock resolution.
    if bits <= 512:
        return 20000
    if bits <= 4096:
        return 2000
    if bits <= 32768:
        return 200
    return 20


def main():
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <machine_label> <out.csv>", file=sys.stderr)
        sys.exit(1)

    machine = sys.argv[1]
    outpath = sys.argv[2]

    # Raise this once, globally, since Python 3.11+ limits int<->str
    # conversions by default -- doesn't block arithmetic, but trips up
    # anyone who later adds a print/repr of these numbers for debugging.
    sys.set_int_max_str_digits(1_000_000)

    rng = random.Random(SEED)

    with open(outpath, "w") as csv:
        csv.write("impl,machine,x,trial,time_ns\n")

        for bits in SIZES_BITS:
            reps = reps_for_bits(bits)

            for trial in range(TRIALS):
                # getrandbits, not a string conversion. It avoids the
                # int<->str machinery entirely and matches the C's bigintrandom
                # , which also forces the top bit on so the
                # operand is genuinely `bits` wide, not shorter.
                a = rng.getrandbits(bits) | (1 << (bits - 1))
                b = rng.getrandbits(bits) | (1 << (bits - 1))

                gc.disable()
                t0 = time.perf_counter_ns()
                for _ in range(reps):
                    _ = a * b
                t1 = time.perf_counter_ns()
                gc.enable()

                time_ns = (t1 - t0) / reps
                csv.write(f"cpython,{machine},{bits},{trial},{time_ns:.3f}\n")

            print(f"sizes: done {bits} bits", file=sys.stderr)


if __name__ == "__main__":
    main()