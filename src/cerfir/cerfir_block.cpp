#include "cerfir_block.h"

#include <algorithm>
#include <cstdio>

#include "../emit_fault.h"

namespace cerfengine::cerfir {

void BlockLimitExceeded(const char* what) {
    char text[96];
    std::snprintf(text, sizeof(text), "CerfIR block exceeds its %s limit", what);
    throw EmitFault(text);
}

void Block::GrowConsts() {
    if (const_capacity_ >= kMaxConsts) BlockLimitExceeded("constant");
    const_capacity_ = std::min<uint32_t>(const_capacity_ == 0u ? 256u : const_capacity_ * 2u, kMaxConsts);
    consts_.resize(const_capacity_);
}

void Block::GrowSlotTests() {
    if (slot_tests_.size() > 0xFFFFu) BlockLimitExceeded("slot test");
    slot_test_capacity_ = GrowSideTable(slot_tests_);
}

}
