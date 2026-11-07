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

AMD 
<!-- namespace(implementation='clock_gettime(CLOCK_MONOTONIC)', monotonic=True, adjustable=False, resolution=1e-09) -->

<!-- CLOCK_MONOTONIC resolution: 0 sec, 1 ns -->


<!-- With Java, I used JDK 25, and not 17/21 -->
<!-- curl -s https://raw.githubusercontent.com/openjdk/jdk/master/src/java.base/share/classes/java/math/BigInteger.java | grep -A1 "KARATSUBA_THRESHOLD\|TOOM_COOK_THRESHOLD" -->
<!-- Used this to check the thresholds -->