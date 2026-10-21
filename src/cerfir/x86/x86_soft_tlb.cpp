#include "x86_soft_tlb.h"

#include <cstring>

#include "../../emit_fault.h"
#include "../../lowest_set_bit.h"
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"
#include "../../x86_emit_pointer.h"
#include "../../x86_emit_shift.h"
#include "x86_host_abi.h"

namespace cerfengine::cerfir {

namespace {

struct EntryDisps {
    int32_t tag;
    int32_t addend;
    int32_t pa;
    int32_t context;
    int32_t global;
    int32_t writable;
};

EntryDisps DispsFor(const SoftTlbModel& m, uint32_t privilege) {
    const int32_t unit = m.unit_aux[privilege];
    return EntryDisps{unit + m.entry_tag,     unit + m.entry_addend, unit + m.entry_pa,
                      unit + m.entry_context, unit + m.entry_global, unit + m.entry_writable};
}

uint32_t PageMask(const SoftTlbModel& m) {
    return ~((1u << m.page_shift) - 1u);
}

uint32_t OffsetMask(const SoftTlbModel& m) {
    return (1u << m.page_shift) - 1u;
}

}

uint8_t* X86SoftTlb::EmitFold(uint8_t* cursor) const {
    using namespace x86;
    EmitMovRegReg(cursor, kEax, kEcx);
    if (model_.fold_mask == 0u) return cursor;
    EmitTestRegImm32(cursor, kEax, model_.fold_mask);
    uint8_t* no_fold = EmitJnzLabel(cursor);
    EmitOrRegBaseDisp32(cursor, kEax, kMmuReg, model_.fold_aux);
    FixupLabel(no_fold, cursor);
    return cursor;
}

uint8_t* X86SoftTlb::EmitDirect(uint8_t* cursor, uint32_t privilege, TlbAccess access,
                                size_t* call_rel) const {
    using namespace x86;
    uint8_t* const   start    = cursor;
    const bool       is_write = access != TlbAccess::kRead;
    const EntryDisps e        = DispsFor(model_, privilege);

    uint8_t* miss[3];
    int      nmiss = 0;

    cursor = EmitFold(cursor);

    EmitMovRegReg    (cursor, kEdx, kEax);
    EmitShrReg32Imm  (cursor, kEdx, static_cast<uint8_t>(model_.page_shift));
    EmitAndRegImm32  (cursor, kEdx, model_.set_mask);
    EmitShlReg32Imm  (cursor, kEdx, static_cast<uint8_t>(set_shift_));
    EmitAddPtrRegReg (cursor, kEdx, kMmuReg);

    EmitAndRegImm32     (cursor, kEax, PageMask(model_));
    EmitCmpRegBaseDisp32(cursor, kEax, kEdx, e.tag);
    miss[nmiss++] = EmitJnzLabel32(cursor);

    EmitMovRegBaseDisp32      (cursor, kEax, kMmuReg, model_.context_aux);
    EmitTestByteBaseDisp32Imm8(cursor, kEdx, e.global, 0xFF);
    uint8_t* context_ok = EmitJnzLabel(cursor);
    EmitCmpReg8BaseDisp32     (cursor, kAl, kEdx, e.context);
    miss[nmiss++] = EmitJnzLabel32(cursor);
    FixupLabel(context_ok, cursor);

    if (is_write) {
        EmitTestByteBaseDisp32Imm8(cursor, kEdx, e.writable, 0xFF);
        miss[nmiss++] = EmitJzLabel32(cursor);

        if (model_.watch_writes) {
            EmitMovRegReg      (cursor, kEax, kEcx);
            EmitAndRegImm32    (cursor, kEax, OffsetMask(model_));
            EmitOrRegBaseDisp32(cursor, kEax, kEdx, e.pa);

            EmitCmpRegBaseDisp32(cursor, kEax, kMmuReg, model_.watch_base_aux);
            uint8_t* below = EmitJbLabel32(cursor);
            EmitCmpRegBaseDisp32(cursor, kEax, kMmuReg, model_.watch_top_aux);
            uint8_t* above = EmitJaeLabel32(cursor);

            EmitSubRegBaseDisp32   (cursor, kEax, kMmuReg, model_.watch_base_aux);
            EmitPushReg            (cursor, kEcx);
            EmitMovPtrRegBaseDisp32(cursor, kEcx, kMmuReg, model_.watch_words_aux);
            EmitShrReg32Imm        (cursor, kEax, 2);
            EmitBtMemReg           (cursor, kEcx, kEax);
            uint8_t* restore = EmitJncLabel32(cursor);
            EmitShrReg32Imm        (cursor, kEax, 10);
            EmitMovPtrRegBaseDisp32(cursor, kEcx, kMmuReg, model_.watch_dirty_aux);
            EmitBtsMemReg          (cursor, kEcx, kEax);
            FixupLabel32(restore, cursor);
            EmitPopReg          (cursor, kEcx);
            FixupLabel32(below, cursor);
            FixupLabel32(above, cursor);
        }
    }

    cursor = EmitFold(cursor);
    EmitAddPtrRegBaseDisp32(cursor, kEax, kEdx, e.addend);
    uint8_t* hit_done = EmitJmpLabel(cursor);

    for (int i = 0; i < nmiss; ++i) FixupLabel32(miss[i], cursor);
    *call_rel = static_cast<size_t>(cursor - start) + 1u;
    EmitOpcodeRel32(cursor, 0xE8, miss_[privilege][static_cast<size_t>(access)]);
    FixupLabel(hit_done, cursor);
    return cursor;
}

uint8_t* X86SoftTlb::EmitMiss(uint8_t* cursor, uint32_t privilege, TlbAccess access) const {
    using namespace x86;
    const bool       is_write = access != TlbAccess::kRead;
    const EntryDisps e        = DispsFor(model_, privilege);

    if (model_.io_tag_bit != 0u) {
        uint8_t* io_miss[3];
        int      n_io_miss = 0;

        cursor = EmitFold(cursor);
        EmitAndRegImm32     (cursor, kEax, PageMask(model_));
        EmitAddRegImm32     (cursor, kEax, model_.io_tag_bit);
        EmitCmpRegBaseDisp32(cursor, kEax, kEdx, e.tag);
        io_miss[n_io_miss++] = EmitJnzLabel32(cursor);

        EmitMovRegBaseDisp32      (cursor, kEax, kMmuReg, model_.context_aux);
        EmitTestByteBaseDisp32Imm8(cursor, kEdx, e.global, 0xFF);
        uint8_t* context_ok = EmitJnzLabel(cursor);
        EmitCmpReg8BaseDisp32     (cursor, kAl, kEdx, e.context);
        io_miss[n_io_miss++] = EmitJnzLabel32(cursor);
        FixupLabel(context_ok, cursor);

        if (is_write) {
            EmitTestByteBaseDisp32Imm8(cursor, kEdx, e.writable, 0xFF);
            io_miss[n_io_miss++] = EmitJzLabel32(cursor);
        }

        EmitMovRegBaseDisp32(cursor, kEax, kEdx, e.pa);
        EmitMovRegReg       (cursor, kEdx, kEcx);
        EmitAndRegImm32     (cursor, kEdx, OffsetMask(model_));
        EmitOrReg32Reg32    (cursor, kEax, kEdx);
        EmitMovDwordPtrReg  (cursor, model_.io_pending_address, kEax);
        EmitMovDwordPtrImm32(cursor, model_.io_pending_valid, 1u);
        EmitXorRegReg       (cursor, kEax, kEax);
        EmitRet             (cursor);

        for (int i = 0; i < n_io_miss; ++i) FixupLabel32(io_miss[i], cursor);
    }
    EmitPushReg(cursor, kEcx);
    cursor = EmitHelperCall(cursor, model_.miss_helper[static_cast<size_t>(access)]);
    EmitPopReg(cursor, kEcx);
    EmitRet(cursor);
    return cursor;
}

uint8_t* X86SoftTlb::EmitHelperCall(uint8_t* cursor, const void* helper) const {
    using namespace x86;
    if constexpr (kX64) {
        X86RequireKnownCallAbi();
        EmitMovRegReg(cursor, kX86ArgRegs[0], kEcx);
        EmitMovPtrRegImm(cursor, kX86ArgRegs[1], model_.context);
    } else {
        EmitMovRegImm32(cursor, kEdx,
                        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(model_.context)));
    }
    EmitCall(cursor, helper);
    return cursor;
}

uint8_t* X86SoftTlb::EmitInlineHelperCall(uint8_t* cursor, const void* helper) const {
    using namespace x86;
    EmitPushReg(cursor, kEcx);
    if constexpr (kX64) EmitSubPtrRegImm32(cursor, kEsp, 8u);
    cursor = EmitHelperCall(cursor, helper);
    if constexpr (kX64) EmitAddPtrRegImm32(cursor, kEsp, 8u);
    EmitPopReg(cursor, kEcx);
    return cursor;
}

uint8_t* X86SoftTlb::EmitUserLookup(uint8_t* cursor, TlbAccess access) const {
    if (access == TlbAccess::kReadWrite) {
        throw EmitFault("CerfIR x86 soft TLB: no unprivileged read-write lookup exists");
    }
    return EmitInlineHelperCall(cursor, model_.user_helper[access == TlbAccess::kRead ? 0u : 1u]);
}

uint8_t* X86SoftTlb::EmitMissCall(uint8_t* cursor, TlbAccess access) const {
    return EmitInlineHelperCall(cursor, model_.miss_helper[static_cast<size_t>(access)]);
}

void X86SoftTlb::Build(const SoftTlbModel& model, uint8_t* region) {
    if (model.privileges == 0u || model.privileges > kMaxPrivileges) {
        throw EmitFault("CerfIR x86 soft TLB: the model names an unsupported privilege count");
    }
    if (model.set_bytes == 0u || (model.set_bytes & (model.set_bytes - 1u)) != 0u) {
        throw EmitFault("CerfIR x86 soft TLB: the size of a TLB set is not a power of two");
    }
    model_     = model;
    set_shift_ = LowestSetBit(model.set_bytes);
    for (uint32_t p = 0; p < model.privileges; ++p) {
        for (uint32_t i = 0; i < kTlbAccessKinds; ++i) {
            const TlbAccess access = static_cast<TlbAccess>(i);
            uint8_t         scratch[1024];
            if (static_cast<size_t>(EmitMiss(scratch, p, access) - scratch) > kMissBytes) {
                throw EmitFault("CerfIR x86 soft TLB: a miss routine exceeds its slot");
            }
            miss_[p][i] = region + (p * kTlbAccessKinds + i) * kMissBytes;
            EmitMiss(miss_[p][i], p, access);
        }
        for (uint32_t i = 0; i < kTlbAccessKinds; ++i) {
            uint8_t        scratch[4096];
            Stamp&         s   = stamps_[p][i];
            const uint8_t* end = EmitDirect(scratch, p, static_cast<TlbAccess>(i), &s.call_rel);
            const size_t   length = static_cast<size_t>(end - scratch);
            if (length > kStampMaxBytes) {
                throw EmitFault("CerfIR x86 soft TLB: a lookup exceeds its stamp");
            }
            std::memcpy(s.bytes, scratch, length);
            s.length = length;
            s.target = miss_[p][i];
        }
    }
}

}
