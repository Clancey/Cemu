#include "PPCAOTCompiler.h"
#include "PPCAOTCache.h"
#include "PPCAOTAssemblyGen.h"
#include "../PPCRecompiler.h"
#include "../PPCRecompilerIml.h"
#include "../PPCFunctionBoundaryTracker.h"
#include "Cafe/OS/RPL/rpl.h"
#include "Cafe/OS/RPL/rpl_structs.h"
#include "Cafe/HW/Espresso/Interpreter/PPCInterpreterInternal.h"
#include "Cafe/CafeSystem.h"
#include "Common/precompiled.h"
#include "config/ActiveSettings.h"

#include "../IML/IML.h"
#include "../IML/IMLRegisterAllocator.h"
#ifdef __aarch64__
#include "../BackendAArch64/BackendAArch64.h"
#endif
#include "../BackendX64/BackendX64.h"

extern bool PPCRecompiler_ApplyIMLPasses(ppcImlGenContext_t& ppcImlGenContext);

namespace PPCAOTCompiler
{

std::set<uint32_t> DiscoverFunctionEntryPoints()
{
	std::set<uint32_t> entryPoints;

	RPLModule** moduleList = RPLLoader_GetModuleList();
	sint32 moduleCount = RPLLoader_GetModuleCount();

	for (sint32 m = 0; m < moduleCount; m++)
	{
		RPLModule* rpl = moduleList[m];
		if (!rpl)
			continue;

		// Add module entry point
		if (rpl->entrypoint != 0 && rpl->entrypoint < PPC_REC_CODE_AREA_END)
			entryPoints.insert(rpl->entrypoint);

		// Add all function exports
		if (rpl->exportFDataPtr && rpl->exportFCount > 0)
		{
			for (uint32 f = 0; f < rpl->exportFCount; f++)
			{
				uint32 addr = (uint32)rpl->exportFDataPtr[f].virtualOffset;
				if (addr != 0 && addr < PPC_REC_CODE_AREA_END)
					entryPoints.insert(addr);
			}
		}

		// Scan the text region for additional entry points using boundary tracking.
		// Start from each known entry and let the tracker discover reachable code.
		uint32 textBase = rpl->regionMappingBase_text.GetMPTR();
		uint32 textSize = rpl->regionSize_text;
		if (textBase == 0 || textSize == 0)
			continue;

		cemuLog_log(LogType::Force, "AOT: Module '{}' text region: 0x{:08x} - 0x{:08x} ({} bytes)",
			rpl->moduleName2, textBase, textBase + textSize, textSize);
	}

	// Now use PPCFunctionBoundaryTracker to discover all reachable functions
	// from each entry point. This traces branch targets recursively.
	std::set<uint32_t> discoveredFunctions;
	for (uint32_t addr : entryPoints)
	{
		PPCFunctionBoundaryTracker tracker;
		tracker.trackStartPoint(addr);

		for (const auto& range : tracker.GetRanges())
		{
			discoveredFunctions.insert(range.startAddress);
		}
	}

	// Merge discovered functions back into entryPoints
	entryPoints.insert(discoveredFunctions.begin(), discoveredFunctions.end());

	cemuLog_log(LogType::Force, "AOT: Discovered {} function entry points from {} modules",
		entryPoints.size(), moduleCount);

	return entryPoints;
}

static AOTFunctionRecord CompileFunction(uint32_t address, PPCFunctionBoundaryTracker& tracker)
{
	AOTFunctionRecord record{};
	record.ppcAddress = address;

	PPCFunctionBoundaryTracker::PPCRange_t range;
	if (!tracker.getRangeForAddress(address, range))
		return record;

	record.ppcSize = range.length;

	PPCRecFunction_t* ppcRecFunc = new PPCRecFunction_t();
	ppcRecFunc->ppcAddress = range.startAddress;
	ppcRecFunc->ppcSize = range.length;

	std::set<uint32> entryAddresses;
	entryAddresses.insert(address);

	ppcImlGenContext_t ppcImlGenContext = { 0 };
	ppcImlGenContext.debug_entryPPCAddress = range.startAddress;
	bool compiledSuccessfully = PPCRecompiler_generateIntermediateCode(ppcImlGenContext, ppcRecFunc, entryAddresses, tracker);
	if (!compiledSuccessfully)
	{
		delete ppcRecFunc;
		return record;
	}

	if (!PPCRecompiler_ApplyIMLPasses(ppcImlGenContext))
	{
		delete ppcRecFunc;
		return record;
	}

	// Generate native code
	bool codeGenSuccess = false;
#if defined(ARCH_X86_64)
	codeGenSuccess = PPCRecompiler_generateX64Code(ppcRecFunc, &ppcImlGenContext);
#elif defined(__aarch64__)
	codeGenSuccess = PPCRecompiler_generateAArch64Code(ppcRecFunc, &ppcImlGenContext);
#endif

	if (!codeGenSuccess || !ppcRecFunc->x86Code || ppcRecFunc->x86Size == 0)
	{
		delete ppcRecFunc;
		return record;
	}

	// Capture machine code bytes
	record.machineCode.assign(
		static_cast<const uint8_t*>(ppcRecFunc->x86Code),
		static_cast<const uint8_t*>(ppcRecFunc->x86Code) + ppcRecFunc->x86Size);

	// Collect entry points from IML segments
	for (IMLSegment* seg : ppcImlGenContext.segmentList2)
	{
		if (!seg->isEnterable)
			continue;
		AOTEntryPoint ep;
		ep.ppcAddress = seg->enterPPCAddress;
		ep.nativeOffset = seg->x64Offset;
		record.entryPoints.push_back(ep);
	}

	// Clean up the generated code (free the xbyak allocation)
#if defined(__aarch64__)
	PPCRecompiler_cleanupAArch64Code(ppcRecFunc->x86Code, ppcRecFunc->x86Size);
#endif

	delete ppcRecFunc;
	return record;
}

bool CompileLoadedModules(const std::filesystem::path& outputPath)
{
	cemuLog_log(LogType::Force, "AOT: Starting compilation...");

	// Discover all function entry points
	auto entryPoints = DiscoverFunctionEntryPoints();
	if (entryPoints.empty())
	{
		cemuLog_log(LogType::Force, "AOT: No function entry points found");
		return false;
	}

	// Initialize the recompiler instance data if not already done
	// (PPCRecompiler_init may have been called in interpreter-only mode)
	if (!ppcRecompilerInstanceData)
	{
		cemuLog_log(LogType::Force, "AOT: Recompiler instance data not available. Ensure recompiler is initialized.");
		return false;
	}

	AOTCache cache;
	// Use the title ID from the loaded game
	cache.SetTitleId(CafeSystem::GetForegroundTitleId());

	uint32_t compiledCount = 0;
	uint32_t failedCount = 0;

	// Group entry points by function range to avoid duplicate compilation
	std::set<uint32_t> compiledAddresses;

	for (uint32_t addr : entryPoints)
	{
		if (compiledAddresses.count(addr))
			continue;

		// Use boundary tracker to find the full function range
		PPCFunctionBoundaryTracker tracker;
		tracker.trackStartPoint(addr);

		PPCFunctionBoundaryTracker::PPCRange_t range;
		if (!tracker.getRangeForAddress(addr, range))
		{
			failedCount++;
			continue;
		}

		// Skip if we already compiled this range
		if (compiledAddresses.count(range.startAddress))
			continue;

		auto record = CompileFunction(range.startAddress, tracker);
		if (record.machineCode.empty())
		{
			failedCount++;
			continue;
		}

		// Mark all addresses in this range as compiled
		compiledAddresses.insert(range.startAddress);
		for (const auto& ep : record.entryPoints)
			compiledAddresses.insert(ep.ppcAddress);

		cache.AddFunction(std::move(record));
		compiledCount++;

		if (compiledCount % 100 == 0)
			cemuLog_log(LogType::Force, "AOT: Compiled {} functions...", compiledCount);
	}

	cemuLog_log(LogType::Force, "AOT: Compilation complete. {} succeeded, {} failed",
		compiledCount, failedCount);

	// Save the AOT cache
	if (!cache.SaveToFile(outputPath))
	{
		cemuLog_log(LogType::Force, "AOT: Failed to save cache to {}", outputPath.string());
		return false;
	}

	cemuLog_log(LogType::Force, "AOT: Cache saved to {} ({} functions, {} bytes)",
		outputPath.string(), compiledCount, std::filesystem::file_size(outputPath));

	// Also generate assembly and registration table alongside the cache
	auto outputDir = outputPath.parent_path();
	PPCAOTAssemblyGen::GeneratorOptions genOpts;
	genOpts.outputDir = outputDir;
	if (PPCAOTAssemblyGen::GenerateAll(cache, genOpts))
	{
		cemuLog_log(LogType::Force, "AOT: Generated assembly and registration table in {}",
			outputDir.string());
	}

	return true;
}

} // namespace PPCAOTCompiler
