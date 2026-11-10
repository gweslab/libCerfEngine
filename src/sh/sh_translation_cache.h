#pragma once

#include <cstdint>

#include <cerfengine/engine.h>
#include <cerfengine/isa_block_space.h>
#include "../dram_host_regions.h"
#include "../jit_code_arena.h"

namespace cerfengine {

class PhysicalMap;
struct ShCpuState;

class ShTranslationCache : public EngineService {
public:
    using EngineService::EngineService;

    bool ShouldRegister() override { return engine_.GetSetup().isa == Isa::kSh; }
    void OnReady() override;

    JitCodeArena&  Arena() { return arena_; }
    IsaBlockSpace& Space() { return blocks_; }

    void Flush();

    uint32_t BlockIndexKey(uint32_t phys_start, uint32_t guest_pc);
    void     InvalidateOnRamWrite(const uint8_t* host, uint32_t bytes);
    void     InvalidateBusMasterWrite(const uint8_t* host, uint32_t bytes, bool block_executing);

private:
    bool TryBlockIndexKey(uint32_t pa, uint32_t* key);
    void RemoveWritten(const uint8_t* host, uint32_t bytes, bool block_executing);

    JitCodeArena  arena_;
    IsaBlockSpace blocks_;
    DramHostTable dram_;
    PhysicalMap*  map_       = nullptr;
    ShCpuState*   cpu_state_ = nullptr;
};

}
