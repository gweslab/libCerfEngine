#include <cstddef>
#include <cstring>
#include <utility>

#include <cerfengine/byte_order.h>
#include <cerfengine/isa_block_space.h>
#include <cerfengine/jit_block.h>
#include <cerfengine/jit_block_index.h>

#include "../../emit_fault.h"
#include "../../host_platform.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
#include "../../x86_emit_pointer.h"
#include "../../x86_emit_shift.h"
#include "x86_copy_lanes.h"
#include "x86_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

constexpr uint32_t kJumpCacheEntryShift = x86::kX64 ? 5u : 4u;

static_assert(sizeof(JumpCacheEntry) == (1u << kJumpCacheEntryShift),
              "emitted jump-cache probes scale the index by a shift");

uint32_t FoldKey(const ExitModel& e, const BlockLink& link, uint32_t va) {
    if (e.fold_mask == 0u) return va;
    return (va & e.fold_mask) == 0u ? (va | link.fold_value) : va;
}

void EmitPoll(uint8_t*& c, const ExitModel& e, uint8_t* (&to_dispatcher)[2]) {
    using namespace x86;
    EmitMovRegBaseDisp32(c, kEax, kStateReg, e.chain_exit_slot);
    EmitTestRegReg(c, kEax, kEax);
    to_dispatcher[0] = EmitJnzLabel32(c);
    EmitMovRegBaseDisp32(c, kEax, kStateReg, e.cycle_slot);
    EmitSubRegBaseDisp32(c, kEax, kStateReg, e.deadline_slot);
    to_dispatcher[1] = EmitJnsLabel32(c);
}

[[noreturn]] void StampOverflow() {
    throw EmitFault("CerfIR x86 lowering: an exit stamp outgrew its buffer");
}

void EmitChain(X86Lowering& l, uint32_t target, uint32_t slot) {
    using namespace x86;
    const ExitModel& e    = l.Target().model->exits;
    const BlockLink& link = *l.Target().link;
    uint8_t*&        c    = l.Cursor();
    const uint32_t   key  = FoldKey(e, link, target);
    uint8_t*         to_dispatcher[2];

    if (key == link.key_start) {
        if (link.self->index_split != 0u) return;
        EmitPoll(c, e, to_dispatcher);
        EmitJmp32(c, link.native_start);
        FixupLabel32(to_dispatcher[0], c);
        FixupLabel32(to_dispatcher[1], c);
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

    EmitPoll(c, e, to_dispatcher);
    Emit8(c, 0xE9);
    uint8_t* const site = c;
    Emit32(c, 0u);

    link.self->chain_site[slot]       = site;
    link.self->chain_fallback[slot]   = c;
    link.self->chain_pending_va[slot] = key;
    if (dest != nullptr) {
        le::Put32(site, static_cast<uint32_t>(dest->native_start - (site + 4)));
        link.index->LinkChain(link.self, slot, dest, site, c);
    } else {
        le::Put32(site, static_cast<uint32_t>(c - (site + 4)));
    }
    FixupLabel32(to_dispatcher[0], c);
    FixupLabel32(to_dispatcher[1], c);
}

constexpr uint32_t kProbeMissSites = 7u;

void EmitCacheLookup(uint8_t*& c, const void* jump_cache, int32_t key_disp,
                     uint8_t* (&miss)[kProbeMissSites], uint32_t& n) {
    using namespace x86;
    EmitMovRegReg(c, kEcx, kEax);
    EmitShrReg32Imm(c, kEcx, 2);
    EmitAndRegImm32(c, kEcx, kJumpCacheSize - 1u);
    EmitShlReg32Imm(c, kEcx, static_cast<uint8_t>(kJumpCacheEntryShift));
    if constexpr (kX64) {
        EmitMovPtrRegImm(c, kAddressReg, jump_cache);
        EmitAddPtrRegReg(c, kEcx, kAddressReg);
    } else {
        EmitAddRegImm32(c, kEcx, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(jump_cache)));
    }

    EmitCmpRegBaseDisp32(c, kEax, kEcx, key_disp);
    miss[n++] = EmitJnzLabel32(c);

    EmitMovPtrRegBaseDisp32(c, kEcx, kEcx, static_cast<int32_t>(offsetof(JumpCacheEntry, native)));
    EmitTestPtrRegReg(c, kEcx, kEcx);
    miss[n++] = EmitJzLabel32(c);

    /* JMP r/m32 - FF /4 (SDM Vol. 2A 3-552 JMP, Op/En M). */
    Emit8(c, 0xFF);
    EmitModRmReg(c, 3u, kEcx, 4u);
}

void EmitProbeBytes(uint8_t*& c, const ExitModel& e, const BlockLink& link, int32_t pc_slot) {
    using namespace x86;
    uint8_t* miss[kProbeMissSites];
    uint32_t n = 0;

    uint8_t* to_dispatcher[2];
    EmitPoll(c, e, to_dispatcher);
    miss[n++] = to_dispatcher[0];
    miss[n++] = to_dispatcher[1];

    EmitMovRegBaseDisp32(c, kEax, kStateReg, pc_slot);
    if (e.fold_mask != 0u) {
        EmitMovRegReg(c, kEcx, kEax);
        EmitAndRegImm32(c, kEcx, e.fold_mask);
        uint8_t* const no_fold = EmitJnzLabel32(c);
        EmitOrRegBaseDisp32(c, kEax, kMmuReg, e.fold_aux);
        FixupLabel32(no_fold, c);
    }

    uint8_t* other_mode = nullptr;
    if (e.mode_mask != 0u) {
        EmitMovRegBaseDisp32(c, kEcx, kStateReg, e.mode_slot);
        EmitTestRegImm32(c, kEcx, e.mode_mask);
        uint8_t* const mismatch = link.mode_set ? EmitJzLabel32(c) : EmitJnzLabel32(c);
        if (link.other_mode_jump_cache != nullptr) {
            other_mode = mismatch;
        } else {
            miss[n++] = mismatch;
        }
    }

    EmitCacheLookup(c, link.jump_cache, link.jump_cache_key_disp, miss, n);
    if (other_mode != nullptr) {
        FixupLabel32(other_mode, c);
        EmitCacheLookup(c, link.other_mode_jump_cache, link.jump_cache_key_disp, miss, n);
    }

    for (uint32_t i = 0; i < n; ++i) FixupLabel32(miss[i], c);
}

CERFENGINE_NOINLINE void BuildProbeStamp(X86ProbeStamp& s, const ExitModel& e,
                                         const BlockLink& link, int32_t pc_slot) {
    uint8_t  scratch[512];
    uint8_t* c = scratch;
    EmitProbeBytes(c, e, link, pc_slot);
    const size_t length = static_cast<size_t>(c - scratch);
    if (length > sizeof(s.bytes)) StampOverflow();
    std::memcpy(s.bytes, scratch, length);
    s.length                = static_cast<uint32_t>(length);
    s.model                 = &e;
    s.jump_cache            = link.jump_cache;
    s.other_mode_jump_cache = link.other_mode_jump_cache;
    s.key_disp              = link.jump_cache_key_disp;
    s.pc_slot               = pc_slot;
    s.mode_set              = link.mode_set;
}

CERFENGINE_FORCEINLINE bool ProbeStampFits(const X86ProbeStamp& s, const ExitModel& e,
                                           const BlockLink& link, int32_t pc_slot) {
    return s.model == &e && s.jump_cache == link.jump_cache &&
           s.other_mode_jump_cache == link.other_mode_jump_cache &&
           s.key_disp == link.jump_cache_key_disp && s.pc_slot == pc_slot &&
           s.mode_set == link.mode_set;
}

CERFENGINE_NOINLINE X86ProbeStamp* ProbeStampMiss(X86Lowering& l, const ExitModel& e,
                                                 const BlockLink& link, int32_t pc_slot) {
    X86ProbeStamp* s = &l.ProbeStamp(1);
    if (ProbeStampFits(*s, e, link, pc_slot)) return s;
    std::swap(l.ProbeStamp(0), l.ProbeStamp(1));
    s = &l.ProbeStamp(0);
    BuildProbeStamp(*s, e, link, pc_slot);
    return s;
}

CERFENGINE_FORCEINLINE void EmitProbe(X86Lowering& l) {
    const ExitModel& e       = l.Target().model->exits;
    const BlockLink& link    = *l.Target().link;
    if (link.probe_routine != nullptr) {
        x86::EmitJmp32(l.Cursor(), link.probe_routine);
        return;
    }
    const int32_t    pc_slot = l.GetBlock().PcSlot();
    X86ProbeStamp*   s       = &l.ProbeStamp(0);
    if (!ProbeStampFits(*s, e, link, pc_slot)) s = ProbeStampMiss(l, e, link, pc_slot);
    uint8_t*& c = l.Cursor();
    X86CopyLanes(c, s->bytes, s->length);
    c += s->length;
}

}

const void* X86Lowering::BuildProbeRoutine(const ExitModel& e, const BlockLink& link, int32_t pc_slot,
                                           uint8_t* region) {
    uint8_t  scratch[kProbeRoutineBytes * 2];
    uint8_t* end = scratch;
    EmitProbeBytes(end, e, link, pc_slot);
    x86::EmitRet(end);
    if (static_cast<size_t>(end - scratch) > kProbeRoutineBytes) {
        throw EmitFault("CerfIR x86 lowering: a shared probe routine exceeds its slot");
    }
    const platform::CodeWriteScope write;
    uint8_t* c = region;
    EmitProbeBytes(c, e, link, pc_slot);
    x86::EmitRet(c);
    platform::FlushInstructionCache(region, static_cast<size_t>(c - region));
    return region;
}

void X86EmitExitDirect(X86Lowering& l, uint32_t target, uint16_t flags) {
    const Block& b = l.GetBlock();
    l.Regs().BeforeFlagsClobber();
    if ((flags & kExitStorePc) != 0u) {
        l.Regs().BeforeSlotWrite(b.PcSlot(), 4u);
        x86::EmitMovBaseDisp32Imm32(l.Cursor(), x86::kStateReg, b.PcSlot(), target);
    }
    if (l.Target().model->exits.chain) {
        if (l.Target().link->self != nullptr) EmitChain(l, target, flags & kExitSlotMask);
        if ((flags & kExitProbe) != 0u) EmitProbe(l);
    }
    x86::EmitRet(l.Cursor());
}

void X86EmitExitIndirect(X86Lowering& l) {
    l.Regs().BeforeFlagsClobber();
    if (l.Target().model->exits.chain) EmitProbe(l);
    x86::EmitRet(l.Cursor());
}

void X86EmitExitDispatcher(X86Lowering& l) {
    l.Regs().BeforeFlagsClobber();
    x86::EmitRet(l.Cursor());
}

}
