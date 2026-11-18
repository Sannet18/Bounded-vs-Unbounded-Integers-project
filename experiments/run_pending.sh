#!/usr/bin/env bash
# run_pending runs every pending experiment for the paper on THIS machine
# and save ONE text file, with a section per RESULT PENDING placeholder.
#
#   bash experiments/run_pending.sh <machine-label> [--skip-same]
#   e.g.  bash experiments/run_pending.sh amd-8940hx
#         bash experiments/run_pending.sh intel-xxxx --skip-same
#
# Run it from inside WSL. Before the AMD run: plug the laptop in and set the
# Windows power mode to "Best performance", and close other programs.
# Anything not installed (Java, Rust, wolframscript...) is skipped and noted.
# Output: results/pending_<label>.txt   (paste it back, or send the file)

set -u
LABEL="${1:-}"; [ -z "$LABEL" ] && { echo "usage: bash experiments/run_pending.sh <machine-label> [--skip-same]"; exit 1; }
SKIP_SAME=0; [ "${2:-}" = "--skip-same" ] && SKIP_SAME=1

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
# Folder names differ in case between machines (C/ vs c/): find them.
finddir() { for d in "$ROOT/$1" "$ROOT/$(echo "$1" | tr a-z A-Z)" "$ROOT/$(echo "${1:0:1}" | tr a-z A-Z)${1:1}"; do [ -d "$d" ] && { echo "$d"; return; }; done; }
CD="$(finddir c)"; JD="$(finddir java)"; RD="$(finddir rust)"; PD="$(finddir python)"; MD="$(finddir mathematica)"
[ -z "$CD" ] && { echo "cannot find the C folder under $ROOT"; exit 1; }

mkdir -p "$ROOT/results" "$ROOT/data/pending"
OUT="$ROOT/results/pending_${LABEL}.txt"
TMP="$(mktemp -d)"
: > "$OUT"
say()  { echo "$*" | tee -a "$OUT"; }
sec()  { say ""; say "=================================================================="; say "$*"; say "=================================================================="; }
have() { command -v "$1" >/dev/null 2>&1; }
run()  { "$@" 2>&1 | tee -a "$OUT"; }

say "pending-experiments report, label: $LABEL"
say "started: $(date -Is)"

# 0. machine facts 
sec "0. MACHINE FACTS (fills: fill-ins for CPU, clock, power plan, versions)"
say "kernel: $(uname -srm)"
have lscpu && lscpu | grep -E "Model name|^CPU\(s\)|Thread|MHz|max MHz|min MHz|L1d|L2|L3" | tee -a "$OUT"
say "BMI2 supported by this CPU: $(grep -qm1 bmi2 /proc/cpuinfo && echo yes || echo NO)"
say "cpu MHz right now (first 4 cores): $(grep -m4 'cpu MHz' /proc/cpuinfo | awk '{printf "%s ", $4}')"
have gcc && say "gcc: $(gcc --version | head -1)"
say "glibc: $(ldd --version 2>&1 | head -1)"
say "GMP: $(dpkg-query -W -f='${Version}' libgmp-dev 2>/dev/null || echo 'not found -- run: sudo apt install libgmp-dev')"
have python3 && say "python: $(python3 --version 2>&1)"
have java && say "java: $(java -version 2>&1 | grep -v Picked | head -1)"
have rustc && say "rustc: $(rustc --version)"
say "ON AC POWER / power plan: write this down by hand (Windows cannot be read from here)."

# 1. asm-check 
sec "1. PLACEHOLDER 1: mul / mulx instruction counts (make asm-check)"
if have gcc && have objdump && have make; then
  ( cd "$CD" && make asm-check 2>&1 ) | tee -a "$OUT"
else say "SKIPPED: need gcc, make and objdump (sudo apt install build-essential binutils)"; fi

# 2. default vs -mbmi2 
sec "2. PLACEHOLDER 2: schoolbook, default vs -mbmi2 (median of 30 trials, ns per multiply)"
if have gcc; then
  CORE="$CD/bn.c $CD/multiply_schoolbook.c $CD/multiply_karatsuba.c $CD/bigint.c"
  gcc -O2 -I"$CD" -o "$TMP/m_def" "$HERE/mulx_compare.c" $CORE \
    && say "(columns: build, limbs, median_ns, min_ns; builds alternate 3 times to expose drift)"
  if grep -qm1 bmi2 /proc/cpuinfo && gcc -O2 -mbmi2 -I"$CD" -o "$TMP/m_bmi" "$HERE/mulx_compare.c" $CORE; then
    for r in 1 2 3; do "$TMP/m_def" "default-r$r" | tee -a "$OUT"; "$TMP/m_bmi" "bmi2-r$r" | tee -a "$OUT"; done
  else
    say "-mbmi2 build skipped (CPU lacks BMI2 or compile failed); default only:"
    for r in 1 2 3; do "$TMP/m_def" "default-r$r" | tee -a "$OUT"; done
  fi
else say "SKIPPED: no gcc"; fi

# 3. allocator bytes 
sec "3. PLACEHOLDER 5: C allocator_bytes at 1, 64 and 65 bits, and glibc version"
if have gcc; then
  if ( cd "$CD" && make memory >/dev/null 2>&1 && ./memory "$TMP/mem_c.csv" >/dev/null 2>&1 ); then
    head -1 "$TMP/mem_c.csv" | tee -a "$OUT"
    say "impl,bits,bytes,payload_bytes,allocator_bytes"
    grep -E '^(c_bn|gmp_mpz|c_uint64),(1|64|65),' "$TMP/mem_c.csv" | tee -a "$OUT"
  else say "FAILED to build/run C memory tool (run: cd C && make memory)"; fi
else say "SKIPPED: no gcc"; fi

# 4. same-session five-language run 
if [ "$SKIP_SAME" = 1 ]; then
  sec "4. PLACEHOLDER 7: same-session timing -- SKIPPED (--skip-same)"
else
  sec "4. PLACEHOLDER 7: same-session timing, all languages back to back (can take 10-25 min; leave the PC alone)"
  S="$ROOT/data/pending"; L="same-${LABEL}"; START=$(date +%s)
  if have gcc && ( cd "$CD" && make benchmark >/dev/null 2>&1 ); then
    say "[C + GMP] running..."; ( cd "$CD" && ./benchmark sizes "$L" "$S/same_${LABEL}_c.csv" 16 ) >/dev/null 2>&1 && say "  done" || say "  FAILED"
  else say "[C + GMP] SKIPPED"; fi
  if have python3 && [ -n "$PD" ]; then
    say "[Python] running..."; python3 "$PD/benchmark.py" "$L" "$S/same_${LABEL}_python.csv" >/dev/null 2>&1 && say "  done" || say "  FAILED"
  else say "[Python] SKIPPED"; fi
  if have javac && have java && [ -n "$JD" ]; then
    say "[Java] running..."; mkdir -p "$TMP/jc"
    ( javac -d "$TMP/jc" "$JD/Benchmark.java" && java -cp "$TMP/jc" Benchmark sizes "$L" "$S/same_${LABEL}_java.csv" ) >/dev/null 2>&1 && say "  done" || say "  FAILED"
  else say "[Java] SKIPPED (not installed)"; fi
  if have cargo && [ -n "$RD" ]; then
    say "[Rust] running..."
    ( cd "$RD" && cargo build --release >/dev/null 2>&1 && \
      BIN="$(ls target/release/ | grep -v '\.d$' | grep -vE '^(build|deps|examples|incremental|memory)$' | head -1)" && \
      ./target/release/"$BIN" sizes "$L" "$S/same_${LABEL}_rust.csv" ) >/dev/null 2>&1 && say "  done" || say "  FAILED"
  else say "[Rust] SKIPPED (not installed)"; fi
  if have wolframscript.exe && [ -n "$MD" ]; then
    say "[Mathematica] running via Windows..."
    wolframscript.exe -file "$(wslpath -w "$MD/benchmark.wls")" "$L" "$(wslpath -w "$S/same_${LABEL}_mathematica.csv")" >/dev/null 2>&1 && say "  done" || say "  FAILED"
  else say "[Mathematica] not reachable from WSL. Right after this script ends, in PowerShell run:"
    say "    wolframscript -file mathematica\\benchmark.wls same-${LABEL} data\\pending\\same_${LABEL}_mathematica.csv"; fi
  say "elapsed: $(( $(date +%s) - START )) s"
  say ""
  say "Median ns per multiply, same session (rows: bits; columns: implementation):"
  python3 - "$S" "$LABEL" <<'PY' 2>&1 | tee -a "$OUT"
import csv, glob, statistics as st, sys, collections
S, lab = sys.argv[1], sys.argv[2]
d = collections.defaultdict(lambda: collections.defaultdict(list))
for f in glob.glob(f"{S}/same_{lab}_*.csv"):
    for r in csv.DictReader(l for l in open(f) if not l.startswith("#")):
        d[r["impl"]][int(r["x"])].append(float(r["time_ns"]))
want = [1024, 8192, 65536, 131072]
impls = sorted(d)
print(f"{'bits':>8} " + " ".join(f"{i:>12}" for i in impls))
for b in want:
    print(f"{b:>8} " + " ".join(f"{st.median(d[i][b]):>12.0f}" if b in d[i] else f"{'-':>12}" for i in impls))
PY
fi

# 5. Mathematica scans 
sec "5. PLACEHOLDERS 3 and 4: Mathematica overhead scan and 60-66-bit promotion scan"
if have wolframscript.exe && [ -n "$MD" ]; then
  wolframscript.exe -file "$(wslpath -w "$MD/pending_scans.wls")" 2>&1 | tr -d '\r' | tee -a "$OUT"
else
  say "NOT RUN from WSL (wolframscript.exe not reachable). In PowerShell, from the project folder, run:"
  say "  wolframscript -file mathematica\\pending_scans.wls"
  say "and paste its output into the reply."
fi

say ""; say "finished: $(date -Is)"
say "Report saved to: $OUT"
rm -rf "$TMP"
