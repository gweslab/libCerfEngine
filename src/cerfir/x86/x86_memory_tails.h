#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <cerfengine/byte_order.h>

#include <cerfengine/force_inline.h>
#include "../cerfir_guest_model.h"
#include "../cerfir_types.h"
#include "x86_operand.h"

namespace cerfengine::cerfir {

class X86Lowering;

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
    void BeginBlock() { count_ = 0; }

    CERFENGINE_FORCEINLINE X86MemoryTail& New() {
        if (count_ == capacity_) Grow();
        X86MemoryTail& t = tails_[count_++];
        t.wb       = X86Operand{};
        t.value    = X86Operand{};
        t.align[0] = nullptr;
        t.align[1] = nullptr;
        return t;
    }

    bool Emit(X86Lowering& lowering);

private:
    void Grow();
    void EmitOne(X86Lowering& lowering, const X86MemoryTail& tail);

    std::vector<X86MemoryTail> tails_;
    size_t                     count_    = 0;
    size_t                     capacity_ = 0;
};

}
