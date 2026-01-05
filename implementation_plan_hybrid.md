# Implementation Plan: Full Support for Dynamic Code (Hybrid Architecture)

## 1. Analysis

### The Problem
The `cpu_instrs.gb` ROM (and many others) fails because it copies code to RAM (`$C000`+) and jumps to it (`JP $C000`).
The current Static Recompiler only processes instructions found in ROM at compile time. It has no knowledge of code generated or copied to RAM at runtime.

### The Reddit Insight
The Reddit discussion highlights two key approaches:
1.  **Trace Analysis / Heuristics**: Doing a "dry run" or deeper search to find all code paths. This helps with `JP HL` in ROM but **fails** for dynamic RAM code because the code itself doesn't exist until runtime.
2.  **Interpreter Fallback**: "run and trace the code beforehand" or have a "full interpreter".

### The Solution: Hybrid Static/Dynamic Recompilation
To support real-world GameBoy ROMs, we cannot rely solely on static recompilation. We must implement a "Hybrid" architecture:
1.  **Static**: Compiled C code for all reachable ROM instructions (Fast, Efficient).
2.  **Dynamic**: An Interpreter embedded in the Runtime (`libgbrt`) to handle RAM execution and unknown ROM paths (Robust, Compatible).

This ensures checking `cpu_instrs.gb` works: the test runner (in RAM) will run via Interpreter, while the library functions it calls (if in ROM) could potentially run natively. Even if the entire test runs in the Interpreter, it validates the ALU logic shared by both systems.

## 2. Implementation Plan

### Phase 1: Runtime Decoder & Interpreter
We need to transplant the "know-how" of decoding instructions from the `recompiler` (C++ build tool) to the `runtime` (C library).

1.  **Create `runtime/src/interpreter.c`**:
    *   Implement a `gb_step(GBContext* ctx)` function.
    *   This function reads `ctx->rom[ctx->pc]` (or RAM), decodes the opcode, and executes it.
2.  **Reuse ALU Logic**:
    *   The interpreter MUST use the exact same `gb_add8`, `gb_sub8` helpers in `gbrt.c` that the recompiled code uses.
    *   This guarantees that if the interpreter passes `cpu_instrs`, the recompiled code's logic is also correct.
3.  **Control Flow Handling**:
    *   `CALL`: Push PC, Jump to target.
    *   `RET`: Pop PC.
    *   **Context Switching**: If the interpreter jumps to an address that *is* statically compiled, we should ideally switch back to compiled code.

### Phase 2: Dispatcher Upgrade
Modify `gbrecomp` to generate a "Smart Dispatcher".

**Current Dispatcher (`cpu_instrs_rom.c`):**
```c
void gb_dispatch(GBContext* ctx, uint16_t addr) {
    switch(addr) {
        case 0x0100: func_0100(ctx); break;
        // ...
        default: printf("Error: Unknown address"); exit(1);
    }
}
```

**New "Smart" Dispatcher:**
```c
void gb_dispatch(GBContext* ctx, uint16_t addr) {
    // 1. Try to find compiled function (Fast Path)
    switch(addr) {
        case 0x0100: func_0100(ctx); return;
        // ...
    }
    
    // 2. Fallback to Interpreter (Slow Path)
    // Run one instruction (or a block) then check dispatch again
    ctx->pc = addr;
    gb_step(ctx); // Executes one instruction at pc, updates pc
    
    // In a real loop, the main loop handles this:
    // while(1) { gb_dispatch(ctx, ctx->pc); }
}
```

### Phase 3: Main Loop Integration
The generated `main` function needs to support this hybrid loop.

```c
void cpu_instrs_run(GBContext* ctx) {
    while (1) {
        // Try to execute compiled block at PC
        if (gb_has_compiled_block(ctx->pc)) {
            gb_dispatch(ctx, ctx->pc);
        } else {
            // No compiled code? Interpret!
            gb_step(ctx);
        }
        
        // Handle interrupts, timing, etc.
    }
}
```

## 3. Work Items

1.  [x] **Runtime**: Extract instruction logic from `recompiler` and implement `gb_step` in `runtime`. ✅ DONE (`runtime/src/interpreter.c`)
2.  [x] **Runtime**: Implement a simple table-based decoder in C for `gb_step`. ✅ DONE (switch-based decoder in interpreter.c)
3.  [x] **Recompiler**: Update `CEmitter` to generate the new Main Loop and Dispatcher logic. ✅ DONE (dispatcher with interpreter fallback)
4.  [x] **Testing**: Verify `cpu_instrs.gb` passes (using the interpreter for RAM parts). ✅ **PARTIAL** - Test 01 (special) passes! Full suite in progress.

## 4. Implementation Notes (January 5, 2026)

### Key Fix Applied
The interpreter in `runtime/src/interpreter.c` was incorrectly returning immediately when `pc < 0x8000` (ROM area). This assumed all ROM code would be statically compiled, but `cpu_instrs.gb` uses callback tables and other patterns that the static analyzer doesn't discover.

**Solution**: The interpreter now runs ALL uncompiled code, including ROM addresses missed by static analysis. This allows `cpu_instrs.gb` to work correctly:
- Static recompiler handles functions it discovers
- Interpreter handles everything else (RAM code AND undiscovered ROM paths)

### Test Results
```
01-special.gb -> Passed ✅
cpu_instrs.gb -> 01:ok (Test 01 passed, other tests in progress)
```
