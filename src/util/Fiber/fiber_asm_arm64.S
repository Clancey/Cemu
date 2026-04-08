// Minimal fiber context switch for ARM64 (visionOS)
// Two functions: _fiber_make_context and _fiber_switch_context

.text
.align 4

// void* fiber_make_context(void* stackTop, void (*entryFunc)(void*))
// Sets up a new fiber context on the given stack.
// Returns a context pointer (saved SP) that can be passed to fiber_switch_context.
.globl _fiber_make_context
_fiber_make_context:
    // x0 = stack top (highest address, 16-byte aligned)
    // x1 = entry function pointer

    // Reserve space for saved registers on the new stack
    // We save: x19-x30 (12 regs) + d8-d15 (8 regs) = 20 * 8 = 160 bytes
    sub x0, x0, #160

    // Store entry point as the saved LR (x30) — when we "return" to this context,
    // it will jump to the entry function
    str x1, [x0, #88]   // offset of x30 in our save area

    // Store zero for frame pointer (x29) to terminate backtraces
    str xzr, [x0, #80]  // offset of x29

    // Return the context pointer (= saved SP on new stack)
    ret

// void fiber_switch_context(void** fromCtx, void* toCtx)
// Saves current context to *fromCtx, restores context from toCtx.
.globl _fiber_switch_context
_fiber_switch_context:
    // x0 = pointer to save current context (void**)
    // x1 = context to switch to (void*)

    // Save callee-saved registers to current stack
    sub sp, sp, #160
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    stp d8,  d9,  [sp, #96]
    stp d10, d11, [sp, #112]
    stp d12, d13, [sp, #128]
    stp d14, d15, [sp, #144]

    // Save current SP as the context
    mov x2, sp
    str x2, [x0]

    // Restore target context
    mov sp, x1
    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    ldp d8,  d9,  [sp, #96]
    ldp d10, d11, [sp, #112]
    ldp d12, d13, [sp, #128]
    ldp d14, d15, [sp, #144]
    add sp, sp, #160

    // "Return" to the target — x30 (LR) was restored, so ret jumps there
    ret
