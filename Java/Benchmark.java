// Benchmark.java
//
// Two modes, same CSV schema as benchmark.c and benchmark.py:
// impl,machine,x,trial,time_ns
//
//   sizes <machine> <out.csv>
//     Same bit-width ladder as the C and Python harnesses, so this
//     data lines up with theirs on the same x-axis.
//
//   threshold <machine> <out.csv>
//     Wide geometric sweep (16 to ~800 ints) spanning BigInteger's
//     documented thresholds (KARATSUBA_THRESHOLD = 80 ints = 2560 bits,
//     TOOM_COOK_THRESHOLD = 240 ints = 7680 bits -- current openjdk/jdk
//     source). The analysis fits the growth exponent in each zone and
//     checks whether it changes across the documented boundaries.
//
// Run with: taskset -c N java Benchmark <mode> ...
// (CPU affinity has to be set on the JVM process from outside --
// there's no portable equivalent to sched_setaffinity from pure Java.)

// Run with
// java Benchmark sizes amd-8940hx ../data/raw/sizes_java_amd-8940hx.csv
// java Benchmark threshold amd-8940hx ../data/raw/threshold_java_amd-8940hx.csv

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.PrintWriter;
import java.math.BigInteger;
import java.util.Random;

public class Benchmark {

    static final int TRIALS = 30;
    static final long SEED = 42L;

    // Same bit ladder as C'benchmark.c and python's benchmark.py.
    static final int[] SIZES_BITS = {
        64, 128, 256, 512, 1024, 2048, 4096, 8192,
        16384, 32768, 65536, 131072
    };

    static int repsForBits(int bits) {
        if (bits <= 512) {
            return 20000;
        }
        if (bits <= 4096) {
            return 2000;
        }
        if (bits <= 32768) {
            return 200;
        }
        return 20;
    }

    // BigInteger(bitLength, Random) can return a value with LEADING
    // zero bits, i.e. shorter than requested -- setBit forces the top
    // bit on, matching bigint_random's behavior in C and the
    // |= (1 << (bits-1)) line in Python, so "bits" means what it says
    // across all three languages.
    static BigInteger randomOfBits(int bits, Random rnd) {
        return new BigInteger(bits, rnd).setBit(bits - 1);
    }

    // Run throwaway multiplications first so the JIT has compiled the
    // hot path before any timed measurement -- an un-warmed JVM would
    // make early trials look artificially slow for reasons that have
    // nothing to do with BigInteger's algorithm choice.
    static void warmup(Random rnd) {
        for (int w = 0; w < 20000; w++) {
            int bits = SIZES_BITS[w % SIZES_BITS.length];
            BigInteger a = randomOfBits(Math.min(bits, 4096), rnd);
            BigInteger b = randomOfBits(Math.min(bits, 4096), rnd);
            BigInteger r = a.multiply(b);
            if (r.signum() == 0) {
                System.out.print("");  // prevent dead-code elimination

                    }}
    }

    // HotSpot's optimizing compiler (C2) only compiles a method after
    // roughly 10,000 invocations. multiplyKaratsuba and the Toom-Cook
    // method are called about ONCE per multiply, so at the first sizes of
    // each zone they are still being compiled while timing is running --
    // which shows up as huge trial-to-trial spread exactly at the start of
    // each algorithm's size range, and distorts the growth-exponent
    // comparison the threshold analysis makes. (The global warmup() above
    // also only touches sizes up to 4096 bits.) So warm up by INVOCATION
    // COUNT at each size: at least 12,000 multiplies, capped at 2 seconds
    // so the largest sizes don't make the run take forever.
    static void warmupAtSize(int bits, int reps, Random rnd) {
        BigInteger a = randomOfBits(bits, rnd);
        BigInteger b = randomOfBits(bits, rnd);
        long deadline = System.nanoTime() + 2_000_000_000L;
        BigInteger r = null;
        for (int i = 0; i < 12000 && System.nanoTime() < deadline; i++) {
            r = a.multiply(b);
        }
        if (r != null && r.signum() == 0) {
            System.out.print("");
        }
    }

    static void benchSizes(PrintWriter csv, String machine) {
        Random rnd = new Random(SEED);
        warmup(rnd);

        for (int bits : SIZES_BITS) {
            int reps = repsForBits(bits);
            warmupAtSize(bits, reps, rnd);
            for (int trial = 0; trial < TRIALS; trial++) {
                BigInteger a = randomOfBits(bits, rnd);
                BigInteger b = randomOfBits(bits, rnd);

                long t0 = System.nanoTime();
                BigInteger r = null;
                for (int i = 0; i < reps; i++) {
                    r = a.multiply(b);
                }
                long t1 = System.nanoTime();
                if (r.signum() == 0) {
                    System.out.print("");
                }

                double timeNs = (double) (t1 - t0) / reps;
                csv.printf("java,%s,%d,%d,%.3f%n", machine, bits, trial, timeNs);
            }
            System.err.println("sizes: done " + bits + " bits");
            csv.flush();
        }
    }

    static void benchThreshold(PrintWriter csv, String machine) {
        Random rnd = new Random(SEED + 1);
        warmup(rnd);

        // Step by individual ints right across each documented
        // threshold, not a log-scale ladder -- the whole point is to
        // see the exact int count where the timing curve bends.
        int[] intCounts = buildThresholdSweep();

        for (int ints : intCounts) {
            int bits = ints * 32;
            int reps = repsForBits(bits);
            warmupAtSize(bits, reps, rnd);
            for (int trial = 0; trial < TRIALS; trial++) {
                BigInteger a = randomOfBits(bits, rnd);
                BigInteger b = randomOfBits(bits, rnd);

                long t0 = System.nanoTime();
                BigInteger r = null;
                for (int i = 0; i < reps; i++) {
                    r = a.multiply(b);
                }
                long t1 = System.nanoTime();
                if (r.signum() == 0) {
                    System.out.print("");
                }

                double timeNs = (double) (t1 - t0) / reps;
                // x = bits here, same column meaning as "sizes" mode --
                // convert back to ints in analysis (bits / 32) when you
                // need to compare directly against the 80 / 240 constants.
                csv.printf("java_threshold,%s,%d,%d,%.3f%n", machine, bits, trial, timeNs);
            }
            System.err.println("threshold: done " + ints + " ints (" + bits + " bits)");
            csv.flush();
        }
    }

    // Wide geometric grid (~10% steps) from 16 to ~800 ints. This spans
    // roughly 0.2x to 10x of KARATSUBA_THRESHOLD (80) and covers
    // TOOM_COOK_THRESHOLD (240) as well. A narrow window around each
    // threshold is NOT enough: across +/-20% in size the curve changes by
    // only a few percent, which is below run-to-run noise. Fitting the
    // growth exponent separately in each zone needs a wide range.
    static int[] buildThresholdSweep() {
        java.util.TreeSet<Integer> set = new java.util.TreeSet<>();
        for (double v = 16; v <= 800; v *= 1.10) {
            set.add((int) Math.round(v));
        }
        return set.stream().mapToInt(Integer::intValue).toArray();
    }

    public static void main(String[] args) throws IOException {
        if (args.length < 3) {
            System.err.println("usage:");
            System.err.println("  java Benchmark sizes     <machine_label> <out.csv>");
            System.err.println("  java Benchmark threshold <machine_label> <out.csv>");
            System.exit(1);
        }

        String mode = args[0];
        String machine = args[1];
        String outpath = args[2];

        File outFile = new File(outpath);
        File parent = outFile.getParentFile();
        if (parent != null && !parent.exists()) {
            System.err.println("creating missing output directory: " + parent);
            parent.mkdirs();
        }

        try (PrintWriter csv = new PrintWriter(new FileWriter(outFile))) {
            csv.println("impl,machine,x,trial,time_ns");

            switch (mode) {
                case "sizes" ->
                    benchSizes(csv, machine);
                case "threshold" ->
                    benchThreshold(csv, machine);
                default -> {
                    System.err.println("unknown mode '" + mode + "' (use 'sizes' or 'threshold')");
                    System.exit(1);
                }
            }
        }
    }
}
