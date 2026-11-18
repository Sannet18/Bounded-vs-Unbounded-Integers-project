"""
The centerpiece: where does Karatsuba start to win?

For each machine, compares schoolbook against exactly ONE level of Karatsuba
at every even size from 4 to 192 limbs, for

  * the hand-rolled C code  (cross_schoolbook vs cross_karatsuba)
  * GMP's own internals     (cross_gmp_basecase vs cross_gmp_toom22),
                            if this libgmp exports them

One level, not a full recursion, because that is the clean definition of a
crossover: the size at which splitting once beats not splitting. It is the
same comparison GMP's own tuning program makes to set MUL_TOOM22_THRESHOLD.

Writes plots/crossover_<machine>.png for each machine, and when two or more
machines are present, plots/crossover_compare_machines.png plus a printed
AMD-vs-Intel style comparison.

HOW TO READ THE NUMBERS
  * The interval comes from resampling trials. It captures trial-to-trial
    noise only. It does NOT capture session-to-session drift (power state,
    temperature, background load), so a very narrow interval, or even a
    zero-width one, is not a guarantee. Repeat the run in separate sessions to
    see the real spread.
  * The scan has a resolution: sizes step by 2 limbs up to 64, then by 4. A
    difference of one step is within what the scan can resolve.

To run:  python3 analysis/plot_crossover.py [--raw data/raw] [--out plots]
"""

import argparse
import itertools
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from common import (DEFAULT_PLOTS, DEFAULT_RAW, LABELS, bootstrap_crossover,
                    load_all, summarize)

PAIRS = [
    # (fast impl, base impl, label, colour)
    ("cross_karatsuba", "cross_schoolbook", "hand-rolled C", "#d62728"),
    ("cross_gmp_toom22", "cross_gmp_basecase", "GMP internal", "#2ca02c"),
]
LINESTYLES = ["-", "--", ":", "-."]


def trials_by_limbs(df, impl, machine):
    d = df[(df.impl == impl) & (df.machine == machine)]
    return {int(x // 64): g["time_ns"].values for x, g in d.groupby("x")}


def scan_step(limbs):
    """Spacing of the benchmark's size scan near `limbs` (2 up to 64, then 4)."""
    return 2 if limbs <= 64 else 4


def per_machine(df, s, machine, out_dir):
    """One machine's figure and printout. Returns {label: result}."""
    mdf = df[(df.kind == "crossover") & (df.machine == machine)]
    trials = int(mdf.groupby(["impl", "x"]).size().max())
    print(f"\n=== {machine}: schoolbook vs ONE level of Karatsuba "
          f"(even sizes, 4-192 limbs, {trials} trials each) ===")

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
        ax_r.plot(x, res["ratio"], "o-", ms=3, color=colour,
                  label=f"{label}: Karatsuba / schoolbook")
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
            print(f"  hand-rolled {fa['crossover']} limbs vs GMP {fg['crossover']} limbs: "
                  + ("distinguishable (intervals do not overlap)." if (hi_g < lo_a or hi_a < lo_g)
                     else "the intervals overlap, so not cleanly separable."))
            print("  (GMP's ratio steps down again at larger sizes because its internal")
            print("  recursion adds further levels: only its FIRST crossing is one-level.)")
    return found


def compare_machines(machines, results, out_dir):
    """Overlay and compare crossovers between machines."""
    print("\n" + "=" * 72)
    print(f"=== cross-machine comparison: {' vs '.join(machines)} ===")

    fig, axes = plt.subplots(1, len(PAIRS), figsize=(7.5 * len(PAIRS), 5.2), squeeze=False)
    for ax, (_, _, label, colour) in zip(axes[0], PAIRS):
        for k, m in enumerate(machines):
            res = results[m].get(label)
            if not res:
                continue
            ax.plot(res["sizes"], res["ratio"], LINESTYLES[k % 4], marker="o", ms=3,
                    color=colour, label=m)
            ax.fill_between(res["sizes"], res["ratio_lo"], res["ratio_hi"],
                            color=colour, alpha=0.12)
        ax.axhline(1.0, color="gray", lw=1)
        ax.set(xscale="log", xlabel="operand size (limbs)",
               ylabel="Karatsuba / schoolbook time ratio", title=f"{label}: by machine")
        ax.grid(True, which="both", alpha=0.25)
        ax.legend(fontsize=8)
    fig.tight_layout()
    path = out_dir / "crossover_compare_machines.png"
    fig.savefig(path, dpi=140)
    plt.close(fig)
    print(f"wrote {path}")

    for _, _, label, _ in PAIRS:
        have = [m for m in machines if results[m].get(label) and results[m][label]["crossover"]]
        if len(have) < 2:
            continue
        print(f"\n  {label}:")
        for m in have:
            r = results[m][label]
            lo, hi = r["crossover_ci"]
            print(f"    {m:<22} crossover {r['crossover']:>3} limbs  (95% CI {lo:.0f}-{hi:.0f}; "
                  f"first dip below 1 at {r['first_below_one']}; "
                  f"ratio at {r['sizes'][-1]} limbs {r['ratio'][-1]:.2f})")
        for a, b in itertools.combinations(have, 2):
            ra, rb = results[a][label], results[b][label]
            (lo_a, hi_a), (lo_b, hi_b) = ra["crossover_ci"], rb["crossover_ci"]
            diff = abs(ra["crossover"] - rb["crossover"])
            step = scan_step(min(ra["crossover"], rb["crossover"]))
            overlap = lo_a <= hi_b and lo_b <= hi_a
            if not overlap and diff > step:
                verdict = (f"DISTINGUISHABLE: {ra['crossover']} vs {rb['crossover']} limbs, "
                           f"intervals do not overlap and the gap exceeds one scan step ({step}).")
            elif overlap:
                verdict = ("NOT SEPARABLE: the intervals overlap, so the crossover lands in the "
                           "same place to within what this data can resolve.")
            else:
                verdict = (f"NOT SEPARABLE: the estimates differ by {diff} limbs, within one scan "
                           f"step ({step}).")
            print(f"    {a} vs {b}: {verdict}")

    print("\nReading this: if the crossover lands in the same place on both machines it is")
    print("mostly a property of the algorithm (multiplications saved vs. extra additions and")
    print("data passes). If it moves, look at cache sizes, clock speed, and which instructions")
    print("were used. Absolute times are NOT comparable across machines unless both ran under")
    print("the same power and thermal conditions; the ratios and the crossover are, because")
    print("each is measured within a single machine.")
    print("\nCaveat: intervals reflect trial noise only. Repeat the crossover run in 2-3")
    print("separate sessions per machine; the spread of those estimates is the realistic")
    print("uncertainty.")


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

    machines = sorted(cross.machine.unique())
    results = {m: per_machine(df, s, m, out_dir) for m in machines}
    if len(machines) >= 2:
        compare_machines(machines, results, out_dir)


if __name__ == "__main__":
    main()