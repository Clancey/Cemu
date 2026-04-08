#pragma once

#include <cstdint>
#include <cstddef>

// Runtime interface for loading AOT-compiled functions into the recompiler jump table.
// The AOT function table and interface functions are generated at build time
// and linked into the binary as compiled assembly.

struct AOTFunctionEntry
{
	uint32_t ppcAddress;    // PPC address this function handles
	void* nativeCode;       // pointer to the pre-compiled native code
};

struct AOTEntryPointEntry
{
	uint32_t ppcAddress;    // PPC entry point address
	uint32_t functionIndex; // index into g_aotFunctionTable
	uint32_t nativeOffset;  // byte offset within the function's native code
};

// These are defined in the generated aot_table.cpp
extern "C" const AOTFunctionEntry g_aotFunctionTable[];
extern "C" const size_t g_aotFunctionCount;
extern "C" const AOTEntryPointEntry g_aotEntryPointTable[];
extern "C" const size_t g_aotEntryPointCount;

// Static AOT interface functions (defined in PPCAOTInterface_aarch64.s)
extern "C" void PPCRecompilerAOT_enterRecompilerCode(uint64_t codeMem, uint64_t ppcInterpreterInstance);
extern "C" void PPCRecompilerAOT_leaveRecompilerCode_unvisited();
extern "C" void PPCRecompilerAOT_leaveRecompilerCode_visited();

struct PPCRecompilerInstanceData_t;

namespace PPCAOTLoader
{
	// Check if AOT-compiled functions are available (linked into the binary)
	bool HasPrecompiledFunctions();

	// Populate the recompiler jump table with AOT function entry points.
	// Returns the number of entry points loaded.
	size_t PopulateJumpTable(PPCRecompilerInstanceData_t* instanceData);

	// Initialize AOT interface function pointers (enter/leave handlers).
	void InitializeInterfaceFunctions();
}
