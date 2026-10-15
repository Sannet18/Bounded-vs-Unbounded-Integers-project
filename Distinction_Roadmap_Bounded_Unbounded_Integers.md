# Roadmap to Distinction

## Bounded vs. Unbounded Integers in Programming Languages: Implementation, Memory Representation, and Performance Optimizations

**Languages:** C, Python, Java, Rust, Mathematica
**Author:** Nhyiraba Saint Appiah — Bridgewater College

---

## How the pieces fit together

Your title already gives you three pillars. The crossover-point question and the CPU/hardware question aren't a new topic — they're the deepest, most defensible material *inside* two of those three pillars. Here's the map:

| Pillar (from your title) | What it covers | Where your two deep-dive threads live |
|---|---|---|
| **1. Implementation** | How each language actually represents bounded vs. unbounded integers internally (limbs, digit arrays, fixed registers) | The CPU/hardware explanation (Section 2 below) belongs here — it's the foundation for *why* unbounded integers have to be implemented the way they are |
| **2. Memory Representation** | How bounded vs. unbounded integers are laid out and stored (fixed registers/words vs. arrays of limbs in heap memory) | Also draws on the hardware section — bounded ints live in a register/fixed word, unbounded ints live in memory arrays *because* no register-width instruction can hold them |
| **3. Performance Optimizations** | What each language/library does to make unbounded arithmetic fast despite the overhead | The schoolbook↔Karatsuba crossover-point investigation (Section 3 below) belongs here — it's one concrete, measurable optimization story, not the whole pillar |

So the plan below keeps your full five-language, three-pillar scope, and treats the crossover work as **one worked example under Performance Optimizations** — detailed enough to be a strong centerpiece, without swallowing the rest of the project.

---

## What "distinction" means for a project of this scope

- **Breadth done rigorously, not shallowly.** With three pillars and five languages, the risk is doing all of it superficially. Distinction-level work usually means picking *one* pillar (Performance Optimizations, via the crossover study) to go genuinely deep on, while still covering the other two solidly — not spreading equal shallow effort across everything.
- **A working comparison table across the two axes (bounded vs. unbounded behavior, memory layout)** that ties Implementation and Memory Representation together.
- **A quantitative highlight** (the crossover-point measurement) that ties Performance Optimizations to real data, not just description.
- **An explicit hardware grounding**, since your committee has already signaled this is expected and currently under-covered.

---

## Pillar 1 — Implementation

### What to cover
- [ ] For each language, describe how bounded integers are represented (fixed-width machine words: `int`/`long` in C, `int`/`long` in Java, `i32`/`i64` etc. in Rust) vs. how unbounded/arbitrary-precision integers are represented (digit/limb arrays).
- [ ] Cover overflow *behavior* per language — this was your original comparison table; keep it, it's Table 1 of your report:

  | Language | Default int type | Fixed-width overflow behavior | Arbitrary-precision access | Opt-in or automatic? |
  |---|---|---|---|---|
  | **C** | `int`/`long` (fixed) | Signed overflow is *undefined behavior*; unsigned wraps mod 2^n | External library (GMP `mpz_t`) | Fully opt-in |
  | **Java** | `int`/`long` (fixed) | Silent, well-defined two's-complement wraparound | `java.math.BigInteger` | Opt-in — separate type |
  | **Python** | `int` | N/A — no fixed-width int type exists | Built in; is the default | Automatic and unified |
  | **Rust** | `i32`/`i64`/etc. (fixed) | Panics in debug builds, wraps in release (unless `wrapping_*`/`checked_*` used explicitly) | External crate (`num-bigint`) | Opt-in |
  | **Mathematica** | `Integer` | N/A in normal use — always exact/arbitrary-precision | Built in; is the default | Automatic — most "invisible" of the five |

### The CPU/hardware grounding (deep dive for this pillar)

Your committee flagged this directly and said they'll ask it at your defense, so it needs real depth here, not a footnote.

**The question, paraphrased:** x86-64's multiply instruction only produces a 128-bit result from two 64-bit inputs, stored across two fixed registers (`RDX:RAX`). So how does software produce 32-byte, 64-byte, or arbitrarily large products? Is the CPU doing more? Is it the compiler? Where's the actual bottleneck — hardware or algorithm?

**The explanation, in full:**

1. **The CPU primitive is fixed and small.** `MUL`/`IMUL` on x86-64 always produces a 128-bit result split across `RDX:RAX`. There is no instruction that natively produces a wider result — the hardware's "grain size" for multiplication is fixed at 64×64→128, full stop.

2. **Big numbers are stored as arrays of limbs in RAM, not in registers.** CPython's `int` stores digits in 30-bit chunks in a C array; GMP stores 64-bit limbs; a hand-rolled C bignum typically does the same. Past a couple of limbs, none of this lives in registers — it lives in heap memory, like any array.

3. **Multiplying two n-limb numbers means invoking the fixed 64×64→128 primitive many times and combining results with carry propagation.** The schoolbook algorithm multiplies every limb of A against every limb of B (O(n²) individual multiplications) and sums the partial 128-bit products into the right positions of a result array, propagating carries — the same technique you'd hand-assemble with `mulq`/`addq`/`adcq` chains.

4. **Karatsuba is a smarter way to combine the same primitive, not a different primitive.** It splits each number into high/low halves and uses 3 multiplications instead of 4 to compute the same product, recursively — dropping the exponent from O(n²) to O(n^1.585). It's still built entirely from the same 64×64→128 CPU operation; the algorithm just needs fewer calls to it as numbers grow.

5. **There is a CPU feature built specifically to help this pattern: `MULX` (part of the BMI2 instruction set extension — supported on both Intel, since Haswell, and AMD, since Excavator/Zen, so it's present on both of your machines).** Ordinary `MUL` clobbers the carry/overflow flags, which conflicts with using those same flags to propagate carries across limbs. `MULX` does the same 64×64→128 multiply into two explicit destination registers without touching the flags — it exists purely so bignum libraries like GMP can chain limb-multiplications efficiently. This is the concrete answer to "does the CPU play a part": yes, by making the software technique faster, not by doing arbitrary-size multiplication itself.

**One-line answer for your defense:** *the CPU provides a fixed 64×64→128 primitive; everything beyond that size is a software decomposition problem carried out in memory, with the hardware only setting the "grain size" and the speed of each step.*

### Where to add it in the proposal
- [ ] Add a Background subsection: *"Why the CPU's fixed-width multiply forces an algorithmic answer."* Use the 5 points above as your outline.
- [ ] Rehearse a 90-second spoken version before your defense — this is very likely to be asked close to verbatim.

---

## Pillar 2 — Memory Representation

- [ ] For each language, document: how many bytes a bounded int occupies (fixed, always known at compile time) vs. how an unbounded int's memory footprint grows with value (array length × limb width + object/header overhead).
- [ ] Concretely measure memory footprint growth for at least Python, Java (`BigInteger`), and your hand-rolled C bignum as digit count increases (this reuses your Phase 3 benchmarking harness — just capture memory alongside time).
- [ ] Tie back to Pillar 1: explain memory representation as the direct consequence of the hardware constraint — since no register can hold an arbitrarily large value, the value *must* live in addressable memory as an array.

---

## Pillar 3 — Performance Optimizations

### The crossover-point study (your centerpiece example)

**Question for this section:** *How does the crossover point between schoolbook and Karatsuba multiplication vary across CPython's `int`, GMP, and a hand-rolled C bignum, and what does this reveal about implementation trade-offs?*

This is one concrete, well-scoped optimization story that demonstrates real rigor — not the whole project, but the strongest single piece of evidence in it.

- **Tier A (direct crossover measurement):** CPython `int`, GMP, your hand-rolled C bignum (implement both schoolbook and Karatsuba yourself, with the threshold as a tunable variable — this is your one fully original, sweep-able result).
- **Tier B (documented threshold, good bonus):** Java's `BigInteger` — OpenJDK's source defines `KARATSUBA_THRESHOLD = 50` (ints, i.e. 2560 bits), explicitly commented as *"found experimentally to work well"* — a genuine primary-source citation showing even production engineers tune this empirically, not mathematically. You can directly verify this threshold by timing numbers just below/above 80 ints.
- **Tier C (context only, no threshold claim):** Rust (`num-bigint` crate) and Mathematica — measure raw multiplication timing across the same input sizes to support the broader "opt-in vs. automatic" comparison from Pillar 1, without forcing a crossover-point claim these don't cleanly support (Mathematica in particular has no user-visible threshold at all — worth stating as a genuine finding: automatic algorithm selection trades transparency for convenience).

### Steps
- [ ] Implement schoolbook + Karatsuba in your hand-rolled C bignum; sweep the threshold and plot performance vs. threshold choice.
- [ ] Benchmark CPython and GMP across a log-scale range of digit sizes; locate the empirical inflection point (fit two slopes, or fit O(n²) and O(n^1.585) curves separately and find where they cross).
- [ ] If including Java: verify the documented 80-int threshold empirically.
- [ ] Explain *why* crossovers differ across implementations by connecting back to Pillar 1 — different limb widths, different constant-factor overhead in each Karatsuba implementation, different use of instructions like `MULX`.
- [ ] **If you run the cross-vendor comparison:** compare the measured crossover point on your AMD Ryzen 9 8940HX vs. your Intel machine. If the crossover point lands close together on both, that's evidence it's primarily an *algorithmic* property (the multiplication-count-vs-overhead trade-off) rather than a vendor-specific hardware quirk — a genuinely nice result to report. If they diverge noticeably, that's equally interesting and worth investigating against cache size, clock speed, or microarchitectural differences (e.g., Zen 4 vs. whichever Intel generation you're comparing against).
- [ ] Use ≥30 trials per data point; control for background load and CPU frequency scaling.

---

## Full phase timeline

### Phase 1 — Scope and literature (Weeks 1–3)
- [ ] Narrow research questions per pillar — keep them as sub-questions under the umbrella title, not standalone replacements for it.
- [ ] Expand literature to 15–25 sources: language specs, GMP internals, CVE case studies, OpenJDK source comments, Wolfram precision docs, Intel SDM.
- [ ] Finalize the Pillar 1 comparison table with real data (above).

### Phase 2 — Experiment design (Weeks 3–5)
- [ ] Design the crossover-point experiment (Tier A/B/C structure above).
- [ ] Design the memory-footprint measurement for Pillar 2.
- [ ] Decide trial counts, input size ranges, and controls.
- [ ] **Cross-vendor run (new — you have access to both an AMD and an Intel machine):** decide now whether you'll run the full benchmark matrix on both, or just a representative subset (e.g., the crossover-point sweep only). Running on both isn't required for the core question, but it upgrades your Limitations section from "single-machine, so generalizability is unknown" to an actual controlled comparison — genuinely distinction-level polish if time allows.

### Phase 3 — Build and run (Weeks 5–9)
- [ ] Build one benchmark harness capturing time *and* memory per trial (CSV: language/implementation, digit count, time, memory, trial number).
- [ ] Implement your hand-rolled C bignum (schoolbook + Karatsuba, tunable threshold).
- [ ] Run the full matrix across all five languages.
- [ ] If running cross-vendor: record full specs for both machines (CPU model, microarchitecture, core/thread count, cache sizes, RAM speed, OS) in a methodology appendix — e.g. AMD Ryzen 9 8940HX (Zen 4, 16C/32T, 64MB L3) vs. your Intel machine's equivalent specs. Keep OS, compiler version, and language runtime versions identical across both where possible, so differences in results are attributable to the CPU, not the software stack.
- [ ] Version-control everything.

### Phase 4 — Analysis (Weeks 9–11)
- [ ] Pillar 1/2 analysis: overflow behavior table + memory growth curves.
- [ ] Pillar 3 analysis: crossover-point curves, log-log scaling plots, comparison against documented constants (Java's 80, GMP's tuned value).
- [ ] Explicitly connect each pillar's findings back to the hardware explanation in Section "Pillar 1."

### Phase 5 — Writing (Weeks 11–13)
- [ ] Related Work section (Mora et al. 2024, Langer 2015, plus new sources).
- [ ] Limitations section — single-machine benchmarking, and the Tier A/B/C scoping decision for the crossover study (frame as deliberate, not an oversight).
- [ ] Conclusion reports actual findings across all three pillars, not predicted ones.

### Phase 6 — Polish and defend
- [ ] Rehearse the 90-second hardware explanation.
- [ ] Prepare the likely follow-up: *"So is the crossover point about the CPU or the algorithm?"* — Answer: both, inseparably; the CPU fixes the cost of the primitive operation, and the crossover is exactly where the algorithm's reduced multiplication count starts outweighing its own overhead, given that fixed cost.
- [ ] Have a one-sentence summary ready for each of the three pillars, so you can always re-anchor to the full title if a question pulls you into just one area.

---

## Resources

### Hardware / CPU background
- **Intel® 64 and IA-32 Architectures Software Developer's Manual, Vol. 2** (primary source for `MUL`/`IMUL`/`MULX` on Intel): https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html
- **AMD64 Architecture Programmer's Manual, Volume 3 (General-Purpose and System Instructions)** — the AMD-side equivalent covering `MUL`/`IMUL` semantics and the BMI2/`MULX` extension on Zen microarchitectures, including your Ryzen 9 8940HX (Zen 4): https://www.amd.com/en/search/documentation/hub.html (search "AMD64 Architecture Programmer's Manual Volume 3")
- GMP `mpn` low-level function docs: https://gmplib.org/manual/Low_002dlevel-Functions
- Worked assembly example of chained 64×64→128 multiply-accumulate: https://arxiv.org/pdf/1609.09840

**Why cite both:** x86-64 is a shared instruction set — Intel authored the original spec, but AMD has implemented (and in places extended, e.g. early adoption of BMI2 on Zen) it for decades. `MUL`/`IMUL` behave identically across vendors; citing both manuals shows the committee you verified this isn't an Intel-only mechanism, and it directly supports running the same experiment on two different physical CPUs (see Methodology note below).

### Implementation / crossover primary sources
- OpenJDK `BigInteger.java` source (search `KARATSUBA_THRESHOLD`, `TOOM_COOK_THRESHOLD`): https://github.com/openjdk/jdk/blob/master/src/java.base/share/classes/java/math/BigInteger.java
- CPython `Objects/longobject.c`: https://github.com/python/cpython/blob/main/Objects/longobject.c
- GMP manual, Algorithms chapter: https://gmplib.org/manual/
- Rust RFC 560 — Integer Overflow (design rationale): https://rust-lang.github.io/rfcs/0560-integer-overflow.html

### Theory and methodology
- Modern Computer Arithmetic (Brent & Zimmermann, free PDF): https://arxiv.org/pdf/1004.4710
- "Rigorous Benchmarking in Reasonable Time" (Kalibera & Jones): https://www.researchgate.net/publication/257193308_Rigorous_Benchmarking_in_Reasonable_Time

### Mathematica
- Wolfram Language docs — Numerical Evaluation & Precision: https://reference.wolfram.com/language/guide/NumericalEvaluationAndPrecision.html
- `MachinePrecision` reference: https://reference.wolfram.com/language/ref/MachinePrecision.html

### Memory profiling tools (Pillar 2)
- Python `tracemalloc` (standard library docs)
- Valgrind `massif` (C/GMP heap profiling)
- `/usr/bin/time -v` (quick cross-language wall-clock + peak memory)
