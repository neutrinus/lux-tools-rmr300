// Find cross-references to display functions
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;

public class FindDisplayPins extends GhidraScript {
    @Override
    protected void run() throws Exception {
        println("=== Cross-references to FUN_401908b4 (CONFIG peripheral) ===");
        printXrefsTo(0x401908b4L);
        
        println("\n=== Cross-references to FUN_401913e0 (SERIALIZE) ===");
        printXrefsTo(0x401913e0L);
        
        println("\n=== Cross-references to FUN_40190e38 (WRITE data) ===");
        printXrefsTo(0x40190e38L);
        
        println("\n=== Cross-references to FUN_4019153c (INIT) ===");
        printXrefsTo(0x4019153cL);
        
        println("\n=== Cross-references to FUN_401915cc (CONFIG/GPIO setup) ===");
        printXrefsTo(0x401915ccL);
        
        println("\n=== Lookup DAT references near 0x40190520 (0x3FF4E0C4 base) ===");
        findDataRefsAround(0x40190520L, 64);
    }
    
    void printXrefsTo(long addrVal) throws Exception {
        var addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(addrVal);
        var func = getFunctionAt(addr);
        if (func != null) {
            println("  Target: " + func.getName() + " at 0x" + addr + " size=0x" + func.getBody().getNumAddresses());
        }
        
        var refs = currentProgram.getReferenceManager().getReferencesTo(addr);
        int count = 0;
        for (var ref : refs) {
            if (count >= 15) {
                println("  ... and more");
                break;
            }
            var fromAddr = ref.getFromAddress();
            var fromFunc = getFunctionContaining(fromAddr);
            println("  caller: 0x" + fromAddr + " in " + 
                (fromFunc != null ? fromFunc.getName() : "no-func"));
            count++;
        }
        if (count == 0) {
            println("  (no xrefs)");
        }
    }
    
    void findDataRefsAround(long addrVal, int range) throws Exception {
        var space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        var start = space.getAddress(addrVal - range);
        var end = space.getAddress(addrVal + range);
        
        var refs = currentProgram.getReferenceManager().getReferencesTo(
            space.getAddress(addrVal));
        println("  References to DAT@" + Long.toHexString(addrVal) + ":");
        for (var ref : refs) {
            var fromAddr = ref.getFromAddress();
            var fromFunc = getFunctionContaining(fromAddr);
            println("    from 0x" + fromAddr + " in " + 
                (fromFunc != null ? fromFunc.getName() : "no-func"));
        }
    }
}
