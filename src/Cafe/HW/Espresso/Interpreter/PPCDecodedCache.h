#pragma once

#include "PPCInterpreterInternal.h"

#if TARGET_OS_VISION

// Handler function pointer type - matches existing interpreter handlers
typedef void (*PPCDecodedHandler)(PPCInterpreter_t* hCPU, uint32 opcode);

// Decoded instruction entry
struct DecodedInstr {
    PPCDecodedHandler handler;
    // Future: could add pre-extracted operands here for Phase 2 optimization

    DecodedInstr() : handler(nullptr) {}
};

// Size constants
static constexpr uint32 DECODED_CACHE_SIZE = 0x2000000;  // 32MB address space / 4 = 8M instructions
static constexpr uint32 DECODED_CACHE_MASK = DECODED_CACHE_SIZE - 1;

// Global decoded instruction cache
extern DecodedInstr g_decodedCache[DECODED_CACHE_SIZE];

// Function declarations
PPCDecodedHandler PPCDecodedCache_decodeInstruction(uint32 opcode);
void PPCDecodedCache_executeInstruction(PPCInterpreter_t* hCPU);

#endif // TARGET_OS_VISION