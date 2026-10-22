#pragma once

#include <cstdint>

namespace cerfengine::cerfir {

enum class A64OperandKind : uint8_t { kNone, kConst, kSlot, kSpill, kReg, kRecompute };

struct A64Operand {
    A64OperandKind kind = A64OperandKind::kNone;
    uint8_t        reg  = 0;
    int32_t        disp = 0;
    uint32_t       imm  = 0;
};

}
