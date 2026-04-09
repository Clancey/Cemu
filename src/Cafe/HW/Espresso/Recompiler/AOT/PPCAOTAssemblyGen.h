#pragma once

#include "PPCAOTCache.h"
#include <filesystem>
#include <string>

// Generates assembly source files and C++ registration tables from an AOT cache.
// The output can be compiled and linked into the target binary.

namespace PPCAOTAssemblyGen
{
	struct GeneratorOptions
	{
		std::filesystem::path outputDir;
		std::string symbolPrefix{"cemu_aot_func_"};
		bool generateAssembly{true};
		bool generateRegistrationTable{true};
	};

	// Generate assembly .s file containing all AOT function machine code as .byte directives
	bool GenerateAssembly(const AOTCache& cache, const std::filesystem::path& outputPath,
						  const std::string& symbolPrefix = "cemu_aot_func_");

	// Generate C++ registration table mapping PPC addresses to function symbols
	bool GenerateRegistrationTable(const AOTCache& cache, const std::filesystem::path& outputPath,
								   const std::string& symbolPrefix = "cemu_aot_func_");

	// Generate both assembly and registration table
	bool GenerateAll(const AOTCache& cache, const GeneratorOptions& options);

	// Format a PPC address as a hex string for use in symbol names
	std::string FormatAddress(uint32_t address);
}
