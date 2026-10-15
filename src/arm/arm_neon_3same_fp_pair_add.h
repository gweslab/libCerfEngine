#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* VPADD (floating-point) - A8.8.363, opc=1101 C=0 U=1 bit[21]=0. */
class ArmNeon3SameFpPairAdd : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void Handle3SameFpPairAdd(uint32_t d_idx, uint32_t n_idx, uint32_t m_idx);

    static void CERFENGINE_CDECL Handle3SameFpPairAddHelper(ArmNeon3SameFpPairAdd* svc,
                                                            uint32_t d_idx,
                                                            uint32_t n_idx,
                                                            uint32_t m_idx);
};

}
