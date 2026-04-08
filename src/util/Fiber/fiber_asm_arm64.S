// Minimal fiber context switch for ARM64 (visionOS)
// Handles PAC (Pointer Authentication) for Apple Silicon

.text
.align 4

// void* fiber_make_context(void* stackTop, void (*entryFunc)(void*))
.globl _fiber_make_context
_fiber_make_context:
    sub x0, x0, #160

    // Zero all saved registers
    stp xzr, xzr, [x0, #0]
    stp xzr, xzr, [x0, #16]
    stp xzr, xzr, [x0, #32]
    stp xzr, xzr, [x0, #48]
    stp xzr, xzr, [x0, #64]
    stp xzr, xzr, [x0, #96]
    stp xzr, xzr, [x0, #112]
    stp xzr, xzr, [x0, #128]
    stp xzr, xzr, [x0, #144]

    // x29=0, x30=entry point (raw, unsigned)
    str xzr, [x0, #80]
    str x1,  [x0, #88]

    ret

// void fiber_switch_context(void** fromCtx, void* toCtx)
.globl _fiber_switch_context
_fiber_switch_context:
    // Save all callee-saved registers
    stp x19, x20, [sp, #-160]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    stp d8,  d9,  [sp, #96]
    stp d10, d11, [sp, #112]
    stp d12, d13, [sp, #128]
    stp d14, d15, [sp, #144]

    // Save current SP
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

    // Strip any PAC bits from LR before branching
    // This makes it safe for both new fibers (unsigned entry) and
    // resumed fibers (PAC-signed return address)
    xpaclri
    br x30
