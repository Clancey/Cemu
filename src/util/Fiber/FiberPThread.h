#pragma once

#include <cstdint>
#include <cstdlib>

// Minimal fiber implementation using custom ARM64 assembly context switch
// All fibers run on the same OS thread — compatible with Cemu's scheduler

class Fiber
{
public:
	Fiber(void (*FiberEntryPoint)(void* userParam), void* userParam, void* privateData);
	~Fiber();

	static Fiber* PrepareCurrentThread(void* privateData = nullptr);
	static void Switch(Fiber& targetFiber);
	static void* GetFiberPrivateData();

	void* m_privateData{};
	void* m_context{};       // saved SP (context pointer for asm switch)
	void (*m_entryPoint)(void* userParam){};
	void* m_userParam{};
	void* m_stackPtr{};
	size_t m_stackSize{};

private:
	Fiber(void* privateData); // fiber from current thread
};
