#include "JITMemoryUtil_visionOS.h"

#if defined(__aarch64__) && (TARGET_OS_VISION || defined(IPHONEOS))

#include <sys/mman.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <unistd.h>
#include <dirent.h>
#include <mutex>
#include <cassert>
#include <cstring>

namespace JITMemoryUtil
{

// Global state
static uint8_t* s_rxRegion = nullptr;      // RX (executable) base
static uint8_t* s_rwRegion = nullptr;      // RW (writable) base
static size_t s_regionSize = 0;
static ptrdiff_t s_rwOffset = 0;           // rwRegion - rxRegion

// Simple bump allocator state
static std::mutex s_allocMutex;
static size_t s_allocOffset = 0;           // current allocation offset within region
static const size_t ALLOC_ALIGNMENT = 16;  // AArch64 code alignment

static bool s_hasTxm = false;

// Check if device uses Trusted Execution Monitor (iOS 26+ / visionOS 3+)
static bool DetectTXM()
{
	// TXM detection: check for the TXM firmware image
	// This is a heuristic used by DolphiniOS
	const char* paths[] = {
		"/System/Volumes/Preboot",
		"/private/preboot",
	};

	for (const char* basePath : paths)
	{
		// Look for a directory entry matching the expected UUID length
		DIR* dir = opendir(basePath);
		if (!dir) continue;

		struct dirent* entry;
		while ((entry = readdir(dir)) != nullptr)
		{
			if (entry->d_type != DT_DIR) continue;
			size_t nameLen = strlen(entry->d_name);

			// Look for directories of expected UUID lengths (36 or 96 chars)
			if (nameLen == 36 || nameLen == 96)
			{
				char txmPath[512];
				if (nameLen == 36)
				{
					// /System/Volumes/Preboot/<36>/boot/<96>/usr/standalone/firmware/FUD/Ap,TrustedExecutionMonitor.img4
					// We'd need to recurse further, skip for simplicity
					continue;
				}
				snprintf(txmPath, sizeof(txmPath),
					"%s/%s/usr/standalone/firmware/FUD/Ap,TrustedExecutionMonitor.img4",
					basePath, entry->d_name);
				if (access(txmPath, F_OK) == 0)
				{
					closedir(dir);
					return true;
				}
			}
		}
		closedir(dir);
	}
	return false;
}

bool Initialize(size_t regionSize)
{
	if (s_rxRegion)
		return true; // Already initialized

	s_regionSize = regionSize;
	s_hasTxm = DetectTXM();

	// Step 1: Allocate RX (read-execute) region
	void* rxPtr = mmap(nullptr, regionSize,
		PROT_READ | PROT_EXEC,
		MAP_PRIVATE | MAP_ANON,
		-1, 0);

	if (rxPtr == MAP_FAILED)
		return false;

	s_rxRegion = static_cast<uint8_t*>(rxPtr);

	// Step 2: If TXM is present, register the region with the kernel
	if (s_hasTxm)
	{
		asm volatile(
			"mov x0, %0\n"
			"mov x1, %1\n"
			"brk #0x69"
			:: "r"(s_rxRegion), "r"(regionSize)
			: "x0", "x1"
		);
	}

	// Step 3: Create RW remap of the same physical memory via vm_remap
	vm_address_t rwAddress = 0;
	vm_prot_t curProtection = 0;
	vm_prot_t maxProtection = 0;

	kern_return_t kr = vm_remap(
		mach_task_self(),                                    // target task
		&rwAddress,                                          // output address
		regionSize,                                          // size
		0,                                                   // alignment mask
		VM_FLAGS_ANYWHERE,                                   // flags: find space
		mach_task_self(),                                    // source task
		reinterpret_cast<vm_address_t>(s_rxRegion),          // source address
		FALSE,                                               // copy: NO (share physical pages)
		&curProtection,                                      // current protection out
		&maxProtection,                                      // max protection out
		VM_INHERIT_DEFAULT                                   // inheritance
	);

	if (kr != KERN_SUCCESS)
	{
		munmap(s_rxRegion, regionSize);
		s_rxRegion = nullptr;
		return false;
	}

	s_rwRegion = reinterpret_cast<uint8_t*>(rwAddress);

	// Step 4: Set the RW region to writable
	if (mprotect(s_rwRegion, regionSize, PROT_READ | PROT_WRITE) != 0)
	{
		vm_deallocate(mach_task_self(), rwAddress, regionSize);
		munmap(s_rxRegion, regionSize);
		s_rxRegion = nullptr;
		s_rwRegion = nullptr;
		return false;
	}

	s_rwOffset = s_rwRegion - s_rxRegion;
	s_allocOffset = 0;

	return true;
}

void Shutdown()
{
	if (s_rwRegion)
	{
		vm_deallocate(mach_task_self(),
			reinterpret_cast<vm_address_t>(s_rwRegion), s_regionSize);
		s_rwRegion = nullptr;
	}
	if (s_rxRegion)
	{
		munmap(s_rxRegion, s_regionSize);
		s_rxRegion = nullptr;
	}
	s_regionSize = 0;
	s_rwOffset = 0;
	s_allocOffset = 0;
}

bool IsAvailable()
{
	return s_rxRegion != nullptr && s_rwRegion != nullptr;
}

void* AllocateCode(size_t size)
{
	std::lock_guard<std::mutex> lock(s_allocMutex);

	// Align size up
	size = (size + ALLOC_ALIGNMENT - 1) & ~(ALLOC_ALIGNMENT - 1);

	if (s_allocOffset + size > s_regionSize)
		return nullptr; // Out of space

	void* rxPtr = s_rxRegion + s_allocOffset;
	s_allocOffset += size;
	return rxPtr;
}

void FreeCode(void* rxPtr)
{
	// Bump allocator — individual frees are no-ops.
	// Memory is reclaimed when the entire region is shut down.
	// This matches JIT emulator usage where code is rarely freed individually.
	(void)rxPtr;
}

ptrdiff_t GetWritableOffset()
{
	return s_rwOffset;
}

} // namespace JITMemoryUtil

#else

// Stub implementation for non-iOS/visionOS platforms
namespace JITMemoryUtil
{
	bool Initialize(size_t) { return false; }
	void Shutdown() {}
	bool IsAvailable() { return false; }
	void* AllocateCode(size_t) { return nullptr; }
	void FreeCode(void*) {}
	ptrdiff_t GetWritableOffset() { return 0; }
}

#endif
