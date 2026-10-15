#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

class ArmNeonVtbl : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    static constexpr uint32_t kTbl = 0u;
    static constexpr uint32_t kTbx = 1u;

    void HandleVtbl(uint32_t op_sel, uint32_t d_idx, uint32_t n_idx,
                    uint32_t m_idx, uint32_t length);

    static void CERFENGINE_CDECL HandleVtblHelper(ArmNeonVtbl* svc,
                                                  uint32_t     op_sel,
                                                  uint32_t     d_idx,
                                                  uint32_t     n_idx,
                                                  uint32_t     m_idx,
                                                  uint32_t     length);
};

}
