#include "arm_block_compiler.h"
#include <cerfengine/calling_convention.h>

#include <cstddef>

#include "../emit_fault.h"
#include "../jit_predecessor_link.h"
#include "../steady_clock_ns.h"
#include <cerfengine/byte_order.h>
#include <cerfengine/engine.h>
#include "../zero_fill.h"
#include <cerfengine/arm/arm_cpu.h>
#include "arm_decoder.h"
#include "thumb32_decoder.h"
#include "thumb_decoder.h"
#include "arm_emit_services.h"
#include <cerfengine/arm/arm_instruction_timing.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_mmu_state.h>
#include "arm_opcode.h"
#include "arm_routed_instruction.h"
#include "arm_translation_cache.h"
#include "cerfir/arm_ir_codegen.h"
#include <cerfengine/arm/arm_translator.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmBlockCompiler);

namespace {

/* ARM DDI 0406C.c D15.6.3 (p. D15-2606): "Only ARMv4 and ARMv5 support tiny (1KB) pages." */
constexpr uint32_t kThumb2PageMask = 0xFFFu;

}

void ArmBlockCompiler::OnReady() {
    cache_      = &engine_.Get<ArmTranslationCache>();
    emit_       = &engine_.Get<ArmEmitServices>();
    cpu_        = &engine_.Get<ArmCpu>();
    cpu_state_  = cpu_->State();
    decoder_    = &engine_.Get<ArmDecoder>();
    thumb_decoder_ = &engine_.Get<ThumbDecoder>();
    thumb32_decoder_ = &engine_.Get<Thumb32Decoder>();
    mmu_        = &engine_.Get<ArmMmu>();
    translator_ = &engine_.Get<ArmTranslator>();
    routed_     = &engine_.Get<ArmRoutedInstruction>();
    timing_     = engine_.GetSetup().arm_timing != nullptr
                      ? engine_.GetSetup().arm_timing
                      : &uniform_timing_;
    host_       = &engine_.Host();
    report_compiles_ = engine_.GetSetup().report_block_compiles;
    ir_codegen_ = &engine_.Get<ArmIrCodegen>();

    block_ctx_.emit = emit_;
    for (uint32_t p = 0; p < kBlockPrivilegeCount; ++p) {
        jump_cache_key_disp_[p] = JumpCacheKeyDisp(p);
    }
    link_.granule_bytes = kArmBlockPageBytes;
}

void CERFENGINE_FASTCALL ArmBlockCompiler::SctlrWriteHelper(uint32_t          value,
                                                            ArmBlockCompiler* compiler) {
    if (compiler->mmu_->State()->control_register.word == value) return;

    compiler->mmu_->SetControlRegister(value);
    compiler->cache_->PendFlush();
}

void CERFENGINE_FASTCALL ArmBlockCompiler::RaiseAbortDataHelper(
    uint32_t guest_pc, ArmBlockCompiler* compiler, uint32_t raw) {
    compiler->routed_->CompleteOrRaiseAbort(guest_pc, raw);
}

void CERFENGINE_CDECL ArmBlockCompiler::TraceDispatchPcHelper(ArmBlockCompiler* compiler,
                                                              uint32_t          pc) {
    compiler->host_->OnArmPcTrace(pc, compiler->cpu_state_->gprs,
                                  ArmPackCpsr(*compiler->cpu_state_));
}

bool ArmBlockCompiler::SecondPageMapped(const JitBlock* blk, uint32_t guest_pc) {
    if (blk->index_split == 0u) return true;
    return translator_->TranslateExecute(guest_pc + blk->index_split) != nullptr &&
           translator_->LastExecPa() == blk->index_start2;
}

CERFENGINE_FORCEINLINE uint8_t* ArmBlockCompiler::FetchInBlockPage(uint32_t folded_pc) {
    const uint32_t delta = folded_pc - fetch_folded_;
    if (fetch_tracked_) ArmNoteCodeTracking<ArmMmuAccess::kExecute>(*mmu_->State(), fetch_pa_ + delta);
    return fetch_host_ + delta;
}

template <bool kThumb>
void ArmBlockCompiler::Decode(uint32_t guest_pc, uint32_t folded_pc, uint8_t* entry_host,
                              uint32_t entry_pa, bool entry_tracked) {
    const uint32_t page_end =
        (folded_pc & ~(kArmBlockPageBytes - 1u)) + kArmBlockPageBytes;
    fetch_fault_   = false;
    wide_split_    = 0u;
    fetch_host_    = entry_host;
    fetch_folded_  = folded_pc;
    fetch_pa_      = entry_pa;
    fetch_tracked_ = entry_tracked;

    uint32_t itstate     = kThumb ? ArmItFromCpsr(*cpu_state_) : 0u;
    tail_itstate_        = 0u;
    tail_itstate_valid_  = false;

    uint32_t i = 0;
    for (; i < kMaxArmInsnPerBlock && folded_pc < page_end; ++i) {
        DecodedInsn& insn = block_ctx_.insns[i];
        ZeroFill(insn);

        uint8_t* const host = FetchInBlockPage(folded_pc);

        insn.guest_address        = guest_pc;
        insn.actual_guest_address = folded_pc;

        bool decoded;
        if constexpr (kThumb) {
            const uint16_t half = le::U16(host);
            if (thumb32_decoder_->IsWide(half)) {
                const bool crosses = folded_pc + 4u > page_end;
                if (crosses && i != 0u) break;
                uint8_t* hi = crosses ? translator_->TranslateExecute(guest_pc + 2u)
                                      : FetchInBlockPage(folded_pc + 2u);
                if (hi == nullptr) {
                    if (i == 0u) {
                        fetch_fault_    = true;
                        fetch_fault_pc_ = guest_pc;
                    }
                    break;
                }
                if (crosses && ((guest_pc + 2u) & kThumb2PageMask) == 0u) {
                    wide_split_ = 2u;
                    wide_pa2_   = translator_->LastExecPa();
                }
                insn.length = 4u;
                insn.raw    = (static_cast<uint32_t>(half) << 16) | le::U16(hi);
                decoded     = thumb32_decoder_->DecodeThumb32(&insn, insn.raw);
            } else {
                insn.length = 2u;
                insn.raw    = half;
                decoded     = thumb_decoder_->DecodeThumb(&insn, half);
            }
        } else {
            ArmOpcode op{};
            op.word     = le::U32(host);
            insn.length = 4u;
            insn.raw    = op.word;
            decoded     = decoder_->DecodeArm(&insn, op);
        }

        if constexpr (kThumb) {
            if (ArmItInBlock(itstate)) {
                const uint32_t next      = ArmItAdvance(itstate);
                const bool     last_slot = next == 0u;
                const bool unpredictable =
                    insn.itstate_valid != 0u || insn.cond != 14u ||
                    (insn.r15_modified && !last_slot) || insn.und_in_it != 0u;

                if (insn.uncond_in_it == 0u) {
                    insn.cond = (itstate >> 4) & 0xFu;
                }
                insn.itstate             = itstate;
                insn.itstate_valid       = 1u;
                insn.itstate_after       = next;
                insn.itstate_after_valid = last_slot ? 1u : 0u;
                itstate                  = next;

                if (unpredictable) {
                    insn.kind = ArmInsnKind::kUndefined;
                    ++i;
                    break;
                }
                /* A8.8.6 (p. A8-310) and its 18 siblings: "setflags =
                   !InITBlock()". */
                if (insn.s_outside_it != 0u) {
                    insn.s = 0u;
                }
            } else if (insn.itstate_valid != 0u) {
                itstate            = insn.itstate;
                insn.itstate_valid = 0u;
            }
        }

        /* B1.9.2 "Conditional execution of undefined instructions"
           (p. B1-1209): "The conditional execution rules ... apply to all
           instructions. This includes undefined instructions". */
        if (!decoded) {
            insn.kind = ArmInsnKind::kUndefined;
            ++i;
            break;
        }
        /* ARM DDI 0406C.c B3.15.5 (p. B3-1461): a Context synchronization
           operation is where a system-control-register write becomes
           guaranteed visible. */
        if (insn.r15_modified || insn.context_sync) {
            ++i;
            break;
        }

        guest_pc  += insn.length;
        folded_pc += insn.length;
    }
    block_ctx_.num_insns = i;
    tail_itstate_        = itstate;
    tail_itstate_valid_  = ArmItInBlock(itstate);
}

size_t ArmBlockCompiler::GenerateCodeChecked(uint8_t* code, uint8_t* code_end) {
    if (block_ctx_.num_insns == 0u) {
        engine_.Panic("ArmBlockCompiler::GenerateCode called with num_insns == 0");
    }
    try {
        link_.native_start = code;
        return ir_codegen_->Generate(
            ArmIrBlockInput{&block_ctx_, timing_, timing_ == &uniform_timing_, this,
                            tail_itstate_, tail_itstate_valid_, &link_},
            code, code_end);
    } catch (const cerfir::HostArenaFull&) {
        return 0u;
    } catch (const cerfengine::EmitFault& fault) {
        engine_.Panic("%s (block at guest PC 0x%08X)", fault.what(),
                      block_ctx_.insns[0].guest_address);
    }
}

void* ArmBlockCompiler::Compile(uint32_t guest_pc) {
    const bool     thumb      = cpu_state_->cpsr.bits.thumb_mode != 0u;
    const uint32_t align_mask = thumb ? 1u : 3u;
    if ((guest_pc & align_mask) != 0u) {
        engine_.Panic("ArmBlockCompiler::Compile: misaligned PC 0x%08X", guest_pc);
    }

    ArmMmuState*   mmu_state = mmu_->State();
    const uint32_t folded_pc = ArmFcseFold(guest_pc, mmu_state->fcse_fold_id);

    uint8_t* const entry_host = translator_->TranslateExecute(guest_pc);
    if (entry_host == nullptr) {
        if (mmu_->io_pending()) {
            engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                         "ArmBlockCompiler::Compile: instruction fetch at 0x%08X "
                         "resolves to peripheral PA 0x%08X",
                         guest_pc, mmu_->io_pending_address());
        }
        cpu_->RaiseAbortPrefetchException(guest_pc);
        return nullptr;
    }
    const uint32_t phys_start    = translator_->LastExecPa();
    const bool     entry_tracked = translator_->LastExecTracked();

    static_assert(kBlockPrivileged == 0u && kBlockUser == 1u);
    const bool     user         = cpu_state_->cpsr.bits.mode == ArmMode::kUser;
    const uint32_t privilege    = static_cast<uint32_t>(user);
    JitCodeArena&  arena        = cache_->Arena();
    IsaBlockSpace& space        = cache_->Space(thumb);
    const uint8_t  asid         = static_cast<uint8_t>(mmu_state->contextidr & 0xFFu);
    const bool     outer_global = translator_->LastExecGlobal();
    BlockIndexSet& set          = space.indices[privilege];
    JitBlockIndex& idx          = set.Index(outer_global, asid);

    JitBlock* ex = idx.FindExact(folded_pc);
    if (ex != nullptr) {
        if (ex->phys_start == phys_start && SecondPageMapped(ex, guest_pc)) {
            space.JumpCacheInsert(privilege, folded_pc, ex->native_start, ex);
            return ex->native_start;
        }
        space.RemoveBlock(ex);
    }

    const uint64_t compile_start_ns = report_compiles_ ? SteadyClockNs() : 0u;

    if (thumb) {
        Decode<true>(guest_pc, folded_pc, entry_host, phys_start, entry_tracked);
    } else {
        Decode<false>(guest_pc, folded_pc, entry_host, phys_start, entry_tracked);
    }
    if (block_ctx_.num_insns == 0u) {
        if (fetch_fault_) {
            if (mmu_->io_pending()) {
                engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                             "ArmBlockCompiler::Compile: instruction fetch at 0x%08X "
                             "resolves to peripheral PA 0x%08X",
                             fetch_fault_pc_ + 2u, mmu_->io_pending_address());
            }
            cpu_->RaiseAbortPrefetchException(fetch_fault_pc_);
            return nullptr;
        }
        engine_.Panic("ArmBlockCompiler::Compile: decoded 0 insns at 0x%08X", guest_pc);
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        const size_t slab_size = arena.Remaining();
        uint8_t*     slab      = arena.Allocate(slab_size);
        if (slab_size > JitBlockIndex::OuterEntrySize()) {
            uint8_t* code = slab + JitBlockIndex::OuterEntrySize();

            const DecodedInsn& tail = block_ctx_.insns[block_ctx_.num_insns - 1];
            JitBlock* stored = idx.PlaceOuterAt(slab, folded_pc,
                                                tail.actual_guest_address + tail.length - 1u,
                                                phys_start, code);
            space.IndexInsert(stored, &idx, phys_start, wide_split_,
                              wide_split_ != 0u ? wide_pa2_ : kBlockUnindexed);
            if (!outer_global) set.MarkPopulated(asid);

            link_.index               = &idx;
            link_.self                = stored;
            link_.key_start           = folded_pc;
            link_.phys_start          = phys_start;
            link_.fold_value          = mmu_state->fcse_fold_id;
            link_.mode_set            = thumb;
            link_.jump_cache          = space.jump_cache;
            link_.jump_cache_key_disp = jump_cache_key_disp_[privilege];
            link_.privilege           = privilege;
            link_.translation_off     = mmu_state->effective_control_register.bits.m == 0u;
            block_ctx_.thumb             = thumb;
            block_ctx_.alignment_checked = mmu_state->effective_control_register.bits.a != 0u;
            block_ctx_.unaligned_faults  = mmu_->UnalignedAccessesFault();

            LinkPredecessor(idx, predecessor_va_, stored);

            const size_t code_size = GenerateCodeChecked(code, slab + slab_size);
            if (code_size != 0u) {
                arena.FreeUnusedTail(code + code_size);
                space.JumpCacheInsert(privilege, folded_pc, stored->native_start, stored);
                if (report_compiles_) {
                    host_->OnBlockCompiled(BlockCompileStats{
                        block_ctx_.num_insns,
                        static_cast<uint32_t>(code_size),
                        SteadyClockNs() - compile_start_ns});
                }
                return stored->native_start;
            }
            space.RemoveBlock(stored);
        }
        cache_->Flush();
    }

    engine_.Panic("ArmBlockCompiler::Compile: %u-insn block at 0x%08X does not "
                  "fit a flushed %zu-byte arena",
                  block_ctx_.num_insns, guest_pc, arena.MaxSize());
}

}
