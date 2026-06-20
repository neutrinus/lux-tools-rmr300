import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
public class Decompile90634 extends GhidraScript {
    long[] targets = {0x40190634L, 0x401906acL, 0x401907b4L, 0x40191790L};
    @Override
    protected void run() throws Exception {
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        for (long addrVal : targets) {
            println("=== 0x" + Long.toHexString(addrVal) + " ===");
            var addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(addrVal);
            var func = getFunctionAt(addr);
            if (func == null) { println("not found\n"); continue; }
            println("Name: " + func.getName() + " size=" + func.getBody().getNumAddresses());
            var res = decomp.decompileFunction(func, 120, monitor);
            if (res != null && res.getDecompiledFunction() != null) {
                println(res.getDecompiledFunction().getC());
            } else { println("decomp failed\n"); }
        }
        decomp.dispose();
    }
}
