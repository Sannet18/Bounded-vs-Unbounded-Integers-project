/* clock_estimate -- estimates the CPU's effective clock (GHz) under load.
 * A chain of dependent 64-bit multiplies has a fixed 3-cycle latency on both
 * Intel and AMD, so (multiplies/second) x 3 = clock rate. (Dependent adds do
 * not work: modern cores fold add-immediate chains and read 5x too fast.) WSL reports a fixed "cpu MHz" that says nothing
 * about boost, so this measures it. Prints the best of 5 half-second runs. */
#include <stdio.h>
#include <stdint.h>
#include <time.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(void){
    double best=0;
    for(int run=0;run<5;run++){
        uint64_t x=3, iters=0; double t0=now(), t;
        do{
            for(int i=0;i<1000000;i++){
                __asm__ volatile("imul %0,%0\n imul %0,%0\n imul %0,%0\n imul %0,%0\n"
                                 "imul %0,%0\n imul %0,%0\n imul %0,%0\n imul %0,%0" : "+r"(x));
            }
            iters+=1000000; t=now();
        }while(t-t0<0.5);
        double ghz=(double)iters*8*3/(t-t0)/1e9; if(ghz>best)best=ghz;
    }
    printf("%.2f\n",best); return 0;
}
