#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <cerfengine/force_inline.h>
#include "../cerfir_block.h"
#include "../cerfir_guest_model.h"
#include "../cerfir_types.h"
#include "a64_operand.h"
#include "a64_pinned_slots.h"

namespace cerfengine::cerfir {

class A64Lowering;

struct A64MemoryTail {
    MemorySlowDesc desc;
    SlowEntries    entries;
    A64Operand     wb;
    A64Operand     value;
    uint8_t*       align[2];
    uint8_t*       abort[kMaxWords];
    uint32_t       abort_count;
    uint8_t*       resume;
    bool           push_value;
    bool           complete;
    bool           pin_recorded;
    uint32_t       pin_dirty;
    uint32_t       pin_stale;
    uint32_t       pin_resume_live;
};

struct A64MissStub {
    uint8_t*    call;
    const void* target;
    uint32_t    pin_dirty;
};

class A64MemoryTails {
public:
    void BeginBlock() {
        count_      = 0;
        stub_count_ = 0;
    }

    CERFENGINE_FORCEINLINE A64MemoryTail& New() {
        if (count_ == capacity_) Grow();
        A64MemoryTail& t  = tails_[count_++];
        t.wb              = A64Operand{};
        t.value           = A64Operand{};
        t.align[0]        = nullptr;
        t.align[1]        = nullptr;
        t.abort_count     = 0u;
        t.resume          = nullptr;
        t.pin_recorded    = false;
        t.pin_dirty       = 0u;
        t.pin_stale       = 0u;
        t.pin_resume_live = 0u;
        return t;
    }

    CERFENGINE_FORCEINLINE void AddMissStub(uint8_t* call, const void* target, uint32_t pin_dirty) {
        if (stub_count_ == stubs_.size()) stubs_.resize(stubs_.empty() ? 16u : stubs_.size() * 2u);
        stubs_[stub_count_++] = A64MissStub{call, target, pin_dirty};
    }

    static void RecordBranch(A64MemoryTail& t, const A64PinState& pins);

    bool Emit(A64Lowering& lowering);

private:
    void Grow();
    void EmitOne(A64Lowering& lowering, const A64MemoryTail& tail);
    void EmitMissStub(A64Lowering& lowering, const A64MissStub& stub);

    std::vector<A64MemoryTail> tails_;
    size_t                     count_    = 0;
    size_t                     capacity_ = 0;
    std::vector<A64MissStub>   stubs_;
    size_t                     stub_count_ = 0;
};

}
