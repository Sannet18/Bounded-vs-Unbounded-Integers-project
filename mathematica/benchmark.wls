(*had to install the wolframscript license*)
#!/usr/bin/env wolframscript
(* benchmark.wls
   
   Mathematica has about no user-visible threshold the way OpenJDK or num-bigint do,
   algorithm selection is fully automatic and invisible. So there's only
   one mode here, unlike the C/Java/Rust harnesses.

   This times native Integer multiplication (Mathematica's arbitrary-
   precision integers are the default and only integer type) across the same bit ladder as the
   other four languages, writing the same CSV schema:
   impl,machine,x,trial,time_ns

   Usage:
     wolframscript -file benchmark.wls <machine_label> <out.csv>
*)

(* $HistoryLength defaults to infinite: every evaluation's result
   gets cached in Out[n]. Across thousands of timed multiplications
   that memory grows without bound and can slow the whole run down
   over time. This is the Wolfram-specific equivalent of Python's
   gc.disable() concern, just the opposite fix (stop it from
   accumulating in the first place). *)
$HistoryLength = 0;

sizesBits = {64, 128, 256, 512, 1024, 2048, 4096, 8192,
             16384, 32768, 65536, 131072};
trials = 30;
seed = 42;

repsForBits[bits_] := Which[
  bits <= 512, 20000,
  bits <= 4096, 2000,
  bits <= 32768, 200,
  True, 20
];

(* RandomInteger[{2^(bits-1), 2^bits - 1}] draws uniformly from the
   range of integers whose bit length is EXACTLY `bits`, the lower
   bound already has the top bit set, so this gets the same "force
   the top bit on" guarantee the other four harnesses implement by
   hand, for free. *)
randomOfBits[bits_] := RandomInteger[{2^(bits - 1), 2^bits - 1}];

(* Plain ToString on a real number can come out as scientific
   notation ("1.23`*^6") for large magnitudes, or with comma
   thousands-separators, depending on context -- either one silently
   corrupts the CSV for anything reading it as a plain float later
   (pandas, numpy, your C/Python readers). Force fixed-decimal,
   no grouping, no scientific notation, matching the "%.3f" style
   the other four harnesses already write. *)
formatNs[x_] := ToString[
  NumberForm[N[x], {Infinity, 3},
    ExponentFunction -> (Null &),
    DigitBlock -> Infinity]
];

If[Length[$ScriptCommandLine] < 3,
  Print["usage: wolframscript -file benchmark.wls <machine_label> <out.csv>"];
  Exit[1];
];

machine = $ScriptCommandLine[[2]];
outpath = $ScriptCommandLine[[3]];

Print["starting: machine=", machine, ", output=", outpath];

outDir = DirectoryName[outpath];
If[outDir =!= "" && !DirectoryQ[outDir],
  Print["creating missing output directory: ", outDir];
  CreateDirectory[outDir, CreateIntermediateDirectories -> True];
];

SeedRandom[seed];

(* Warmup: this, like Rust, runs a batch of multiplications before any timed trial.
   Mathematica's kernel does some one-time setup (library loading,
   internal caching) on first use of large-integer arithmetic;
   without this, the very first few timed trials could look
   artificially slow for reasons unrelated to the algorithm itself,
   the same concern as Java's JIT warmup, just a smaller effect here. *)
Do[
  a = randomOfBits[2048];
  b = randomOfBits[2048];
  a*b;
  ,
  {50}
];

stream = OpenWrite[outpath];
WriteString[stream, "impl,machine,x,trial,time_ns\n"];

Do[
  reps = repsForBits[bits];
  Do[
    a = randomOfBits[bits];
    b = randomOfBits[bits];

    (* AbsoluteTiming[expr] returns {seconds, result-of-expr}; Do[]
       always returns Null as its own result, so only the timing
       (First[...]) is useful here. `r` inside the loop is still a
       real, required side-effecting assignment the interpreter must
       execute every iteration, unlike a compiled optimizer, WL
       can't skip it just because the value goes unused afterward. *)
    elapsedSeconds = First[AbsoluteTiming[Do[r = a*b, {reps}]]];

    timeNs = (elapsedSeconds * 10^9) / reps;
    WriteString[stream,
      "mathematica," <> machine <> "," <> ToString[bits] <> ","
        <> ToString[trial] <> "," <> formatNs[timeNs] <> "\n"
    ];
    ,
    {trial, 0, trials - 1}
  ];
  Print["sizes: done ", bits, " bits"];
  (* No Flush[] exists for WL streams the way C has fflush, data
     written with WriteString isn't guaranteed on disk until Close[]
     runs at the very end. If this run is long enough that a crash
     partway through would be costly, the safe pattern is closing and
     reopening in append mode after each size: Close[stream];
     stream = OpenAppend[outpath]; Given the whole sweep finishes in
     well under a minute for every other language, that's probably
     not worth the complexity here but it's worth knowing if a run ever
     does hang or get killed partway through. *)
  ,
  {bits, sizesBits}
];

Close[stream];
