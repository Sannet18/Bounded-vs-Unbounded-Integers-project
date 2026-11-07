"""
                The threshold studies.

1. C Karatsuba threshold sweep (plots/threshold_c_<machine>.png)
   Time vs threshold at one fixed operand size. If Karatsuba helps, the
   curve has an interior minimum. The horizontal line is pure
   schoolbook at the same size: any point BELOW it means Karatsuba
   actually paid off there.

2. Documented-threshold checks for Java and Rust
   (plots/threshold_java_<machine>.png, plots/threshold_rust_<machine>.png)
   Java:  KARATSUBA_THRESHOLD = 80 ints,  TOOM_COOK_THRESHOLD = 240 ints
   Rust:  long-mult -> Karatsuba at 32 limbs, Karatsuba -> Toom-3 at 256
   We fix those thresholds in advance, fit the growth exponent in each
   zone, and bootstrap the CHANGE in exponent across each boundary.

   HOW STRONG IS THIS TEST? Weaker than the C sweep. We cannot force
   BigInteger or num-bigint to use one algorithm above its threshold, so
   we only see the combined curve. A tuned threshold sits where the two
   algorithms cost the SAME, so the curve is continuous there and the
   exponent changes gradually. Real zones also carry overheads (JIT,
   allocation) that blur the textbook 2 / 1.585 / 1.465. So:
     * an exponent drop whose interval excludes 0 is CONSISTENT WITH the
       documented switch
     * no detectable change is NOT evidence against it
   Neither result proves the threshold value; the source code does that.

To run:  python3 analysis/plot_thresholds.py [--c-limbs 1024]
"""

import argparse
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from common import (DEFAULT_PLOTS, DEFAULT_RAW, bootstrap_zones,
                    load_all, pretty_time, summarize)

# (impl name, bits per unit, unit name, library label,
#  [(documented threshold, source name, textbook change in exponent), ...],
#  [zone names])
# Textbook exponents: schoolbook 2, Karatsuba log2(3)=1.585, Toom-3
# log3(5)=1.465, so the change is -0.415 at the first boundary and -0.12
# at the second.
FINE_SWEEPS = [
    ("java_threshold", 32, "ints", "Java BigInteger",
     [(80, "KARATSUBA_THRESHOLD", -0.415), (240, "TOOM_COOK_THRESHOLD", -0.12)],
     ["schoolbook", "Karatsuba", "Toom-Cook"]),
    ("rust_threshold", 64, "limbs", "Rust num-bigint",
     [(32, "long mult -> Karatsuba", -0.415), (256, "Karatsuba -> Toom-3", -0.12)],
     ["long mult", "Karatsuba", "Toom-3"]),
]


def c_threshold(df, s, machine, c_limbs, out_dir):
    sub = s[(s.kind == "threshold") & (s.impl == "karatsuba_threshold")
            & (s.machine == machine)].sort_values("x")
    if sub.empty:
        return
    x = sub["x"].values.astype(float)
    fig, ax = plt.subplots(figsize=(8, 5.2))
    ax.plot(x, sub["median"], "o-", color="#d62728", label="C Karatsuba")
    ax.fill_between(x, sub["q25"], sub["q75"], color="#d62728", alpha=0.2)

    school = s[(s.kind == "sizes") & (s.impl == "schoolbook") & (s.machine == machine)
               & (s.x == c_limbs * 64)]
    verdict = ""
    if not school.empty:
        base = float(school["median"].iloc[0])
        ax.axhline(base, color="#9467bd", ls="--",
                   label=f"pure schoolbook at {c_limbs} limbs ({pretty_time(base)})")
        # A threshold >= the operand size means the base case fires
        # immediately: that run IS schoolbook, not Karatsuba. Leave those
        # settings out when asking whether Karatsuba ever paid off.
        real = sub[sub["x"] < c_limbs]
        if real.empty:
            verdict = "every threshold tested is >= the operand size, so nothing here is Karatsuba."
        else:
            best = real.loc[real["median"].idxmin()]
            if best["median"] < base:
                verdict = (f"Best Karatsuba threshold {int(best['x'])}: {pretty_time(best['median'])}, "
                           f"{base / best['median']:.2f}x faster than schoolbook.")
            else:
                verdict = (f"Karatsuba never beat schoolbook ({pretty_time(base)}) at this size. "
                           f"Best setting was threshold {int(best['x'])} at {pretty_time(best['median'])}, "
                           f"{best['median'] / base:.1f}x slower. No interior minimum exists.")
    ax.set(xscale="log", yscale="log", xlabel="Karatsuba threshold (limbs)",
           ylabel=f"time per {c_limbs}-limb multiply (ns)",
           title=f"C Karatsuba threshold sweep [{machine}]")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend(fontsize=8)
    fig.tight_layout()
    path = out_dir / f"threshold_c_{machine}.png"
    fig.savefig(path, dpi=140)
    plt.close(fig)
    print(f"wrote {path}")
    if verdict:
        print(f"  [{machine}] {verdict}")


def fine_sweep(df, machine, spec, out_dir):
    impl, unit_bits, unit, label, knots, zone_names = spec
    raw = df[(df.impl == impl) & (df.machine == machine)].copy()
    if raw.empty:
        return
    raw["u"] = raw["x"] / unit_bits
    trials = {float(u): g["time_ns"].values for u, g in raw.groupby("u")}
    thresholds = [k for k, _, _ in knots]

    print(f"\n=== {label} [{machine}]: {len(trials)} sizes, "
          f"{int(min(trials))}-{int(max(trials))} {unit} ===")
    res = bootstrap_zones(trials, thresholds)
    if res is None:
        print("  too few sizes in at least one zone for a fit (need 4+ per zone)")
        return

    u = np.array(sorted(trials))
    med = np.array([np.median(trials[x]) for x in u])
    q25 = np.array([np.percentile(trials[x], 25) for x in u])
    q75 = np.array([np.percentile(trials[x], 75) for x in u])

    fig, ax = plt.subplots(figsize=(9, 5.6))
    ax.plot(u, med / u**2, "o", color="#1f77b4", ms=4, label="median / n^2")
    ax.fill_between(u, q25 / u**2, q75 / u**2, color="#1f77b4", alpha=0.18)

    edges = [u.min(), *thresholds, u.max()]
    fit_colors = ["#9467bd", "#d62728", "#2ca02c"]
    for z in range(len(edges) - 1):
        xs = np.linspace(edges[z], edges[z + 1], 60)
        ax.plot(xs, np.exp(res["predict"](np.log(xs))) / xs**2, "-", lw=2.4,
                color=fit_colors[z % 3],
                label=f"{zone_names[z]}: exponent {res['slopes'][z]:.2f} "
                      f"[{res['slope_ci'][z][0]:.2f}, {res['slope_ci'][z][1]:.2f}]")
    for k, name, _ in knots:
        ax.axvline(k, color="gray", ls="--", lw=1)
        ax.text(k, ax.get_ylim()[1], f" {k} {unit}\n {name}", fontsize=8,
                va="top", color="gray")

    ax.set(xscale="log", yscale="log", xlabel=f"operand size ({unit})",
           ylabel="time / n^2 (ns)",
           title=f"{label}: growth exponent by zone [{machine}]")
    ax.grid(True, which="both", alpha=0.25)
    ax.legend(fontsize=8, loc="lower left")
    fig.tight_layout()
    path = out_dir / f"threshold_{impl.split('_')[0]}_{machine}.png"
    fig.savefig(path, dpi=140)
    plt.close(fig)

    print("  growth exponent per zone (textbook: 2 / 1.585 / 1.465; 95% bootstrap interval):")
    for z, name in enumerate(zone_names):
        lo_edge = "start" if z == 0 else f"{thresholds[z - 1]}"
        hi_edge = "end" if z == len(thresholds) else f"{thresholds[z]}"
        print(f"    {name:<10} ({lo_edge:>5}-{hi_edge:<5} {unit}, {res['zone_counts'][z]:>2} sizes): "
              f"{res['slopes'][z]:.2f}  [{res['slope_ci'][z][0]:.2f}, {res['slope_ci'][z][1]:.2f}]")
    print("  change in exponent across each documented boundary "
          "(observed [95% interval] vs textbook):")
    for j, (k, name, expected) in enumerate(knots):
        ch, (lo, hi) = res["change"][j], res["change_ci"][j]
        if lo > 0:
            verdict = "exponent RISES: unexpected, check the data"
        elif hi < 0 and ch <= 0.5 * expected:
            verdict = "drops by roughly the textbook amount: consistent with the switch"
        elif hi < 0:
            verdict = ("drops, but by much less than the textbook change: weak evidence "
                       "(overheads blur the switch)")
        else:
            verdict = "no detectable change (interval includes 0): inconclusive"
        print(f"    at {k:>3} {unit} ({name}): {ch:+.2f} [{lo:+.2f}, {hi:+.2f}] "
              f"vs {expected:+.2f}  -> {verdict}")
    print("  note: intervals reflect trial-to-trial noise only. They do not capture")
    print("  systematic curvature inside a zone, so treat them as optimistic.")
    print(f"  wrote {path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", default=str(DEFAULT_RAW))
    ap.add_argument("--out", default=str(DEFAULT_PLOTS))
    ap.add_argument("--c-limbs", type=int, default=1024,
                    help="operand size (limbs) that c/benchmark.c's threshold mode used")
    args = ap.parse_args()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    df = load_all(args.raw)
    s = summarize(df)
    for machine in sorted(df.machine.unique()):
        c_threshold(df, s, machine, args.c_limbs, out_dir)
        for spec in FINE_SWEEPS:
            fine_sweep(df, machine, spec, out_dir)


if __name__ == "__main__":
    main()