#include "arm_ir_codegen.h"

#include <array>
#include <cstddef>
#include <string>

#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/engine_host.h>

#include "../../cerfir/cerfir_builder.h"
#include "../../cerfir/cerfir_verify.h"
#include "../../emit_fault.h"
#include "../arm_block_compiler.h"
#include "../arm_translation_cache.h"
#include "arm_ir_context.h"
#include "arm_ir_fns.h"
#include "arm_ir_memory_slow_path.h"

namespace cerfengine {

CERFENGINE_REGISTER(ArmIrCodegen);

void ArmIrCodegen::OnReady() {
    host_   = &engine_.Host();
    verify_ = engine_.GetSetup().verify_ir;
    slow_   = &engine_.Get<ArmIrMemorySlowPath>();
    BuildGuestModel();
    uint8_t* const region = engine_.Get<ArmTranslationCache>().Arena().AllocatePermanent(
        cerfir::HostSoftTlb::RegionBytes(model_.tlb.privileges));
    if (region == nullptr) {
        engine_.Panic("ArmIrCodegen: the code arena has no room for the TLB miss paths");
    }
    try {
        tlb_.Build(model_.tlb, region);
    } catch (const EmitFault& fault) {
        engine_.Panic("%s", fault.what());
    }
    target_.model = &model_;
    target_.tlb   = &tlb_;
    block_.SetFlagSlots(ArmNfDisp(), ArmZfDisp(), ArmCfDisp(), ArmVfDisp());
    block_.SetPcSlot(ArmGprDisp(ArmGpr::kR15));
}

void ArmIrCodegen::BuildGuestModel() {
    ArmMmu& mmu       = engine_.Get<ArmMmu>();
    model_.cycle_slot = static_cast<int32_t>(offsetof(ArmCpuState, guest_cycle_counter));

    cerfir::ExitModel& e = model_.exits;
    e.chain              = true;
    e.chain_exit_slot    = static_cast<int32_t>(offsetof(ArmCpuState, chain_exit_request));
    e.cycle_slot         = model_.cycle_slot;
    e.deadline_slot      = static_cast<int32_t>(offsetof(ArmCpuState, guest_cycle_deadline));
    e.fold_mask          = kArmFcsePidMask;
    e.fold_aux           = static_cast<int32_t>(offsetof(ArmMmuState, fcse_fold_id));
    e.mode_slot          = ArmCpsrDisp();
    e.mode_mask          = kArmCpsrT;

    model_.memory          = cerfir::MemoryModelKind::kSoftTlb;
    cerfir::SoftTlbModel& t = model_.tlb;
    t.fold_mask            = kArmFcsePidMask;
    t.fold_aux             = static_cast<int32_t>(offsetof(ArmMmuState, fcse_fold_id));
    t.page_shift           = 12u;
    t.set_mask             = kArmTlbSetMask;
    t.set_bytes            = kArmTlbWays * static_cast<uint32_t>(sizeof(ArmTlbEntry));
    t.privileges           = kArmTlbPrivilegeCount;
    for (uint32_t p = 0; p < kArmTlbPrivilegeCount; ++p) {
        t.unit_aux[p] = static_cast<int32_t>(offsetof(ArmMmuState, data_tlb) +
                                             ArmTlbPrivilege(p != 0u) * sizeof(ArmTlbUnit));
    }
    t.entry_tag       = static_cast<int32_t>(offsetof(ArmTlbEntry, tag));
    t.entry_addend    = static_cast<int32_t>(offsetof(ArmTlbEntry, va_addend));
    t.entry_pa        = static_cast<int32_t>(offsetof(ArmTlbEntry, pa_page));
    t.entry_context   = static_cast<int32_t>(offsetof(ArmTlbEntry, asid));
    t.entry_global    = static_cast<int32_t>(offsetof(ArmTlbEntry, global));
    t.entry_writable  = static_cast<int32_t>(offsetof(ArmTlbEntry, writable));
    t.context_aux     = static_cast<int32_t>(offsetof(ArmMmuState, contextidr));
    t.watch_writes    = true;
    t.watch_base_aux  = static_cast<int32_t>(offsetof(ArmMmuState, code_word_base));
    t.watch_top_aux   = static_cast<int32_t>(offsetof(ArmMmuState, code_word_top));
    t.watch_words_aux = static_cast<int32_t>(offsetof(ArmMmuState, code_xlat_bitmap));
    t.watch_dirty_aux = static_cast<int32_t>(offsetof(ArmMmuState, code_page_dirty));
    t.io_tag_bit         = kArmTlbIoTagBit;
    t.io_pending_address = mmu.IoPendingAddressPtr();
    t.io_pending_valid   = mmu.IoPendingValidPtr();
    t.miss_helper[static_cast<size_t>(cerfir::TlbAccess::kRead)] =
        reinterpret_cast<const void*>(&ArmMmu::TranslateReadHelper);
    t.miss_helper[static_cast<size_t>(cerfir::TlbAccess::kWrite)] =
        reinterpret_cast<const void*>(&ArmMmu::TranslateWriteHelper);
    t.miss_helper[static_cast<size_t>(cerfir::TlbAccess::kReadWrite)] =
        reinterpret_cast<const void*>(&ArmMmu::TranslateReadWriteHelper);
    t.user_helper[0] = reinterpret_cast<const void*>(&ArmMmu::TranslateUserReadHelper);
    t.user_helper[1] = reinterpret_cast<const void*>(&ArmMmu::TranslateUserWriteHelper);
    t.context        = &mmu;

    model_.slow = slow_->SlowModel();
}

size_t ArmIrCodegen::Generate(const ArmIrBlockInput& in, uint8_t* code, uint8_t* code_end) {
    target_.link = in.link;
    block_.Reset();
    lowering_.Begin(block_, code, code_end, target_);
    TranslateBlock(in);
    if (verify_) VerifyBlock(in);
    return lowering_.Finish();
}

void ArmIrCodegen::VerifyBlock(const ArmIrBlockInput& in) {
    std::string error;
    if (!cerfir::Verify(block_, &error)) {
        engine_.Panic("%s (block at guest PC 0x%08X)", error.c_str(),
                      in.ctx->insns[0].guest_address);
    }
}

namespace {

/* ARM DDI 0406C.c Table A8-1 (p. A8-288): EQ Z==1, NE Z==0, CS C==1, CC C==0,
   MI N==1, PL N==0, VS V==1, VC V==0, HI C==1 and Z==0, LS C==0 or Z==1,
   GE N==V, LT N!=V, GT Z==0 and N==V, LE Z==1 or N!=V. */
constexpr cerfir::SlotTest ConditionTest(uint32_t cond) {
    using cerfir::SlotPredicate;
    cerfir::SlotTest t{};
    t.if_true = (cond & 1u) != 0u;
    switch (cond) {
    case 0u:
    case 1u:
        t.disp[0] = ArmZfDisp();
        break;
    case 2u:
    case 3u:
        t.disp[0] = ArmCfDisp();
        break;
    case 4u:
    case 5u:
        t.disp[0] = ArmNfDisp();
        break;
    case 6u:
    case 7u:
        t.disp[0] = ArmVfDisp();
        break;
    case 8u:
    case 9u:
        t.pred    = SlotPredicate::kAndNot;
        t.disp[0] = ArmCfDisp();
        t.disp[1] = ArmZfDisp();
        break;
    case 10u:
    case 11u:
        t.pred    = SlotPredicate::kNotEqual;
        t.if_true = cond == 10u;
        t.disp[0] = ArmNfDisp();
        t.disp[1] = ArmVfDisp();
        break;
    case 12u:
    case 13u:
        t.pred    = SlotPredicate::kOrNotEqual;
        t.if_true = cond == 12u;
        t.disp[0] = ArmZfDisp();
        t.disp[1] = ArmNfDisp();
        t.disp[2] = ArmVfDisp();
        break;
    default:
        break;
    }
    return t;
}

constexpr std::array<cerfir::SlotTest, 14> ConditionTests() {
    std::array<cerfir::SlotTest, 14> tests{};
    for (uint32_t cond = 0; cond < tests.size(); ++cond) tests[cond] = ConditionTest(cond);
    return tests;
}

constexpr std::array<cerfir::SlotTest, 14> kConditionTests = ConditionTests();

[[noreturn]] CERFENGINE_NOINLINE void ConditionFieldFault() {
    throw EmitFault("CerfIR ARM frontend: condition field 15 reached the condition guard");
}

}

CERFENGINE_FORCEINLINE cerfir::Builder::Guard ArmIrCodegen::EmitCondition(ArmIrContext& c,
                                                                         uint32_t cond) {
    if (cond >= kConditionTests.size()) ConditionFieldFault();
    return c.b->GuardBranch(kConditionTests[cond]);
}

void ArmIrCodegen::TranslateBlock(const ArmIrBlockInput& in) {
    cerfir::Builder          b(block_, lowering_, verify_);
    ArmIrContext             c{&b, in.ctx, nullptr, in.compiler};
    const DecodedInsn* const insns = in.ctx->insns;
    const uint32_t           n     = in.ctx->num_insns;
    const bool traced = host_->HasPcTraceIn(insns[0].guest_address, insns[n - 1u].guest_address);
    if (traced) {
        if (in.uniform_timing) {
            TranslateInsns<true, true>(in, b, c);
        } else {
            TranslateInsns<false, true>(in, b, c);
        }
    } else if (in.uniform_timing) {
        TranslateInsns<true, false>(in, b, c);
    } else {
        TranslateInsns<false, false>(in, b, c);
    }
}

template <bool kUniform, bool kTraced>
CERFENGINE_FORCEINLINE void ArmIrCodegen::TranslateInsns(const ArmIrBlockInput& in,
                                                         cerfir::Builder& b, ArmIrContext& c) {
    const DecodedInsn* const insns = in.ctx->insns;
    const uint32_t           n     = in.ctx->num_insns;
    for (uint32_t i = 0; i < n; ++i) {
        const DecodedInsn& d = insns[i];
        c.d                  = &d;
        if (d.itstate_valid != 0u) ArmIrStoreItState(c, d.itstate);
        if constexpr (kTraced) {
            if (host_->HasPcTrace(d.guest_address)) EmitPcTrace(b, in, d.guest_address);
        }
        if constexpr (kUniform) {
            b.CycleAdd(1u);
        } else {
            b.CycleAdd(in.timing->ArmCycleCost(d));
        }

        const bool              conditional = d.cond != 14u;
        cerfir::Builder::Guard guard{};
        if (conditional) guard = EmitCondition(c, d.cond);
        ArmIrTranslateFnFor(d.kind)(c);
        if (conditional) {
            b.GuardBind(guard);
        } else if (b.LastWasTerminal()) {
            return;
        }
        if (d.itstate_after_valid != 0u) ArmIrStoreItState(c, d.itstate_after);
    }
    EmitBlockEnd(in, c);
}

void ArmIrCodegen::EmitPcTrace(cerfir::Builder& b, const ArmIrBlockInput& in, uint32_t address) {
    b.SetSlot(static_cast<uint32_t>(ArmGprDisp(ArmGpr::kR15)), b.Const32(address));
    const cerfir::CallDesc trace{
        reinterpret_cast<const void*>(&ArmBlockCompiler::TraceDispatchPcHelper),
        cerfir::CallConv::kCdecl, 2u};
    b.Call(trace, cerfir::Type::kVoid, b.ConstPtr(in.compiler), b.Const32(address),
           cerfir::kNoRef);
}

void ArmIrCodegen::EmitBlockEnd(const ArmIrBlockInput& in, ArmIrContext& c) {
    const DecodedInsn& last = in.ctx->insns[in.ctx->num_insns - 1u];
    if (last.cond == 14u && last.r15_modified && last.r15_conditional == 0u) return;
    cerfir::Builder& b = *c.b;
    if (in.tail_itstate_valid) ArmIrStoreItState(c, in.tail_itstate);
    const uint32_t next = last.guest_address + last.length;
    if (last.context_sync) {
        b.SetSlotConst(static_cast<uint32_t>(ArmGprDisp(ArmGpr::kR15)), next);
        b.ExitDispatcher();
    } else {
        b.ExitDirect(next, 1u, true);
    }
}

}
