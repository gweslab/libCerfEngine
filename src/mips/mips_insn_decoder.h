#pragma once

#include <cstdint>

#include <cerfengine/engine.h>
#include "mips16_decoder.h"
#include "mips_block_context.h"
#include "mips_decoder.h"

namespace cerfengine {

struct MipsCpuState;

class MipsMmu;
class MipsPlaceFnSelector;
class PhysicalMap;

class MipsInsnDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kMips;
    }

    void Decode(MipsBlockContext* ctx, uint32_t guest_pc);
    void Decode16(MipsBlockContext* ctx, uint32_t guest_pc);

private:
    bool Fetch16(MipsBlockContext* ctx, uint32_t va, uint16_t* hw, uint32_t* pa);

    MipsDecoder   decoder_;
    Mips16Decoder m16_decoder_;

    MipsCpuState*        cpu_state_ = nullptr;
    MipsMmu*             mmu_       = nullptr;
    PhysicalMap*         map_       = nullptr;
    MipsPlaceFnSelector* selector_  = nullptr;
};

}
