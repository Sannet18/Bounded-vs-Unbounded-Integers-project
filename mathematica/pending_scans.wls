#!/usr/bin/env wolframscript
(* pending_scans.wls -- the two Mathematica checks the paper still needs.
   Usage: wolframscript -file pending_scans.wls
   Prints plain text; run_pending.sh captures it (or paste it back by hand). *)
$HistoryLength = 0;
SeedRandom[42];
rnd[bits_] := RandomInteger[{2^(bits - 1), 2^bits - 1}];
perOp[bits_, reps_] := Median[Table[
   a = rnd[bits]; b = rnd[bits];
   First[AbsoluteTiming[Do[r = a*b, {reps}]]]*10^9/reps, {15}]];

Print["Mathematica ", $Version];
Print["--- SCAN 1: overhead change point, bit lengths 262144..524288 step 16384 ---"];
Print["bits, median_ns_per_multiply, ByteCount_of_operand"];
Do[Print[bits, ", ", NumberForm[perOp[bits, 20], {12, 1}], ", ", ByteCount[rnd[bits]]],
  {bits, 262144, 524288, 16384}];

Print["--- SCAN 2: promotion boundary, bit lengths 60..66 ---"];
Print["bits, MachineIntegerQ, ByteCount, median_ns_per_multiply"];
Do[x = rnd[bits];
  Print[bits, ", ", Developer`MachineIntegerQ[x], ", ", ByteCount[x], ", ",
    NumberForm[perOp[bits, 20000], {10, 1}]], {bits, 60, 66}];
Print["(Mathematica machine integers are signed 64-bit, so the boundary is expected at 63/64 bits.)"];
