#!/usr/bin/env wolframscript
(* 
   Memory footprint of a Mathematica Integer as a function of its bit length,
   using ByteCount. Same CSV columns as the other memory tools:
       impl,bits,bytes,payload_bytes,allocator_bytes

   ByteCount[x] is the number of bytes Mathematica reports the expression
   occupies. payload_bytes and allocator_bytes are left blank: the kernel does
   not expose how many digits are in use or the allocator's block size.

   The number measured at each bit length b is 2^b - 1, so its bit length is
   exactly b. The same bit lengths are used by every language's memory tool.

   To run:
     wolframscript -file memory.wls <out.csv>
*)

$HistoryLength = 0;

If[Length[$ScriptCommandLine] < 2,
  Print["usage: wolframscript -file memory.wls <out.csv>"];
  Exit[1];
];

outpath = $ScriptCommandLine[[2]];
outDir = DirectoryName[outpath];
If[outDir =!= "" && !DirectoryQ[outDir],
  CreateDirectory[outDir, CreateIntermediateDirectories -> True];
];

bitList = Join[Range[1, 320],
  {384, 448, 512, 640, 768, 1024, 1536, 2048, 3072, 4096, 6144, 8192,
   12288, 16384, 32768, 65536, 131072, 262144, 524288, 1048576}];

stream = OpenWrite[outpath];
WriteString[stream, "# Mathematica " <> ToString[$Version] <> ", ByteCount of 2^bits - 1\n"];
WriteString[stream, "impl,bits,bytes,payload_bytes,allocator_bytes\n"];

Do[
  x = 2^bits - 1;
  If[BitLength[x] =!= bits,
    Print["bit length mismatch at ", bits];
    Exit[2];
  ];
  WriteString[stream,
    "mathematica," <> ToString[bits] <> "," <> ToString[ByteCount[x]] <> ",,\n"];
  ,
  {bits, bitList}
];

Close[stream];
Print["wrote ", outpath];
