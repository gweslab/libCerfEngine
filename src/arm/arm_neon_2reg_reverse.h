#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* VREV16/VREV32/VREV64 (A8.8.386). Per-element shuffle: dst[i] = src[i XOR
   (groupsize-1)] within each groupsize-wide window of elements. */
class ArmNeon2RegReverse : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void HandleRev(uint32_t op, uint32_t d_idx, uint32_t m_idx,
                   uint32_t esize, uint32_t groupsize, uint32_t regs);

    static void CERFENGINE_CDECL HandleRevHelper(ArmNeon2RegReverse* svc, uint32_t op,
                                                 uint32_t d_idx, uint32_t m_idx,
                                                 uint32_t esize, uint32_t groupsize,
                                                 uint32_t regs);
};

}
