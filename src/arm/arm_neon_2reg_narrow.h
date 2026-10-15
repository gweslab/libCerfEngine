#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* VMOVN / VQMOVUN / VQMOVN.S / VQMOVN.U (A8.8.347 / A8.8.374): op field
   at bits[7:6]. */
class ArmNeon2RegNarrow : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    static constexpr uint32_t kMovn   = 0u;
    static constexpr uint32_t kQmovun = 1u;
    static constexpr uint32_t kQmovnS = 2u;
    static constexpr uint32_t kQmovnU = 3u;

    void HandleNarrow(uint32_t op_sel, uint32_t esize_out,
                      uint32_t d_idx, uint32_t m_idx);

    static void CERFENGINE_CDECL HandleNarrowHelper(ArmNeon2RegNarrow* svc,
                                                    uint32_t op_sel, uint32_t esize_out,
                                                    uint32_t d_idx, uint32_t m_idx);
};

}
