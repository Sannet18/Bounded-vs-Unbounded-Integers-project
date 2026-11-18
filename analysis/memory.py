"""
How does an integer's memory footprint grow?

Reads data/memory/memory_*.csv (written by C/memory.c, python/memory.py,
java/MemBench.java, rust/src/bin/memory.rs, mathematica/memory.wls) and
answers the research question: does the observed scaling match the
prediction from each implementation's limb width and header overhead?

For every growing representation it reports
  * the fitted slope (bytes per bit) and fixed overhead (bytes), from a
    straight-line fit over the large sizes where the staircase is negligible,
    against the theoretical slope (bytes per limb / bits per limb)
  * the STEP WIDTH: memory is a staircase, stepping up only when another
    limb is needed, and the spacing of the steps is the limb size measured
    directly. This is a stronger check than a fitted slope.
  * payload efficiency: useful bytes (bits / 8) over total bytes

Fixed-size types (uint64_t, long, u128, and the old fixed 4096-limb struct)
are reported separately: constant size, whatever the value, until they
overflow or run out of capacity.

Writes plots/memory_footprint.png and data/processed/memory_summary.csv.

To run:  python3 analysis/memory.py [--mem data/memory] [--out plots]
"""

import argparse
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

from common import DEFAULT_PLOTS, ROOT

DEFAULT_MEM = ROOT / "data" / "memory"
DEFAULT_PROCESSED = ROOT / "data" / "processed"

# impl -> (theoretical bytes per bit once the digit array dominates, explanation)
THEORY = {
    "c_bn":     (8 / 64, "8 bytes per 64-bit limb"),
    "gmp_mpz":  (8 / 64, "8 bytes per 64-bit limb"),
    "rust":     (8 / 64, "8 bytes per 64-bit limb"),
    "cpython":  (4 / 30, "4 bytes per 30-bit digit"),
    "java":     (4 / 32, "4 bytes per 32-bit int"),
}
FIXED = ["c_uint64", "java_long", "rust_u64", "rust_u128", "c_bigint_t"]

LABELS = {
    "c_bn": "C limb array (hand-rolled)",
    "gmp_mpz": "GMP mpz_t",
    "rust": "Rust num-bigint",
    "cpython": "CPython int",
    "java": "Java BigInteger",
    "mathematica": "Mathematica",
    "c_uint64": "C uint64_t (fixed)",
    "java_long": "Java long (fixed)",
    "rust_u64": "Rust u64 (fixed)",
    "rust_u128": "Rust u128 (fixed)",
    "c_bigint_t": "C fixed 4096-limb struct (v1 design)",
}
COLORS = {
    "c_bn": "#9467bd", "gmp_mpz": "#2ca02c", "rust": "#8c564b", "cpython": "#1f77b4",
    "java": "#ff7f0e", "mathematica": "#e377c2", "c_bigint_t": "#7f7f7f",
    "c_uint64": "#bbbbbb", "java_long": "#bbbbbb", "rust_u64": "#bbbbbb", "rust_u128": "#999999",
}
FIT_FROM_BITS = 4096        # fit only where the staircase is negligible
DENSE_MAX_BITS = 320        # the contiguous 1..320 scan used for step widths


def load(mem_dir):
    frames = []
    for p in sorted(Path(mem_dir).glob("memory_*.csv")):
        df = pd.read_csv(p, comment="#")
        df["source"] = p.name
        frames.append(df)
    if not frames:
        raise SystemExit(f"no memory_*.csv files found in {mem_dir}")
    return pd.concat(frames, ignore_index=True)


def modal_step(bits, vals):
    """Most common spacing between the bit lengths at which `vals` steps up."""
    order = np.argsort(bits)
    b, v = np.asarray(bits)[order], np.asarray(vals, dtype=float)[order]
    ups = b[1:][np.diff(v) > 0]
    if len(ups) < 3:
        return None
    widths = np.diff(ups).astype(int)
    return int(np.bincount(widths).argmax())


def summarize(df):
    rows = []
    for impl in sorted(df.impl.unique()):
        if impl in FIXED:
            continue
        d = df[df.impl == impl].sort_values("bits")
        big = d[d.bits >= FIT_FROM_BITS]
        slope, intercept = np.polyfit(big.bits, big.bytes, 1)
        dense = d[d.bits <= DENSE_MAX_BITS]
        step_reported = modal_step(dense.bits, dense.bytes)
        has_payload = dense.payload_bytes.notna().all()
        step_payload = modal_step(dense.bits, dense.payload_bytes) if has_payload else None

        # Fixed overhead: take it from the exact limb boundaries (bit lengths that
        # are a multiple of the reported step), where there is no rounding. A
        # fit over arbitrary sizes would absorb about half a limb of average
        # round-up into the intercept.
        overhead = intercept
        if step_reported:
            exact = dense[dense.bits % step_reported == 0]
            if len(exact) >= 3:
                overhead = np.polyfit(exact.bits, exact.bytes, 1)[1]

        th = THEORY.get(impl)
        row = {
            "impl": impl,
            "slope_bytes_per_bit": slope,
            "bytes_per_64_bits": slope * 64,
            "theory_bytes_per_64_bits": th[0] * 64 if th else np.nan,
            "slope_vs_theory_pct": 100 * (slope / th[0] - 1) if th else np.nan,
            "fixed_overhead_bytes": overhead,
            "step_width_payload_bits": step_payload,
            "step_width_reported_bits": step_reported,
        }
        for b in (64, 1024, 1048576):
            r = d[d.bits == b]
            if not r.empty:
                nbytes = float(r.bytes.iloc[0])
                row[f"bytes_at_{b}"] = nbytes
                row[f"efficiency_at_{b}"] = (b / 8) / nbytes
        rows.append(row)
    return pd.DataFrame(rows)


def print_report(df, summ):
    print("=== Growing representations: fitted growth vs theory "
          f"(fit over bits >= {FIT_FROM_BITS}) ===")
    print(f"{'implementation':<30} {'B per 64 bits':>13} {'theory':>7} {'diff':>7} "
          f"{'fixed overhead':>15}")
    for _, r in summ.iterrows():
        diff = (f"{round(r.slope_vs_theory_pct, 2) + 0.0:+.2f}%"
                if not np.isnan(r.slope_vs_theory_pct) else "n/a")
        th = f"{r.theory_bytes_per_64_bits:.2f}" if not np.isnan(r.theory_bytes_per_64_bits) else "n/a"
        print(f"{LABELS.get(r.impl, r.impl):<30} {r.bytes_per_64_bits:>13.3f} {th:>7} {diff:>7} "
              f"{r.fixed_overhead_bytes:>12.1f} B")

    print("\n=== Step width: the limb size, measured directly (bit lengths 1..320) ===")
    print("  (memory only steps up when another limb is needed)")
    for _, r in summ.iterrows():
        sp, sr = r.step_width_payload_bits, r.step_width_reported_bits
        sp_ok, sr_ok = pd.notna(sp), pd.notna(sr)
        sp_s = f"{int(sp)} bits" if sp_ok else "n/a (not exposed)"
        sr_s = f"{int(sr)} bits" if sr_ok else "n/a"
        note = ""
        if sp_ok and sr_ok and sp != sr:
            note = "  <- reported size steps at a different width: allocator/object alignment padding"
        print(f"  {LABELS.get(r.impl, r.impl):<30} digit array steps: {sp_s:<19} reported size steps: {sr_s}{note}")

    print("\n=== Payload efficiency: useful bytes (bits/8) as a share of total bytes ===")
    print(f"{'implementation':<30} {'64-bit value':>13} {'1,024 bits':>11} {'1,048,576 bits':>15}")
    for _, r in summ.iterrows():
        def f(b):
            v = r.get(f"efficiency_at_{b}")
            return f"{100 * v:.1f}%" if v is not None and not pd.isna(v) else "n/a"
        print(f"{LABELS.get(r.impl, r.impl):<30} {f(64):>13} {f(1024):>11} {f(1048576):>15}")

    print("\n=== Bytes to hold specific values ===")
    sizes = [64, 1024, 1048576]
    print(f"{'implementation':<38}" + "".join(f"{s:>16,}" for s in sizes) + "   (bits)")
    for impl in list(summ.impl) + [i for i in FIXED if i in set(df.impl)]:
        d = df[df.impl == impl].set_index("bits")["bytes"]
        cells = [f"{int(d[s]):>16,}" if s in d.index else f"{'overflows':>16}" for s in sizes]
        print(f"{LABELS.get(impl, impl):<38}" + "".join(cells))
    print("  ('overflows' = a fixed-size type cannot represent a value that wide)")

    fixed = df[df.impl == "c_bigint_t"]
    if not fixed.empty:
        print(f"\nThe fixed 4096-limb struct costs {int(fixed.bytes.iloc[0]):,} bytes whether it holds "
              f"1 or 262,144 bits: O(1) in the value, and wasteful.")

    print("\nCaveats:")
    print("  * 'reported' sizes are each runtime's own accounting (sys.getsizeof, Instrumentation,")
    print("    ByteCount, size_of plus counted heap bytes). CPython's pymalloc and glibc's malloc")
    print("    round blocks up further; C's allocator_bytes column shows that for C and GMP.")
    print("  * Numbers are measured for values built directly at the target size. A value grown")
    print("    through repeated arithmetic can hold spare capacity (e.g. a Vec that doubled).")
    print("  * Python/Java/Rust/Mathematica versions differ in object layout; see the CSV headers.")


def plot(df, summ, out_dir):
    growing = [i for i in summ.impl]
    fig, axes = plt.subplots(1, 3, figsize=(19, 5.4))
    ax_a, ax_b, ax_c = axes

    for impl in growing:
        d = df[df.impl == impl].sort_values("bits")
        ax_a.plot(d.bits, d.bytes, "-", color=COLORS.get(impl), label=LABELS.get(impl, impl), lw=1.8)
        big = d[d.bits >= 64]
        ax_b.plot(big.bits, (big.bits / 8) / big.bytes, "-", color=COLORS.get(impl),
                  label=LABELS.get(impl, impl), lw=1.8)
        dense = d[d.bits <= DENSE_MAX_BITS]
        ax_c.step(dense.bits, dense.bytes, where="post", color=COLORS.get(impl),
                  label=LABELS.get(impl, impl), lw=1.5)
    for impl in FIXED:
        d = df[df.impl == impl].sort_values("bits")
        if d.empty:
            continue
        ax_a.plot(d.bits, d.bytes, "--" if impl == "c_bigint_t" else ":", color=COLORS.get(impl),
                  label=LABELS.get(impl, impl), lw=1.8)

    ax_a.set(xscale="log", yscale="log", xlabel="value size (bits)", ylabel="bytes",
             title="A. Memory footprint vs bit length")
    ax_b.set(xscale="log", xlabel="value size (bits)", ylabel="useful bytes / total bytes",
             title="B. Payload efficiency (1.0 = no overhead)", ylim=(0, 1.1))
    ax_c.set(xlabel="value size (bits)", ylabel="bytes",
             title="C. The staircase (bit lengths 1-320)")
    for ax in axes:
        ax.grid(True, which="both", alpha=0.25)
    ax_a.legend(fontsize=7, loc="upper left")
    ax_c.legend(fontsize=8, loc="upper left")
    fig.tight_layout()
    path = out_dir / "memory_footprint.png"
    fig.savefig(path, dpi=140)
    plt.close(fig)
    print(f"\nwrote {path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mem", default=str(DEFAULT_MEM))
    ap.add_argument("--out", default=str(DEFAULT_PLOTS))
    args = ap.parse_args()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    DEFAULT_PROCESSED.mkdir(parents=True, exist_ok=True)

    df = load(args.mem)
    print("Files:", ", ".join(sorted(df.source.unique())), "\n")
    summ = summarize(df)
    print_report(df, summ)
    summ.to_csv(DEFAULT_PROCESSED / "memory_summary.csv", index=False)
    print(f"wrote {DEFAULT_PROCESSED / 'memory_summary.csv'}")
    plot(df, summ, out_dir)


if __name__ == "__main__":
    main()