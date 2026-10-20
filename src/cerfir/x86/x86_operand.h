#pragma once

#include <cstdint>

namespace cerfengine::cerfir {

enum class X86OperandKind : uint8_t { kNone, kConst, kSlot, kSpill, kReg, kRecompute };

struct X86Operand {
    X86OperandKind  kind  = X86OperandKind::kNone;
    uint8_t         reg   = 0;
    int32_t         disp  = 0;
    uint32_t        imm   = 0;
    const uint32_t* spill = nullptr;
};

}
