// Memory footprint of java.math.BigInteger as a function of its bit length.
// Same CSV columns as the other memory tools:
//     impl,bits,bytes,payload_bytes,allocator_bytes
//
// How it measures: a BigInteger is TWO objects, the BigInteger itself (a
// header plus a handful of int fields) and a separate int[] "mag" holding the
// magnitude. Instrumentation.getObjectSize() asks the JVM for the real size
// of each, including headers and alignment padding, so
//     bytes         = size(BigInteger) + size(mag)
//     payload_bytes = 4 * mag.length          (just the digits)
// mag is private, so it is read by reflection; that needs the JVM flag
//     --add-opens java.base/java.math=ALL-UNNAMED
//
// The Instrumentation object is only available to a Java agent, so this one
// class is both the agent (premain) and the program (main).
//
// Build and run (from the java/ directory):
//   javac MemBench.java
//   printf 'Premain-Class: MemBench\nMain-Class: MemBench\n' > manifest.txt
//   jar cfm membench.jar manifest.txt MemBench.class
//   java -javaagent:membench.jar --add-opens java.base/java.math=ALL-UNNAMED \
//        -cp membench.jar MemBench ../data/memory/memory_java.csv
//
// The numbers assume the default 64-bit HotSpot configuration (compressed
// class pointers, 8-byte object alignment). The JVM arguments are recorded in
// the CSV header so the result can be reproduced.

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.lang.instrument.Instrumentation;
import java.lang.management.ManagementFactory;
import java.lang.reflect.Field;
import java.math.BigInteger;

public class MemBench {

    private static Instrumentation inst;

    public static void premain(String args, Instrumentation i) {
        inst = i;
    }

    static int[] bitList() {
        java.util.ArrayList<Integer> l = new java.util.ArrayList<>();
        for (int b = 1; b <= 320; b++) {
            l.add(b);
        }
        int[] ladder = {384, 448, 512, 640, 768, 1024, 1536, 2048, 3072, 4096, 6144,
            8192, 12288, 16384, 32768, 65536, 131072, 262144, 524288, 1048576};
        for (int b : ladder) {
            l.add(b);
        }
        return l.stream().mapToInt(Integer::intValue).toArray();
    }

    public static void main(String[] argv) throws Exception {
        if (inst == null) {
            System.err.println("This program must be run with -javaagent:membench.jar "
                    + "(see the comment at the top of MemBench.java).");
            System.exit(1);
        }
        if (argv.length != 1) {
            System.err.println("usage: java ... MemBench <out.csv>");
            System.exit(1);
        }

        Field magField;
        try {
            magField = BigInteger.class.getDeclaredField("mag");
            magField.setAccessible(true);
        } catch (RuntimeException e) {
            System.err.println("Cannot read BigInteger.mag. Add: "
                    + "--add-opens java.base/java.math=ALL-UNNAMED");
            throw e;
        }

        File out = new File(argv[0]);
        if (out.getParentFile() != null) {
            out.getParentFile().mkdirs();
        }

        try (PrintWriter csv = new PrintWriter(new FileWriter(out))) {
            csv.println("# Java " + System.getProperty("java.version") + " ("
                    + System.getProperty("java.vm.name") + "), JVM args: "
                    + ManagementFactory.getRuntimeMXBean().getInputArguments());
            csv.println("impl,bits,bytes,payload_bytes,allocator_bytes");

            // the bounded case: a primitive long is 8 bytes whatever its value
            for (int b = 1; b <= 63; b++) {
                csv.printf("java_long,%d,8,8,%n", b);
            }

            for (int bits : bitList()) {
                BigInteger x = BigInteger.ONE.shiftLeft(bits).subtract(BigInteger.ONE);
                if (x.bitLength() != bits) {
                    throw new IllegalStateException("bit length " + bits);
                }
                int[] mag = (int[]) magField.get(x);
                long bytes = inst.getObjectSize(x) + inst.getObjectSize(mag);
                long payload = 4L * mag.length;
                csv.printf("java,%d,%d,%d,%n", bits, bytes, payload);
            }
        }
        System.err.println("wrote " + out);
    }
}
