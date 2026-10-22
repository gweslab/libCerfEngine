#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../a64_emit_memory.h"
#include "../../emit_fault.h"
#include "a64_lower_fns.h"
#include "a64_memory_operands.h"
#include "a64_soft_tlb.h"

namespace cerfengine::cerfir {

namespace {

constexpr ptrdiff_t kWordBytes = static_cast<ptrdiff_t>(A64SoftTlb::kStampMaxBytes) + 64;

A64MemoryTail& PlaceWordTail(A64Lowering& l, const MemAttr& m, const WordList& w, uint8_t* align,
                             bool back_out) {
    A64MemoryTail& t = l.Tails().New();
    if (m.writeback_slot != kNoSlot) {
        t.wb = A64Operand{A64OperandKind::kRecompute, 0u, m.base_slot, w.wb_add};
    }
    A64PrepareMemoryTail(l, t, m, w.store);
    t.desc.back_out = back_out;
    t.align[0]      = align;
    return t;
}

}

void A64EmitTransferWords(A64Lowering& l, uint32_t attr, uint32_t list) {
    if (l.Target().model->memory != MemoryModelKind::kSoftTlb) {
        throw EmitFault("CerfIR A64 lowering: a word transfer needs the soft-TLB memory model");
    }
    const Block&    b  = l.GetBlock();
    const MemAttr&  m  = b.MemAttrAt(attr);
    const WordList& w  = b.WordListAt(list);
    A64RegAlloc&    ra = l.Regs();
    if (!l.HasRoomFor(kWordBytes * (w.count + 1))) throw A64ArenaFull{};

    ra.BeforeFlagsClobber();
    if (!ra.Quiet()) {
        if (!w.store) {
            for (uint32_t k = 0; k < w.count; ++k) ra.BeforeSlotWrite(w.slot[k], 4u);
        }
        if (m.writeback_slot != kNoSlot) ra.BeforeSlotWrite(m.writeback_slot, 4u);
    }
    ra.ClaimFixed(kA64AddressReg);
    ra.Evict(kA64LookupClobberRegs);

    A64PinnedSlots& p = l.Pins();
    uint8_t*        c = l.Cursor();
    if (!w.store) {
        for (uint32_t k = 0; k < w.count; ++k) {
            if (w.slot[k] != kNoSlot) p.Unstale(c, w.slot[k]);
        }
    }
    p.Load(c, kA64AddressReg, m.base_slot);
    A64AddOffset(c, kA64AddressReg, m.offset);
    if (m.align == AlignPolicy::kIgnoreLowBits) {
        a64::EmitLogicImm(c, a64::kAnd, kA64AddressReg, kA64AddressReg, 0xFFFFFFFCu);
    }
    if (A64InlineLookup(l, m)) p.Flush(c);
    uint8_t*    align = nullptr;
    A64PinState align_state{};
    if (m.slow_mask != 0u) {
        if (a64::LogicImmFits(m.slow_mask, false)) {
            a64::EmitLogicImm(c, a64::kAnds, a64::kZr, kA64AddressReg, m.slow_mask);
        } else {
            a64::EmitMovImm32(c, kA64Tmp1, m.slow_mask);
            a64::EmitTstReg(c, kA64AddressReg, kA64Tmp1);
        }
        align_state = p.State();
        align       = a64::EmitBCondLabel(c, a64::kNe);
    }

    const TlbAccess access = w.store ? TlbAccess::kWrite : TlbAccess::kRead;
    uint8_t*        sites[kMaxWords];
    A64PinState     site_state[kMaxWords];
    int32_t         at = 0;
    for (uint32_t k = 0; k < w.count; ++k) {
        if (w.offset[k] != at) {
            A64AddOffset(c, kA64AddressReg, w.offset[k] - at);
            at = w.offset[k];
        }
        c             = A64EmitTlbLookup(l, c, m, access);
        site_state[k] = p.State();
        sites[k]      = a64::EmitCbzLabel(c, kA64HostReg, false, true);
        if (!w.store) {
            a64::EmitLdrW(c, kA64ValueReg, kA64HostReg, 0);
            p.Store(c, kA64ValueReg, w.slot[k]);
        } else if (w.slot[k] == kNoSlot) {
            a64::EmitMovImm32(c, kA64ValueReg, w.imm[k]);
            a64::EmitStrW(c, kA64ValueReg, kA64HostReg, 0);
        } else {
            p.Load(c, kA64ValueReg, w.slot[k]);
            a64::EmitStrW(c, kA64ValueReg, kA64HostReg, 0);
        }
    }
    l.Cursor() = c;

    ra.ReleaseTemp(kA64AddressReg);
    l.ClearFlagSource();
    A64MemoryTail& first = PlaceWordTail(l, m, w, align, m.back_out);
    if (align != nullptr) A64MemoryTails::RecordBranch(first, align_state);
    if (w.count != 0u) {
        A64MemoryTails::RecordBranch(first, site_state[0]);
        first.abort[first.abort_count++] = sites[0];
    }
    if (w.count <= 1u) return;
    A64MemoryTail& rest = PlaceWordTail(l, m, w, nullptr, false);
    for (uint32_t k = 1; k < w.count; ++k) {
        A64MemoryTails::RecordBranch(rest, site_state[k]);
        rest.abort[rest.abort_count++] = sites[k];
    }
}

}
