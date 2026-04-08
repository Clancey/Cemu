#include "FiberPThread.h"
#include <cstdio>
#include <sys/mman.h>

// Assembly functions defined in fiber_asm_arm64.S
extern "C" void* fiber_make_context(void* stackTop, void (*entryFunc)(void*));
extern "C" void fiber_switch_context(void** fromCtx, void* toCtx);

thread_local Fiber* tl_currentFiber{nullptr};

// Fiber entry wrapper — called when switching to a new fiber for the first time
__attribute__((noinline))
static void FiberEntry(void* arg)
{
	Fiber* fiber = tl_currentFiber;
	if (!fiber)
		abort();

	if (fiber->m_entryPoint)
		fiber->m_entryPoint(fiber->m_userParam);

	abort();
}

Fiber::Fiber(void (*fiberEntryPoint)(void* userParam), void* userParam, void* privateData)
	: m_entryPoint(fiberEntryPoint),
	  m_userParam(userParam),
	  m_privateData(privateData)
{
	m_stackSize = 2 * 1024 * 1024; // 2MB
	m_stackPtr = mmap(nullptr, m_stackSize, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANON, -1, 0);
	if (m_stackPtr == MAP_FAILED) {
		fprintf(stderr, "Fiber: mmap stack failed\n");
		m_stackPtr = nullptr;
		return;
	}

	// Stack grows down — leave 256 bytes headroom from top, 16-byte aligned
	void* stackTop = (void*)(((uintptr_t)m_stackPtr + m_stackSize - 256) & ~(uintptr_t)0xF);
	m_context = fiber_make_context(stackTop, FiberEntry);
}

Fiber::Fiber(void* privateData)
	: m_privateData(privateData),
	  m_context(nullptr)
{
}

Fiber::~Fiber()
{
	if (m_stackPtr)
		munmap(m_stackPtr, m_stackSize);
}

Fiber* Fiber::PrepareCurrentThread(void* privateData)
{
	tl_currentFiber = new Fiber(privateData);
	return tl_currentFiber;
}

void Fiber::Switch(Fiber& targetFiber)
{
	if (&targetFiber == tl_currentFiber)
		return;

	Fiber* prevFiber = tl_currentFiber;
	tl_currentFiber = &targetFiber;

	fiber_switch_context(&prevFiber->m_context, targetFiber.m_context);
}

void* Fiber::GetFiberPrivateData()
{
	return tl_currentFiber ? tl_currentFiber->m_privateData : nullptr;
}
