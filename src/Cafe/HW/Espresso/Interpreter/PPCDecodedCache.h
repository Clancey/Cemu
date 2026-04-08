#pragma once

#include "PPCInterpreterInternal.h"

#if TARGET_OS_VISION

// Enumeration of all PPC instruction opcodes for computed goto optimization
enum PPCOpcodeId : uint16 {
    OPC_INVALID = 0,

    // Arithmetic immediate
    OPC_ADDI,
    OPC_ADDIS,
    OPC_ADDIC,
    OPC_ADDIC_,
    OPC_SUBFIC,
    OPC_MULLI,

    // Compare immediate
    OPC_CMPI,
    OPC_CMPLI,

    // Logical immediate
    OPC_ANDI_,
    OPC_ANDIS_,
    OPC_ORI,
    OPC_ORIS,
    OPC_XORI,
    OPC_XORIS,

    // Rotate and shift immediate
    OPC_RLWIMI,
    OPC_RLWINM,
    OPC_RLWNM,

    // Memory - Load/Store Word
    OPC_LWZ,
    OPC_LWZU,
    OPC_STW,
    OPC_STWU,

    // Memory - Load/Store Byte
    OPC_LBZ,
    OPC_LBZU,
    OPC_STB,
    OPC_STBU,

    // Memory - Load/Store Halfword
    OPC_LHZ,
    OPC_LHZU,
    OPC_LHA,
    OPC_LHAU,
    OPC_STH,
    OPC_STHU,

    // Memory - Load/Store Float
    OPC_LFS,
    OPC_LFSU,
    OPC_LFD,
    OPC_LFDU,
    OPC_STFS,
    OPC_STFSU,
    OPC_STFD,
    OPC_STFDU,

    // Memory - Load/Store Multiple
    OPC_LMW,
    OPC_STMW,

    // Branches
    OPC_BX,
    OPC_BCX,
    OPC_BCLRX,
    OPC_BCCTR,

    // Arithmetic register (opcode 31)
    OPC_ADD,
    OPC_ADDC,
    OPC_ADDE,
    OPC_ADDME,
    OPC_ADDZE,
    OPC_SUBF,
    OPC_SUBFC,
    OPC_SUBFE,
    OPC_SUBFME,
    OPC_SUBFZE,
    OPC_NEG,
    OPC_MULLW,
    OPC_MULHW,
    OPC_MULHWU,
    OPC_DIVW,
    OPC_DIVWU,

    // Logical register (opcode 31)
    OPC_ANDX,
    OPC_ANDCX,
    OPC_OR,
    OPC_ORC,
    OPC_XOR,
    OPC_NANDX,
    OPC_NORX,
    OPC_EQV,

    // Compare register (opcode 31)
    OPC_CMP,
    OPC_CMPL,

    // Shift register (opcode 31)
    OPC_SLWX,
    OPC_SRWX,
    OPC_SRAW,
    OPC_SRAWI,

    // Count leading zeros
    OPC_CNTLZW,

    // Sign extend
    OPC_EXTSB,
    OPC_EXTSH,

    // Load/Store indexed (opcode 31)
    OPC_LWZX,
    OPC_LWZXU,
    OPC_STWX,
    OPC_STWUX,
    OPC_LBZX,
    OPC_LBZXU,
    OPC_STBX,
    OPC_STBUX,
    OPC_LHZX,
    OPC_LHZXU,
    OPC_LHAX,
    OPC_LHAUX,
    OPC_STHX,
    OPC_STHUX,
    OPC_LWARX,
    OPC_STWCX,

    // Special Purpose Register access
    OPC_MFSPR,
    OPC_MTSPR,
    OPC_MFCR,
    OPC_MTCRF,
    OPC_MFMSR,
    OPC_MTMSR,

    // Time Base access
    OPC_MFTB,

    // Cache operations
    OPC_DCBF,
    OPC_DCBST,
    OPC_DCBT,
    OPC_DCBI,
    OPC_DCBZ,
    OPC_DCBZL,
    OPC_ICBI,

    // Sync operations
    OPC_SYNC,
    OPC_ISYNC,
    OPC_EIEIO,

    // System call
    OPC_SC,

    // Float operations (opcode 59)
    OPC_FADDS,
    OPC_FSUBS,
    OPC_FMULS,
    OPC_FDIVS,
    OPC_FRES,
    OPC_FMADDS,
    OPC_FMSUBS,
    OPC_FNMADDS,
    OPC_FNMSUBS,

    // Float operations (opcode 63)
    OPC_FADD,
    OPC_FSUB,
    OPC_FMUL,
    OPC_FDIV,
    OPC_FMADD,
    OPC_FMSUB,
    OPC_FNMADD,
    OPC_FNMSUB,
    OPC_FABS,
    OPC_FNABS,
    OPC_FNEG,
    OPC_FMR,
    OPC_FRSP,
    OPC_FCTIWZ,
    OPC_FCTIW,
    OPC_FCMPU,
    OPC_FCMPO,
    OPC_FSEL,
    OPC_FRSQRTE,
    OPC_MFFS,
    OPC_MTFSF,

    // Condition register operations (opcode 19)
    OPC_MCRF,
    OPC_CRAND,
    OPC_CRANDC,
    OPC_CREQV,
    OPC_CRNAND,
    OPC_CRNOR,
    OPC_CROR,
    OPC_CRORC,
    OPC_CRXOR,

    // Generic fallback for uncommon instructions
    OPC_GENERIC,

    OPC_COUNT
};

// Phase 2: Decoded instruction using opcode ID for computed goto
struct DecodedInstr {
    PPCOpcodeId opcodeId;
    uint32 ppcAddress;

    DecodedInstr() : opcodeId(OPC_INVALID), ppcAddress(0) {}
};

// Size constants
static constexpr uint32 DECODED_CACHE_SIZE = 0x2000000;  // 32MB address space / 4 = 8M instructions
static constexpr uint32 DECODED_CACHE_MASK = DECODED_CACHE_SIZE - 1;

// Global decoded instruction cache
extern DecodedInstr g_decodedCache[DECODED_CACHE_SIZE];

// Function declarations
PPCOpcodeId PPCDecodedCache_decodeOpcodeId(uint32 opcode);
void PPCDecodedCache_executeInstruction(PPCInterpreter_t* hCPU);

#endif // TARGET_OS_VISION