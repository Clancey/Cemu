#pragma once

// JIT memory allocation for platforms where MAP_JIT is unavailable (iOS/visionOS).
// Uses vm_remap() to create a dual-mapped memory region:
//   - RX (Read-Execute) view: used for code execution
//   - RW (Read-Write) view: used for writing code
// Both views map the same physical memory, so writes to the RW view are
// immediately visible in the RX view.

#include <cstddef>
#include <cstdint>

namespace JITMemoryUtil
{
	// Initialize the global JIT memory region.
	// Allocates a large RX region and creates an RW remap.
	// Must be called once before any JIT code allocation.
	// Returns true on success.
	bool Initialize(size_t regionSize = 512 * 1024 * 1024);

	// Shut down and release all JIT memory.
	void Shutdown();

	// Check if the JIT memory system is initialized and usable.
	bool IsAvailable();

	// Allocate a block from the JIT memory region.
	// Returns an RX pointer (executable). To write to it, add GetWritableOffset().
	void* AllocateCode(size_t size);

	// Free a previously allocated code block.
	void FreeCode(void* rxPtr);

	// Get the offset from RX to RW region.
	// To write: (uint8*)rxPtr + GetWritableOffset() → writable address
	ptrdiff_t GetWritableOffset();

	// Convert an RX pointer to its writable alias.
	template<typename T>
	T* ToWritable(T* rxPtr)
	{
		return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(rxPtr) + GetWritableOffset());
	}
}
