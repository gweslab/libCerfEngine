#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* VTRN / VUZP / VZIP (A8.8.420 / A8.8.422 / A8.8.423). */
class ArmNeon2RegShuffle : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    static constexpr uint32_t kTrn = 0u;
    static constexpr uint32_t kUzp = 1u;
    static constexpr uint32_t kZip = 2u;

    void HandleShuffle(uint32_t op_sel, uint32_t esize, uint32_t Q,
                       uint32_t d_idx, uint32_t m_idx);

    static void CERFENGINE_CDECL HandleShuffleHelper(ArmNeon2RegShuffle* svc,
                                                     uint32_t op_sel, uint32_t esize,
                                                     uint32_t Q, uint32_t d_idx,
                                                     uint32_t m_idx);
};

}
