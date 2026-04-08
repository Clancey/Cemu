#pragma once

#include <filesystem>
#include <cstdint>

// AOT Compiler: discovers all PPC functions in loaded game modules and
// pre-compiles them to native code, saving the result as an AOT cache file.
// This runs as a mode of Cemu (--aot-compile) after the game RPX is loaded.

namespace PPCAOTCompiler
{
	// Run AOT compilation on all loaded RPL modules.
	// Discovers function entry points via export tables and boundary tracking,
	// compiles each function through the existing PPC→IML→AArch64 pipeline,
	// and saves the results to the specified output path.
	// Returns true on success.
	bool CompileLoadedModules(const std::filesystem::path& outputPath);

	// Scan all loaded RPL modules for function entry points.
	// Returns a set of discovered PPC addresses.
	std::set<uint32_t> DiscoverFunctionEntryPoints();
}
