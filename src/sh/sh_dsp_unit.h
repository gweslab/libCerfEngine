#pragma once

#include <cstdint>

#include <cerfengine/calling_convention.h>
#include <cerfengine/engine.h>

#include "sh_dsp_encoding.h"

namespace cerfengine {

class ShDspAlu;
struct ShCpuState;
struct ShDspOutcome;

class ShDspUnit : public EngineService {
public:
    using EngineService::EngineService;

    bool ShouldRegister() override { return engine_.GetSetup().isa == Isa::kSh; }
    void OnReady() override;

    static void CERFENGINE_CDECL ParallelHelper(ShDspUnit* unit, uint32_t code, uint32_t operation);
    static void CERFENGINE_CDECL TransferHelper(ShDspUnit* unit, uint32_t field_a);

private:
    uint32_t NextPointer(uint32_t pointer, ShDspAddressing mode, bool x) const;
    void     UpdatePointer(uint32_t field_a, bool x);
    bool     LoadConflicts(uint32_t field_a, bool x, const ShDspOutcome& outcome) const;
    void     ExecuteParallel(uint32_t code, uint32_t operation);

    ShCpuState* state_ = nullptr;
    ShDspAlu*   alu_   = nullptr;
};

}
