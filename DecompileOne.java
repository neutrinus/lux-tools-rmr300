import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
public class DecompileOne extends GhidraScript {
    long[] targets = {0x4019052cL, 0x401908b4L, 0x40191790L, 0x40191490L};
    @Override
    protected void run() throws Exception {
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        for (long addrVal : targets) {
            println("\n=== 0x" + Long.toHexString(addrVal) + " ===");
            var addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(addrVal);
            var func = getFunctionAt(addr);
            if (func == null) { println("not found"); continue; }
            println("Name: " + func.getName() + " size=" + func.getBody().getNumAddresses());
            var res = decomp.decompileFunction(func, 120, monitor);
            if (res != null && res.getDecompiledFunction() != null) {
                String c = res.getDecompiledFunction().getC();
                // Print only first 50 lines
                String[] lines = c.split("\n");
                for (int i = 0; i < Math.min(lines.length, 60); i++) println(lines[i]);
                if (lines.length > 60) println("... (" + (lines.length - 60) + " more lines)");
            } else { println("decomp failed"); }
        }
        decomp.dispose();
    }
}
