#include <cstddef>

#include <cerfengine/isa_block_space.h>
#include <cerfengine/jit_block.h>
#include <cerfengine/jit_block_index.h>

#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../a64_emit_memory.h"
#include "../../chain_jump_patch.h"
#include "../../emit_fault.h"
#include "a64_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

using a64::kMmuReg;

constexpr uint32_t kJumpCacheEntryShift = 5u;
constexpr uint32_t kJumpCacheIndexBits  = 12u;

static_assert(sizeof(JumpCacheEntry) == (1u << kJumpCacheEntryShift),
              "emitted jump-cache probes scale the index by a shift");
static_assert(kJumpCacheSize == (1u << kJumpCacheIndexBits),
              "emitted jump-cache probes extract the index as a bit field");

uint32_t FoldKey(const ExitModel& e, const BlockLink& link, uint32_t va) {
    if (e.fold_mask == 0u) return va;
    return (va & e.fold_mask) == 0u ? (va | link.fold_value) : va;
}

void EmitTestImm(uint8_t*& c, uint8_t reg, uint32_t mask) {
    if (a64::LogicImmFits(mask, false)) {
        a64::EmitLogicImm(c, a64::kAnds, a64::kZr, reg, mask);
        return;
    }
    a64::EmitMovImm32(c, a64::kIp0, mask);
    a64::EmitTstReg(c, reg, a64::kIp0);
}

void EmitPoll(A64PinnedSlots& p, uint8_t*& c, const ExitModel& e, uint8_t* (&to_dispatcher)[2]) {
    p.Sync(c);
    p.Load(c, kA64Tmp0, e.chain_exit_slot);
    to_dispatcher[0] = a64::EmitCbzLabel(c, kA64Tmp0, true, false);
    p.Load(c, kA64Tmp0, e.cycle_slot);
    p.Load(c, kA64Tmp1, e.deadline_slot);
    a64::EmitSubReg(c, kA64Tmp0, kA64Tmp0, kA64Tmp1);
    to_dispatcher[1] = a64::EmitTbzLabel(c, kA64Tmp0, 31u, false);
}

void EmitChain(A64Lowering& l, uint32_t target, uint32_t slot) {
    const ExitModel& e    = l.Target().model->exits;
    const BlockLink& link = *l.Target().link;
    uint8_t*&        c    = l.Cursor();
    const uint32_t   key  = FoldKey(e, link, target);
    uint8_t*         to_dispatcher[2];

    if (key == link.key_start) {
        if (link.self->index_split != 0u) return;
        EmitPoll(l.Pins(), c, e, to_dispatcher);
        a64::EmitB(c, link.native_start);
        a64::FixupLabel(to_dispatcher[0], c);
        a64::FixupLabel(to_dispatcher[1], c);
        return;
    }

    const uint32_t granule_mask = ~(link.granule_bytes - 1u);
    if ((key & granule_mask) != (link.key_start & granule_mask)) return;

    const uint32_t expect_pa = link.phys_start + (key - link.key_start);
    JitBlock*      dest      = link.index->FindExact(key);
    if (dest != nullptr && (dest->native_start == nullptr || dest->phys_start != expect_pa ||
                            dest->index_split != 0u)) {
        dest = nullptr;
    }

    EmitPoll(l.Pins(), c, e, to_dispatcher);
    uint8_t* const site = c;
    a64::EmitNop(c);

    link.self->chain_site[slot]       = site;
    link.self->chain_fallback[slot]   = c;
    link.self->chain_pending_va[slot] = key;
    if (dest != nullptr) {
        PatchChainJump(site, dest->native_start);
        link.index->LinkChain(link.self, slot, dest, site, c);
    } else {
        PatchChainJump(site, c);
    }
    a64::FixupLabel(to_dispatcher[0], c);
    a64::FixupLabel(to_dispatcher[1], c);
}

void EmitProbe(A64Lowering& l) {
    const ExitModel& e       = l.Target().model->exits;
    const BlockLink& link    = *l.Target().link;
    const int32_t    pc_slot = l.GetBlock().PcSlot();
    A64PinnedSlots&  p       = l.Pins();
    uint8_t*&        c       = l.Cursor();
    uint8_t*         miss[5];
    uint32_t         n = 0;

    uint8_t* to_dispatcher[2];
    EmitPoll(p, c, e, to_dispatcher);
    miss[n++] = to_dispatcher[0];
    miss[n++] = to_dispatcher[1];

    if (e.mode_mask != 0u) {
        p.Load(c, kA64Tmp0, e.mode_slot);
        EmitTestImm(c, kA64Tmp0, e.mode_mask);
        miss[n++] = a64::EmitBCondLabel(c, link.mode_set ? a64::kEq : a64::kNe);
    }

    p.Load(c, kA64Tmp0, pc_slot);
    if (e.fold_mask != 0u) {
        EmitTestImm(c, kA64Tmp0, e.fold_mask);
        uint8_t* const no_fold = a64::EmitBCondLabel(c, a64::kNe);
        a64::EmitLdrW(c, kA64Tmp1, kMmuReg, e.fold_aux);
        a64::EmitLogicReg(c, a64::kOrr, kA64Tmp0, kA64Tmp0, kA64Tmp1);
        a64::FixupLabel(no_fold, c);
    }

    a64::EmitUbfx(c, kA64Tmp1, kA64Tmp0, 2u, kJumpCacheIndexBits);
    a64::EmitMovPtr(c, kA64Tmp2, link.jump_cache);
    a64::EmitAddReg(c, kA64Tmp2, kA64Tmp2, kA64Tmp1, a64::kLsl, kJumpCacheEntryShift, true);
    a64::EmitLdrW(c, a64::kIp0, kA64Tmp2, link.jump_cache_key_disp);
    a64::EmitCmpReg(c, a64::kIp0, kA64Tmp0);
    miss[n++] = a64::EmitBCondLabel(c, a64::kNe);
    a64::EmitLdrX(c, a64::kIp0, kA64Tmp2, static_cast<int32_t>(offsetof(JumpCacheEntry, native)));
    miss[n++] = a64::EmitCbzLabel(c, a64::kIp0, false, true);
    a64::EmitBr(c, a64::kIp0);

    for (uint32_t i = 0; i < n; ++i) a64::FixupLabel(miss[i], c);
}

void EmitBlockReturn(A64Lowering& l) {
    uint8_t*& c = l.Cursor();
    l.Pins().Flush(c);
    a64::EmitLdrX(c, a64::kLr, a64::kSp, kA64ReturnSlot);
    a64::EmitRet(c);
    l.Pins().MarkUnreachable();
}

}

void A64EmitExitDirect(A64Lowering& l, uint32_t target, uint16_t flags) {
    const Block& b = l.GetBlock();
    l.Regs().BeforeFlagsClobber();
    if ((flags & kExitStorePc) != 0u) {
        l.Regs().BeforeSlotWrite(b.PcSlot(), 4u);
        l.Pins().StoreImm(l.Cursor(), b.PcSlot(), target, kA64Tmp0);
    }
    if (l.Target().model->exits.chain) {
        if (l.Target().link->self != nullptr) EmitChain(l, target, flags & kExitSlotMask);
        if ((flags & kExitProbe) != 0u) EmitProbe(l);
    }
    EmitBlockReturn(l);
}

void A64EmitExitIndirect(A64Lowering& l) {
    l.Regs().BeforeFlagsClobber();
    if (l.Target().model->exits.chain) EmitProbe(l);
    EmitBlockReturn(l);
}

void A64EmitExitDispatcher(A64Lowering& l) {
    l.Regs().BeforeFlagsClobber();
    EmitBlockReturn(l);
}

}
