Use this code on other models to find the resolution value
<!-- python3 -c "import time; print(time.get_clock_info('perf_counter'))" -->

<!-- cat > /tmp/check_res.c << 'EOF'
#include <stdio.h>
#include <time.h>
int main(void) {
    struct timespec ts;
    clock_getres(CLOCK_MONOTONIC, &ts);
    printf("CLOCK_MONOTONIC resolution: %ld sec, %ld ns\n", ts.tv_sec, ts.tv_nsec);
    return 0;
}
EOF
gcc -o /tmp/check_res /tmp/check_res.c
/tmp/check_res -->



AMD Machine

CPU(s):                                  32
Model name:                              AMD Ryzen 9 8940HX with Radeon Graphics
Thread(s) per core:                      2
Core(s) per socket:                      16
Socket(s):                               1
PRETTY_NAME="Ubuntu 26.04.1 LTS"
<!-- namespace(implementation='clock_gettime(CLOCK_MONOTONIC)', monotonic=True, adjustable=False, resolution=1e-09) -->

<!-- CLOCK_MONOTONIC resolution: 0 sec, 1 ns -->


<!-- With Java, I used JDK 25, and not 17/21 -->
<!-- curl -s https://raw.githubusercontent.com/openjdk/jdk/master/src/java.base/share/classes/java/math/BigInteger.java | grep -A1 "KARATSUBA_THRESHOLD\|TOOM_COOK_THRESHOLD" -->
<!-- Used this to check the thresholds -->



INTEL machine

gcc version: gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
Copyright (C) 2025 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

CPU(s):                                  12
Model name:                              Intel(R) Core(TM) i5-10500H CPU @ 2.50GHz
Thread(s) per core:                      2
Core(s) per socket:                      6
L1d cache:                               192 KiB (6 instances)
L2 cache:                                1.5 MiB (6 instances)
L3 cache:                                12 MiB (1 instance)

<!-- +-- Desired=Unknown/Install/Remove/Purge/Hold
|+- Status=Not/Inst/Conf-files/Unpacked/halF-conf/Half-inst/trig-aWait/Trig-pend
||+ Err?=(none)/Reinst-required (Status,Err: uppercase=bad)
||| Name             Version               Architecture Description
+++-================-=====================-============-==================================================
ii  libgmp-dev:amd64 2:6.3.0+dfsg-5ubuntu2 amd64        Multiprecision arithmetic library developers tools -->