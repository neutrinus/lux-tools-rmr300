// Decompile ESP32 display functions
// Target addresses:
//   0x401908B4 - CONFIG peripheral (GPIO # to 0x3FF4E0C4/C8/CC)
//   0x401915CC - CONFIG/GPIO setup
//   0x40190FB4 - RENDER (13-case switch)
//   0x401913E0 - SERIALIZE
//   0x40190E38 - WRITE data buffer
//   0x4019153C - INIT

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;

public class DecompileDisplay extends GhidraScript {
    long[] targets = {
        0x401908B4L, 0x401915CCL, 0x40190FB4L, 0x401913E0L,
        0x40190E38L, 0x4019153CL, 0x401B3D00L, 0x401B2D34L
    };

    @Override
    protected void run() throws Exception {
        println("============================================================");
        println("Program: " + currentProgram.getName());
        println("Language: " + currentProgram.getLanguage().getLanguageID());
        int fc = 0;
        var fit = currentProgram.getFunctionManager().getFunctions(true);
        while (fit.hasNext()) { fit.next(); fc++; }
        println("Functions: " + fc);
        println("============================================================");

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        for (long addrVal : targets) {
            println("\n============================================================");
            println("=== Function at 0x" + Long.toHexString(addrVal) + " ===");
            println("============================================================");

            var addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(addrVal);
            var func = getFunctionAt(addr);

            if (func == null) {
                println("(no function found at 0x" + Long.toHexString(addrVal) + ")");
                // Show nearby functions
                var fm = currentProgram.getFunctionManager();
                var start = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(addrVal - 0x2000);
                var end = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(addrVal + 0x2000);
                var it = fm.getFunctions(start, true);
                while (it.hasNext()) {
                    var f = it.next();
                    if (f.getEntryPoint().compareTo(end) > 0) break;
                    if (f.getEntryPoint().compareTo(start) >= 0) {
                        println("  near: 0x" + f.getEntryPoint() + " " + f.getName());
                    }
                }
                continue;
            }

            println("Name: " + func.getName());
            println("Body: " + func.getBody());

            var res = decomp.decompileFunction(func, 120, monitor);
            if (res != null && res.getDecompiledFunction() != null) {
                println(res.getDecompiledFunction().getC());
            } else {
                println("(decompilation failed)");
            }
        }

        decomp.dispose();
    }
}
