#include "PPCDecodedCache.h"

#if TARGET_OS_VISION

// Global cache storage
DecodedInstr g_decodedCache[DECODED_CACHE_SIZE];

#define PPC_getBits(v, start, count) (((v) >> (31 - (start))) & ((1 << (count)) - 1))

PPCOpcodeId PPCDecodedCache_decodeOpcodeId(uint32 opcode)
{
    uint32 primaryOpcode = opcode >> 26;

    switch (primaryOpcode)
    {
        case 7:  return OPC_MULLI;
        case 8:  return OPC_SUBFIC;
        case 10: return OPC_CMPLI;
        case 11: return OPC_CMPI;
        case 12: return OPC_ADDIC;
        case 13: return OPC_ADDIC_;
        case 14: return OPC_ADDI;
        case 15: return OPC_ADDIS;
        case 16: return OPC_BCX;
        case 17: return OPC_SC;
        case 18: return OPC_BX;
        case 20: return OPC_RLWIMI;
        case 21: return OPC_RLWINM;
        case 23: return OPC_RLWNM;
        case 24: return OPC_ORI;
        case 25: return OPC_ORIS;
        case 26: return OPC_XORI;
        case 27: return OPC_XORIS;
        case 28: return OPC_ANDI_;
        case 29: return OPC_ANDIS_;
        case 32: return OPC_LWZ;
        case 33: return OPC_LWZU;
        case 34: return OPC_LBZ;
        case 35: return OPC_LBZU;
        case 36: return OPC_STW;
        case 37: return OPC_STWU;
        case 38: return OPC_STB;
        case 39: return OPC_STBU;
        case 40: return OPC_LHZ;
        case 41: return OPC_LHZU;
        case 42: return OPC_LHA;
        case 43: return OPC_LHAU;
        case 44: return OPC_STH;
        case 45: return OPC_STHU;
        case 46: return OPC_LMW;
        case 47: return OPC_STMW;
        case 48: return OPC_LFS;
        case 49: return OPC_LFSU;
        case 50: return OPC_LFD;
        case 51: return OPC_LFDU;
        case 52: return OPC_STFS;
        case 53: return OPC_STFSU;
        case 54: return OPC_STFD;
        case 55: return OPC_STFDU;
        default: return OPC_GENERIC;
    }
}

void PPCDecodedCache_init()
{
    memset(g_decodedCache, 0, sizeof(g_decodedCache));
}

void PPCDecodedCache_invalidateRange(uint32 startAddr, uint32 endAddr)
{
    uint32 startIdx = (startAddr >> 2) & DECODED_CACHE_MASK;
    uint32 endIdx = (endAddr >> 2) & DECODED_CACHE_MASK;
    if (startIdx <= endIdx)
        memset(&g_decodedCache[startIdx], 0, (endIdx - startIdx + 1) * sizeof(DecodedInstr));
}

#endif // TARGET_OS_VISION
