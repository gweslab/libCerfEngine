#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* VEXT - A8.8.316, byte-wise concat-and-select from (Dm:Dn) or
   (Qm:Qn). U=0, bit23=1, bits[21:20]=11, bit[4]=0. */
class ArmNeonVext : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void HandleVext(uint32_t d_idx, uint32_t n_idx, uint32_t m_idx,
                    uint32_t imm4_bytes, uint32_t q);

    static void CERFENGINE_CDECL HandleVextHelper(ArmNeonVext* svc,
                                                  uint32_t     d_idx,
                                                  uint32_t     n_idx,
                                                  uint32_t     m_idx,
                                                  uint32_t     imm4_bytes,
                                                  uint32_t     q);
};

}
