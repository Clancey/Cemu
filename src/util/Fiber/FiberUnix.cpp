#include "Fiber.h"
#ifndef __ANDROID__
#include <ucontext.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#endif
#include <atomic>

// Simple assert fallback for debug builds if not already defined
#ifndef cemu_assert_debug
#ifdef _DEBUG
#include <cassert>
#define cemu_assert_debug(condition) assert(condition)
#else
#define cemu_assert_debug(condition) ((void)0)
#endif
#endif

thread_local Fiber* sCurrentFiber{};

#ifdef __ANDROID__
// Android ARM64 fiber context structure
struct AndroidFiberContext {
    // ARM64 callee-saved registers that need to be preserved across function calls
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28; // x19-x28 (10 registers)
    uint64_t x29; // frame pointer (fp)
    uint64_t x30; // link register (lr)
    uint64_t sp;  // stack pointer

    // ARM64 callee-saved NEON/FP registers (d8-d15)
    uint64_t d8, d9, d10, d11, d12, d13, d14, d15;

    // Fiber entry point and user parameter
    void (*entryPoint)(void*);
    void* userParam;

    // Flag to indicate if this is the initial context
    bool isInitialContext;
};

// ARM64 context switching functions implemented in inline assembly
extern "C" {
    // Switch from current context to target context
    // Returns 0 when switching away, 1 when switching back
    int android_fiber_switch(AndroidFiberContext* from, AndroidFiberContext* to);

    // Set up initial context for a new fiber
    void android_fiber_setup(AndroidFiberContext* ctx, void* stack_top);
}

// Implementation of context switching using ARM64 assembly
asm(
    ".global android_fiber_switch\n"
    ".type android_fiber_switch, %function\n"
    "android_fiber_switch:\n"
    "    // Save current context (x0 = from, x1 = to)\n"
    "    stp x19, x20, [x0, #0]\n"     // Save x19, x20
    "    stp x21, x22, [x0, #16]\n"    // Save x21, x22
    "    stp x23, x24, [x0, #32]\n"    // Save x23, x24
    "    stp x25, x26, [x0, #48]\n"    // Save x25, x26
    "    stp x27, x28, [x0, #64]\n"    // Save x27, x28
    "    stp x29, x30, [x0, #80]\n"    // Save fp, lr
    "    mov x2, sp\n"
    "    str x2, [x0, #96]\n"          // Save sp
    "    \n"
    "    // Save NEON/FP registers d8-d15\n"
    "    stp d8,  d9,  [x0, #104]\n"
    "    stp d10, d11, [x0, #120]\n"
    "    stp d12, d13, [x0, #136]\n"
    "    stp d14, d15, [x0, #152]\n"
    "    \n"
    "    // Load target context\n"
    "    ldp x19, x20, [x1, #0]\n"     // Load x19, x20
    "    ldp x21, x22, [x1, #16]\n"    // Load x21, x22
    "    ldp x23, x24, [x1, #32]\n"    // Load x23, x24
    "    ldp x25, x26, [x1, #48]\n"    // Load x25, x26
    "    ldp x27, x28, [x1, #64]\n"    // Load x27, x28
    "    ldp x29, x30, [x1, #80]\n"    // Load fp, lr
    "    ldr x2, [x1, #96]\n"          // Load sp
    "    mov sp, x2\n"
    "    \n"
    "    // Load NEON/FP registers d8-d15\n"
    "    ldp d8,  d9,  [x1, #104]\n"
    "    ldp d10, d11, [x1, #120]\n"
    "    ldp d12, d13, [x1, #136]\n"
    "    ldp d14, d15, [x1, #152]\n"
    "    \n"
    "    // Check if this is initial context setup\n"
    "    ldrb w2, [x1, #184]\n"        // Load isInitialContext flag
    "    cbz w2, 1f\n"                 // If not initial, jump to normal return
    "    \n"
    "    // Initial context - call entry point\n"
    "    strb wzr, [x1, #184]\n"       // Clear isInitialContext flag
    "    ldr x0, [x1, #176]\n"         // Load userParam
    "    ldr x1, [x1, #168]\n"         // Load entryPoint
    "    blr x1\n"                     // Call entryPoint(userParam)
    "    \n"
    "    // Fiber function returned - this should not normally happen\n"
    "    // For safety, just infinite loop to prevent execution continuing\n"
    "2:  b 2b\n"                       // Infinite loop - fiber returned unexpectedly
    "    \n"
    "1:  // Normal context switch return\n"
    "    mov x0, #1\n"                 // Return 1 (switched back to this context)
    "    ret\n"
);

asm(
    ".global android_fiber_setup\n"
    ".type android_fiber_setup, %function\n"
    "android_fiber_setup:\n"
    "    // x0 = context, x1 = stack_top\n"
    "    // Set up stack pointer\n"
    "    str x1, [x0, #96]\n"          // Set sp in context
    "    \n"
    "    // Set frame pointer to stack top (following ARM64 ABI)\n"
    "    str x1, [x0, #80]\n"          // Set fp in context
    "    \n"
    "    // Set link register to a safe return address (not used in our case)\n"
    "    adr x2, android_fiber_setup\n"
    "    str x2, [x0, #88]\n"          // Set lr in context
    "    \n"
    "    // Mark as initial context\n"
    "    mov w2, #1\n"
    "    strb w2, [x0, #184]\n"        // Set isInitialContext = true
    "    \n"
    "    ret\n"
);

#endif

Fiber::Fiber(void(*FiberEntryPoint)(void* userParam), void* userParam, void* privateData) : m_privateData(privateData)
{
#ifndef __ANDROID__
	ucontext_t* ctx = (ucontext_t*)malloc(sizeof(ucontext_t));

	const size_t stackSize = 2 * 1024 * 1024;
	m_stackPtr = malloc(stackSize);

	getcontext(ctx);
	ctx->uc_stack.ss_sp = m_stackPtr;
	ctx->uc_stack.ss_size = stackSize;
	ctx->uc_link = &ctx[0];
#ifdef __arm64__
	// https://www.man7.org/linux/man-pages/man3/makecontext.3.html#NOTES
	makecontext(ctx, (void(*)())FiberEntryPoint, 2, (uint64) userParam >> 32, userParam);
#else
	makecontext(ctx, (void(*)())FiberEntryPoint, 1, userParam);
#endif
	this->m_implData = (void*)ctx;
#else
	// Android ARM64 fiber implementation
	const size_t stackSize = 2 * 1024 * 1024;

	// Allocate stack memory using mmap for better alignment and control
	m_stackPtr = mmap(nullptr, stackSize, PROT_READ | PROT_WRITE,
	                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (m_stackPtr == MAP_FAILED) {
		m_stackPtr = nullptr;
		this->m_implData = nullptr;
		return;
	}

	// Create and initialize Android fiber context
	AndroidFiberContext* ctx = new AndroidFiberContext();
	std::memset(ctx, 0, sizeof(AndroidFiberContext));

	// Set up entry point and user parameter
	ctx->entryPoint = FiberEntryPoint;
	ctx->userParam = userParam;
	ctx->isInitialContext = true;

	// Calculate stack top (stacks grow downward on ARM64)
	// Leave some space at the top for alignment and safety
	void* stackTop = static_cast<char*>(m_stackPtr) + stackSize - 64;

	// Ensure 16-byte stack alignment as required by ARM64 ABI
	uintptr_t stackAddr = reinterpret_cast<uintptr_t>(stackTop);
	stackAddr &= ~0xFULL; // Align to 16 bytes
	stackTop = reinterpret_cast<void*>(stackAddr);

	// Set up the fiber context
	android_fiber_setup(ctx, stackTop);

	this->m_implData = static_cast<void*>(ctx);
#endif
}

Fiber::Fiber(void* privateData) : m_privateData(privateData)
{
#ifndef __ANDROID__
	ucontext_t* ctx = (ucontext_t*)malloc(sizeof(ucontext_t));
	getcontext(ctx);
	this->m_implData = (void*)ctx;
	m_stackPtr = nullptr;
#else
	// Android fiber for current thread (no stack allocation needed)
	AndroidFiberContext* ctx = new AndroidFiberContext();
	std::memset(ctx, 0, sizeof(AndroidFiberContext));

	// This context represents the current thread, so no setup needed
	// The context will be filled when we first switch away from this fiber
	ctx->isInitialContext = false;
	ctx->entryPoint = nullptr;
	ctx->userParam = nullptr;

	this->m_implData = static_cast<void*>(ctx);
	m_stackPtr = nullptr;
#endif
}

Fiber::~Fiber()
{
#ifndef __ANDROID__
	if(m_stackPtr)
		free(m_stackPtr);
	free(m_implData);
#else
	// Clean up Android fiber context and stack
	if (m_implData) {
		delete static_cast<AndroidFiberContext*>(m_implData);
	}

	if (m_stackPtr && m_stackPtr != MAP_FAILED) {
		// Unmap the stack memory
		const size_t stackSize = 2 * 1024 * 1024;
		munmap(m_stackPtr, stackSize);
	}
#endif
}

Fiber* Fiber::PrepareCurrentThread(void* privateData)
{
	cemu_assert_debug(sCurrentFiber == nullptr);
    sCurrentFiber = new Fiber(privateData);
	return sCurrentFiber;
}

void Fiber::Switch(Fiber& targetFiber)
{
#ifndef __ANDROID__
    Fiber* leavingFiber = sCurrentFiber;
    sCurrentFiber = &targetFiber;
	std::atomic_thread_fence(std::memory_order_seq_cst);
	swapcontext((ucontext_t*)(leavingFiber->m_implData), (ucontext_t*)(targetFiber.m_implData));
	std::atomic_thread_fence(std::memory_order_seq_cst);
#else
	// Android ARM64 fiber switching
	if (!sCurrentFiber || !sCurrentFiber->m_implData || !targetFiber.m_implData) {
		// Safety check - if contexts are not properly initialized, just update current fiber
		sCurrentFiber = &targetFiber;
		return;
	}

	Fiber* leavingFiber = sCurrentFiber;
	AndroidFiberContext* fromCtx = static_cast<AndroidFiberContext*>(leavingFiber->m_implData);
	AndroidFiberContext* toCtx = static_cast<AndroidFiberContext*>(targetFiber.m_implData);

	// Update current fiber before context switch to maintain consistency
	sCurrentFiber = &targetFiber;

	// Ensure memory synchronization
	std::atomic_thread_fence(std::memory_order_seq_cst);

	// Perform the actual context switch using our ARM64 assembly implementation
	// This will save the current context in fromCtx and restore the context in toCtx
	android_fiber_switch(fromCtx, toCtx);

	// Memory fence after returning from context switch
	std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
}

void* Fiber::GetFiberPrivateData()
{
	return sCurrentFiber->m_privateData;
}
