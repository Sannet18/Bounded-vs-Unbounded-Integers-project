"""
For each machine, compares schoolbook against exactly ONE level of Karatsuba
at every even size from 4 to 192 limbs, for

  * the hand-rolled C code  (cross_schoolbook vs cross_karatsuba)
  * GMP's own internals     (cross_gmp_basecase vs cross_gmp_toom22),
                            if this libgmp exports them

One level, not a full recursion, because that is the clean definition of a
crossover: the size at which splitting once beats not splitting. It is the
same comparison GMP's own tuning program makes to set MUL_TOOM22_THRESHOLD.

Writes plots/crossover_<machine>.png (left: absolute times, right: the ratio
Karatsuba/schoolbook with a 95% bootstrap band; the crossover is where the
ratio crosses 1) and prints the crossover with a confidence interval.

To run:  python3 analysis/plot_crossover.py [--raw data/raw] [--out plots]
"""

import argparse
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from common import (DEFAULT_PLOTS, DEFAULT_RAW, LABELS, bootstrap_crossover,
                    load_all, summarize)

PAIRS = [
    # (fast impl, base impl, label, colour)
    ("cross_karatsuba", "cross_schoolbook", "hand-rolled C", "#d62728"),
    ("cross_gmp_toom22", "cross_gmp_basecase", "GMP internal", "#2ca02c"),
]


def trials_by_limbs(df, impl, machine):
    d = df[(df.impl == impl) & (df.machine == machine)]
    return {int(x // 64): g["time_ns"].values for x, g in d.groupby("x")}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", default=str(DEFAULT_RAW))
    ap.add_argument("--out", default=str(DEFAULT_PLOTS))
    args = ap.parse_args()
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    df = load_all(args.raw)
    s = summarize(df)
    cross = df[df.kind == "crossover"]
    if cross.empty:
        raise SystemExit("no crossover data found (run: ./benchmark crossover <machine> <out.csv>)")

    for machine in sorted(cross.machine.unique()):
        print(f"\n=== {machine}: schoolbook vs ONE level of Karatsuba "
              f"(even sizes, 4-192 limbs, {int(cross.groupby(['impl', 'x']).size().max())} trials each) ===")

        fig, (ax_t, ax_r) = plt.subplots(1, 2, figsize=(15, 5.4))
        found = {}

        sub = s[(s.kind == "crossover") & (s.machine == machine)]
        impl_colour = {"cross_karatsuba": "#d62728", "cross_schoolbook": "#e8918f",
                       "cross_gmp_toom22": "#2ca02c", "cross_gmp_basecase": "#98d198"}
        for impl in sorted(sub.impl.unique()):
            d = sub[sub.impl == impl].sort_values("x")
            ax_t.plot(d["x"] / 64, d["median"], "o-", ms=3, color=impl_colour.get(impl),
                      label=LABELS.get(impl, impl))
        ax_t.set(xscale="log", yscale="log", xlabel="operand size (limbs)",
                 ylabel="time per multiply (ns)", title=f"Absolute time [{machine}]")
        ax_t.grid(True, which="both", alpha=0.25)
        ax_t.legend(fontsize=8)

        for fast_impl, base_impl, label, colour in PAIRS:
            fast = trials_by_limbs(df, fast_impl, machine)
            base = trials_by_limbs(df, base_impl, machine)
            if not fast or not base:
                print(f"  {label}: no data (impl not present)")
                continue
            res = bootstrap_crossover(fast, base)
            found[label] = res
            x = res["sizes"]
            ax_r.plot(x, res["ratio"], "o-", ms=3, color=colour, label=f"{label}: Karatsuba / schoolbook")
            ax_r.fill_between(x, res["ratio_lo"], res["ratio_hi"], color=colour, alpha=0.18)

            if res["crossover"] is None:
                print(f"  {label}: the ratio never stays below 1 in the scanned range.")
                continue
            lo, hi = res["crossover_ci"]
            ax_r.axvline(res["crossover"], color=colour, ls="--", lw=1)
            print(f"  {label}: crossover at {res['crossover']} limbs "
                  f"({res['crossover'] * 64} bits); 95% CI {lo:.0f}-{hi:.0f} limbs")
            print(f"      (ratio below 1 at that size and the next {res['persist'] - 1} scanned sizes)")
            print(f"      first size with ratio < 1: {res['first_below_one']} limbs; "
                  f"{100 * res['p_no_crossover']:.1f}% of resamples had no crossover")
            if label == "hand-rolled C":
                print(f"      ratio at the largest size ({x[-1]} limbs): {res['ratio'][-1]:.2f}  "
                      f"(a perfect one-level split approaches 0.75)")
            else:
                print(f"      ratio at the largest size ({x[-1]} limbs): {res['ratio'][-1]:.2f}  "
                      f"(below 0.75 because GMP recurses further than one level there)")

        ax_r.axhline(1.0, color="gray", lw=1)
        ax_r.set(xscale="log", xlabel="operand size (limbs)",
                 ylabel="time(1-level Karatsuba) / time(schoolbook)",
                 title="Crossover: where the ratio drops below 1")
        ax_r.grid(True, which="both", alpha=0.25)
        ax_r.legend(fontsize=8)
        fig.tight_layout()
        path = out_dir / f"crossover_{machine}.png"
        fig.savefig(path, dpi=140)
        plt.close(fig)
        print(f"  wrote {path}")

        if "hand-rolled C" in found and "GMP internal" in found:
            fa, fg = found["hand-rolled C"], found["GMP internal"]
            if fa["crossover"] and fg["crossover"]:
                lo_a, hi_a = fa["crossover_ci"]
                lo_g, hi_g = fg["crossover_ci"]
                print(f"  hand-rolled {fa['crossover']} limbs vs GMP {fg['crossover']} limbs.")
                if hi_g < lo_a or hi_a < lo_g:
                    print("  The intervals do not overlap, so the two crossovers are distinguishable.")
                else:
                    print("  The intervals overlap, so this data cannot cleanly separate them.")
                print("  A later crossover means more per-level overhead (extra passes over the data,")
                print("  no assembly). GMP's ratio steps down again at larger sizes because its")
                print("  internal recursion adds further Karatsuba levels there: only its FIRST")
                print("  crossing is a one-level comparison.")

    print("\nHow to read this: the crossover is a property of the CPU and the code together.")
    print("Compare it between your AMD and Intel machines: if it lands in the same place, it is")
    print("mostly algorithmic (multiplications saved vs. extra additions); if it moves, look at")
    print("cache sizes, clock speed, and whether the code used mulx.")


if __name__ == "__main__":
    main()