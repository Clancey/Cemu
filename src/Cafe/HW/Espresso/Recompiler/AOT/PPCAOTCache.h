#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>

// AOT cache file format for pre-compiled PPC functions.
// Generated offline (on a machine that supports JIT), consumed at runtime
// on platforms where JIT is not available (e.g. visionOS).

constexpr uint32_t AOT_CACHE_MAGIC = 0x43454D55; // 'CEMU'
constexpr uint32_t AOT_CACHE_VERSION = 1;

enum class AOTRelocationType : uint8_t
{
	AbsoluteAddress64 = 0, // 64-bit absolute address embedded as MOVZ+MOVK sequence (4 instructions, 16 bytes)
};

struct AOTRelocationEntry
{
	uint32_t offsetInCode;        // byte offset within the function's machine code
	AOTRelocationType type;
	std::string symbolName;       // target symbol name (e.g. "PPCRecompiler_virtualHLE")
};

struct AOTEntryPoint
{
	uint32_t ppcAddress;          // PPC address that maps to this entry point
	uint32_t nativeOffset;        // byte offset within the function's machine code
};

struct AOTFunctionRecord
{
	uint32_t ppcAddress;          // PPC function start address
	uint32_t ppcSize;             // size of PPC code in bytes
	std::vector<AOTEntryPoint> entryPoints;    // all valid entry points into this function
	std::vector<uint8_t> machineCode;          // AArch64 machine code bytes
	std::vector<AOTRelocationEntry> relocations; // addresses that need fixing at link/load time
	uint32_t codeAlignment{16};   // required alignment (AArch64 instructions are 4-byte aligned, 16 for SIMD)
};

struct AOTCacheHeader
{
	uint32_t magic{AOT_CACHE_MAGIC};
	uint32_t version{AOT_CACHE_VERSION};
	uint64_t titleId{0};          // game title ID for validation
	uint32_t functionCount{0};
};

class AOTCache
{
public:
	AOTCacheHeader header;
	std::vector<AOTFunctionRecord> functions;

	void SetTitleId(uint64_t titleId) { header.titleId = titleId; }

	void AddFunction(AOTFunctionRecord&& func);

	bool SaveToFile(const std::filesystem::path& path) const;
	bool LoadFromFile(const std::filesystem::path& path);

	const AOTFunctionRecord* FindFunction(uint32_t ppcAddress) const;

	size_t GetFunctionCount() const { return functions.size(); }
	void Clear();

private:
	void WriteU8(std::vector<uint8_t>& buf, uint8_t v) const;
	void WriteU32(std::vector<uint8_t>& buf, uint32_t v) const;
	void WriteU64(std::vector<uint8_t>& buf, uint64_t v) const;
	void WriteString(std::vector<uint8_t>& buf, const std::string& s) const;
	void WriteBytes(std::vector<uint8_t>& buf, const std::vector<uint8_t>& data) const;

	bool ReadU8(const uint8_t*& ptr, const uint8_t* end, uint8_t& v) const;
	bool ReadU32(const uint8_t*& ptr, const uint8_t* end, uint32_t& v) const;
	bool ReadU64(const uint8_t*& ptr, const uint8_t* end, uint64_t& v) const;
	bool ReadString(const uint8_t*& ptr, const uint8_t* end, std::string& s) const;
	bool ReadBytes(const uint8_t*& ptr, const uint8_t* end, std::vector<uint8_t>& data, size_t count) const;
};
