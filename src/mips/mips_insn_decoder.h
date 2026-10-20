#pragma once

#include <cstdint>

#include <cerfengine/engine.h>
#include "mips16_decoder.h"
#include "mips_block_context.h"
#include "mips_decoder.h"

namespace cerfengine {

struct MipsCpuState;

class MipsMmu;
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
    struct FetchPage {
        uint32_t va   = 1u;
        uint32_t pa   = 0u;
        uint8_t* host = nullptr;
    };

    bool Fetch(MipsBlockContext* ctx, uint32_t va, uint32_t page_mask, bool record_fault,
               FetchPage& page, const uint8_t** host, uint32_t* pa);
    bool FetchPageOf(MipsBlockContext* ctx, uint32_t va, uint32_t page_mask, bool record_fault,
                     FetchPage& page);
    static uint32_t CloseDelaySlot(MipsBlockContext* ctx, uint32_t count, bool delay_pending,
                                   bool slot_decoded);

    MipsDecoder   decoder_;
    Mips16Decoder m16_decoder_;

    MipsCpuState* cpu_state_ = nullptr;
    MipsMmu*      mmu_       = nullptr;
    PhysicalMap*  map_       = nullptr;
};

}
