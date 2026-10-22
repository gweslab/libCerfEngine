#include "a64_soft_tlb.h"

#include <bit>
#include <cstring>

#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../a64_emit_memory.h"
#include "../../emit_fault.h"
#include "../../host_platform.h"
#include "../../lowest_set_bit.h"
#include "a64_pinned_slots.h"

namespace cerfengine::cerfir {

namespace {

using namespace a64;

constexpr uint8_t kVa      = kX1;
constexpr uint8_t kHost    = kX0;
constexpr uint8_t kEntry   = kX2;
constexpr uint8_t kFolded  = kX9;
constexpr uint8_t kTmpA    = kX10;
constexpr uint8_t kTmpB    = kX11;
constexpr uint8_t kTmpC    = kX12;
constexpr uint8_t kTmpD    = kX13;
constexpr uint8_t kTmpE    = kIp1;

uint32_t PageMask(const SoftTlbModel& m) {
    return ~((1u << m.page_shift) - 1u);
}

uint32_t OffsetMask(const SoftTlbModel& m) {
    return (1u << m.page_shift) - 1u;
}

void EmitTestMask(uint8_t*& c, uint8_t reg, uint32_t mask) {
    if (LogicImmFits(mask, false)) {
        EmitLogicImm(c, kAnds, kZr, reg, mask);
        return;
    }
    EmitMovImm32(c, kTmpE, mask);
    EmitTstReg(c, reg, kTmpE);
}

void EmitAndMask(uint8_t*& c, uint8_t rd, uint8_t rn, uint32_t mask) {
    if (LogicImmFits(mask, false)) {
        EmitLogicImm(c, kAnd, rd, rn, mask);
        return;
    }
    EmitMovImm32(c, kTmpE, mask);
    EmitLogicReg(c, kAnd, rd, rn, kTmpE);
}

void EmitAddConst(uint8_t*& c, uint8_t rd, uint8_t rn, uint64_t value, bool x64) {
    if (value == 0u) {
        if (rd != rn) EmitMovReg(c, rd, rn, x64);
        return;
    }
    if (value < 0x1000000u && AddSubImmFits(static_cast<uint32_t>(value))) {
        EmitAddImm(c, rd, rn, static_cast<uint32_t>(value), x64);
        return;
    }
    EmitMovImm64(c, kTmpE, value);
    EmitAddReg(c, rd, rn, kTmpE, kLsl, 0u, x64);
}

}

uint8_t* A64SoftTlb::EmitFold(uint8_t* c) const {
    EmitMovReg(c, kFolded, kVa);
    if (model_.fold_mask == 0u) return c;
    EmitTestMask(c, kFolded, model_.fold_mask);
    uint8_t* no_fold = EmitBCondLabel(c, kNe);
    EmitLdrW(c, kTmpA, kMmuReg, model_.fold_aux);
    EmitLogicReg(c, kOrr, kFolded, kFolded, kTmpA);
    FixupLabel(no_fold, c);
    return c;
}

uint8_t* A64SoftTlb::EmitEntryChecks(uint8_t* c, uint32_t, TlbAccess access, uint32_t tag_add,
                                     uint8_t** misses, int* count) const {
    EmitAndMask(c, kTmpA, kFolded, PageMask(model_));
    EmitAddConst(c, kTmpA, kTmpA, tag_add, false);
    EmitLdrW(c, kTmpB, kEntry, model_.entry_tag);
    EmitCmpReg(c, kTmpA, kTmpB);
    misses[(*count)++] = EmitBCondLabel(c, kNe);

    EmitLdrb(c, kTmpB, kEntry, model_.entry_global);
    uint8_t* context_ok = EmitCbzLabel(c, kTmpB, true, false);
    EmitLdrb(c, kTmpA, kMmuReg, model_.context_aux);
    EmitLdrb(c, kTmpB, kEntry, model_.entry_context);
    EmitCmpReg(c, kTmpA, kTmpB);
    misses[(*count)++] = EmitBCondLabel(c, kNe);
    FixupLabel(context_ok, c);

    if (access != TlbAccess::kRead) {
        EmitLdrb(c, kTmpB, kEntry, model_.entry_writable);
        misses[(*count)++] = EmitCbzLabel(c, kTmpB, false, false);
    }
    return c;
}

uint8_t* A64SoftTlb::EmitWatchWrite(uint8_t* c, uint32_t) const {
    EmitAndMask(c, kTmpA, kVa, OffsetMask(model_));
    EmitLdrW(c, kTmpB, kEntry, model_.entry_pa);
    EmitLogicReg(c, kOrr, kTmpA, kTmpA, kTmpB);

    EmitLdrW(c, kTmpB, kMmuReg, model_.watch_base_aux);
    EmitCmpReg(c, kTmpA, kTmpB);
    uint8_t* below = EmitBCondLabel(c, kCc);
    EmitLdrW(c, kTmpC, kMmuReg, model_.watch_top_aux);
    EmitCmpReg(c, kTmpA, kTmpC);
    uint8_t* above = EmitBCondLabel(c, kCs);

    EmitSubReg(c, kTmpA, kTmpA, kTmpB);
    EmitLsrImm(c, kTmpA, kTmpA, 2u);
    EmitLdrX(c, kTmpB, kMmuReg, model_.watch_words_aux);
    EmitLsrImm(c, kTmpC, kTmpA, 5u);
    EmitLoadStoreIndexed(c, kWord, kLoad, kTmpD, kTmpB, kTmpC, true);
    EmitShiftReg(c, kLsr, kTmpD, kTmpD, kTmpA);
    uint8_t* clean = EmitTbzLabel(c, kTmpD, 0u, false);

    EmitLsrImm(c, kTmpA, kTmpA, 10u);
    EmitLdrX(c, kTmpB, kMmuReg, model_.watch_dirty_aux);
    EmitLsrImm(c, kTmpC, kTmpA, 5u);
    EmitAddReg(c, kTmpB, kTmpB, kTmpC, kLsl, 2u, true);
    EmitLdrW(c, kTmpD, kTmpB, 0);
    EmitMovImm32(c, kTmpE, 1u);
    EmitShiftReg(c, kLsl, kTmpE, kTmpE, kTmpA);
    EmitLogicReg(c, kOrr, kTmpD, kTmpD, kTmpE);
    EmitStrW(c, kTmpD, kTmpB, 0);

    FixupLabel(below, c);
    FixupLabel(above, c);
    FixupLabel(clean, c);
    return c;
}

uint8_t* A64SoftTlb::EmitDirect(uint8_t* c, uint32_t privilege, TlbAccess access,
                                size_t* call_offset) const {
    uint8_t* const start = c;
    uint8_t*       miss[3];
    int            nmiss = 0;

    c = EmitFold(c);
    const uint32_t set_bits = 32u - static_cast<uint32_t>(std::countl_zero(model_.set_mask));
    EmitUbfx(c, kEntry, kFolded, model_.page_shift, set_bits);
    EmitAddReg(c, kEntry, kMmuReg, kEntry, kLsl, set_shift_, true);
    EmitAddConst(c, kEntry, kEntry, static_cast<uint32_t>(model_.unit_aux[privilege]), true);

    c = EmitEntryChecks(c, privilege, access, 0u, miss, &nmiss);
    if (access != TlbAccess::kRead && model_.watch_writes) c = EmitWatchWrite(c, privilege);

    EmitLdrX(c, kTmpB, kEntry, model_.entry_addend);
    EmitAddUxtw(c, kHost, kTmpB, kFolded);
    uint8_t* hit_done = EmitBLabel(c);

    for (int i = 0; i < nmiss; ++i) FixupLabel(miss[i], c);
    *call_offset = static_cast<size_t>(c - start);
    Emit32(c, 0x94000000u);
    FixupLabel(hit_done, c);
    return c;
}

uint8_t* A64SoftTlb::EmitMiss(uint8_t* c, uint32_t privilege, TlbAccess access) const {
    if (model_.io_tag_bit != 0u) {
        uint8_t* io_miss[3];
        int      n_io_miss = 0;
        c = EmitFold(c);
        c = EmitEntryChecks(c, privilege, access, model_.io_tag_bit, io_miss, &n_io_miss);
        EmitLdrW(c, kTmpA, kEntry, model_.entry_pa);
        EmitAndMask(c, kTmpB, kVa, OffsetMask(model_));
        EmitLogicReg(c, kOrr, kTmpA, kTmpA, kTmpB);
        EmitMovPtr(c, kIp0, model_.io_pending_address);
        EmitStrW(c, kTmpA, kIp0, 0);
        EmitMovPtr(c, kIp0, model_.io_pending_valid);
        EmitMovImm32(c, kTmpB, 1u);
        EmitStrW(c, kTmpB, kIp0, 0);
        EmitMovReg(c, kHost, kZr, true);
        EmitRet(c);
        for (int i = 0; i < n_io_miss; ++i) FixupLabel(io_miss[i], c);
    }
    EmitStpPre(c, kVa, kLr, kSp, -16);
    for (uint32_t i = 0; i < saved_count_; i += 2u) {
        EmitStpPre(c, saved_[i], i + 1u < saved_count_ ? saved_[i + 1u] : kIp1, kSp, -16);
    }
    c = EmitHelperCall(c, model_.miss_helper[static_cast<size_t>(access)]);
    for (uint32_t i = (saved_count_ + 1u) & ~1u; i != 0u;) {
        i -= 2u;
        EmitLdpPost(c, saved_[i], i + 1u < saved_count_ ? saved_[i + 1u] : kIp1, kSp, 16);
    }
    EmitLdpPost(c, kVa, kLr, kSp, 16);
    EmitRet(c);
    return c;
}

uint8_t* A64SoftTlb::EmitHelperCall(uint8_t* c, const void* helper) const {
    EmitMovReg(c, kX0, kVa);
    EmitMovPtr(c, kX1, model_.context);
    EmitCall(c, helper);
    return c;
}

uint8_t* A64SoftTlb::EmitInlineHelperCall(uint8_t* c, const void* helper) const {
    EmitStpPre(c, kVa, kVa, kSp, -16);
    c = EmitHelperCall(c, helper);
    EmitLdpPost(c, kVa, kFolded, kSp, 16);
    return c;
}

uint8_t* A64SoftTlb::EmitUserLookup(uint8_t* cursor, TlbAccess access) const {
    if (access == TlbAccess::kReadWrite) {
        throw EmitFault("CerfIR A64 soft TLB: no unprivileged read-write lookup exists");
    }
    return EmitInlineHelperCall(cursor, model_.user_helper[access == TlbAccess::kRead ? 0u : 1u]);
}

uint8_t* A64SoftTlb::EmitMissCall(uint8_t* cursor, TlbAccess access) const {
    return EmitInlineHelperCall(cursor, model_.miss_helper[static_cast<size_t>(access)]);
}

void A64SoftTlb::Build(const GuestModel& guest, uint8_t* region) {
    const SoftTlbModel& model = guest.tlb;
    if (guest.pinned.count > kA64MaxPinned) {
        throw EmitFault("CerfIR A64 soft TLB: the guest pins more slots than the host has pinned registers");
    }
    saved_count_ = 0u;
    for (uint32_t i = 0; i < guest.pinned.count; ++i) {
        if ((kA64CallerSavedRegs & (1u << kA64PinnedRegs[i])) != 0u) saved_[saved_count_++] = kA64PinnedRegs[i];
    }
    if (model.privileges == 0u || model.privileges > kMaxPrivileges) {
        throw EmitFault("CerfIR A64 soft TLB: the model names an unsupported privilege count");
    }
    if (model.set_bytes == 0u || (model.set_bytes & (model.set_bytes - 1u)) != 0u) {
        throw EmitFault("CerfIR A64 soft TLB: the size of a TLB set is not a power of two");
    }
    if (model.set_mask == 0u || (model.set_mask & (model.set_mask + 1u)) != 0u) {
        throw EmitFault("CerfIR A64 soft TLB: the set mask is not a run of low bits");
    }
    model_     = model;
    set_shift_ = LowestSetBit(model.set_bytes);
    const platform::CodeWriteScope write;
    for (uint32_t p = 0; p < model.privileges; ++p) {
        for (uint32_t i = 0; i < kTlbAccessKinds; ++i) {
            const TlbAccess access = static_cast<TlbAccess>(i);
            uint8_t         scratch[1024];
            if (static_cast<size_t>(EmitMiss(scratch, p, access) - scratch) > kMissBytes) {
                throw EmitFault("CerfIR A64 soft TLB: a miss routine exceeds its slot");
            }
            miss_[p][i] = region + (p * kTlbAccessKinds + i) * kMissBytes;
            EmitMiss(miss_[p][i], p, access);
        }
        for (uint32_t i = 0; i < kTlbAccessKinds; ++i) {
            uint8_t        scratch[4096];
            Stamp&         s   = stamps_[p][i];
            const uint8_t* end = EmitDirect(scratch, p, static_cast<TlbAccess>(i), &s.call_offset);
            const size_t   length = static_cast<size_t>(end - scratch);
            if (length > kStampMaxBytes) {
                throw EmitFault("CerfIR A64 soft TLB: a lookup exceeds its stamp");
            }
            std::memcpy(s.bytes, scratch, length);
            s.length = length;
            s.target = miss_[p][i];
        }
    }
    platform::FlushInstructionCache(region, RegionBytes(model.privileges));
}

}
