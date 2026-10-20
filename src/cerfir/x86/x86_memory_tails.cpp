#include "x86_memory_tails.h"

#include <cstring>

#include <cerfengine/byte_order.h>

#include "../../emit_fault.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
#include "x86_lowering.h"
#include "x86_memory_operands.h"

namespace cerfengine::cerfir {

namespace {

uint32_t Address(const void* p) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

}

void X86MemoryTails::Grow() {
    tails_.resize(tails_.empty() ? 16u : tails_.size() * 2u);
    capacity_ = tails_.size();
}

bool X86MemoryTails::Emit(X86Lowering& l) {
    for (size_t i = 0; i < count_; ++i) {
        if (!l.HasRoom()) return false;
        EmitOne(l, tails_[i]);
    }
    return true;
}

void X86MemoryTails::EmitOne(X86Lowering& l, const X86MemoryTail& t) {
    using namespace x86;
    uint8_t*   c     = l.Cursor();
    const bool align = t.align[0] != nullptr || t.align[1] != nullptr;

    uint8_t* to_common = nullptr;
    if (align) {
        if (t.align[0] != nullptr) FixupLabel32(t.align[0], c);
        if (t.align[1] != nullptr) FixupLabel32(t.align[1], c);
        EmitMovRegImm32(c, kEax, Address(t.entries.align));
        to_common = EmitJmpLabel(c);
    }
    for (uint8_t* site = t.abort; site != nullptr;) {
        uint8_t* const next = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(le::U32(site)));
        FixupLabel32(site, c);
        site = next;
    }
    if (align) {
        EmitMovRegImm32(c, kEax, Address(t.entries.abort));
        FixupLabel(to_common, c);
    }
    if (t.wb.kind != X86OperandKind::kNone) X86PushSlowOperand(c, t.wb);
    if (t.push_value) {
        if (t.value.kind == X86OperandKind::kRecompute) X86SlowOperandFault();
        X86PushSlowOperand(c, t.value);
    }
    EmitMovRegImm32(c, kEdx, 0u);
    uint8_t* const desc_imm = c - 4;
    if (align) {
        EmitCallReg(c, kEax);
    } else {
        EmitCall(c, t.entries.abort);
    }
    if (!t.complete) {
        EmitTestRegReg(c, kEax, kEax);
        FixupLabel32(EmitJzLabel32(c), t.resume);
    }
    EmitRet(c);

    c += (0u - reinterpret_cast<uintptr_t>(c)) & (alignof(MemorySlowDesc) - 1u);
    std::memcpy(c, &t.desc, sizeof(MemorySlowDesc));
    le::Put32(desc_imm, Address(c));
    l.Cursor() = c + sizeof(MemorySlowDesc);
}

}
