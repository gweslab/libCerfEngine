#include "x86_memory_tails.h"

#include <cstring>

#include <cerfengine/byte_order.h>

#include "../../emit_fault.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
#include "../../x86_emit_pointer.h"
#include "x86_host_abi.h"
#include "x86_lowering.h"
#include "x86_memory_operands.h"

namespace cerfengine::cerfir {

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
        EmitMovPtrRegImm(c, kEax, t.entries.align);
        to_common = EmitJmpLabel(c);
    }
    for (uint8_t* site = t.abort; site != nullptr;) {
        uint8_t* const next = X86NextChainedSite(site);
        FixupLabel32(site, c);
        site = next;
    }
    if (align) {
        EmitMovPtrRegImm(c, kEax, t.entries.abort);
        FixupLabel(to_common, c);
    }
    if (t.push_value && t.value.kind == X86OperandKind::kRecompute) X86SlowOperandFault();
    uint8_t* desc_field = nullptr;
    if constexpr (kX64) {
        X86RequireKnownCallAbi();
        const bool has_wb = t.wb.kind != X86OperandKind::kNone;
        EmitMovRegReg(c, kX86ArgRegs[0], kEcx);
        if (t.push_value) {
            X86LoadSlowOperand(c, t.value, kX86ArgRegs[2]);
            if (has_wb) X86LoadSlowOperand(c, t.wb, kX86ArgRegs[3]);
        } else if (has_wb) {
            X86LoadSlowOperand(c, t.wb, kX86ArgRegs[2]);
        }
        desc_field = EmitLeaRipRelative(c, kX86ArgRegs[1]);
    } else {
        if (t.wb.kind != X86OperandKind::kNone) X86PushSlowOperand(c, t.wb);
        if (t.push_value) X86PushSlowOperand(c, t.value);
        EmitMovRegImm32(c, kEdx, 0u);
        desc_field = c - 4;
    }
    if (align) {
        EmitCallReg(c, kEax);
    } else {
        EmitCall(c, t.entries.abort);
    }
    if (!t.complete) {
        if constexpr (kX64) {
            EmitMovPtrRegReg(c, kEdx, kEax);
            EmitShrReg64Imm(c, kEdx, 32u);
        }
        EmitTestRegReg(c, kEax, kEax);
        FixupLabel32(EmitJzLabel32(c), t.resume);
    }
    EmitRet(c);

    c += (0u - reinterpret_cast<uintptr_t>(c)) & (alignof(MemorySlowDesc) - 1u);
    std::memcpy(c, &t.desc, sizeof(MemorySlowDesc));
    if constexpr (kX64) {
        le::Put32(desc_field, static_cast<uint32_t>(c - (desc_field + 4)));
    } else {
        le::Put32(desc_field, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(c)));
    }
    l.Cursor() = c + sizeof(MemorySlowDesc);
}

}
