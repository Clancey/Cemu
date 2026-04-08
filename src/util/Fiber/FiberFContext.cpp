#include "FiberFContext.h"
#if TARGET_OS_VISION || TARGET_OS_IPHONE
#include <sys/mman.h>
#endif

thread_local Fiber* sCurrentFiber{};

using namespace boost::context::detail;

Fiber::Fiber(void (*fiberEntryPoint)(void* userParam), void* userParam, void* privateData)
	: m_entryPoint(fiberEntryPoint),
	  m_userParam(userParam),
	  m_privateData(privateData)
{
	const size_t stackSize = 2 * 1024 * 1024;
#if TARGET_OS_VISION || TARGET_OS_IPHONE
	// Use mmap for fiber stacks on iOS/visionOS — provides proper alignment and guard pages
	m_stackPtr = mmap(nullptr, stackSize, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANON, -1, 0);
	if (m_stackPtr == MAP_FAILED) {
		m_stackPtr = nullptr;
		return;
	}
#else
	m_stackPtr = malloc(stackSize);
#endif
	m_stackSize = stackSize;
	void* stackTop = static_cast<uint8_t*>(m_stackPtr) + stackSize;
	m_context = make_fcontext(stackTop, stackSize, Fiber::Start);
}

Fiber::Fiber(void* privateData) : m_privateData(privateData)
{
}

void Fiber::Start(transfer_t transfer)
{
	auto fiber = static_cast<Fiber*>(transfer.data);
	fiber->m_prevFiber->m_context = transfer.fctx;
	fiber->m_entryPoint(fiber->m_userParam);
}

Fiber::~Fiber()
{
	if (m_stackPtr) {
#if TARGET_OS_VISION || TARGET_OS_IPHONE
		munmap(m_stackPtr, m_stackSize);
#else
		free(m_stackPtr);
#endif
	}
}

Fiber* Fiber::PrepareCurrentThread(void* privateData)
{
	sCurrentFiber = new Fiber(privateData);
	return sCurrentFiber;
}

void Fiber::Switch(Fiber& targetFiber)
{
	if (&targetFiber == sCurrentFiber)
		return;

	Fiber* thisFiber = sCurrentFiber;
	sCurrentFiber = &targetFiber;
	targetFiber.m_prevFiber = thisFiber;
	transfer_t transfer = jump_fcontext(targetFiber.m_context, &targetFiber);
	thisFiber->m_prevFiber->m_context = transfer.fctx;
}

void* Fiber::GetFiberPrivateData()
{
	return sCurrentFiber->m_privateData;
}