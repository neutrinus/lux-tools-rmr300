# Decompile ESP32 display functions for LCD reverse engineering
# Target addresses from report-glm-1836:
#   0x401908B4 - CONFIG peripheral (GPIO # to 0x3FF4E0C4/C8/CC) - PINS + masks
#   0x401915CC - CONFIG/GPIO setup - PINS (second write)
#   0x40190FB4 - RENDER (13-case switch) - segment map
#   0x401913E0 - SERIALIZE - bit/byte order
#   0x40190E38 - WRITE data buffer
#   0x4019153C - INIT - init sequence
#   0x401B3D00 - peripheral/GPIO setup
#   0x401B2D34 - peripheral/GPIO setup

TARGETS = [
    0x401908B4,
    0x401915CC,
    0x40190FB4,
    0x401913E0,
    0x40190E38,
    0x4019153C,
    0x401B3D00,
    0x401B2D34,
]

def decompile_function(func_addr):
    from ghidra.app.decompiler import DecompInterface
    from ghidra.util.task import ConsoleTaskMonitor
    from ghidra.program.model.address import Address
    
    addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(func_addr)
    func = getFunctionAt(addr)
    
    if func is None:
        return None, "NOT_FOUND"
    
    decomp = DecompInterface()
    decomp.openProgram(currentProgram)
    
    monitor = ConsoleTaskMonitor()
    results = decomp.decompileFunction(func, 120, monitor)
    
    if results and results.getDecompiledFunction():
        return results.getDecompiledFunction().getC(), "OK"
    return None, "DECOMP_FAILED"

def find_nearby_functions(addr, range_kb=4):
    """Find functions around a given address range."""
    from ghidra.program.model.address import Address

    space = currentProgram.getAddressFactory().getDefaultAddressSpace()
    start = space.getAddress(addr - range_kb * 1024)
    end = space.getAddress(addr + range_kb * 1024)
    fm = currentProgram.getFunctionManager()
    funcs = []
    it = fm.getFunctions(start, True)
    while True:
        f = it.next()
        if f is None or f.getEntryPoint().compareTo(end) > 0:
            break
        if f.getEntryPoint().compareTo(start) >= 0:
            funcs.append(f)
    return funcs

def main():
    # First run auto-analysis
    print("Running auto-analysis...")
    from ghidra.app.plugin.core.analysis import AutoAnalysisManager
    from ghidra.util.task import ConsoleTaskMonitor
    
    mgr = AutoAnalysisManager.getAnalysisManager(currentProgram)
    mgr.initializeOptions()
    mgr.reAnalyzeAll(None)
    monitor = ConsoleTaskMonitor()
    while not mgr.isAnalyzing():
        import time
        time.sleep(0.1)
    mgr.waitForAnalysisCompletion(monitor)
    print("Auto-analysis complete.")
    
    print("=" * 60)
    print(f"Program: {currentProgram.getName()}")
    print(f"Language: {currentProgram.getLanguage().getLanguageID()}")
    print(f"Functions: {currentProgram.getFunctionManager().getNumFunctions(True)}")
    print(f"Memory blocks:")
    for block in currentProgram.getMemory().getBlocks():
        print(f"  {block.getName():20s} {block.getStart():30s} size=0x{block.getSize():X}")
    print("=" * 60)
    
    for addr in TARGETS:
        print(f"\n{'=' * 60}")
        print(f"=== Function at 0x{addr:08X} ===")
        print(f"{'=' * 60}")
        
        code, status = decompile_function(addr)
        if status == "NOT_FOUND":
            # Search for nearby functions
            nearby = find_nearby_functions(addr, 2)
            if nearby:
                print(f"(no function at 0x{addr:08X}, nearby functions:)")
                for f in nearby:
                    sz = f.getBody().getNumAddresses()
                    print(f"  0x{f.getEntryPoint():08X}  {f.getName():40s} size={sz}")
            else:
                print(f"(no function found at 0x{addr:08X})")
        elif status == "DECOMP_FAILED":
            print(f"(decompilation failed for function at 0x{addr:08X})")
        else:
            print(code)

if __name__ == "__main__":
    main()
