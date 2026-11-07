"""
Every benchmark harness (C, Python, Java, Rust, Mathematica) writes the
same CSV schema:  impl,machine,x,trial,time_ns

  * "sizes" rows:      x = operand size in BITS
  * "threshold" rows:  x = bits too, EXCEPT for C's karatsuba_threshold,
                       where x = the Karatsuba threshold value (in limbs)
  * "crossover" rows:  (impl names starting "cross_") x = bits; a fine sweep
                       of schoolbook vs ONE level of Karatsuba

This module loads everything in data/raw/, labels each row as "sizes" or
"threshold", and provides the statistics the plotting scripts share.

Shared helpers for the analysis scripts.

"""

from pathlib import Path
import sys

import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_RAW = ROOT / "data" / "raw"
DEFAULT_PLOTS = ROOT / "plots"

COLUMNS = ["impl", "machine", "x", "trial", "time_ns"]
THRESHOLD_IMPLS = {"karatsuba_threshold", "java_threshold", "rust_threshold"}

# Display names / plot order. Anything not listed still plots, just last.
LABELS = {
    "schoolbook": "C schoolbook (hand-rolled)",
    "karatsuba": "C Karatsuba (hand-rolled)",
    "gmp": "GMP mpz_mul",
    "gmp_mpn": "GMP mpn_mul_n",
    "cpython": "CPython int",
    "java": "Java BigInteger",
    "rust": "Rust num-bigint",
    "mathematica": "Mathematica",
    "cross_schoolbook": "C schoolbook (hand-rolled)",
    "cross_karatsuba": "C Karatsuba, 1 level (hand-rolled)",
    "cross_gmp_basecase": "GMP basecase (internal)",
    "cross_gmp_toom22": "GMP Toom-22, 1 level (internal)",
}
ORDER = list(LABELS)

# Reference exponents: the asymptotic cost of each algorithm is n^e.
EXP_SCHOOLBOOK = 2.0
EXP_KARATSUBA = np.log(3) / np.log(2)   # ~1.585
EXP_TOOM3 = np.log(5) / np.log(3)       # ~1.465


def load_all(raw_dir=DEFAULT_RAW):
    # Read every CSV in raw_dir into one DataFrame.
    raw_dir = Path(raw_dir)
    frames = []
    for path in sorted(raw_dir.glob("*.csv")):
        try:
            df = pd.read_csv(path)
        except pd.errors.EmptyDataError:
            print(f"skipping {path.name}: file is empty "
                  f"(interrupted run?)", file=sys.stderr)
            continue
        if df.empty:
            print(f"skipping {path.name}: header only, no data rows",
                  file=sys.stderr)
            continue
        if list(df.columns) != COLUMNS:
            print(f"skipping {path.name}: columns are {list(df.columns)}, "
                  f"expected {COLUMNS}", file=sys.stderr)
            continue
        df["source_file"] = path.name
        frames.append(df)
    if not frames:
        sys.exit(f"no usable CSV files found in {raw_dir}")

    df = pd.concat(frames, ignore_index=True)
    df["kind"] = np.select(
        [df["impl"].isin(THRESHOLD_IMPLS), df["impl"].str.startswith("cross_")],
        ["threshold", "crossover"],
        default="sizes",
    )
    # Common unit for comparing across languages: 64-bit words. The
    # exponent of the growth curve doesn't depend on limb width, so
    # using one unit for everything keeps curves directly comparable.
    df["words64"] = df["x"] / 64.0
    return df


def summarize(df):
    # Per (kind, impl, machine, x): median, spread, and trial count.
    g = df.groupby(["kind", "impl", "machine", "x"])["time_ns"]
    out = g.agg(
        median="median",
        min="min",
        q25=lambda s: s.quantile(0.25),
        q75=lambda s: s.quantile(0.75),
        mean="mean",
        std="std",
        n="count",
    ).reset_index()
    out["cv_pct"] = 100.0 * out["std"] / out["mean"]
    return out


def impl_sort_key(impl):
    return ORDER.index(impl) if impl in ORDER else len(ORDER)


def pretty_time(ns):
    # Human-readable time from nanoseconds.
    if ns < 1e3:
        return f"{ns:.0f} ns"
    if ns < 1e6:
        return f"{ns / 1e3:.1f} us"
    if ns < 1e9:
        return f"{ns / 1e6:.2f} ms"
    return f"{ns / 1e9:.2f} s"


def local_exponents(x, y):
    """Log-log slope between each pair of adjacent points.

    Returns (x_mid, slope), where x_mid is the geometric midpoint of
    each adjacent pair. A slope of 2 means quadratic growth, ~1.585
    means Karatsuba-like growth, and ~0 means the time is dominated by
    a fixed cost that doesn't depend on operand size.
    """
    x = np.asarray(x, dtype=float)
    y = np.asarray(y, dtype=float)
    lx, ly = np.log(x), np.log(y)
    slopes = np.diff(ly) / np.diff(lx)
    x_mid = np.exp((lx[:-1] + lx[1:]) / 2)
    return x_mid, slopes


# ---------------------------------------------------------------------------
# Zone-slope test around documented thresholds
# ---------------------------------------------------------------------------
#
# Locating a breakpoint from data is unreliable when the curve changes by
# only a few percent across it (an early version of this project tried it
# and produced slopes of +5 and -10 from noise). Instead we fix the
# documented thresholds in advance, fit the growth exponent separately in
# each zone, and ask whether the exponent changes across each boundary.

def fit_zones(log_x, log_y, log_knots):
    """Continuous piecewise-linear fit in log-log space, knots fixed.

    Returns (slopes, predict): one slope per zone (len(knots)+1 of them)
    and a function mapping log-size -> fitted log-time.
    """
    lx = np.asarray(log_x, dtype=float)
    ly = np.asarray(log_y, dtype=float)

    def design(v):
        v = np.asarray(v, dtype=float)
        return np.column_stack([np.ones_like(v), v] + [np.maximum(v - k, 0.0) for k in log_knots])

    coef, *_ = np.linalg.lstsq(design(lx), ly, rcond=None)
    slopes = np.cumsum(coef[1:])          # slope in zone 0, then + each hinge
    return slopes, (lambda v: design(v) @ coef)


def bootstrap_zones(trials_by_x, knots, n_boot=1000, seed=0, min_points=4):
    """Per-zone exponents with bootstrap 95% intervals.

    trials_by_x: {size: array of per-trial times}.  knots: thresholds in
    the same units as size. Each resample redraws the trials at every
    size (with replacement), takes the median per size, and refits.
    Also returns the interval for the change in exponent across each
    knot, which is the quantity that answers "does the algorithm switch
    show up in the data?".
    """
    rng = np.random.default_rng(seed)
    xs = np.array(sorted(trials_by_x))
    lx = np.log(xs)
    lk = np.log(np.asarray(knots, dtype=float))

    edges = [-np.inf, *np.exp(lk), np.inf]
    counts = [int(((xs > edges[i]) & (xs <= edges[i + 1])).sum()) for i in range(len(edges) - 1)]
    if min(counts) < min_points:
        return None

    meds = np.array([np.median(trials_by_x[x]) for x in xs])
    slopes, predict = fit_zones(lx, np.log(meds), lk)

    boots = []
    for _ in range(n_boot):
        m = np.array([
            np.median(rng.choice(trials_by_x[x], size=len(trials_by_x[x]), replace=True))
            for x in xs
        ])
        boots.append(fit_zones(lx, np.log(m), lk)[0])
    boots = np.array(boots)
    diffs = np.diff(boots, axis=1)

    return {
        "slopes": slopes,
        "slope_ci": np.percentile(boots, [2.5, 97.5], axis=0).T,
        "change": np.diff(slopes),
        "change_ci": np.percentile(diffs, [2.5, 97.5], axis=0).T,
        "zone_counts": counts,
        "predict": predict,
        "n_boot": n_boot,
    }


# ---------------------------------------------------------------------------
# Crossover estimation: schoolbook vs one level of Karatsuba
# ---------------------------------------------------------------------------

def bootstrap_crossover(fast, base, n_boot=2000, seed=0, persist=6):
    """Where does `fast` (Karatsuba) become faster than `base` (schoolbook)?

    fast, base: {size: array of per-trial times}, same sizes in both.
    Returns the per-size ratio fast/base (median-based) with a 95% bootstrap
    band, and the PERSISTENT crossover: the smallest size at which the ratio
    is below 1 and stays below 1 for the next `persist` scanned sizes. A
    single noisy dip below 1 near the crossover is not a crossover, but
    requiring the ratio to stay below 1 at EVERY larger size is too fragile:
    one noise burst far from the crossover would move the estimate. Local
    persistence avoids both problems.
    """
    rng = np.random.default_rng(seed)
    sizes = np.array(sorted(set(fast) & set(base)))
    S = len(sizes)

    ratio_pt = np.array([np.median(fast[s]) / np.median(base[s]) for s in sizes])

    boot = np.empty((n_boot, S))
    for j, s in enumerate(sizes):
        f, b = np.asarray(fast[s]), np.asarray(base[s])
        fi = rng.integers(0, len(f), size=(n_boot, len(f)))
        bi = rng.integers(0, len(b), size=(n_boot, len(b)))
        boot[:, j] = np.median(f[fi], axis=1) / np.median(b[bi], axis=1)

    def sustained(ratio_rows):
        below = ratio_rows < 1.0
        # window i is "ok" if sizes i .. i+persist-1 are all below 1; windows
        # running off the end of the scan are padded with True
        pad = np.ones(below.shape[:-1] + (persist - 1,), dtype=bool)
        win = np.lib.stride_tricks.sliding_window_view(
            np.concatenate([below, pad], axis=-1), persist, axis=-1)
        ok = win.all(axis=-1)
        return ok.any(axis=-1), np.argmax(ok, axis=-1)

    has_pt, first_pt = sustained(ratio_pt[None, :])
    has_b, first_b = sustained(boot)
    point = int(sizes[first_pt[0]]) if has_pt[0] else None
    ci = None
    if has_b.any():
        vals = sizes[first_b[has_b]]
        ci = (float(np.percentile(vals, 2.5)), float(np.percentile(vals, 97.5)))
    return {
        "sizes": sizes,
        "ratio": ratio_pt,
        "ratio_lo": np.percentile(boot, 2.5, axis=0),
        "ratio_hi": np.percentile(boot, 97.5, axis=0),
        "crossover": point,
        "crossover_ci": ci,
        "p_no_crossover": float(1.0 - has_b.mean()),
        "persist": persist,
        "first_below_one": int(sizes[np.argmax(ratio_pt < 1)]) if (ratio_pt < 1).any() else None,
    }