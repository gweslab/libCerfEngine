#include "arm_block_compiler.h"
#include <cerfengine/calling_convention.h>

#include <cstdio>
#include <cstring>

#include "../emit_fault.h"
#include <cerfengine/byte_order.h>
#include <cerfengine/engine.h>
#include "../x86_emit.h"
#include "../x86_emit_alu.h"
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
#include <cerfengine/arm/arm_translator.h>
#include <cerfengine/arm/place_fns.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmBlockCompiler);

namespace {

constexpr size_t kArmTrampolineBytes  = 64;
constexpr size_t kArmBlockReserveBytes = 128u * 1024u;

constexpr int32_t kCycleOff =
    static_cast<int32_t>(offsetof(ArmCpuState, guest_cycle_counter));
constexpr int32_t kPcOff =
    ArmGprDisp(ArmGpr::kR15);

void EmitCycleAdvance(uint8_t*& cursor, uint32_t cycles) {
    using namespace x86;
    if (cycles >= 0x80u) {
        char text[96];
        std::snprintf(text, sizeof(text),
                      "ArmBlockCompiler::GenerateCode: cycle cost %u exceeds the "
                      "sign-extended imm8 range", cycles);
        throw cerfengine::EmitFault(text);
    }
    EmitAddBaseDisp32Imm8(cursor, kStateReg, kCycleOff,
                          static_cast<uint8_t>(cycles));
}

/* ARM ARM DDI 0406C.c Table A8-1, p. A8-288: EQ Z==1, NE Z==0, CS C==1,
   CC C==0, MI N==1, PL N==0, VS V==1, VC V==0, HI C==1 and Z==0, LS C==0 or
   Z==1, GE N==V, LT N!=V, GT Z==0 and N==V, LE Z==1 or N!=V. */
uint8_t* EmitConditionGuard(uint8_t*& cursor, uint32_t cond) {
    using namespace x86;
    switch (cond) {
    case 0u:
    case 1u:
        EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmZfDisp(), 0u);
        return cond == 0u ? EmitJzLabel32(cursor) : EmitJnzLabel32(cursor);
    case 2u:
    case 3u:
        EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmCfDisp(), 0u);
        return cond == 2u ? EmitJzLabel32(cursor) : EmitJnzLabel32(cursor);
    case 4u:
    case 5u:
        EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmNfDisp(), 0u);
        return cond == 4u ? EmitJzLabel32(cursor) : EmitJnzLabel32(cursor);
    case 6u:
    case 7u:
        EmitCmpByteBaseDisp32Imm8(cursor, kStateReg, ArmVfDisp(), 0u);
        return cond == 6u ? EmitJzLabel32(cursor) : EmitJnzLabel32(cursor);
    case 8u:
    case 9u:
        EmitMovByteRegBaseDisp32(cursor, kAl, kStateReg, ArmCfDisp());
        EmitSubByteRegBaseDisp32(cursor, kAl, kStateReg, ArmZfDisp());
        EmitCmpReg8Imm8(cursor, kAl, 1u);
        return cond == 8u ? EmitJnzLabel32(cursor) : EmitJzLabel32(cursor);
    case 10u:
    case 11u:
        EmitMovByteRegBaseDisp32(cursor, kAl, kStateReg, ArmNfDisp());
        EmitCmpReg8BaseDisp32(cursor, kAl, kStateReg, ArmVfDisp());
        return cond == 10u ? EmitJnzLabel32(cursor) : EmitJzLabel32(cursor);
    case 12u:
    case 13u:
        EmitMovByteRegBaseDisp32(cursor, kAl, kStateReg, ArmNfDisp());
        EmitXorByteRegBaseDisp32(cursor, kAl, kStateReg, ArmVfDisp());
        EmitOrByteRegBaseDisp32(cursor, kAl, kStateReg, ArmZfDisp());
        return cond == 12u ? EmitJnzLabel32(cursor) : EmitJzLabel32(cursor);
    default: break;
    }
    char text[80];
    std::snprintf(text, sizeof(text),
                  "ArmBlockCompiler::GenerateCode: condition field %u at guard emit", cond);
    throw cerfengine::EmitFault(text);
}

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

    block_ctx_.emit = emit_;
    try {
        BuildTrampolines();
    } catch (const cerfengine::EmitFault& fault) {
        engine_.Panic("%s", fault.what());
    }
}

void ArmBlockCompiler::BuildTrampolines() {
    using namespace x86;

    const uint32_t self =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
    uint8_t* cursor = cache_->Arena().AllocatePermanent(kArmTrampolineBytes);

    block_ctx_.sctlr_write_target = cursor;
    EmitMovRegImm32(cursor, kEdx, self);
    EmitCall(cursor, reinterpret_cast<void*>(&ArmBlockCompiler::SctlrWriteHelper));
    EmitRet(cursor);

    block_ctx_.raise_abort_data_helper_target = cursor;
    EmitMovRegImm32(cursor, kEdx, self);
    EmitJmp32(cursor,
              reinterpret_cast<void*>(&ArmBlockCompiler::RaiseAbortDataHelper));
}

void CERFENGINE_FASTCALL ArmBlockCompiler::SctlrWriteHelper(uint32_t          value,
                                                            ArmBlockCompiler* compiler) {
    if (compiler->mmu_->State()->control_register.word == value) return;

    compiler->mmu_->SetControlRegister(value);
    compiler->cache_->PendFlush();
}

void CERFENGINE_FASTCALL ArmBlockCompiler::RaiseAbortDataHelper(
    uint32_t guest_pc, ArmBlockCompiler* compiler) {
    if (compiler->mmu_->io_pending()) {
        compiler->routed_->Complete(guest_pc);
        return;
    }
    compiler->cpu_->RaiseAbortDataException(guest_pc);
}

void CERFENGINE_CDECL ArmBlockCompiler::TraceDispatchPcHelper(ArmBlockCompiler* compiler,
                                                              uint32_t          pc) {
    compiler->host_->OnArmPcTrace(pc, compiler->cpu_state_->gprs,
                                  ArmPackCpsr(*compiler->cpu_state_));
}

void ArmBlockCompiler::Decode(uint32_t guest_pc, uint32_t folded_pc) {
    const uint32_t page_end =
        (folded_pc & ~(kArmBlockPageBytes - 1u)) + kArmBlockPageBytes;
    fetch_fault_ = false;
    wide_split_  = 0u;

    uint32_t itstate     = thumb_ ? ArmItFromCpsr(*cpu_state_) : 0u;
    tail_itstate_        = 0u;
    tail_itstate_valid_  = false;

    uint32_t i = 0;
    for (; i < kMaxArmInsnPerBlock && folded_pc < page_end; ++i) {
        DecodedInsn& insn = block_ctx_.insns[i];
        std::memset(&insn, 0, sizeof(insn));

        uint8_t* host = translator_->TranslateExecute(guest_pc);
        if (host == nullptr) break;

        insn.guest_address        = guest_pc;
        insn.actual_guest_address = folded_pc;

        bool decoded;
        if (thumb_) {
            const uint16_t half = le::U16(host);
            if (thumb32_decoder_->IsWide(half)) {
                const bool crosses = folded_pc + 4u > page_end;
                if (crosses && i != 0u) break;
                uint8_t* hi = translator_->TranslateExecute(guest_pc + 2u);
                if (hi == nullptr) {
                    if (i == 0u) {
                        fetch_fault_    = true;
                        fetch_fault_pc_ = guest_pc;
                    }
                    break;
                }
                if (crosses) {
                    wide_split_ = 2u;
                    wide_pa2_   = translator_->LastExecPa();
                }
                insn.length = 4u;
                decoded     = thumb32_decoder_->DecodeThumb32(
                    &insn, (static_cast<uint32_t>(half) << 16) | le::U16(hi));
            } else {
                insn.length = 2u;
                decoded     = thumb_decoder_->DecodeThumb(&insn, half);
            }
        } else {
            ArmOpcode op{};
            op.word     = le::U32(host);
            insn.length = 4u;
            decoded     = decoder_->DecodeArm(&insn, op);
        }

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
                insn.place_fn = &EmitRaiseUndAndReturn;
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

        /* B1.9.2 "Conditional execution of undefined instructions"
           (p. B1-1209): "The conditional execution rules ... apply to all
           instructions. This includes undefined instructions". */
        if (!decoded) {
            insn.place_fn = &EmitRaiseUndAndReturn;
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
    try {
        return GenerateCode(code, code_end);
    } catch (const cerfengine::EmitFault& fault) {
        engine_.Panic("%s (block at guest PC 0x%08X)", fault.what(),
                      block_ctx_.insns[0].guest_address);
    }
}

size_t ArmBlockCompiler::GenerateCode(uint8_t* code, uint8_t* code_end) {
    using namespace x86;
    if (block_ctx_.num_insns == 0u) {
        engine_.Panic("ArmBlockCompiler::GenerateCode called with num_insns == 0");
    }

    uint8_t* const start = code;
    uint8_t*       cursor = code;
    block_ctx_.native_start = start;
    const uint32_t self =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));

    for (uint32_t i = 0; i < block_ctx_.num_insns; ++i) {
        if (static_cast<size_t>(code_end - cursor) < kArmBlockReserveBytes) {
            return 0;
        }
        DecodedInsn& insn = block_ctx_.insns[i];

        if (insn.itstate_valid != 0u) {
            cursor = EmitItStateStore(cursor, insn.itstate);
        }

        if (host_->HasPcTrace(insn.guest_address)) {
            EmitMovBaseDisp32Imm32(cursor, kStateReg, kPcOff,
                                   insn.guest_address);
            EmitPush32(cursor, insn.guest_address);
            EmitPush32(cursor, self);
            EmitCall(cursor, reinterpret_cast<void*>(&TraceDispatchPcHelper));
            EmitAddRegImm32(cursor, kEsp, 8);
        }

        EmitCycleAdvance(cursor, timing_->ArmCycleCost(insn));
        uint8_t* skip = nullptr;
        if (insn.cond != 14u) {
            skip = EmitConditionGuard(cursor, insn.cond);
        }

        cursor = insn.place_fn(cursor, &insn, &block_ctx_);

        if (skip != nullptr) {
            FixupLabel32(skip, cursor);
        }
        if (insn.itstate_after_valid != 0u) {
            cursor = EmitItStateStore(cursor, insn.itstate_after);
        }

        if (cursor > code_end) {
            engine_.Panic("ArmBlockCompiler::GenerateCode: insn %u at 0x%08X "
                          "emitted past the slab end", i, insn.guest_address);
        }
    }

    const DecodedInsn& last = block_ctx_.insns[block_ctx_.num_insns - 1];
    if (tail_itstate_valid_) {
        cursor = EmitItStateStore(cursor, tail_itstate_);
    }
    if (last.cond != 14u || !last.r15_modified || last.r15_conditional != 0u) {
        EmitMovBaseDisp32Imm32(cursor, kStateReg, kPcOff,
                               last.guest_address + last.length);
        /* QEMU ARM gen_lookup_tb (translate.c): DISAS_EXIT ends the
           TB without goto_tb chaining - arm_tr_tb_stop's default case emits
           tcg_gen_exit_tb(NULL, 0). */
        if (!last.context_sync) {
            cursor = EmitChainToBlock(cursor, &block_ctx_,
                                      last.guest_address + last.length, 1u);
        }
        EmitRet(cursor);
    }

    return static_cast<size_t>(cursor - start);
}

void* ArmBlockCompiler::Compile(uint32_t guest_pc) {
    const bool     thumb      = cpu_state_->cpsr.bits.thumb_mode != 0u;
    const uint32_t align_mask = thumb ? 1u : 3u;
    thumb_                    = thumb;
    if ((guest_pc & align_mask) != 0u) {
        engine_.Panic("ArmBlockCompiler::Compile: misaligned PC 0x%08X", guest_pc);
    }

    ArmMmuState*   mmu_state = mmu_->State();
    const uint32_t folded_pc = ArmFcseFold(guest_pc, mmu_state->fcse_fold_id);

    if (translator_->TranslateExecute(guest_pc) == nullptr) {
        if (mmu_->io_pending()) {
            engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                         "ArmBlockCompiler::Compile: instruction fetch at 0x%08X "
                         "resolves to peripheral PA 0x%08X",
                         guest_pc, mmu_->io_pending_address());
        }
        cpu_->RaiseAbortPrefetchException(guest_pc);
        return nullptr;
    }
    const uint32_t phys_start = translator_->LastExecPa();

    JitCodeArena&  arena        = cache_->Arena();
    IsaBlockSpace& space        = cache_->Space(thumb);
    const uint8_t  asid         = static_cast<uint8_t>(mmu_state->contextidr & 0xFFu);
    const bool     outer_global = translator_->ExecPageGlobal(folded_pc);
    JitBlockIndex& idx          = outer_global ? space.global : space.per_asid[asid];

    JitBlock* ex = idx.FindExact(folded_pc);
    if (ex != nullptr) {
        if (ex->phys_start == phys_start) {
            space.JumpCacheInsert(folded_pc, ex->native_start, ex);
            return ex->native_start;
        }
        space.RemoveBlock(ex);
    }

    if (report_compiles_) host_->OnBlockCompiled();

    Decode(guest_pc, folded_pc);
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

            JitBlock nb{};
            nb.guest_start  = folded_pc;
            const DecodedInsn& tail =
                block_ctx_.insns[block_ctx_.num_insns - 1];
            nb.guest_end    = tail.actual_guest_address + tail.length - 1u;
            nb.phys_start   = phys_start;
            nb.native_start = code;
            JitBlock* stored = idx.PlaceOuterAt(slab, nb);
            space.IndexInsert(stored, &idx, phys_start, wide_split_,
                              wide_split_ != 0u ? wide_pa2_ : kBlockUnindexed);
            if (!outer_global) space.MarkPopulated(asid);

            block_ctx_.index       = &idx;
            block_ctx_.self        = stored;
            block_ctx_.guest_start = folded_pc;
            block_ctx_.phys_start  = phys_start;
            block_ctx_.fcse_pid    = mmu_state->fcse_fold_id;
            block_ctx_.jump_cache  = space.jump_cache;
            block_ctx_.thumb       = thumb;

            JitBlock* const src = idx.FindExact(predecessor_va_);
            if (src != nullptr && src != stored &&
                phys_start ==
                    src->phys_start + (folded_pc - src->guest_start)) {
                for (uint32_t s = 0; s < 2u; ++s) {
                    if (src->chain_target[s] != nullptr ||
                        src->chain_site[s] == nullptr ||
                        src->chain_pending_va[s] != folded_pc) {
                        continue;
                    }
                    le::Put32(src->chain_site[s],
                              static_cast<uint32_t>(code - (src->chain_site[s] + 4)));
                    idx.LinkChain(src, s, stored, src->chain_site[s],
                                  src->chain_fallback[s]);
                }
            }

            const size_t code_size = GenerateCodeChecked(code, slab + slab_size);
            if (code_size != 0u) {
                arena.FreeUnusedTail(code + code_size);
                space.JumpCacheInsert(folded_pc, stored->native_start, stored);
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
