// AOT static recompiler interface functions for AArch64
// These are the statically-compiled equivalents of the dynamically-generated
// interface functions in BackendAArch64.cpp.
//
// Register conventions (same as JIT):
//   x29 (HCPU_REG)               - PPCInterpreter_t* instance pointer
//   x28 (MEM_BASE_REG)           - memory_base pointer
//   x27 (PPC_REC_INSTANCE_REG)   - PPCRecompilerInstanceData_t* pointer
//   x26 (TEMP_GPR2 / LR_REG)     - PPC LR temp / instruction pointer on leave
//   x25 (TEMP_GPR1)              - general temp
//
// enterRecompilerCode(uint64 codeMem, uint64 ppcInterpreterInstance):
//   x0 = pointer to native function to call
//   x1 = pointer to PPCInterpreter_t instance
//
// leaveRecompilerCode:
//   Stores LR_REG (w26) into hCPU->instructionPointer and returns via ret

.text
.align 4

// ============================================================
// PPCRecompilerAOT_enterRecompilerCode
// Called from C++: void enterRecompilerCode(uint64 codeMem, uint64 ppcInterpreterInstance)
// ============================================================
.globl _PPCRecompilerAOT_enterRecompilerCode
_PPCRecompilerAOT_enterRecompilerCode:
    // Save callee-saved registers (x19-x30, d8-d15)
    // Stack: 6 GPR pairs * 16 + 4 FPR pairs * 16 = 160 bytes
    sub     sp, sp, #160
    mov     x9, sp

    stp     x19, x20, [x9], #16
    stp     x21, x22, [x9], #16
    stp     x23, x24, [x9], #16
    stp     x25, x26, [x9], #16
    stp     x27, x28, [x9], #16
    stp     x29, x30, [x9], #16
    stp     d8,  d9,  [x9], #16
    stp     d10, d11, [x9], #16
    stp     d12, d13, [x9], #16
    stp     d14, d15, [x9], #16

    // Set up dedicated registers
    mov     x29, x1                         // HCPU_REG = ppcInterpreterInstance

    // Load ppcRecompilerInstanceData pointer
    adrp    x27, _ppcRecompilerInstanceData@GOTPAGE
    ldr     x27, [x27, _ppcRecompilerInstanceData@GOTPAGEOFF]
    ldr     x27, [x27]                      // x27 = *ppcRecompilerInstanceData

    // Load memory_base pointer
    adrp    x28, _memory_base@GOTPAGE
    ldr     x28, [x28, _memory_base@GOTPAGEOFF]
    ldr     x28, [x28]                      // x28 = *memory_base

    // Branch to the recompiled function
    blr     x0

    // Restore callee-saved registers
    mov     x9, sp
    ldp     x19, x20, [x9], #16
    ldp     x21, x22, [x9], #16
    ldp     x23, x24, [x9], #16
    ldp     x25, x26, [x9], #16
    ldp     x27, x28, [x9], #16
    ldp     x29, x30, [x9], #16
    ldp     d8,  d9,  [x9], #16
    ldp     d10, d11, [x9], #16
    ldp     d12, d13, [x9], #16
    ldp     d14, d15, [x9], #16

    add     sp, sp, #160
    ret


// ============================================================
// PPCRecompilerAOT_leaveRecompilerCode_unvisited
// Called when recompiled code branches to an unvisited address.
// Stores the current PPC IP (in w26) back to hCPU and returns to the caller.
// ============================================================
.globl _PPCRecompilerAOT_leaveRecompilerCode_unvisited
_PPCRecompilerAOT_leaveRecompilerCode_unvisited:
    // w26 contains the PPC instruction pointer
    // x29 = HCPU_REG, instructionPointer is at offset 0
    str     w26, [x29, #0]
    ret


// ============================================================
// PPCRecompilerAOT_leaveRecompilerCode_visited
// Same as unvisited — the function has been queued for compilation
// but isn't ready yet. Store IP and return.
// ============================================================
.globl _PPCRecompilerAOT_leaveRecompilerCode_visited
_PPCRecompilerAOT_leaveRecompilerCode_visited:
    str     w26, [x29, #0]
    ret
