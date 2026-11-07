"""
plot_sizes.py figures for the size sweep (all five languages).

For each machine, writes plots/sizes_<machine>.png with three panels:

  A. time vs operand size (log-log), median line with interquartile band
  B. time / n^2 vs size. Flat = quadratic. Falling = sub-quadratic.
     This is the clearest way to see an algorithm switch, because the
     curve bends away from flat where Karatsuba/Toom start paying off.
  C. local growth exponent between neighbouring sizes, with reference
     lines at 2.0 (schoolbook), 1.585 (Karatsuba), 1.465 (Toom-3).

If data from more than one machine is present, also writes
plots/sizes_compare_machines.png.

To run:  python3 analysis/plot_sizes.py [--raw data/raw] [--out plots]
"""

import argparse
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from common import (DEFAULT_PLOTS, DEFAULT_RAW, EXP_KARATSUBA, EXP_SCHOOLBOOK,
                    EXP_TOOM3, LABELS, impl_sort_key, load_all,
                    local_exponents, summarize)

COLORS = {
    "schoolbook": "#9467bd", "karatsuba": "#d62728", "gmp": "#2ca02c",
    "gmp_mpn": "#17becf",
    "cpython": "#1f77b4", "java": "#ff7f0e", "rust": "#8c564b",
    "mathematica": "#e377c2",
}


def plot_machine(s, machine, out_dir):
    sub = s[(s.kind == "sizes") & (s.machine == machine)]
    if sub.empty:
        return
    impls = sorted(sub.impl.unique(), key=impl_sort_key)

    fig, axes = plt.subplots(1, 3, figsize=(18, 5.2))
    ax_t, ax_n, ax_e = axes

    for impl in impls:
        d = sub[sub.impl == impl].sort_values("x")
        x = d["x"].values.astype(float)
        med, q25, q75 = d["median"].values, d["q25"].values, d["q75"].values
        words = x / 64.0
        c = COLORS.get(impl)
        label = LABELS.get(impl, impl)

        ax_t.plot(x, med, "o-", color=c, label=label, ms=4)
        ax_t.fill_between(x, q25, q75, color=c, alpha=0.2)

        ax_n.plot(x, med / words**2, "o-", color=c, label=label, ms=4)

        if len(x) >= 2:
            xm, slopes = local_exponents(x, med)
            ax_e.plot(xm, slopes, "o-", color=c, label=label, ms=4)

    ax_t.set(xscale="log", yscale="log", xlabel="operand size (bits)",
             ylabel="time per multiply (ns)", title=f"A. Time vs size  [{machine}]")
    ax_n.set(xscale="log", yscale="log", xlabel="operand size (bits)",
             ylabel="time / (size in 64-bit words)^2  (ns)",
             title="B. Normalised by n^2 (flat = quadratic)")
    ax_e.set(xscale="log", xlabel="operand size (bits, midpoint of each pair)",
             ylabel="local exponent d(log t)/d(log n)",
             title="C. Local growth exponent (y-axis clipped)")
    # Local slopes on a factor-of-2 size ladder are noisy where an
    # algorithm switches on, so clip the axis to keep the interesting
    # range readable (any clipped points are still in the summary table).
    ax_e.set_ylim(-0.3, 3.2)
    for e, name, va in [(EXP_SCHOOLBOOK, "2 (schoolbook)", "bottom"),
                        (EXP_KARATSUBA, "1.585 (Karatsuba)", "bottom"),
                        (EXP_TOOM3, "1.465 (Toom-3)", "top")]:
        ax_e.axhline(e, color="gray", ls="--", lw=0.8)
        ax_e.text(0.99, e, name + " ", va=va, ha="right", fontsize=8,
                  color="gray", transform=ax_e.get_yaxis_transform())

    for ax in axes:
        ax.grid(True, which="both", alpha=0.25)
    ax_t.legend(fontsize=8)
    fig.tight_layout()
    path = out_dir / f"sizes_{machine}.png"
    fig.savefig(path, dpi=140)
    plt.close(fig)
    print(f"wrote {path}")


def plot_compare(s, machines, out_dir):
    """Overlay machines: same implementation, different CPU."""
    sub = s[s.kind == "sizes"]
    impls = sorted(sub.impl.unique(), key=impl_sort_key)
    styles = ["-", "--", ":", "-."]

    fig, (ax_n, ax_r) = plt.subplots(1, 2, figsize=(14, 5.2))
    for impl in impls:
        for k, m in enumerate(machines):
            d = sub[(sub.impl == impl) & (sub.machine == m)].sort_values("x")
            if d.empty:
                continue
            x = d["x"].values.astype(float)
            ax_n.plot(x, d["median"].values / (x / 64.0) ** 2, styles[k % 4],
                      marker="o", ms=3, color=COLORS.get(impl),
                      label=f"{LABELS.get(impl, impl)} [{m}]")

    base = machines[0]
    for impl in impls:
        a = sub[(sub.impl == impl) & (sub.machine == base)].set_index("x")["median"]
        for m in machines[1:]:
            b = sub[(sub.impl == impl) & (sub.machine == m)].set_index("x")["median"]
            common = sorted(set(a.index) & set(b.index))
            if common:
                ax_r.plot(common, [b[x] / a[x] for x in common], "o-", ms=3,
                          color=COLORS.get(impl), label=f"{LABELS.get(impl, impl)}")
    ax_r.axhline(1.0, color="gray", ls="--", lw=0.8)

    ax_n.set(xscale="log", yscale="log", xlabel="operand size (bits)",
             ylabel="time / n^2 (ns)", title="Normalised time, by machine")
    ax_r.set(xscale="log", xlabel="operand size (bits)",
             ylabel=f"time on {machines[1]} / time on {base}",
             title="Machine ratio (1.0 = identical)")
    for ax in (ax_n, ax_r):
        ax.grid(True, which="both", alpha=0.25)
    ax_n.legend(fontsize=6, ncol=2)
    ax_r.legend(fontsize=8)
    fig.tight_layout()
    path = out_dir / "sizes_compare_machines.png"
    fig.savefig(path, dpi=140)
    plt.close(fig)
    print(f"wrote {path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", default=str(DEFAULT_RAW))
    ap.add_argument("--out", default=str(DEFAULT_PLOTS))
    args = ap.parse_args()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    df = load_all(args.raw)
    s = summarize(df)
    machines = sorted(df[df.kind == "sizes"].machine.unique())
    for m in machines:
        plot_machine(s, m, out_dir)
    if len(machines) >= 2:
        plot_compare(s, machines, out_dir)
    else:
        print("only one machine found; skipping the cross-machine comparison figure")


if __name__ == "__main__":
    main()