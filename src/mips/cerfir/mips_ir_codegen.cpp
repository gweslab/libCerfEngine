#include "mips_ir_codegen.h"

#include <array>
#include <cstddef>
#include <string>

#include <cerfengine/engine_host.h>
#include <cerfengine/isa_block_space.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include <cerfengine/mips/mips_exception_delivery.h>

#include "../../cerfir/cerfir_builder.h"
#include "../../cerfir/cerfir_verify.h"
#include "../../emit_fault.h"
#include "../mips_block_compiler.h"
#include "../mips_emit_services.h"
#include "../mips_memory_access.h"
#include "mips_ir_context.h"
#include "mips_ir_fns.h"

namespace cerfengine {

CERFENGINE_REGISTER(MipsIrCodegen);

namespace {

using cerfir::kNoRef;
using cerfir::Op;
using cerfir::SlotPredicate;
using cerfir::SlotTest;
using cerfir::Type;

bool IsPcRelative(MipsInsnKind kind) {
    return kind == MipsInsnKind::kMips16Addiupc || kind == MipsInsnKind::kMips16Lwpc ||
           kind == MipsInsnKind::kMips16Ldpc;
}

SlotTest BcondSet() {
    SlotTest t{};
    t.pred    = SlotPredicate::kNonZero;
    t.if_true = true;
    t.disp[0] = static_cast<int32_t>(MipsBcondDisp());
    return t;
}

[[noreturn]] void MissingTerminal() {
    throw EmitFault("CerfIR MIPS frontend: a block-ending instruction emitted no exit");
}

/* QEMU MIPS translate.c gen_mtc0: Status and Cause writes end the block with DISAS_EXIT "to
   check for pending interrupts"; TMPR39xx-um p.171: RFE copies IEp/KUp to IEc/KUc. */
bool ReturnsToDispatcher(const MipsDecodedInsn& d) {
    if (d.kind == MipsInsnKind::kRfe) return true;
    return (d.kind == MipsInsnKind::kMtc0 || d.kind == MipsInsnKind::kDmtc0) &&
           (d.rd == MipsCp0::kStatus || d.rd == MipsCp0::kCause);
}

constexpr uint8_t kSlotUnsafe     = 0u;
constexpr uint8_t kSlotWritesNone = 1u;
constexpr uint8_t kSlotWritesRd   = 2u;
constexpr uint8_t kSlotWritesRt   = 3u;

constexpr std::array<uint8_t, kMipsInsnKindCount> DelaySlotClasses() {
    std::array<uint8_t, kMipsInsnKindCount> classes{};
    for (MipsInsnKind k : {MipsInsnKind::kNop, MipsInsnKind::kCache, MipsInsnKind::kMthi,
                           MipsInsnKind::kMtlo, MipsInsnKind::kMult, MipsInsnKind::kMultu}) {
        classes[static_cast<size_t>(k)] = kSlotWritesNone;
    }
    for (MipsInsnKind k : {MipsInsnKind::kAddu,  MipsInsnKind::kSubu,   MipsInsnKind::kAnd,
                           MipsInsnKind::kOr,    MipsInsnKind::kXor,    MipsInsnKind::kNor,
                           MipsInsnKind::kSlt,   MipsInsnKind::kSltu,   MipsInsnKind::kSll,
                           MipsInsnKind::kSra,   MipsInsnKind::kSrl,    MipsInsnKind::kSllv,
                           MipsInsnKind::kSrlv,  MipsInsnKind::kSrav,   MipsInsnKind::kMfhi,
                           MipsInsnKind::kMflo,  MipsInsnKind::kMadd,   MipsInsnKind::kMaddu,
                           MipsInsnKind::kDaddu, MipsInsnKind::kDsubu,  MipsInsnKind::kDsll,
                           MipsInsnKind::kDsrl,  MipsInsnKind::kDsra,   MipsInsnKind::kDsll32,
                           MipsInsnKind::kDsrl32, MipsInsnKind::kDsra32}) {
        classes[static_cast<size_t>(k)] = kSlotWritesRd;
    }
    for (MipsInsnKind k : {MipsInsnKind::kAddiu, MipsInsnKind::kLui,   MipsInsnKind::kOri,
                           MipsInsnKind::kXori,  MipsInsnKind::kAndi,  MipsInsnKind::kSlti,
                           MipsInsnKind::kSltiu, MipsInsnKind::kDaddiu,
                           MipsInsnKind::kMips16Addiupc}) {
        classes[static_cast<size_t>(k)] = kSlotWritesRt;
    }
    return classes;
}

constexpr std::array<uint8_t, kMipsInsnKindCount> kDelaySlotClass = DelaySlotClasses();

uint8_t DelaySlotClass(const MipsDecodedInsn& d) {
    if ((d.kind == MipsInsnKind::kSrl || d.kind == MipsInsnKind::kDsrl32) && (d.rs & 1u) != 0u) {
        return kSlotUnsafe;
    }
    return kDelaySlotClass[static_cast<size_t>(d.kind)];
}

uint32_t DelaySlotWrites(const MipsDecodedInsn& d, uint8_t cls) {
    if (cls == kSlotWritesRd) return 1u << d.rd;
    if (cls == kSlotWritesRt) return 1u << d.rt;
    return 0u;
}

/* QEMU MIPS translate.c decode_opc_legacy OPC_CP0 and OPC_CACHE: check_cp0_enabled raises
   Coprocessor Unusable before the instruction. */
constexpr std::array<bool, kMipsInsnKindCount> Cop0Kinds() {
    std::array<bool, kMipsInsnKindCount> kinds{};
    for (MipsInsnKind k : {MipsInsnKind::kMtc0,  MipsInsnKind::kMfc0,  MipsInsnKind::kDmtc0,
                           MipsInsnKind::kDmfc0, MipsInsnKind::kTlbwi, MipsInsnKind::kTlbwr,
                           MipsInsnKind::kTlbp,  MipsInsnKind::kTlbr,  MipsInsnKind::kRfe,
                           MipsInsnKind::kEret,  MipsInsnKind::kHibernate,
                           MipsInsnKind::kWait,  MipsInsnKind::kCache}) {
        kinds[static_cast<size_t>(k)] = true;
    }
    return kinds;
}

constexpr std::array<bool, kMipsInsnKindCount> kCop0Kind = Cop0Kinds();

bool IsCop0(const MipsDecodedInsn& d) { return kCop0Kind[static_cast<size_t>(d.kind)]; }

void EmitCop0UsableCheck(MipsIrContext& c) {
    MipsIrFastcallConst(c, reinterpret_cast<const void*>(&MipsExceptionDelivery::Cop0UsableHelper),
                        1u, cerfir::CallHostAddress(c.block->emit->Exceptions()), 0u);
}

template <bool kCop0Checked>
CERFENGINE_FORCEINLINE void TranslateOne(MipsIrContext& c) {
    if constexpr (kCop0Checked) {
        if (IsCop0(*c.d)) EmitCop0UsableCheck(c);
    }
    MipsIrTranslateFnFor(c.d->kind)(c);
}

template <bool kCop0Checked>
CERFENGINE_FORCEINLINE uint8_t BranchDelaySlotClass(const MipsDecodedInsn& slot) {
    if constexpr (kCop0Checked) {
        if (IsCop0(slot)) return kSlotUnsafe;
    }
    return DelaySlotClass(slot);
}

}

void MipsIrCodegen::OnReady() {
    host_                      = &engine_.Host();
    verify_                    = engine_.GetSetup().verify_ir;
    system_                    = engine_.GetSetup().mips_system;
    wide_gprs_                 = engine_.GetSetup().mips.isa_level != MipsIsaLevel::kMips1;
    model_.cycle_slot          = static_cast<int32_t>(offsetof(MipsCpuState, guest_cycle_counter));
    cerfir::ExitModel& e       = model_.exits;
    e.chain                    = true;
    e.chain_exit_slot = static_cast<int32_t>(offsetof(MipsCpuState, chain_exit_request));
    e.cycle_slot      = model_.cycle_slot;
    e.deadline_slot   = static_cast<int32_t>(offsetof(MipsCpuState, guest_cycle_deadline));
    e.mode_slot       = static_cast<int32_t>(MipsIsaModeDisp());
    e.mode_mask       = engine_.GetSetup().mips.mips16 ? 1u : 0u;
    model_.memory              = cerfir::MemoryModelKind::kHelper;
    cerfir::HelperMemoryModel& h = model_.helper;
    for (uint32_t p = 0; p < kBlockPrivilegeCount; ++p) {
        const MipsMemoryHelpers& m = MipsMemoryAccess::HelpersFor(p);
        for (uint32_t w = 0; w < 3u; ++w) {
            h.load[p][w]  = reinterpret_cast<const void*>(m.load[w]);
            h.store[p][w] = reinterpret_cast<const void*>(m.store[w]);
        }
    }
    h.context      = &engine_.Get<MipsMemoryAccess>();
    h.store_pc     = true;
    target_.model  = &model_;
    block_.SetPcSlot(static_cast<int32_t>(MipsPcDisp()));
}

size_t MipsIrCodegen::Generate(const MipsIrBlockInput& in, uint8_t* code, uint8_t* code_end) {
    target_.link = in.link;
    block_.Reset();
    lowering_.Begin(block_, code, code_end, target_);
    TranslateBlock(in);
    if (verify_) VerifyBlock(in);
    return lowering_.Finish();
}

void MipsIrCodegen::VerifyBlock(const MipsIrBlockInput& in) {
    std::string error;
    if (!cerfir::Verify(block_, &error)) {
        engine_.Panic("%s (block at guest PC 0x%08X)", error.c_str(),
                      in.ctx->insns[0].guest_address);
    }
}

CERFENGINE_FORCEINLINE void MipsIrCodegen::ExitTo(MipsIrContext& c, uint32_t target,
                                                  uint16_t slot) {
    if (c.dispatch_exit) {
        c.b->SetSlotConst(MipsPcDisp(), target);
        c.b->ExitDispatcher();
        return;
    }
    c.b->ExitDirect(target, slot, true, true);
}

CERFENGINE_FORCEINLINE void MipsIrCodegen::ExitRegister(MipsIrContext& c) {
    if (c.dispatch_exit) {
        c.b->ExitDispatcher();
        return;
    }
    c.b->ExitIndirect();
}

CERFENGINE_FORCEINLINE void MipsIrCodegen::ExitTaken(MipsIrContext& c) {
    if (c.branch.isa == c.block_isa) {
        ExitTo(c, c.branch.target, 0u);
        return;
    }
    c.b->SetSlotConst(MipsPcDisp(), c.branch.target);
    ExitRegister(c);
}

void MipsIrCodegen::TranslateBlock(const MipsIrBlockInput& in) {
    cerfir::Builder              b(block_, lowering_, verify_);
    MipsIrContext c{&b,    in.ctx, this,  nullptr, false, MipsIrBranch{}, wide_gprs_,
                    false, 0u,     false, in.link->mode_set ? 1u : 0u,    in.link->privilege};
    const MipsDecodedInsn* const insns = in.ctx->insns;
    const uint32_t               n     = in.ctx->num_insns;
    const bool traced = host_->HasPcTraceIn(insns[0].guest_address, insns[n - 1u].guest_address);
    if (in.pending_entry && insns[0].is_branch == 0u) {
        TranslatePendingEntry(in, c, traced);
        return;
    }
    const bool kernel = c.privilege == kBlockPrivileged;
    if (traced) {
        if (kernel) {
            TranslateInsns<true, false>(in, c);
        } else {
            TranslateInsns<true, true>(in, c);
        }
    } else if (kernel) {
        TranslateInsns<false, false>(in, c);
    } else {
        TranslateInsns<false, true>(in, c);
    }
}

template <bool kTraced, bool kCop0Checked>
CERFENGINE_FORCEINLINE void MipsIrCodegen::TranslateInsns(const MipsIrBlockInput& in,
                                                          MipsIrContext& c) {
    cerfir::Builder&             b     = *c.b;
    const MipsDecodedInsn* const insns = in.ctx->insns;
    const uint32_t               n     = in.ctx->num_insns;
    bool                         delay_slot = false;
    for (uint32_t i = 0; i < n; ++i) {
        const MipsDecodedInsn& d = insns[i];
        c.d                      = &d;
        c.pc_stored              = false;
        if constexpr (kTraced) {
            if (host_->HasPcTrace(d.guest_address)) EmitPcTrace(c, in.compiler);
        }
        b.CycleAdd(1u);
        if (delay_slot) {
            if (insns[i - 1u].is_likely != 0u) {
                EmitNullifyGate(c);
                if (b.LastWasTerminal()) return;
            }
        } else if (d.is_branch != 0u) {
            c.branch.kind = MipsBranch::kNone;
            c.branch.lean = false;
            c.lean_branch = false;
            if (!in.pending_entry && i + 1u < n) {
                const MipsDecodedInsn& slot = insns[i + 1u];
                const uint8_t          cls  = BranchDelaySlotClass<kCop0Checked>(slot);
                const bool traced_slot      = kTraced && host_->HasPcTrace(slot.guest_address);
                c.lean_branch               = cls != kSlotUnsafe && !traced_slot;
                c.slot_writes               = DelaySlotWrites(slot, cls);
            }
        }
        TranslateOne<kCop0Checked>(c);
        if (d.ends_block != 0u) {
            EmitDispatcherEnd(c, delay_slot);
            return;
        }
        if (delay_slot) {
            EmitResolve(c, d.guest_address + d.length);
            return;
        }
        delay_slot = d.is_branch != 0u;
    }
    const MipsDecodedInsn& last = insns[n - 1u];
    const uint32_t         next = last.guest_address + last.length;
    if (last.is_branch != 0u) {
        b.SetSlotConst(MipsPcDisp(), next);
        b.ExitDispatcher();
        return;
    }
    ExitTo(c, next, 0u);
}

void MipsIrCodegen::EmitDispatcherEnd(MipsIrContext& c, bool delay_slot) {
    cerfir::Builder& b = *c.b;
    if (b.LastWasTerminal()) return;
    if (!ReturnsToDispatcher(*c.d)) MissingTerminal();
    c.dispatch_exit     = true;
    const uint32_t next = c.d->guest_address + c.d->length;
    if (delay_slot) {
        EmitResolve(c, next);
        return;
    }
    b.SetSlotConst(MipsPcDisp(), next);
    b.ExitDispatcher();
}

/* QEMU MIPS decode_opc "blikely not taken" (translate.c): a not-taken likely
   branch skips its delay slot. */
void MipsIrCodegen::EmitNullifyGate(MipsIrContext& c) {
    cerfir::Builder&       b = *c.b;
    const MipsDecodedInsn& d = *c.d;
    if (c.branch.cond_known && c.branch.cond_taken) return;
    const bool             gated    = !c.branch.cond_known;
    const bool             by_value = gated && !c.branch.deferred && c.branch.lean;
    uint32_t               run      = kMipsIrNoLabel;
    cerfir::Builder::Guard guard{};
    if (by_value) {
        run = b.NewLabel();
        b.Branch(c.branch.cond, run, true);
    } else if (gated) {
        guard = b.GuardBranch(c.branch.deferred ? c.branch.test : BcondSet());
    }
    if (!c.branch.lean) b.SetSlotConst(MipsBranchStateDisp(), MipsBranch::kNone);
    ExitTo(c, d.guest_address + d.length, 1u);
    if (by_value) {
        b.Label(run);
    } else if (gated) {
        b.GuardBind(guard);
    }
}

void MipsIrCodegen::EmitResolve(MipsIrContext& c, uint32_t fallthrough) {
    cerfir::Builder&    b  = *c.b;
    const MipsIrBranch& br = c.branch;
    if (br.lean) {
        EmitLeanResolve(c, fallthrough);
        return;
    }
    switch (br.kind) {
    case MipsBranch::kUncond:
        b.SetSlotConst(MipsBranchStateDisp(), MipsBranch::kNone);
        b.SetSlotConst(MipsIsaModeDisp(), br.isa);
        ExitTaken(c);
        return;
    case MipsBranch::kCondLikely:
        b.SetSlotConst(MipsBranchStateDisp(), MipsBranch::kNone);
        ExitTo(c, br.target, 0u);
        return;
    case MipsBranch::kCond: {
        b.SetSlotConst(MipsBranchStateDisp(), MipsBranch::kNone);
        if (br.cond_known) {
            ExitTo(c, br.cond_taken ? br.target : fallthrough, 0u);
            return;
        }
        const cerfir::Builder::Guard taken = b.GuardBranch(BcondSet());
        ExitTo(c, fallthrough, 1u);
        b.GuardBind(taken);
        ExitTo(c, br.target, 0u);
        return;
    }
    case MipsBranch::kRegister:
        b.SetSlotConst(MipsBranchStateDisp(), MipsBranch::kNone);
        b.SetSlot(MipsPcDisp(), b.ReadSlot(MipsBtargetDisp()));
        b.SetSlot(MipsIsaModeDisp(), b.GetSlot(Type::kI32, MipsBtargetIsaDisp()));
        ExitRegister(c);
        return;
    default:
        b.SetSlotConst(MipsPcDisp(), c.d->guest_address);
        b.ExitDispatcher();
        return;
    }
}

void MipsIrCodegen::EmitLeanResolve(MipsIrContext& c, uint32_t fallthrough) {
    cerfir::Builder&    b  = *c.b;
    const MipsIrBranch& br = c.branch;
    switch (br.kind) {
    case MipsBranch::kUncond:
        b.SetSlotConst(MipsIsaModeDisp(), br.isa);
        ExitTaken(c);
        return;
    case MipsBranch::kCondLikely:
        ExitTo(c, br.target, 0u);
        return;
    case MipsBranch::kCond: {
        if (br.cond_known) {
            ExitTo(c, br.cond_taken ? br.target : fallthrough, 0u);
            return;
        }
        if (br.deferred) {
            const cerfir::Builder::Guard taken = b.GuardBranch(br.test);
            ExitTo(c, fallthrough, 1u);
            b.GuardBind(taken);
            ExitTo(c, br.target, 0u);
            return;
        }
        const uint32_t taken = b.NewLabel();
        b.Branch(br.cond, taken, true);
        ExitTo(c, fallthrough, 1u);
        b.Label(taken);
        ExitTo(c, br.target, 0u);
        return;
    }
    case MipsBranch::kRegister:
        ExitRegister(c);
        return;
    default:
        throw EmitFault("CerfIR MIPS frontend: a lean branch of an unknown kind");
    }
}

void MipsIrCodegen::TranslatePendingEntry(const MipsIrBlockInput& in, MipsIrContext& c,
                                          bool traced) {
    cerfir::Builder&       b           = *c.b;
    const MipsDecodedInsn& d           = in.ctx->insns[0];
    const uint32_t         fallthrough = d.guest_address + d.length;
    c.d                                = &d;
    c.pc_stored                        = false;
    if (traced && host_->HasPcTrace(d.guest_address)) EmitPcTrace(c, in.compiler);
    b.CycleAdd(1u);
    const cerfir::Ref nullified =
        MipsIrFastcall(c, reinterpret_cast<const void*>(&MipsBlockCompiler::NullifyLikelyHelper),
                       Type::kI32, b.Const32(fallthrough), b.ConstPtr(in.compiler));
    const uint32_t run = b.NewLabel();
    b.Branch(b.Compare(Op::kCmpEq, nullified, b.Const32(0u)), run, true);
    b.ExitDispatcher();
    b.Label(run);
    if (IsPcRelative(d.kind)) {
        MipsIrCdeclConst(c, reinterpret_cast<const void*>(&MipsBlockCompiler::PcrelDelaySlotHelper),
                         2u, cerfir::CallHostAddress(in.compiler), d.guest_address, 0u);
    }
    if (c.privilege == kBlockPrivileged) {
        TranslateOne<false>(c);
    } else {
        TranslateOne<true>(c);
    }
    if (d.ends_block != 0u) {
        if (b.LastWasTerminal()) return;
        if (!ReturnsToDispatcher(d)) MissingTerminal();
    }
    MipsIrFastcallConst(c, reinterpret_cast<const void*>(&MipsBlockCompiler::ResolveBranchHelper),
                        2u, fallthrough, cerfir::CallHostAddress(in.compiler));
    b.ExitDispatcher();
}

void MipsIrCodegen::EmitPcTrace(MipsIrContext& c, MipsBlockCompiler* compiler) {
    MipsIrCdeclConst(c, reinterpret_cast<const void*>(&MipsBlockCompiler::TraceDispatchPcHelper),
                     2u, cerfir::CallHostAddress(compiler), c.d->guest_address, 0u);
}

}
