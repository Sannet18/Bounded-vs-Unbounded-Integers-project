#!/usr/bin/env bash
# run_final collects the FINAL benchmark dataset for one machine, in one go.
#
#   bash experiments/run_final.sh <machine-label> [--force]
#   e.g.  bash experiments/run_final.sh amd-8940hx
#
# Before you start (every time, both machines):
#   * plug in; Windows power mode = Best performance; vendor Turbo mode on;
#     energy saver off; close every other app (VS Code is fine, idle).
#   * do not touch the PC until it finishes.
# The script measures the CPU clock before and after every stage and refuses to
# start if the CPU looks throttled (override with --force). It writes:
#   data/final/final_<label>_<language>_<mode>.csv   (same schema as before)
#   data/final/final_<label>_meta.txt                (versions + clock readings)
# Anything not installed is skipped and noted.

set -u
LABEL="${1:-}"; [ -z "$LABEL" ] && { echo "usage: bash experiments/run_final.sh <machine-label> [--force]"; exit 1; }
FORCE=0; [ "${2:-}" = "--force" ] && FORCE=1
KTHR=16        # Karatsuba leaf size (limbs) for the C "sizes" runs: the best leaf found earlier

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
finddir() { for d in "$ROOT/$1" "$ROOT/$(echo "$1" | tr a-z A-Z)" "$ROOT/$(echo "${1:0:1}" | tr a-z A-Z)${1:1}"; do [ -d "$d" ] && { echo "$d"; return; }; done; }
CD="$(finddir c)"; JD="$(finddir java)"; RD="$(finddir rust)"; PD="$(finddir python)"; MD="$(finddir mathematica)"
[ -z "$CD" ] && { echo "cannot find the C folder under $ROOT"; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

F="$ROOT/data/final"; mkdir -p "$F"
META="$F/final_${LABEL}_meta.txt"; TMP="$(mktemp -d)"
: > "$META"
note() { echo "$*" | tee -a "$META"; }
clk()  { "$TMP/clk" 2>/dev/null || echo "n/a"; }
stage_clock() { note "clock $1: $(clk) GHz"; }

note "final-dataset metadata, label: $LABEL"
note "started: $(date -Is)"
have lscpu && lscpu | grep -E "Model name" | tee -a "$META"
note "gcc: $(gcc --version | head -1)"
note "glibc: $(ldd --version 2>&1 | head -1)"
note "GMP: $(dpkg-query -W -f='${Version}' libgmp-dev 2>/dev/null || echo unknown)"
have python3 && note "python: $(python3 --version 2>&1)"
have java    && note "java: $(java -version 2>&1 | grep -v Picked | head -1)"
have rustc   && note "rustc: $(rustc --version)"
have wolframscript && note "wolframscript: $(wolframscript -version 2>&1 | head -1)"
note "karatsuba leaf for C sizes runs: $KTHR limbs"

gcc -O2 -o "$TMP/clk" "$HERE/clock_estimate.c" 2>/dev/null || { echo "need gcc and experiments/clock_estimate.c"; exit 1; }
C0="$(clk)"; stage_clock "at start"
if awk "BEGIN{exit !($C0 < 3.0)}" && [ "$FORCE" = 0 ]; then
  echo ""
  echo "STOP: the CPU is running at only $C0 GHz, which looks throttled."
  echo "Plug in, set Best performance / Turbo, close other apps, then run again."
  echo "(Add --force to run anyway.)"
  rm -rf "$TMP"; exit 2
fi

echo ""; echo "correctness tests first (a benchmark of wrong code is worthless)..."
( cd "$CD" && make check >"$TMP/check.log" 2>&1 ) && note "correctness tests: PASSED" || { note "correctness tests: FAILED -- see below"; tail -20 "$TMP/check.log"; exit 3; }

T0=$(date +%s)
stage() { # stage <name> <command...>
  local name="$1"; shift
  echo ""; echo "== $name =="
  local s=$(date +%s)
  if "$@" >"$TMP/$name.log" 2>&1; then note "$name: done in $(( $(date +%s) - s )) s"
  else note "$name: FAILED (last lines of its log below)"; tail -5 "$TMP/$name.log" | tee -a "$META"; fi
  stage_clock "after $name"
}

#  C + GMP 
if ( cd "$CD" && make benchmark >/dev/null 2>&1 ); then
  stage c_sizes     bash -c "cd '$CD' && ./benchmark sizes     '$LABEL' '$F/final_${LABEL}_c_sizes.csv' $KTHR"
  stage c_threshold bash -c "cd '$CD' && ./benchmark threshold '$LABEL' '$F/final_${LABEL}_c_threshold.csv'"
  stage c_crossover bash -c "cd '$CD' && ./benchmark crossover '$LABEL' '$F/final_${LABEL}_c_crossover.csv'"
else note "C benchmark: could not build, SKIPPED"; fi

#  Python 
if have python3 && [ -n "$PD" ]; then
  stage python_sizes python3 "$PD/benchmark.py" "$LABEL" "$F/final_${LABEL}_python_sizes.csv"
else note "Python: SKIPPED"; fi

#  Java 
if have javac && have java && [ -n "$JD" ]; then
  mkdir -p "$TMP/jc" && javac -d "$TMP/jc" "$JD/Benchmark.java" 2>/dev/null
  stage java_sizes     java -cp "$TMP/jc" Benchmark sizes     "$LABEL" "$F/final_${LABEL}_java_sizes.csv"
  stage java_threshold java -cp "$TMP/jc" Benchmark threshold "$LABEL" "$F/final_${LABEL}_java_threshold.csv"
else note "Java: SKIPPED (not installed)"; fi

#  Rust 
if have cargo && [ -n "$RD" ] && ( cd "$RD" && cargo build --release >/dev/null 2>&1 ); then
  RBIN="$(cd "$RD" && ls target/release/ | grep -v '\.d$' | grep -vE '^(build|deps|examples|incremental|memory)$' | head -1)"
  stage rust_sizes     "$RD/target/release/$RBIN" sizes     "$LABEL" "$F/final_${LABEL}_rust_sizes.csv"
  stage rust_threshold "$RD/target/release/$RBIN" threshold "$LABEL" "$F/final_${LABEL}_rust_threshold.csv"
else note "Rust: SKIPPED (not installed or build failed)"; fi

#  Mathematica 
if have wolframscript && [ -n "$MD" ]; then
  stage mathematica_sizes wolframscript -file "$MD/benchmark.wls" "$LABEL" "$F/final_${LABEL}_mathematica_sizes.csv"
else note "Mathematica: SKIPPED (wolframscript not available)"; fi

#  wrap up 
echo ""; note "finished: $(date -Is)   total $(( $(date +%s) - T0 )) s"
echo ""; echo "files written:"
for f in "$F"/final_${LABEL}_*.csv; do [ -f "$f" ] && printf "  %-55s %7d rows\n" "$(basename "$f")" "$(( $(wc -l < "$f") - 1 ))"; done
MIN=$(grep '^clock' "$META" | awk '{print $(NF-1)}' | grep -v n/a | sort -n | head -1)
MAX=$(grep '^clock' "$META" | awk '{print $(NF-1)}' | grep -v n/a | sort -n | tail -1)
note "clock range over the whole run: $MIN - $MAX GHz"
if awk "BEGIN{exit !($MAX > 0 && ($MAX-$MIN)/$MAX > 0.10)}"; then
  note "WARNING: clock moved by more than 10% during the run -- the slower stages are not comparable; re-run."
fi
echo ""; echo "Send me: $META"; echo "and the data/final/ folder (or just zip it)."
rm -rf "$TMP"
