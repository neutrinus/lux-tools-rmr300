// Find GPIO numbers as immediate values in firmware
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.MemoryAccessException;

public class FindGPIORefs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        var space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        var mem = currentProgram.getMemory();
        
        // Focus on IROM where display code lives
        var blocks = mem.getBlocks();
        println("=== Memory Blocks ===");
        for (var block : blocks) {
            println(String.format("  %-20s 0x%08x size=0x%x (%s)", 
                block.getName(), block.getStart().getOffset(), block.getSize(),
                block.isExecute() ? "CODE" : "DATA"));
        }
        
        // Search for specific GPIO numbers as 32-bit LE values in IROM/IRAM
        int[] gpios = {5, 18, 25, 32, 33, 34, 12, 10, 15, 21, 22, 23, 27};
        int[] ranges = {
            0x400d0020, 0x401b5000,  // IROM
            0x40080000, 0x4009c500,  // IRAM0 + IRAM1
        };
        
        println("\n=== Searching for GPIO numbers in code ===");
        for (int r = 0; r < ranges.length; r += 2) {
            long start = ranges[r];
            long end = ranges[r+1];
            for (int gpio : gpios) {
                // Search as 32-bit LE: GPIO, 0x00, 0x00, 0x00
                byte[] pattern = {
                    (byte)(gpio & 0xFF),
                    (byte)((gpio >> 8) & 0xFF),
                    0, 0
                };
                String hex = String.format("%02X %02X %02X %02X", 
                    pattern[0], pattern[1], pattern[2], pattern[3]);
                
                var addr = space.getAddress(start);
                while (addr != null && addr.getOffset() < end) {
                    // Search for GPIO as individual byte in code (immediate in instruction)
                    if ((addr.getOffset() & 0xFF) == 0) {
                        // 4-byte aligned - potential L32R target or 32-bit constant
                        try {
                            int val = mem.getInt(addr);
                            if (val == gpio) {
                                var func = getFunctionContaining(addr);
                                if (func != null) {
                                    println(String.format("  GPIO%d (32-bit) at 0x%08x in %s", 
                                        gpio, addr.getOffset(), func.getName()));
                                }
                            }
                        } catch (MemoryAccessException e) {
                            // skip
                        }
                    }
                    addr = addr.add(4);
                }
            }
        }
        
        // Also check DROM for struct configs
        println("\n=== Searching DROM for GPIO config structures ===");
        long dromStart = 0x3f400020L;
        long dromEnd = 0x3f4262d4L;
        
        for (int gpio : gpios) {
            var addr = space.getAddress(dromStart);
            while (addr != null && addr.getOffset() < dromEnd) {
                try {
                    int val = mem.getInt(addr);
                    if (val == gpio) {
                        println(String.format("  GPIO%d at DROM 0x%08x", gpio, addr.getOffset()));
                    }
                } catch (MemoryAccessException e) {
                    break;
                }
                addr = addr.add(4);
            }
        }
    }
}
