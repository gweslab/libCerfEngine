#include "../../emit_fault.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
#include "../../x86_emit_compare.h"
#include "../../x86_emit_pointer.h"
#include "x86_lower_fns.h"
#include "x86_memory_operands.h"
#include "x86_soft_tlb.h"

namespace cerfengine::cerfir {

namespace {

using x86::kEax;
using x86::kEcx;
using x86::kEdx;
using x86::kStateReg;

constexpr ptrdiff_t kWordBytes = static_cast<ptrdiff_t>(X86SoftTlb::kStampMaxBytes) + 64;

void PlaceWordTail(X86Lowering& l, const MemAttr& m, const WordList& w, uint8_t* align,
                   uint8_t* abort, bool back_out) {
    X86MemoryTail& t = l.Tails().New();
    if (m.writeback_slot != kNoSlot) {
        t.wb = X86Operand{X86OperandKind::kRecompute, 0u, m.base_slot, w.wb_add, nullptr};
    }
    X86PrepareMemoryTail(l, t, m, w.store);
    t.desc.back_out = back_out;
    t.align[0]      = align;
    t.abort         = abort;
}

void EmitWordAccess(uint8_t*& c, const WordList& w, uint32_t k, int32_t host_disp) {
    using namespace x86;
    if (!w.store) {
        EmitMovRegBaseDisp32(c, kEdx, kEax, host_disp);
        EmitMovBaseDisp32Reg(c, kStateReg, w.slot[k], kEdx);
    } else if (w.slot[k] == kNoSlot) {
        EmitMovRegImm32(c, kEdx, w.imm[k]);
        EmitMovBaseDisp32Reg(c, kEax, host_disp, kEdx);
    } else {
        EmitMovRegBaseDisp32(c, kEdx, kStateReg, w.slot[k]);
        EmitMovBaseDisp32Reg(c, kEax, host_disp, kEdx);
    }
}

uint8_t* EmitWordSequence(X86Lowering& l, uint8_t*& c, const MemAttr& m, const WordList& w,
                          int32_t at, uint8_t*& first, uint8_t*& rest) {
    using namespace x86;
    const TlbAccess access      = w.store ? TlbAccess::kWrite : TlbAccess::kRead;
    uint8_t*        first_found = nullptr;
    for (uint32_t k = 0; k < w.count; ++k) {
        if (w.offset[k] != at) {
            EmitAddRegImm32(c, kEcx, static_cast<uint32_t>(w.offset[k] - at));
            at = w.offset[k];
        }
        c = X86EmitTlbLookup(l, c, m, access);
        if (k == 0u) first_found = c;
        EmitTestPtrRegReg(c, kEax, kEax);
        X86ChainLabelSite(k == 0u ? first : rest, EmitJzLabel32(c));
        EmitWordAccess(c, w, k, 0);
    }
    return first_found;
}

bool Batchable(X86Lowering& l, const MemAttr& m, const WordList& w) {
    if (w.count < 2u || m.unpriv || l.Target().link->translation_off) return false;
    for (uint32_t k = 1; k < w.count; ++k) {
        if (w.offset[k] < w.offset[0]) return false;
    }
    return true;
}

void EmitBatched(X86Lowering& l, uint8_t*& c, uint32_t attr, uint32_t list, const MemAttr& m,
                 const WordList& w, uint8_t* align) {
    using namespace x86;
    const X86SoftTlb& tlb  = *l.Target().tlb;
    const BlockLink&  link = *l.Target().link;
    const TlbAccess   access = w.store ? TlbAccess::kWrite : TlbAccess::kRead;

    const int32_t lo = w.offset[0];
    int32_t       hi = lo;
    for (uint32_t k = 1; k < w.count; ++k) {
        if (w.offset[k] > hi) hi = w.offset[k];
    }
    const uint32_t span = static_cast<uint32_t>(hi - lo) + 4u;

    if (lo != 0) EmitAddRegImm32(c, kEcx, static_cast<uint32_t>(lo));
    SoftTlbMissSites misses;
    c = tlb.EmitLookup(c, link.privilege, access, &misses);

    const uint8_t scratch = w.store ? kEax : kEdx;
    if (w.store) EmitPushReg(c, kEax);
    EmitMovRegReg(c, scratch, kEcx);
    EmitAndRegImm32(c, scratch, tlb.PageBytes() - 1u);
    EmitCmpRegImm32(c, scratch, tlb.PageBytes() + 1u - span);
    if (w.store) EmitPopReg(c, kEax);
    uint8_t* const entry = EmitJaeLabel32(c);

    if (w.store) {
        int32_t  delta[kMaxWords];
        uint32_t n = 0;
        for (uint32_t k = 0; k < w.count; ++k) {
            if (w.offset[k] != lo) delta[n++] = (w.offset[k] - lo) / 4;
        }
        c = tlb.EmitWatchWords(c, link.privilege, delta, n);
    }
    for (uint32_t k = 0; k < w.count; ++k) EmitWordAccess(c, w, k, w.offset[k] - lo);

    X86ColdTransfer cold{};
    cold.attr        = attr;
    cold.list        = list;
    cold.entry       = entry;
    cold.align       = align;
    cold.join        = c;
    cold.misses      = misses;
    cold.miss_target = tlb.MissTarget(link.privilege, access);
    cold.pc          = m.guest_pc;
    cold.keep_pc     = tlb.KeepsPc();
    l.Tails().AddColdTransfer(cold);
}

}

bool X86EmitColdWordTransfer(X86Lowering& l, const X86ColdTransfer& cold) {
    using namespace x86;
    const Block&    b = l.GetBlock();
    const MemAttr&  m = b.MemAttrAt(cold.attr);
    const WordList& w = b.WordListAt(cold.list);
    if (!l.HasRoomFor(kWordBytes * (w.count + 1))) return false;

    uint8_t* c = l.Cursor();
    FixupLabel32(cold.entry, c);
    uint8_t*       first = nullptr;
    uint8_t*       rest  = nullptr;
    uint8_t* const found = EmitWordSequence(l, c, m, w, w.offset[0], first, rest);
    l.Tails().AddMissStub(cold.misses, cold.miss_target, found, cold.pc, cold.keep_pc);
    FixupLabel32(EmitJmpLabel32(c), cold.join);
    l.Cursor() = c;

    PlaceWordTail(l, m, w, cold.align, first, m.back_out);
    if (rest != nullptr) PlaceWordTail(l, m, w, nullptr, rest, false);
    return true;
}

void X86EmitTransferWords(X86Lowering& l, uint32_t attr, uint32_t list) {
    using namespace x86;
    if (l.Target().model->memory != MemoryModelKind::kSoftTlb) {
        throw EmitFault("CerfIR x86 lowering: a word transfer needs the soft-TLB memory model");
    }
    const Block&    b  = l.GetBlock();
    const MemAttr&  m  = b.MemAttrAt(attr);
    const WordList& w  = b.WordListAt(list);
    X86RegAlloc&    ra = l.Regs();
    if (!l.HasRoomFor(kWordBytes * (w.count + 1))) throw X86ArenaFull{};

    ra.BeforeFlagsClobber();
    if (!ra.Quiet()) {
        if (!w.store) {
            for (uint32_t k = 0; k < w.count; ++k) ra.BeforeSlotWrite(w.slot[k], 4u);
        }
        if (m.writeback_slot != kNoSlot) ra.BeforeSlotWrite(m.writeback_slot, 4u);
    }
    ra.ClaimFixed(kEcx);
    ra.Evict(RegBit(kEax) | RegBit(kEdx));

    uint8_t* c = l.Cursor();
    EmitMovRegBaseDisp32(c, kEcx, kStateReg, m.base_slot);
    if (m.offset != 0) EmitAddRegImm32(c, kEcx, static_cast<uint32_t>(m.offset));
    if (m.align == AlignPolicy::kIgnoreLowBits) EmitAndRegImm32(c, kEcx, 0xFFFFFFFCu);
    uint8_t* align = nullptr;
    if (m.slow_mask != 0u) {
        EmitTestRegImm32(c, kEcx, m.slow_mask);
        align = EmitJnzLabel32(c);
    }

    if (Batchable(l, m, w)) {
        EmitBatched(l, c, attr, list, m, w, align);
        l.Cursor() = c;
    } else {
        uint8_t* first = nullptr;
        uint8_t* rest  = nullptr;
        EmitWordSequence(l, c, m, w, 0, first, rest);
        l.Cursor() = c;
        PlaceWordTail(l, m, w, align, first, m.back_out);
        if (rest != nullptr) PlaceWordTail(l, m, w, nullptr, rest, false);
    }

    ra.ReleaseTemp(kEcx);
    l.ClearFlagSource();
}

}
