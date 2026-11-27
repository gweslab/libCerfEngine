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

void EmitWordAccess(A64PinnedSlots& p, uint8_t*& c, const WordList& w, uint32_t k, int32_t host_offset) {
    if (!w.store) {
        a64::EmitLdrW(c, kA64ValueReg, kA64HostReg, host_offset);
        p.Store(c, kA64ValueReg, w.slot[k]);
    } else if (w.slot[k] == kNoSlot) {
        a64::EmitMovImm32(c, kA64ValueReg, w.imm[k]);
        a64::EmitStrW(c, kA64ValueReg, kA64HostReg, host_offset);
    } else {
        p.Load(c, kA64ValueReg, w.slot[k]);
        a64::EmitStrW(c, kA64ValueReg, kA64HostReg, host_offset);
    }
}

uint8_t* EmitWordSequence(A64Lowering& l, uint8_t*& c, const MemAttr& m, const WordList& w, int32_t at,
                          uint8_t* align, const A64PinState& align_state) {
    A64PinnedSlots& p      = l.Pins();
    const TlbAccess access = w.store ? TlbAccess::kWrite : TlbAccess::kRead;
    uint8_t*        sites[kMaxWords];
    A64PinState     site_state[kMaxWords];
    uint8_t*        first_found = nullptr;
    for (uint32_t k = 0; k < w.count; ++k) {
        if (w.offset[k] != at) {
            A64AddOffset(c, kA64AddressReg, w.offset[k] - at);
            at = w.offset[k];
        }
        c = A64EmitTlbLookup(l, c, m, access);
        if (k == 0u) first_found = c;
        site_state[k] = p.State();
        sites[k]      = a64::EmitCbzLabel(c, kA64HostReg, false, true);
        EmitWordAccess(p, c, w, k, 0);
    }

    A64MemoryTail& first = PlaceWordTail(l, m, w, align, m.back_out);
    if (align != nullptr) A64MemoryTails::RecordBranch(first, align_state);
    if (w.count != 0u) {
        A64MemoryTails::RecordBranch(first, site_state[0]);
        first.abort[first.abort_count++] = sites[0];
    }
    if (w.count > 1u) {
        A64MemoryTail& rest = PlaceWordTail(l, m, w, nullptr, false);
        for (uint32_t k = 1; k < w.count; ++k) {
            A64MemoryTails::RecordBranch(rest, site_state[k]);
            rest.abort[rest.abort_count++] = sites[k];
        }
    }
    return first_found;
}

bool Batchable(const A64Lowering& l, const MemAttr& m, const WordList& w) {
    if (w.count < 2u || A64InlineLookup(l, m)) return false;
    for (uint32_t k = 1; k < w.count; ++k) {
        if (w.offset[k] < w.offset[0]) return false;
    }
    return true;
}

void EmitBatched(A64Lowering& l, uint8_t*& c, uint32_t attr, uint32_t list, const MemAttr& m,
                 const WordList& w, uint8_t* align, const A64PinState& align_state) {
    const A64SoftTlb& tlb    = *l.Target().tlb;
    const BlockLink&  link   = *l.Target().link;
    A64PinnedSlots&   p      = l.Pins();
    const TlbAccess   access = w.store ? TlbAccess::kWrite : TlbAccess::kRead;

    const int32_t lo = w.offset[0];
    int32_t       hi = lo;
    for (uint32_t k = 1; k < w.count; ++k) {
        if (w.offset[k] > hi) hi = w.offset[k];
    }
    const uint32_t span = static_cast<uint32_t>(hi - lo) + 4u;

    if (lo != 0) A64AddOffset(c, kA64AddressReg, lo);
    SoftTlbMissSites misses;
    c = tlb.EmitLookup(c, link.privilege, access, &misses);
    const A64PinState entry_state = p.State();

    a64::EmitLogicImm(c, a64::kAnd, kA64Tmp1, kA64AddressReg, tlb.PageBytes() - 1u);
    a64::EmitCmpImm(c, kA64Tmp1, tlb.PageBytes() - span);
    uint8_t* const entry = a64::EmitBCondLabel(c, a64::kHi);

    if (w.store) {
        int32_t  delta[kMaxWords];
        uint32_t n = 0;
        for (uint32_t k = 0; k < w.count; ++k) {
            if (w.offset[k] != lo) delta[n++] = (w.offset[k] - lo) / 4;
        }
        c = tlb.EmitWatchWords(c, delta, n);
    }
    for (uint32_t k = 0; k < w.count; ++k) EmitWordAccess(p, c, w, k, w.offset[k] - lo);

    A64ColdTransfer cold{};
    cold.attr        = attr;
    cold.list        = list;
    cold.entry       = entry;
    cold.align       = align;
    cold.join        = c;
    cold.align_state = align_state;
    cold.entry_state = entry_state;
    cold.join_state  = p.State();
    cold.misses      = misses;
    cold.miss_target = tlb.MissTarget(link.privilege, access);
    cold.pc          = m.guest_pc;
    cold.keep_pc     = tlb.KeepsPc();
    l.Tails().AddColdTransfer(cold);
}

}

bool A64EmitColdWordTransfer(A64Lowering& l, const A64ColdTransfer& cold) {
    const Block&    b = l.GetBlock();
    const MemAttr&  m = b.MemAttrAt(cold.attr);
    const WordList& w = b.WordListAt(cold.list);
    if (!l.HasRoomFor(kWordBytes * (w.count + 1))) return false;

    A64PinnedSlots&   p         = l.Pins();
    const A64PinState block_end = p.State();
    p.EnterState(cold.entry_state);
    uint8_t* c = l.Cursor();
    a64::FixupLabel(cold.entry, c);
    uint8_t* const found = EmitWordSequence(l, c, m, w, w.offset[0], cold.align, cold.align_state);
    l.Tails().AddMissStub(cold.misses, cold.miss_target, found, cold.entry_state.dirty, cold.pc,
                          cold.keep_pc);
    const A64PinState join = p.State();
    if (join.dirty != cold.join_state.dirty || join.stale != cold.join_state.stale) {
        throw EmitFault("CerfIR A64 lowering: a cold word transfer reaches its join in another "
                        "pinned-slot state");
    }
    a64::FixupLabel(a64::EmitBLabel(c), cold.join);
    l.Cursor() = c;
    p.EnterState(block_end);
    return true;
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

    if (Batchable(l, m, w)) {
        EmitBatched(l, c, attr, list, m, w, align, align_state);
    } else {
        EmitWordSequence(l, c, m, w, 0, align, align_state);
    }
    l.Cursor() = c;

    ra.ReleaseTemp(kA64AddressReg);
    l.ClearFlagSource();
}

}
