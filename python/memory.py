"""
Writes a CSV with the same columns as the other memory tools:
    impl,bits,bytes,payload_bytes,allocator_bytes

For an int, sys.getsizeof(x) is the FULL size of the object: CPython ints are
variable-sized objects with the digit array stored inline after a fixed header,
so getsizeof already counts the digits. (It does not include pymalloc's
rounding of each object up to a 16-byte multiple; that rounding is left out
here and noted in the analysis.) payload_bytes and allocator_bytes are left
blank: CPython does not expose the number of digits in use or the allocator's
real block size directly.

The number measured at each bit length b is 2^b - 1, so its bit length is
exactly b. The same bit lengths are used by every language's memory tool.

To run:  python3 memory.py <out.csv>
../data/memory/memory_cpython.csv
"""

import os
import sys


def bit_list():
    bits = list(range(1, 321))
    bits += [384, 448, 512, 640, 768, 1024, 1536, 2048, 3072, 4096, 6144, 8192,
             12288, 16384, 32768, 65536, 131072, 262144, 524288, 1048576]
    return bits


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <out.csv>", file=sys.stderr)
        sys.exit(1)
    out = sys.argv[1]
    parent = os.path.dirname(out)
    if parent:
        os.makedirs(parent, exist_ok=True)

    with open(out, "w") as f:
        f.write(f"# CPython {sys.version.split()[0]}, "
                f"digit size {sys.int_info.bits_per_digit} bits "
                f"({sys.int_info.sizeof_digit} bytes), "
                f"sys.getsizeof(0)={sys.getsizeof(0)}, sys.getsizeof(1)={sys.getsizeof(1)}\n")
        f.write("impl,bits,bytes,payload_bytes,allocator_bytes\n")
        for b in bit_list():
            x = (1 << b) - 1
            assert x.bit_length() == b
            f.write(f"cpython,{b},{sys.getsizeof(x)},,\n")
    print(f"wrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()