#pragma once

#include <cstddef>
#include <cstdint>

#include <cerfengine/mips/mips_cpu_state.h>

#include "../../cerfir/cerfir_builder.h"
#include <cerfengine/force_inline.h>
#include "../mips_block_context.h"
#include "../mips_decoded_insn.h"

namespace cerfengine {

class MipsIrCodegen;

constexpr uint32_t kMipsIrNoLabel = 0xFFFFFFFFu;

struct MipsIrBranch {
    uint32_t         kind       = MipsBranch::kNone;
    uint32_t         target     = 0;
    uint32_t         isa        = 0;
    bool             cond_known = false;
    bool             cond_taken = false;
    bool             lean       = false;
    cerfir::Ref      cond       = cerfir::kNoRef;
    bool             deferred   = false;
    cerfir::SlotTest test{};
};

struct MipsIrContext {
    cerfir::Builder*       b;
    MipsBlockContext*      block;
    MipsIrCodegen*         codegen;
    const MipsDecodedInsn* d;
    bool                   pc_stored;
    MipsIrBranch           branch;
    bool                   wide_gprs;
    bool                   lean_branch;
    uint32_t               slot_writes;
    bool                   dispatch_exit;
    uint32_t               block_isa;
    uint32_t               privilege;
};

constexpr uint32_t MipsGprLoDisp(uint32_t r) {
    return static_cast<uint32_t>(offsetof(MipsCpuState, gpr)) + r * 8u;
}
constexpr uint32_t MipsGprHiDisp(uint32_t r) { return MipsGprLoDisp(r) + 4u; }
constexpr uint32_t MipsLoDisp() { return static_cast<uint32_t>(offsetof(MipsCpuState, lo)); }
constexpr uint32_t MipsHiDisp() { return static_cast<uint32_t>(offsetof(MipsCpuState, hi)); }
constexpr uint32_t MipsPcDisp() { return static_cast<uint32_t>(offsetof(MipsCpuState, pc)); }
constexpr uint32_t MipsBranchStateDisp() {
    return static_cast<uint32_t>(offsetof(MipsCpuState, branch_state));
}
constexpr uint32_t MipsBtargetDisp() { return static_cast<uint32_t>(offsetof(MipsCpuState, btarget)); }
constexpr uint32_t MipsBcondDisp() { return static_cast<uint32_t>(offsetof(MipsCpuState, bcond)); }
constexpr uint32_t MipsBtargetIsaDisp() {
    return static_cast<uint32_t>(offsetof(MipsCpuState, btarget_isa));
}
constexpr uint32_t MipsBranchLenDisp() {
    return static_cast<uint32_t>(offsetof(MipsCpuState, branch_len));
}
constexpr uint32_t MipsIsaModeDisp() { return static_cast<uint32_t>(offsetof(MipsCpuState, isa_mode)); }

CERFENGINE_FORCEINLINE uint32_t MipsSignedImm(const MipsDecodedInsn& d) {
    return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(d.imm)));
}

CERFENGINE_FORCEINLINE cerfir::Ref MipsIrReadLo(MipsIrContext& c, uint32_t r) {
    return r == 0u ? c.b->Const32(0u) : c.b->ReadSlot(MipsGprLoDisp(r));
}

CERFENGINE_FORCEINLINE cerfir::Ref MipsIrReadHi(MipsIrContext& c, uint32_t r) {
    return r == 0u ? c.b->Const32(0u) : c.b->ReadSlot(MipsGprHiDisp(r));
}

CERFENGINE_FORCEINLINE cerfir::Ref MipsIrRead64(MipsIrContext& c, uint32_t r) {
    if (r == 0u) return c.b->Const(cerfir::Type::kI64, 0u);
    return c.b->Pair(c.b->ReadSlot(MipsGprLoDisp(r)), c.b->ReadSlot(MipsGprHiDisp(r)));
}

CERFENGINE_FORCEINLINE cerfir::Ref MipsIrConst64(MipsIrContext& c, uint64_t value) {
    return c.b->Const(cerfir::Type::kI64, value);
}

CERFENGINE_FORCEINLINE cerfir::Ref MipsIrReadFull(MipsIrContext& c, uint32_t r) {
    return c.wide_gprs ? MipsIrRead64(c, r) : MipsIrReadLo(c, r);
}

CERFENGINE_FORCEINLINE cerfir::Ref MipsIrFullConst(MipsIrContext& c, uint64_t sext_value) {
    return c.wide_gprs ? MipsIrConst64(c, sext_value)
                       : c.b->Const32(static_cast<uint32_t>(sext_value));
}

CERFENGINE_FORCEINLINE void MipsIrWriteSext(MipsIrContext& c, uint32_t r, cerfir::Ref lo) {
    if (r == 0u) return;
    c.b->SetSlotSext(MipsGprLoDisp(r), lo);
}

CERFENGINE_FORCEINLINE void MipsIrWriteSextConst(MipsIrContext& c, uint32_t r, uint32_t value) {
    if (r == 0u) return;
    c.b->SetSlotSextConst(MipsGprLoDisp(r), value);
}

CERFENGINE_FORCEINLINE void MipsIrWriteZext(MipsIrContext& c, uint32_t r, cerfir::Ref lo) {
    if (r == 0u) return;
    c.b->SetSlot(MipsGprLoDisp(r), lo);
    c.b->SetSlotConst(MipsGprHiDisp(r), 0u);
}

CERFENGINE_FORCEINLINE void MipsIrWrite64(MipsIrContext& c, uint32_t r, cerfir::Ref lo,
                                          cerfir::Ref hi) {
    if (r == 0u) return;
    if (cerfir::IsSlotRef(hi) && static_cast<uint32_t>(cerfir::SlotRefDisp(hi)) == MipsGprLoDisp(r)) {
        hi = c.b->Snapshot(hi);
    }
    c.b->SetSlot(MipsGprLoDisp(r), lo);
    c.b->SetSlot(MipsGprHiDisp(r), hi);
}

CERFENGINE_FORCEINLINE void MipsIrCopy64(MipsIrContext& c, uint32_t r, uint32_t src) {
    if (r == 0u || r == src) return;
    if (src == 0u) {
        c.b->SetSlotConst(MipsGprLoDisp(r), 0u);
        c.b->SetSlotConst(MipsGprHiDisp(r), 0u);
        return;
    }
    c.b->SlotCopy64(MipsGprLoDisp(r), MipsGprLoDisp(src));
}

CERFENGINE_FORCEINLINE void MipsIrWriteHiLo(MipsIrContext& c, uint32_t disp, cerfir::Ref lo) {
    c.b->SetSlotSext(disp, lo);
}

CERFENGINE_FORCEINLINE cerfir::CallSpec MipsIrCallSpec(MipsIrContext& c, const void* fn,
                                                       cerfir::CallConv conv, cerfir::Type result) {
    cerfir::CallSpec spec{fn, conv, result};
    if (!c.pc_stored) {
        spec.store_pc = true;
        spec.pc       = c.d->guest_address;
        c.pc_stored   = true;
    }
    return spec;
}

CERFENGINE_FORCEINLINE cerfir::Ref MipsIrFastcall(MipsIrContext& c, const void* fn,
                                                  cerfir::Type result, cerfir::Ref arg0,
                                                  cerfir::Ref arg1) {
    return c.b->Call(MipsIrCallSpec(c, fn, cerfir::CallConv::kFastcall, result), arg0, arg1,
                     cerfir::kNoRef);
}

CERFENGINE_FORCEINLINE cerfir::Ref MipsIrFastcall(MipsIrContext& c, const void* fn,
                                                  cerfir::Type result, cerfir::Ref arg0,
                                                  cerfir::Ref arg1, cerfir::Ref arg2) {
    return c.b->Call(MipsIrCallSpec(c, fn, cerfir::CallConv::kFastcall, result), arg0, arg1, arg2);
}

CERFENGINE_FORCEINLINE void MipsIrCdecl(MipsIrContext& c, const void* fn, cerfir::Ref a,
                                        cerfir::Ref b, cerfir::Ref c3) {
    c.b->Call(MipsIrCallSpec(c, fn, cerfir::CallConv::kCdecl, cerfir::Type::kVoid), a, b, c3);
}

CERFENGINE_FORCEINLINE void MipsIrCdeclConst(MipsIrContext& c, const void* fn, uint32_t num_args,
                                             cerfir::CallArg a0, cerfir::CallArg a1,
                                             cerfir::CallArg a2) {
    c.b->CallConst(MipsIrCallSpec(c, fn, cerfir::CallConv::kCdecl, cerfir::Type::kVoid), num_args,
                   a0, a1, a2, 0u);
}

CERFENGINE_FORCEINLINE void MipsIrFastcallConst(MipsIrContext& c, const void* fn,
                                                uint32_t num_args, cerfir::CallArg arg0,
                                                cerfir::CallArg arg1, cerfir::CallArg arg2 = 0u,
                                                cerfir::CallArg arg3 = 0u) {
    c.b->CallConst(MipsIrCallSpec(c, fn, cerfir::CallConv::kFastcall, cerfir::Type::kVoid),
                   num_args, arg0, arg1, arg2, arg3);
}

}
