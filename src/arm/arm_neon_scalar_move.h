#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

/* VMOV (ARM core register to scalar) / VMOV (scalar to ARM core
   register) - A8.8.341 / A8.8.342, esize=8 and esize=16 lane
   transfers. */
class ArmNeonScalarMove : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void HandleCoreToScalar(uint32_t d_idx, uint32_t lane_index,
                            uint32_t esize, uint32_t rt_index);

    void HandleScalarToCore(uint32_t n_idx, uint32_t lane_index,
                            uint32_t esize, uint32_t unsigned_ext,
                            uint32_t rt_index);

    static void CERFENGINE_CDECL HandleCoreToScalarHelper(ArmNeonScalarMove* svc,
                                                          uint32_t           d_idx,
                                                          uint32_t           lane_index,
                                                          uint32_t           esize,
                                                          uint32_t           rt_index);

    static void CERFENGINE_CDECL HandleScalarToCoreHelper(ArmNeonScalarMove* svc,
                                                          uint32_t           n_idx,
                                                          uint32_t           lane_index,
                                                          uint32_t           esize,
                                                          uint32_t           unsigned_ext,
                                                          uint32_t           rt_index);
};

}
