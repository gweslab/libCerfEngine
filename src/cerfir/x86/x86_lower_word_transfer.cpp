#include "../../emit_fault.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
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

    const TlbAccess access = w.store ? TlbAccess::kWrite : TlbAccess::kRead;
    uint8_t*        first  = nullptr;
    uint8_t*        rest   = nullptr;
    int32_t         at     = 0;
    for (uint32_t k = 0; k < w.count; ++k) {
        if (w.offset[k] != at) {
            EmitAddRegImm32(c, kEcx, static_cast<uint32_t>(w.offset[k] - at));
            at = w.offset[k];
        }
        c = X86EmitTlbLookup(l, c, m, access);
        EmitTestPtrRegReg(c, kEax, kEax);
        uint8_t* const site = EmitJzLabel32(c);
        if (k == 0u) {
            first = site;
        } else {
            X86ChainLabelSite(rest, site);
        }
        if (!w.store) {
            EmitMovRegBase(c, kEdx, kEax);
            EmitMovBaseDisp32Reg(c, kStateReg, w.slot[k], kEdx);
        } else if (w.slot[k] == kNoSlot) {
            EmitMovRegImm32(c, kEdx, w.imm[k]);
            EmitMovBaseReg(c, kEax, kEdx);
        } else {
            EmitMovRegBaseDisp32(c, kEdx, kStateReg, w.slot[k]);
            EmitMovBaseReg(c, kEax, kEdx);
        }
    }
    l.Cursor() = c;

    ra.ReleaseTemp(kEcx);
    l.ClearFlagSource();
    PlaceWordTail(l, m, w, align, first, m.back_out);
    if (rest != nullptr) PlaceWordTail(l, m, w, nullptr, rest, false);
}

}
