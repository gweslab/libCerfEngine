#include "mips_block_compiler.h"
#include <cerfengine/calling_convention.h>

#include <cstddef>

#include "../emit_fault.h"
#include "../host_platform.h"
#include "../jit_predecessor_link.h"
#include "../steady_clock_ns.h"
#include <cerfengine/engine.h>
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include "mips_emit_services.h"
#include <cerfengine/mips/mips_exception_delivery.h>
#include "mips_insn_decoder.h"
#include <cerfengine/mips/mips_mmu.h>
#include "mips_translation_cache.h"
#include "cerfir/mips_ir_codegen.h"

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
    ir_codegen_ = &engine_.Get<MipsIrCodegen>();

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

void* MipsBlockCompiler::FindBlockNativeStart(uint32_t guest_pc, uint32_t privilege) {
    const IsaBlockSpace&  space = cache_->Space(cpu_state_->isa_mode != 0u);
    const JumpCacheEntry* e     = space.JumpCacheProbe(privilege, guest_pc);
    if (e == nullptr) return nullptr;
    if (e->native != nullptr) return e->native;
    const JitBlock* blk = space.JumpCacheBlock(e);
    if (blk == nullptr || !TailStillMapped(blk, privilege)) return nullptr;
    return blk->native_start;
}

bool MipsBlockCompiler::TailStillMapped(const JitBlock* blk, uint32_t privilege) {
    if (blk->index_split == 0u) {
        return true;
    }
    uint32_t pa = 0;
    if (mmu_->TranslateAs(privilege, cpu_state_, blk->guest_start + blk->index_split,
                          MipsAccess::kFetch, &pa) != MipsTlbResult::kMatch) {
        return false;
    }
    uint32_t key = 0;
    return cache_->TryBlockIndexKey(pa, &key) && key == blk->index_start2;
}

bool MipsBlockCompiler::TranslateEntry(uint32_t guest_pc, uint32_t privilege,
                                       uint32_t* phys_start) {
    /* MIPS16 instructions sit at halfword boundaries ("the instruction is
       located at the halfword boundary", U15509EJ2V0UM Table 3-19 p82 JR). */
    const bool m16 = cpu_state_->isa_mode != 0u;
    if (guest_pc & (m16 ? 0x1u : 0x3u)) {
        exceptions_->DeliverFetchAddressError(guest_pc);
        return false;
    }

    uint32_t pa0 = 0;
    const MipsTlbResult fr =
        mmu_->TranslateAs(privilege, cpu_state_, guest_pc, MipsAccess::kFetch, &pa0);
    if (fr != MipsTlbResult::kMatch) {
        exceptions_->DeliverFetchTlbException(guest_pc, fr);
        return false;
    }
    const uint32_t page_off_mask = (1u << cpu_state_->min_page_shift) - 1u;
    block_ctx_.block_phys_page_base = pa0 & ~page_off_mask;
    *phys_start = block_ctx_.block_phys_page_base | (guest_pc & page_off_mask);
    return true;
}

bool MipsBlockCompiler::DecodeBlock(uint32_t guest_pc, uint32_t privilege) {
    block_ctx_.privilege           = privilege;
    block_ctx_.tail_split          = 0;
    block_ctx_.tail_page_pa        = 0;
    block_ctx_.fetch_fault_pending = 0;
    block_ctx_.uncacheable         = 0;
    if (cpu_state_->isa_mode != 0u) decoder_->Decode16(&block_ctx_, guest_pc);
    else                            decoder_->Decode(&block_ctx_, guest_pc);
    if (block_ctx_.num_insns != 0) return true;
    if (block_ctx_.fetch_fault_pending) {
        exceptions_->DeliverFetchTlbException(block_ctx_.fetch_fault_va,
                                              block_ctx_.fetch_fault_res);
        return false;
    }
    engine_.Halt(cerfengine::HaltReason::kUnmappedPhysical,
                 "MipsBlockCompiler::Compile: decoded 0 instructions at 0x%08X: the "
                 "instruction fetch at 0x%08X (PA 0x%08X) reached a physical address "
                 "with no memory backing", guest_pc, block_ctx_.fetch_unbacked_va,
                 block_ctx_.fetch_unbacked_pa);
}

void* MipsBlockCompiler::CompilePendingEntry(uint32_t guest_pc, uint32_t privilege) {
    uint32_t phys_start = 0;
    if (!TranslateEntry(guest_pc, privilege, &phys_start)) return nullptr;
    const uint64_t start_ns = report_compiles_ ? SteadyClockNs() : 0u;
    if (!DecodeBlock(guest_pc, privilege)) return nullptr;
    return EmitIrBlock(guest_pc, phys_start, nullptr, privilege, true, start_ns);
}

void* MipsBlockCompiler::EmitIrBlock(uint32_t guest_pc, uint32_t phys_start,
                                     const IrIndex* index, uint32_t privilege, bool pending,
                                     uint64_t start_ns) {
    JitCodeArena&                  arena = cache_->Arena();
    const MipsDecodedInsn&         last  = block_ctx_.insns[block_ctx_.num_insns - 1u];
    const platform::CodeWriteScope write;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const size_t slab_size = arena.Remaining();
        uint8_t*     slab      = arena.Allocate(slab_size);
        if (slab_size > JitBlockIndex::OuterEntrySize()) {
            uint8_t*  code   = slab + JitBlockIndex::OuterEntrySize();
            JitBlock* stored = nullptr;
            if (index != nullptr) {
                stored = index->idx->PlaceOuterAt(slab, guest_pc,
                                                  last.guest_address + last.length - 1u,
                                                  phys_start, code);
                index->space->IndexInsert(stored, index->idx, cache_->BlockIndexKey(phys_start),
                                          block_ctx_.tail_split,
                                          block_ctx_.tail_split != 0u
                                              ? cache_->BlockIndexKey(block_ctx_.tail_page_pa)
                                              : kBlockUnindexed);
                if (!index->outer_global) index->set->MarkPopulated(index->asid);
                LinkPredecessor(*index->idx, predecessor_va_, stored);
            }
            FillLink(index, stored, guest_pc, phys_start, code, privilege);
            const size_t code_size = GenerateIrChecked(code, slab + slab_size, pending);
            if (code_size != 0u) {
                arena.FreeUnusedTail(code + code_size);
                platform::FlushInstructionCache(code, code_size);
                if (index != nullptr) {
                    index->space->JumpCacheInsert(privilege, guest_pc,
                                                  stored->index_split != 0u ? nullptr : code,
                                                  stored);
                }
                if (report_compiles_) {
                    host_->OnBlockCompiled(BlockCompileStats{
                        block_ctx_.num_insns, static_cast<uint32_t>(code_size),
                        SteadyClockNs() - start_ns});
                }
                return code;
            }
            if (index != nullptr) index->space->RemoveBlock(stored);
        }
        cache_->Flush();
    }
    engine_.Panic("MipsBlockCompiler::Compile: %u-insn block at 0x%08X does not fit a "
                  "flushed %zu-byte arena", block_ctx_.num_insns, guest_pc, arena.MaxSize());
}

void MipsBlockCompiler::FillLink(const IrIndex* index, JitBlock* stored, uint32_t guest_pc,
                                 uint32_t phys_start, uint8_t* code, uint32_t privilege) {
    const bool m16            = cpu_state_->isa_mode != 0u;
    link_.index               = index != nullptr ? index->idx : nullptr;
    link_.self                = stored;
    link_.key_start           = guest_pc;
    link_.phys_start          = phys_start;
    link_.native_start        = code;
    link_.granule_bytes       = 1u << cpu_state_->min_page_shift;
    link_.mode_set            = m16;
    link_.jump_cache          = cache_->Space(m16).jump_cache;
    link_.jump_cache_key_disp = JumpCacheKeyDisp(privilege);
    link_.privilege           = privilege;
}

size_t MipsBlockCompiler::GenerateIrChecked(uint8_t* code, uint8_t* code_end, bool pending) {
    try {
        return ir_codegen_->Generate(MipsIrBlockInput{&block_ctx_, this, pending, &link_}, code,
                                     code_end);
    } catch (const cerfir::HostArenaFull&) {
        return 0u;
    } catch (const cerfengine::EmitFault& fault) {
        engine_.Panic("%s (block at guest PC 0x%08X)", fault.what(),
                      block_ctx_.insns[0].guest_address);
    }
}

void* MipsBlockCompiler::Compile(uint32_t guest_pc, uint32_t privilege) {
    const bool m16 = cpu_state_->isa_mode != 0u;
    IsaBlockSpace& space = cache_->Space(m16);
    uint32_t phys_start = 0;
    if (!TranslateEntry(guest_pc, privilege, &phys_start)) return nullptr;

    const uint8_t asid = static_cast<uint8_t>(cpu_state_->cp0_entryhi & 0xFFu);
    const bool outer_global = mmu_->ExecPageGlobal(cpu_state_, guest_pc);
    BlockIndexSet& set = space.indices[privilege];
    JitBlockIndex& idx = set.Index(outer_global, asid);

    JitBlock* ex = idx.FindExact(guest_pc);
    if (ex) {
        if (ex->native_start && ex->phys_start == phys_start &&
            TailStillMapped(ex, privilege)) {
            space.JumpCacheInsert(privilege, guest_pc,
                                  ex->index_split != 0u ? nullptr : ex->native_start, ex);
            return ex->native_start;
        }
        const platform::CodeWriteScope write;
        space.RemoveBlock(ex);
    }

    const uint64_t compile_start_ns = report_compiles_ ? SteadyClockNs() : 0u;

    if (!DecodeBlock(guest_pc, privilege)) return nullptr;
    const IrIndex index{&space, &set, &idx, outer_global, asid};
    return EmitIrBlock(guest_pc, phys_start, block_ctx_.uncacheable != 0u ? nullptr : &index,
                       privilege, false, compile_start_ns);
}

}
