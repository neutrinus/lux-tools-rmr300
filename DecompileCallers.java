// Decompile the main display callers to find GPIO numbers
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;

public class DecompileCallers extends GhidraScript {
    long[] targets = {
        0x40191490L,  // Main display update (calls CONFIG + SERIALIZE)
        0x40191790L,  // Another display update (calls CONFIG + SERIALIZE)
        0x4019447cL,  // Display task (calls INIT)
        0x40194110L,  // References DAT_40190520
        0x40194294L,  // References DAT_40190520
    };

    @Override
    protected void run() throws Exception {
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        for (long addrVal : targets) {
            println("\n============================================================");
            println("=== Function at 0x" + Long.toHexString(addrVal) + " ===");
            println("============================================================");

            var addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(addrVal);
            var func = getFunctionAt(addr);

            if (func == null) {
                println("(no function found)");
                continue;
            }

            println("Name: " + func.getName());
            
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
