#include "Fiber.h"
#ifndef __ANDROID__
#include <ucontext.h>
#endif
#include <atomic>

thread_local Fiber* sCurrentFiber{};

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
	// Android doesn't support ucontext, provide minimal stub
	this->m_implData = nullptr;
	m_stackPtr = nullptr;
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
	// Android doesn't support ucontext, provide minimal stub
	this->m_implData = nullptr;
	m_stackPtr = nullptr;
#endif
}

Fiber::~Fiber()
{
#ifndef __ANDROID__
	if(m_stackPtr)
		free(m_stackPtr);
	free(m_implData);
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
	// Android doesn't support fiber switching, just update current fiber
    sCurrentFiber = &targetFiber;
#endif
}

void* Fiber::GetFiberPrivateData()
{
	return sCurrentFiber->m_privateData;
}
