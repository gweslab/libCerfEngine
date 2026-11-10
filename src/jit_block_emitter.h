#pragma once

#include <cstddef>
#include <cstdint>

#include <cerfengine/engine.h>
#include <cerfengine/isa_block_space.h>

namespace cerfengine {

class JitCodeArena;
struct JitBlock;

struct BlockEmitIndex {
    IsaBlockSpace* space;
    BlockIndexSet* set;
    JitBlockIndex* idx;
    bool           outer_global;
    uint8_t        asid;
    uint32_t       key;
    uint32_t       split;
    uint32_t       split_key;
    uint32_t       split_pa;
    uint32_t       predecessor_va;
    bool           cache_split_native;
};

struct BlockEmitRequest {
    JitCodeArena*         arena;
    const BlockEmitIndex* index;
    uint32_t              guest_pc;
    uint32_t              key_start;
    uint32_t              guest_end;
    uint32_t              phys_start;
    uint32_t              privilege;
    uint32_t              num_insns;
    uint64_t              start_ns;
};

class BlockEmitTarget {
public:
    virtual size_t Generate(JitBlock* stored, uint8_t* code, uint8_t* code_end) = 0;
    virtual void   FlushCache() = 0;

protected:
    ~BlockEmitTarget() = default;
};

class JitBlockEmitter : public EngineService {
public:
    using EngineService::EngineService;

    void OnReady() override;

    uint8_t* Emit(const BlockEmitRequest& request, BlockEmitTarget& target);

private:
    size_t GenerateChecked(const BlockEmitRequest& request, BlockEmitTarget& target, JitBlock* stored,
                           uint8_t* code, uint8_t* code_end);

    EngineHost* host_            = nullptr;
    bool        report_compiles_ = false;
};

}
