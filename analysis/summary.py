"""
Run this first. It tells you, per machine:
  * the median time for every implementation at every size
  * which cells are noisy (coefficient of variation > 10%)
  * the local growth exponent between neighbouring sizes
  * whether the hand-rolled C Karatsuba ever beats C schoolbook
  * how far the hand-rolled C code is from GMP

To run:  python3 analysis/summary.py [--raw data/raw]

printsf a plain-text overview of everything in data/raw/.

"""

import argparse

import numpy as np
import pandas as pd

from common import (DEFAULT_RAW, impl_sort_key, load_all,
                    local_exponents, pretty_time, summarize)


def sizes_table(s, machine):
    sub = s[(s.kind == "sizes") & (s.machine == machine)]
    if sub.empty:
        return
    impls = sorted(sub.impl.unique(), key=impl_sort_key)
    piv = sub.pivot(index="x", columns="impl", values="median")[impls]
    cv = sub.pivot(index="x", columns="impl", values="cv_pct")[impls]

    print(f"\n=== {machine}: median time per multiply (30 trials per cell) ===")
    header = f"{'bits':>8} {'words64':>8} " + " ".join(f"{i:>12}" for i in impls)
    print(header)
    for bits, row in piv.iterrows():
        cells = []
        for i in impls:
            v = row[i]
            if pd.isna(v):
                cells.append(f"{'-':>12}")
            else:
                flag = "*" if cv.loc[bits, i] > 10 else " "
                cells.append(f"{pretty_time(v):>11}{flag}")
        print(f"{int(bits):>8} {int(bits // 64):>8} " + " ".join(cells))
    print("  (* = coefficient of variation above 10%: treat that cell with caution)")

    print(f"\n--- {machine}: local growth exponent between adjacent sizes ---")
    print("  (2.0 = quadratic, 1.585 = Karatsuba, ~0 = fixed cost dominates)")
    print(f"{'bits (mid)':>12} " + " ".join(f"{i:>12}" for i in impls))
    xs = piv.index.values.astype(float)
    exps = {}
    for i in impls:
        mask = piv[i].notna().values
        if mask.sum() >= 2:
            exps[i] = local_exponents(xs[mask], piv[i].values[mask])
    ref = next(iter(exps.values()))[0]
    for k, xm in enumerate(ref):
        cells = []
        for i in impls:
            if i in exps and k < len(exps[i][1]):
                cells.append(f"{exps[i][1][k]:>12.2f}")
            else:
                cells.append(f"{'-':>12}")
        print(f"{xm:>12.0f} " + " ".join(cells))


def crossover_checks(s, machine):
    sub = s[(s.kind == "sizes") & (s.machine == machine)]
    have = set(sub.impl.unique())

    if {"schoolbook", "karatsuba"} <= have:
        sb = sub[sub.impl == "schoolbook"].set_index("x")["median"]
        ka = sub[sub.impl == "karatsuba"].set_index("x")["median"]
        common = sorted(set(sb.index) & set(ka.index))
        # "Sustained": the smallest size from which Karatsuba stays faster at
        # EVERY larger size tested. A single early win is usually a tie.
        wins = [x for i, x in enumerate(common) if all(ka[y] < sb[y] for y in common[i:])]
        print(f"\n--- {machine}: does hand-rolled C Karatsuba beat C schoolbook? ---")
        if wins:
            x = wins[0]
            print(f"  Yes: Karatsuba is faster at every size from {int(x)} bits ({int(x // 64)} limbs) up: "
                  f"{pretty_time(ka[x])} vs schoolbook {pretty_time(sb[x])} there.")
            big = common[-1]
            print(f"  At {int(big)} bits ({int(big // 64)} limbs): {sb[big] / ka[big]:.2f}x faster than schoolbook.")
            print("  (This ladder doubles each step, so it is coarse; run plot_crossover.py for the")
            print("   fine sweep and a confidence interval.)")
        else:
            x = common[-1]
            print(f"  NO, not at any tested size up to {int(x)} bits ({int(x // 64)} limbs).")
            print(f"  At that size: karatsuba {pretty_time(ka[x])} vs "
                  f"schoolbook {pretty_time(sb[x])} ({ka[x] / sb[x]:.1f}x slower).")
            print("  If this holds, the threshold sweep cannot show an interior")
            print("  minimum, and that is a property of the data structure's fixed")
            print("  per-call cost, not of Karatsuba itself.")

    ref = "gmp_mpn" if "gmp_mpn" in have else "gmp"
    if {"schoolbook", ref} <= have:
        gm = sub[sub.impl == ref].set_index("x")["median"]
        label = "GMP mpn_mul_n (limb-array layer)" if ref == "gmp_mpn" else "GMP mpz_mul"
        for impl in ("schoolbook", "karatsuba"):
            if impl not in have:
                continue
            mine = sub[sub.impl == impl].set_index("x")["median"]
            xs = sorted(set(mine.index) & set(gm.index))
            print(f"\n--- {machine}: hand-rolled C {impl} relative to {label} ---")
            for x in xs:
                r = mine[x] / gm[x]
                note = f"{r:.2f}x slower" if r >= 1 else f"{1 / r:.2f}x FASTER"
                print(f"  {int(x):>7} bits ({int(x // 64):>5} limbs): {note}")


def threshold_c(s, machine):
    sub = s[(s.kind == "threshold") & (s.impl == "karatsuba_threshold") & (s.machine == machine)]
    if sub.empty:
        return
    sub = sub.sort_values("x")
    best = sub.loc[sub["median"].idxmin()]
    print(f"\n=== {machine}: C Karatsuba threshold sweep ===")
    for _, r in sub.iterrows():
        mark = "  <-- fastest" if r["x"] == best["x"] else ""
        print(f"  threshold {int(r['x']):>4}: {pretty_time(r['median']):>10}{mark}")
    top = sub["x"].max()
    if best["x"] == top:
        print(f"  The fastest setting is the LARGEST threshold tested ({int(top)}), which")
        print(f"  means 'never use Karatsuba' (pure schoolbook) won. No interior minimum.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", default=str(DEFAULT_RAW))
    args = ap.parse_args()

    df = load_all(args.raw)
    s = summarize(df)

    print("Files loaded:")
    for name, grp in df.groupby("source_file"):
        print(f"  {name}: {len(grp)} rows, impl={sorted(grp.impl.unique())}")

    for machine in sorted(df.machine.unique()):
        sizes_table(s, machine)
        crossover_checks(s, machine)
        threshold_c(s, machine)


if __name__ == "__main__":
    main()