# Same flags on every machine: a compiler-flag mismatch would quietly
# confound the AMD-vs-Intel comparison before any data is collected.
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
LDLIBS  := -lgmp -ldl
CORE    := bn.c multiply_schoolbook.c multiply_karatsuba.c bigint.c
HDRS    := bn.h bigint.h

.PHONY: all check bmi2 asm-check clean
all: benchmark test_bn test_correctness test_correctness_karatsuba

benchmark: $(CORE) benchmark.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) benchmark.c $(LDLIBS)

# Identical build except the compiler may emit BMI2's mulx.
benchmark_bmi2: $(CORE) benchmark.c $(HDRS)
	$(CC) $(CFLAGS) -mbmi2 -o $@ $(CORE) benchmark.c $(LDLIBS)

test_bn: $(CORE) test_bn.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) test_bn.c $(LDLIBS)
test_correctness: $(CORE) test_correctness.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) test_correctness.c $(LDLIBS)
test_correctness_karatsuba: $(CORE) test_correctness_karatsuba.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) test_correctness_karatsuba.c $(LDLIBS)

# Run every correctness test. Do this before benchmarking anything.
check: test_bn test_correctness test_correctness_karatsuba
	./test_bn && ./test_correctness && ./test_correctness_karatsuba

bmi2: benchmark_bmi2

# Evidence for the hardware section: does the compiler use mulx when allowed?
asm-check: benchmark benchmark_bmi2
	@echo "mul  instructions, default build : $$(objdump -d benchmark      | grep -cE '\smul[q]?\s')"
	@echo "mulx instructions, default build : $$(objdump -d benchmark      | grep -c  '\smulx\s')"
	@echo "mul  instructions, -mbmi2 build  : $$(objdump -d benchmark_bmi2 | grep -cE '\smul[q]?\s')"
	@echo "mulx instructions, -mbmi2 build  : $$(objdump -d benchmark_bmi2 | grep -c  '\smulx\s')"

clean:
	rm -f benchmark benchmark_bmi2 test_bn test_correctness test_correctness_karatsuba