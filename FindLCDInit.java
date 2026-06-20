import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;

public class FindLCDInit extends GhidraScript {
    @Override
    protected void run() throws Exception {
        long[] targets = {0x3ff4e000L, 0x3ff4e0c4L, 0x3ff4e130L, 0x3ff4e140L};
        for (long addrVal : targets) {
            var addr = currentProgram.getAddressFactory().getAddress("ram:" + Long.toHexString(addrVal));
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(addr);
            int n = 0;
            for (var ref : refs) n++;
            println("Xrefs to 0x" + Long.toHexString(addrVal) + ": " + n);
            refs = currentProgram.getReferenceManager().getReferencesTo(addr);
            for (var ref : refs) {
                var func = getFunctionContaining(ref.getFromAddress());
                String fname = func != null ? func.getName() : "?";
                println("  from " + ref.getFromAddress() + " (" + fname + ")");
            }
        }
        println("\n=== Strings ===");
        var listing = currentProgram.getListing();
        var dataIt = listing.getDefinedData(true);
        while (dataIt.hasNext()) {
            var dd = dataIt.next();
            if (dd.isDefined() && dd.hasStringValue()) {
                String val = dd.getDefaultValueRepresentation().toLowerCase();
                if (val.contains("lcd") || val.contains("rw_display") || val.contains("tube") || val.contains("display") || val.contains("segment")) {
                    println(dd.getAddress() + ": " + dd.getDefaultValueRepresentation());
                }
            }
        }
    }
}
