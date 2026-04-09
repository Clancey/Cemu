#include "PPCAOTCache.h"
#include <fstream>
#include <algorithm>
#include <cstring>

void AOTCache::AddFunction(AOTFunctionRecord&& func)
{
	functions.emplace_back(std::move(func));
	header.functionCount = static_cast<uint32_t>(functions.size());
}

void AOTCache::Clear()
{
	functions.clear();
	header.functionCount = 0;
}

const AOTFunctionRecord* AOTCache::FindFunction(uint32_t ppcAddress) const
{
	for (const auto& func : functions)
	{
		if (func.ppcAddress == ppcAddress)
			return &func;
	}
	return nullptr;
}

// Serialization helpers

void AOTCache::WriteU8(std::vector<uint8_t>& buf, uint8_t v) const
{
	buf.push_back(v);
}

void AOTCache::WriteU32(std::vector<uint8_t>& buf, uint32_t v) const
{
	buf.push_back(static_cast<uint8_t>(v & 0xFF));
	buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
	buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
	buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void AOTCache::WriteU64(std::vector<uint8_t>& buf, uint64_t v) const
{
	WriteU32(buf, static_cast<uint32_t>(v & 0xFFFFFFFF));
	WriteU32(buf, static_cast<uint32_t>((v >> 32) & 0xFFFFFFFF));
}

void AOTCache::WriteString(std::vector<uint8_t>& buf, const std::string& s) const
{
	WriteU32(buf, static_cast<uint32_t>(s.size()));
	buf.insert(buf.end(), s.begin(), s.end());
}

void AOTCache::WriteBytes(std::vector<uint8_t>& buf, const std::vector<uint8_t>& data) const
{
	WriteU32(buf, static_cast<uint32_t>(data.size()));
	buf.insert(buf.end(), data.begin(), data.end());
}

bool AOTCache::ReadU8(const uint8_t*& ptr, const uint8_t* end, uint8_t& v) const
{
	if (ptr + 1 > end) return false;
	v = *ptr++;
	return true;
}

bool AOTCache::ReadU32(const uint8_t*& ptr, const uint8_t* end, uint32_t& v) const
{
	if (ptr + 4 > end) return false;
	v = static_cast<uint32_t>(ptr[0]) |
		(static_cast<uint32_t>(ptr[1]) << 8) |
		(static_cast<uint32_t>(ptr[2]) << 16) |
		(static_cast<uint32_t>(ptr[3]) << 24);
	ptr += 4;
	return true;
}

bool AOTCache::ReadU64(const uint8_t*& ptr, const uint8_t* end, uint64_t& v) const
{
	uint32_t lo, hi;
	if (!ReadU32(ptr, end, lo)) return false;
	if (!ReadU32(ptr, end, hi)) return false;
	v = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
	return true;
}

bool AOTCache::ReadString(const uint8_t*& ptr, const uint8_t* end, std::string& s) const
{
	uint32_t len;
	if (!ReadU32(ptr, end, len)) return false;
	if (ptr + len > end) return false;
	s.assign(reinterpret_cast<const char*>(ptr), len);
	ptr += len;
	return true;
}

bool AOTCache::ReadBytes(const uint8_t*& ptr, const uint8_t* end, std::vector<uint8_t>& data, size_t count) const
{
	if (ptr + count > end) return false;
	data.assign(ptr, ptr + count);
	ptr += count;
	return true;
}

// File format:
//   Header: magic(4) version(4) titleId(8) functionCount(4)
//   For each function:
//     ppcAddress(4) ppcSize(4) codeAlignment(4)
//     entryPointCount(4) [ppcAddress(4) nativeOffset(4)] ...
//     machineCodeSize(4) machineCodeBytes(...)
//     relocationCount(4) [offsetInCode(4) type(1) symbolName(4+N)] ...

bool AOTCache::SaveToFile(const std::filesystem::path& path) const
{
	std::vector<uint8_t> buf;
	buf.reserve(1024 * 1024); // 1MB initial reservation

	// Header
	WriteU32(buf, header.magic);
	WriteU32(buf, header.version);
	WriteU64(buf, header.titleId);
	WriteU32(buf, header.functionCount);

	// Functions
	for (const auto& func : functions)
	{
		WriteU32(buf, func.ppcAddress);
		WriteU32(buf, func.ppcSize);
		WriteU32(buf, func.codeAlignment);

		// Entry points
		WriteU32(buf, static_cast<uint32_t>(func.entryPoints.size()));
		for (const auto& ep : func.entryPoints)
		{
			WriteU32(buf, ep.ppcAddress);
			WriteU32(buf, ep.nativeOffset);
		}

		// Machine code
		WriteBytes(buf, func.machineCode);

		// Relocations
		WriteU32(buf, static_cast<uint32_t>(func.relocations.size()));
		for (const auto& reloc : func.relocations)
		{
			WriteU32(buf, reloc.offsetInCode);
			WriteU8(buf, static_cast<uint8_t>(reloc.type));
			WriteString(buf, reloc.symbolName);
		}
	}

	std::ofstream file(path, std::ios::binary);
	if (!file.is_open())
		return false;

	file.write(reinterpret_cast<const char*>(buf.data()), buf.size());
	return file.good();
}

bool AOTCache::LoadFromFile(const std::filesystem::path& path)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file.is_open())
		return false;

	auto fileSize = file.tellg();
	file.seekg(0);

	std::vector<uint8_t> buf(fileSize);
	file.read(reinterpret_cast<char*>(buf.data()), fileSize);
	if (!file.good())
		return false;

	const uint8_t* ptr = buf.data();
	const uint8_t* end = ptr + buf.size();

	// Header
	if (!ReadU32(ptr, end, header.magic)) return false;
	if (header.magic != AOT_CACHE_MAGIC) return false;
	if (!ReadU32(ptr, end, header.version)) return false;
	if (header.version != AOT_CACHE_VERSION) return false;
	if (!ReadU64(ptr, end, header.titleId)) return false;
	if (!ReadU32(ptr, end, header.functionCount)) return false;

	// Functions
	functions.clear();
	functions.reserve(header.functionCount);
	for (uint32_t i = 0; i < header.functionCount; i++)
	{
		AOTFunctionRecord func;
		if (!ReadU32(ptr, end, func.ppcAddress)) return false;
		if (!ReadU32(ptr, end, func.ppcSize)) return false;
		if (!ReadU32(ptr, end, func.codeAlignment)) return false;

		// Entry points
		uint32_t epCount;
		if (!ReadU32(ptr, end, epCount)) return false;
		func.entryPoints.reserve(epCount);
		for (uint32_t j = 0; j < epCount; j++)
		{
			AOTEntryPoint ep;
			if (!ReadU32(ptr, end, ep.ppcAddress)) return false;
			if (!ReadU32(ptr, end, ep.nativeOffset)) return false;
			func.entryPoints.push_back(ep);
		}

		// Machine code
		uint32_t codeSize;
		if (!ReadU32(ptr, end, codeSize)) return false;
		if (!ReadBytes(ptr, end, func.machineCode, codeSize)) return false;

		// Relocations
		uint32_t relocCount;
		if (!ReadU32(ptr, end, relocCount)) return false;
		func.relocations.reserve(relocCount);
		for (uint32_t j = 0; j < relocCount; j++)
		{
			AOTRelocationEntry reloc;
			if (!ReadU32(ptr, end, reloc.offsetInCode)) return false;
			uint8_t type;
			if (!ReadU8(ptr, end, type)) return false;
			reloc.type = static_cast<AOTRelocationType>(type);
			if (!ReadString(ptr, end, reloc.symbolName)) return false;
			func.relocations.push_back(std::move(reloc));
		}

		functions.push_back(std::move(func));
	}

	return true;
}
