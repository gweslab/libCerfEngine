#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <cerfengine/byte_order.h>

#include <cerfengine/force_inline.h>
#include "../cerfir_guest_model.h"
#include "../cerfir_types.h"
#include "x86_operand.h"
#include "x86_soft_tlb.h"

namespace cerfengine::cerfir {

class X86Lowering;

struct X86MissStub {
    SoftTlbMissSites misses;
    const void*  target;
    uint8_t*     resume;
    uint32_t     pc;
    bool         keep_pc;
};

struct X86ColdTransfer {
    uint32_t         attr;
    uint32_t         list;
    uint8_t*         entry;
    uint8_t*         align;
    uint8_t*         join;
    SoftTlbMissSites misses;
    const void*      miss_target;
    uint32_t         pc;
    bool             keep_pc;
};

struct X86MemoryTail {
    MemorySlowDesc desc;
    SlowEntries    entries;
    X86Operand     wb;
    X86Operand     value;
    uint8_t*       align[2];
    uint8_t*       abort;
    uint8_t*       resume;
    bool           push_value;
    bool           complete;
};

CERFENGINE_FORCEINLINE void X86ChainLabelSite(uint8_t*& head, uint8_t* site) {
    le::Put32(site, head != nullptr ? static_cast<uint32_t>(site - head) : 0u);
    head = site;
}

CERFENGINE_FORCEINLINE uint8_t* X86NextChainedSite(const uint8_t* site) {
    const uint32_t back = le::U32(site);
    return back != 0u ? const_cast<uint8_t*>(site) - back : nullptr;
}

class X86MemoryTails {
public:
    void BeginBlock() {
        count_      = 0;
        stub_count_ = 0;
        cold_count_ = 0;
    }

    void AddColdTransfer(const X86ColdTransfer& cold) {
        if (cold_count_ == colds_.size()) colds_.resize(colds_.empty() ? 8u : colds_.size() * 2u);
        colds_[cold_count_++] = cold;
    }

    CERFENGINE_FORCEINLINE X86MemoryTail& New() {
        if (count_ == capacity_) Grow();
        X86MemoryTail& t = tails_[count_++];
        t.wb       = X86Operand{};
        t.value    = X86Operand{};
        t.align[0] = nullptr;
        t.align[1] = nullptr;
        return t;
    }

    CERFENGINE_FORCEINLINE void AddMissStub(const SoftTlbMissSites& misses, const void* target,
                                            uint8_t* resume, uint32_t pc, bool keep_pc) {
        if (stub_count_ == stubs_.size()) stubs_.resize(stubs_.empty() ? 16u : stubs_.size() * 2u);
        stubs_[stub_count_++] = X86MissStub{misses, target, resume, pc, keep_pc};
    }

    bool Emit(X86Lowering& lowering);

private:
    void Grow();
    void EmitOne(X86Lowering& lowering, const X86MemoryTail& tail);
    void EmitMissStub(X86Lowering& lowering, const X86MissStub& stub);

    std::vector<X86MemoryTail> tails_;
    size_t                     count_    = 0;
    size_t                     capacity_ = 0;
    std::vector<X86MissStub>   stubs_;
    size_t                     stub_count_ = 0;
    std::vector<X86ColdTransfer> colds_;
    size_t                       cold_count_ = 0;
};

}
