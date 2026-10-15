#include "mips_block_compiler.h"
#include <cerfengine/calling_convention.h>

#include <cstddef>

#include "../emit_fault.h"
#include <cerfengine/engine.h>
#include "../x86_emit_alu.h"
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include "mips_emit_services.h"
#include <cerfengine/mips/mips_exception_delivery.h>
#include "mips_insn_decoder.h"
#include <cerfengine/mips/mips_mmu.h>
#include "mips_translation_cache.h"

namespace cerfengine {

CERFENGINE_REGISTER(MipsBlockCompiler);

void MipsBlockCompiler::OnReady() {
    host_       = &engine_.Host();
    report_compiles_ = engine_.GetSetup().report_block_compiles;
    cpu_state_  = engine_.Get<MipsCpu>().State();
    mmu_        = &engine_.Get<MipsMmu>();
    cache_      = &engine_.Get<MipsTranslationCache>();
    decoder_    = &engine_.Get<MipsInsnDecoder>();
    exceptions_ = &engine_.Get<MipsExceptionDelivery>();

    block_ctx_.emit     = &engine_.Get<MipsEmitServices>();
    block_ctx_.compiler = this;
}

void CERFENGINE_CDECL MipsBlockCompiler::UnimplementedHelper(MipsBlockCompiler* c,
                                                             uint32_t pc, uint32_t raw) {
    c->engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                    "MipsBlockCompiler: unimplemented instruction 0x%08X at guest PC 0x%08X",
                    raw, pc);
}

void CERFENGINE_CDECL MipsBlockCompiler::PcrelDelaySlotHelper(MipsBlockCompiler* c,
                                                              uint32_t pc) {
    c->engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                    "MipsBlockCompiler: PC-relative MIPS16 instruction at 0x%08X entered "
                    "as a cross-block jump delay slot - its base PC (the jump's PC, "
                    "U15509EJ2V0UM Table 3-12 p67) is not modeled on this entry", pc);
}

void CERFENGINE_CDECL MipsBlockCompiler::TraceDispatchPcHelper(MipsBlockCompiler* c,
                                                               uint32_t pc) {
    c->host_->OnMipsPcTrace(pc, c->cpu_state_);
}

int CERFENGINE_FASTCALL MipsBlockCompiler::ResolveBranchHelper(uint32_t fallthrough,
                                                               MipsBlockCompiler* c) {
    MipsCpuState& s = *c->cpu_state_;
    if (s.branch_state == MipsBranch::kNone) {
        return 0;
    }
    MipsResolvePendingBranch(s, fallthrough);
    return 1;
}

int CERFENGINE_FASTCALL MipsBlockCompiler::NullifyLikelyHelper(uint32_t fallthrough,
                                                               MipsBlockCompiler* c) {
    MipsCpuState& s = *c->cpu_state_;
    if (s.branch_state == MipsBranch::kCondLikely && s.bcond == 0) {
        s.pc = fallthrough;
        s.branch_state = MipsBranch::kNone;
        return 1;
    }
    return 0;
}

size_t MipsBlockCompiler::GenerateCodeChecked(uint8_t* code_location) {
    try {
        return GenerateCode(code_location);
    } catch (const cerfengine::EmitFault& fault) {
        engine_.Panic("%s (block at guest PC 0x%08X)", fault.what(),
                      block_ctx_.insns[0].guest_address);
    }
}

size_t MipsBlockCompiler::GenerateCode(uint8_t* code_location) {
    using namespace x86;
    if (block_ctx_.num_insns == 0) {
        engine_.Panic("MipsBlockCompiler::GenerateCode called with num_insns == 0");
    }

    uint8_t* const start = code_location;
    constexpr int32_t kCycleOff =
        static_cast<int32_t>(offsetof(MipsCpuState, guest_cycle_counter));
    constexpr int32_t kPcOff =
        static_cast<int32_t>(offsetof(MipsCpuState, pc));
    constexpr int32_t kBranchStateOff =
        static_cast<int32_t>(offsetof(MipsCpuState, branch_state));
    const uint32_t self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));

    bool terminated = false;

    for (uint32_t i = 0; i < block_ctx_.num_insns; ++i) {
        MipsDecodedInsn& insn = block_ctx_.insns[i];
        EmitMovBaseDisp32Imm32(code_location, kStateReg, kPcOff, insn.guest_address);

        if (host_->HasPcTrace(insn.guest_address)) {
            EmitPush32(code_location, insn.guest_address);
            EmitPush32(code_location, self);
            EmitCall(code_location, reinterpret_cast<void*>(&TraceDispatchPcHelper));
            EmitAddRegImm32(code_location, kEsp, 8);
        }

        EmitAddBaseDisp32Imm8(code_location, kStateReg, kCycleOff, 1);

        const bool inblock_likely_ds =
            (i > 0 && block_ctx_.insns[i - 1].is_branch &&
             block_ctx_.insns[i - 1].is_likely);
        const bool xblock_ds = (i == 0 && !insn.is_branch);
        if (inblock_likely_ds || xblock_ds) {
            uint8_t* j_skip_gate = nullptr;
            if (xblock_ds) {
                EmitMovRegBaseDisp32(code_location, kEax, kStateReg, kBranchStateOff);
                EmitCmpRegImm32(code_location, kEax, MipsBranch::kCondLikely);
                j_skip_gate = EmitJnzLabel(code_location);
            }
            EmitMovRegImm32(code_location, kEcx, insn.guest_address + insn.length);
            EmitMovRegImm32(code_location, kEdx, self);
            EmitCall(code_location, reinterpret_cast<void*>(&NullifyLikelyHelper));
            EmitTestRegReg(code_location, kEax, kEax);
            uint8_t* j_run = EmitJzLabel(code_location);
            EmitRet(code_location);
            FixupLabel(j_run, code_location);
            if (j_skip_gate) FixupLabel(j_skip_gate, code_location);
        }

        if (i == 0 && block_ctx_.insn0_pcrel_guard) {
            EmitMovRegBaseDisp32(code_location, kEax, kStateReg, kBranchStateOff);
            EmitTestRegReg(code_location, kEax, kEax);
            uint8_t* j_not_ds = EmitJzLabel(code_location);
            EmitPush32(code_location, insn.guest_address);
            EmitPush32(code_location, self);
            EmitCall(code_location, reinterpret_cast<void*>(&PcrelDelaySlotHelper));
            FixupLabel(j_not_ds, code_location);
        }

        code_location = insn.place_fn(code_location, &insn, &block_ctx_);

        if (insn.ends_block) {
            EmitRet(code_location);
            terminated = true;
            break;
        }
        if (i > 0 && block_ctx_.insns[i - 1].is_branch) {
            EmitMovRegImm32(code_location, kEcx, insn.guest_address + insn.length);
            EmitMovRegImm32(code_location, kEdx, self);
            EmitCall(code_location, reinterpret_cast<void*>(&ResolveBranchHelper));
            EmitRet(code_location);
            terminated = true;
            break;
        }
        if (i == 0 && !insn.is_branch) {
            EmitMovRegImm32(code_location, kEcx, insn.guest_address + insn.length);
            EmitMovRegImm32(code_location, kEdx, self);
            EmitCall(code_location, reinterpret_cast<void*>(&ResolveBranchHelper));
            EmitTestRegReg(code_location, kEax, kEax);
            uint8_t* j_continue = EmitJzLabel(code_location);
            EmitRet(code_location);
            FixupLabel(j_continue, code_location);
        }
    }

    if (!terminated) {
        const MipsDecodedInsn& last = block_ctx_.insns[block_ctx_.num_insns - 1];
        const uint32_t next_pc = last.guest_address + last.length;
        EmitMovBaseDisp32Imm32(code_location, kStateReg, kPcOff, next_pc);
        EmitRet(code_location);
    }

    return static_cast<size_t>(code_location - start);
}

void* MipsBlockCompiler::FindBlockNativeStart(uint32_t guest_pc) {
    const JumpCacheEntry* e =
        cache_->Space(cpu_state_->isa_mode != 0u).JumpCacheProbe(guest_pc);
    if (!e) {
        return nullptr;
    }
    if (e->blk && e->blk->index_split != 0u && !TailStillMapped(e->blk)) {
        return nullptr;
    }
    return e->native;
}

bool MipsBlockCompiler::TailStillMapped(const JitBlock* blk) {
    if (blk->index_split == 0u) {
        return true;
    }
    uint32_t pa = 0;
    if (mmu_->Translate(cpu_state_, blk->guest_start + blk->index_split,
                        MipsAccess::kFetch, &pa) != MipsTlbResult::kMatch) {
        return false;
    }
    uint32_t key = 0;
    return cache_->TryBlockIndexKey(pa, &key) && key == blk->index_start2;
}

void* MipsBlockCompiler::Compile(uint32_t guest_pc) {
    /* MIPS16 instructions sit at halfword boundaries ("the instruction is
       located at the halfword boundary", U15509EJ2V0UM Table 3-19 p82 JR). */
    const bool m16 = cpu_state_->isa_mode != 0u;
    IsaBlockSpace& space = cache_->Space(m16);
    if (guest_pc & (m16 ? 0x1u : 0x3u)) {
        exceptions_->DeliverFetchAddressError(guest_pc);
        return nullptr;
    }

    uint32_t pa0 = 0;
    const MipsTlbResult fr =
        mmu_->Translate(cpu_state_, guest_pc, MipsAccess::kFetch, &pa0);
    if (fr != MipsTlbResult::kMatch) {
        exceptions_->DeliverFetchTlbException(guest_pc, fr);
        return nullptr;
    }
    const uint32_t page_off_mask = (1u << cpu_state_->min_page_shift) - 1u;
    block_ctx_.block_phys_page_base = pa0 & ~page_off_mask;
    const uint32_t phys_start =
        block_ctx_.block_phys_page_base | (guest_pc & page_off_mask);

    const uint8_t asid = static_cast<uint8_t>(cpu_state_->cp0_entryhi & 0xFFu);
    const bool outer_global = mmu_->ExecPageGlobal(cpu_state_, guest_pc);
    JitBlockIndex& idx = outer_global ? space.global : space.per_asid[asid];

    JitBlock* ex = idx.FindExact(guest_pc);
    if (ex) {
        if (ex->native_start && ex->phys_start == phys_start &&
            TailStillMapped(ex)) {
            space.JumpCacheInsert(guest_pc, ex->native_start, ex);
            return ex->native_start;
        }
        space.RemoveBlock(ex);
    }

    if (report_compiles_) host_->OnBlockCompiled();

    block_ctx_.tail_split          = 0;
    block_ctx_.tail_page_pa        = 0;
    block_ctx_.fetch_fault_pending = 0;
    block_ctx_.insn0_pcrel_guard   = 0;
    if (m16) decoder_->Decode16(&block_ctx_, guest_pc);
    else     decoder_->Decode(&block_ctx_, guest_pc);
    if (block_ctx_.num_insns == 0) {
        if (block_ctx_.fetch_fault_pending) {
            exceptions_->DeliverFetchTlbException(block_ctx_.fetch_fault_va,
                                                  block_ctx_.fetch_fault_res);
            return nullptr;
        }
        engine_.Halt(cerfengine::HaltReason::kUnmappedPhysical,
                     "MipsBlockCompiler::Compile: decoded 0 instructions at 0x%08X: the "
                     "instruction fetch at 0x%08X (PA 0x%08X) reached a physical address "
                     "with no memory backing", guest_pc, block_ctx_.fetch_unbacked_va,
                     block_ctx_.fetch_unbacked_pa);
    }

    const size_t code_est  = static_cast<size_t>(block_ctx_.num_insns) * 128u + 64u;
    const size_t slab_size = JitBlockIndex::OuterEntrySize() + code_est;
    JitCodeArena& arena = cache_->Arena();
    uint8_t* slab = arena.Allocate(slab_size);
    if (!slab) {
        cache_->Flush();
        slab = arena.Allocate(slab_size);
        if (!slab) {
            engine_.Panic("MipsBlockCompiler::Compile: arena exhausted (%zu bytes)",
                          slab_size);
        }
    }
    uint8_t* code = slab + JitBlockIndex::OuterEntrySize();

    const MipsDecodedInsn& last = block_ctx_.insns[block_ctx_.num_insns - 1];
    JitBlock nb{};
    nb.guest_start  = guest_pc;
    nb.guest_end    = last.guest_address + last.length - 1u;
    nb.phys_start   = phys_start;
    nb.native_start = code;
    JitBlock* stored = idx.PlaceOuterAt(slab, nb);
    space.IndexInsert(stored, &idx, cache_->BlockIndexKey(phys_start),
                      block_ctx_.tail_split,
                      block_ctx_.tail_split != 0u
                          ? cache_->BlockIndexKey(block_ctx_.tail_page_pa)
                          : kBlockUnindexed);
    if (!outer_global) space.MarkPopulated(asid);

    const size_t code_size = GenerateCodeChecked(code);
    arena.FreeUnusedTail(code + code_size);

    space.JumpCacheInsert(guest_pc, stored->native_start, stored);
    return stored->native_start;
}

}
