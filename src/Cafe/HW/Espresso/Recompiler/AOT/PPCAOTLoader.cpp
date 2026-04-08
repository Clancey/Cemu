#include "PPCAOTLoader.h"
#include "../PPCRecompiler.h"
#include "Common/precompiled.h"

// Weak symbols for AOT tables — if no AOT code is linked, these resolve to defaults
__attribute__((weak)) extern "C" const AOTFunctionEntry g_aotFunctionTable[] = {};
__attribute__((weak)) extern "C" const size_t g_aotFunctionCount = 0;
__attribute__((weak)) extern "C" const AOTEntryPointEntry g_aotEntryPointTable[] = {};
__attribute__((weak)) extern "C" const size_t g_aotEntryPointCount = 0;

namespace PPCAOTLoader
{

bool HasPrecompiledFunctions()
{
	return g_aotFunctionCount > 0;
}

size_t PopulateJumpTable(PPCRecompilerInstanceData_t* instanceData)
{
	if (!instanceData || g_aotEntryPointCount == 0)
		return 0;

	size_t loadedCount = 0;

	for (size_t i = 0; i < g_aotEntryPointCount; i++)
	{
		const auto& ep = g_aotEntryPointTable[i];
		if (ep.functionIndex >= g_aotFunctionCount)
			continue;

		uint32_t ppcAddr = ep.ppcAddress;
		if (ppcAddr >= PPC_REC_CODE_AREA_END)
			continue;

		uint32_t tableIndex = ppcAddr / 4;
		const auto& func = g_aotFunctionTable[ep.functionIndex];

		// Calculate native entry point: function base + offset for this entry point
		auto* nativeEntry = reinterpret_cast<uint8_t*>(func.nativeCode) + ep.nativeOffset;
		auto entryFunc = reinterpret_cast<PPCREC_JUMP_ENTRY>(nativeEntry);

		instanceData->ppcRecompilerDirectJumpTable[tableIndex] = entryFunc;
		loadedCount++;
	}

	return loadedCount;
}

void InitializeInterfaceFunctions()
{
	PPCRecompiler_enterRecompilerCode = reinterpret_cast<decltype(PPCRecompiler_enterRecompilerCode)>(
		PPCRecompilerAOT_enterRecompilerCode);
	PPCRecompiler_leaveRecompilerCode_unvisited = reinterpret_cast<decltype(PPCRecompiler_leaveRecompilerCode_unvisited)>(
		PPCRecompilerAOT_leaveRecompilerCode_unvisited);
	PPCRecompiler_leaveRecompilerCode_visited = reinterpret_cast<decltype(PPCRecompiler_leaveRecompilerCode_visited)>(
		PPCRecompilerAOT_leaveRecompilerCode_visited);
}

} // namespace PPCAOTLoader
