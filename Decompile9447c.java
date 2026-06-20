import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
public class Decompile9447c extends GhidraScript {
    long[] targets = {0x4019447cL, 0x40194294L, 0x40190fb4L, 0x40190c2cL};
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
            var res = decomp.decompileFunction(func, 240, monitor);
            if (res != null && res.getDecompiledFunction() != null) {
                println(res.getDecompiledFunction().getC());
            } else { println("decomp failed\n"); }
        }
        decomp.dispose();
    }
}
