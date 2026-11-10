#include "sh_block_compiler.h"

#include <algorithm>

#include <cerfengine/byte_order.h>
#include <cerfengine/isa_block_space.h>
#include <cerfengine/physical_map.h>
#include <cerfengine/sh/sh_cpu.h>
#include <cerfengine/sh/sh_cpu_state.h>
#include <cerfengine/unmapped_windows.h>
#include "../host_platform.h"
#include "../jit_block_emitter.h"
#include "../steady_clock_ns.h"
#include "sh_address_space.h"
#include "cerfir/sh_ir_codegen.h"
#include "sh_decoder.h"
#include "sh_memory_access.h"
#include "sh_translation_cache.h"

namespace cerfengine {

CERFENGINE_REGISTER(ShBlockCompiler);

void ShBlockCompiler::OnReady() {
    cpu_state_       = engine_.Get<ShCpu>().State();
    map_             = &engine_.Get<PhysicalMap>();
    cache_           = &engine_.Get<ShTranslationCache>();
    mmu_             = &engine_.Get<ShMmu>();
    codegen_         = &engine_.Get<ShIrCodegen>();
    decoder_         = &engine_.Get<ShDecoder>();
    emitter_         = &engine_.Get<JitBlockEmitter>();
    report_compiles_ = engine_.GetSetup().report_block_compiles;
}

uint32_t ShBlockCompiler::ProcessorMode() const {
    return (cpu_state_->sr & kShSrMd) != 0u ? kBlockPrivileged : kBlockUser;
}

uint32_t ShBlockCompiler::MemoryMode(uint32_t mode, uint32_t count) const {
    if (mode == kBlockUser) return static_cast<uint32_t>(ShMemoryMode::kUser);
    const bool returns = std::any_of(insns_, insns_ + count,
                                     [](const ShDecodedInsn& d) { return d.kind == ShInsnKind::kRte; });
    return static_cast<uint32_t>(returns ? ShMemoryMode::kRestored : ShMemoryMode::kPrivileged);
}

bool ShBlockCompiler::TranslateFetch(uint32_t va, bool deliver, uint32_t exception_pc, ShTranslation* out) {
    if ((va & 1u) != 0u || (ProcessorMode() == kBlockUser && ShAddressSpace::PrivilegedOnly(va))) {
        if (deliver) mmu_->RaiseAddressError(va, ShAccess::kFetch, exception_pc);
        return false;
    }
    if (!UnmappedWindows::Contains(va) && !ShAddressSpace::Paged(va)) {
        engine_.Halt(HaltReason::kUnimplemented, "ShBlockCompiler: instruction fetch at PC 0x%08X in P4 is not modeled",
                     va);
    }
    const bool mapped = deliver && mmu_->Translates(va) ? mmu_->Translate(va, ShAccess::kFetch, exception_pc, out)
                                                        : mmu_->ProbeFetch(va, out);
    if (!mapped) return false;
    if (out->pa >= ShAddressSpace::kArea7PaBase) {
        engine_.Halt(HaltReason::kUnimplemented,
                     "ShBlockCompiler: instruction fetch at PC 0x%08X reaches the reserved area 7 PA 0x%08X", va,
                     out->pa);
    }
    return true;
}

bool ShBlockCompiler::TailStillMapped(const JitBlock* blk) {
    if (blk->index_split == 0u) return true;
    ShTranslation t{};
    return mmu_->ProbeFetch(blk->guest_start + blk->index_split, &t) && t.pa == blk->phys_split;
}

uint32_t ShBlockCompiler::DecodeBlock(uint32_t pc, uint32_t phys_start) {
    const uint32_t page_end    = (pc | (kPageBytes - 1u)) + 1u;
    uint32_t       count       = 0;
    bool           slot_next   = false;
    uint32_t       shadow_left = 0;
    tail_split_                = 0u;
    tail_page_pa_              = 0u;
    for (uint32_t va = pc;; va += 2u) {
        const bool must_follow = slot_next || shadow_left != 0u;
        if (!must_follow && (count == kMaxBlockInsns || va >= page_end)) break;
        if (va == page_end) {
            if (shadow_left != 0u && mmu_->Translates(va)) {
                engine_.Halt(HaltReason::kUnimplemented,
                             "ShBlockCompiler: instruction fetch at PC 0x%08X within two instructions after LDTLB "
                             "reaches a translated area",
                             va);
            }
            const bool    rte_slot = slot_next && insns_[count - 1u].kind == ShInsnKind::kRte;
            ShTranslation tail{};
            if (!TranslateFetch(va, count == 1u && !rte_slot, pc, &tail)) {
                if (rte_slot) {
                    engine_.Halt(HaltReason::kUnimplemented,
                                 "ShBlockCompiler: the delay slot fetch of RTE at PC 0x%08X raises an MMU exception",
                                 va - 2u);
                }
                if (count == 1u) return 0u;
                --count;
                break;
            }
            tail_split_   = va - pc;
            tail_page_pa_ = tail.pa;
        }
        const uint32_t pa   = va < page_end ? phys_start + (va - pc) : tail_page_pa_ + (va - page_end);
        const uint8_t* host = map_->PeekRead(pa);
        if (!host) {
            engine_.Halt(HaltReason::kUnmappedPhysical,
                         "ShBlockCompiler: instruction fetch at PC 0x%08X reads PA 0x%08X, which no memory backs",
                         va, pa);
        }
        const ShDecodedInsn d = decoder_->Decode(le::U16(host), va);
        if (d.kind == ShInsnKind::kUnimplemented) {
            if (count == 0u || must_follow) {
                engine_.Halt(HaltReason::kUnimplemented, "ShBlockCompiler: SH opcode 0x%04X at PC 0x%08X", d.raw,
                             va);
            }
            break;
        }
        if (!slot_next && d.delayed_branch != 0u && count + 2u > kMaxBlockInsns) break;
        if (d.kind == ShInsnKind::kLdtlb) {
            if (must_follow) {
                engine_.Halt(HaltReason::kUnimplemented,
                             "ShBlockCompiler: LDTLB at PC 0x%08X in a delay slot or within two instructions after "
                             "LDTLB",
                             va);
            }
            if (count + kLdtlbBlockInsns > kMaxBlockInsns) break;
        }
        if (shadow_left == kLdtlbShadowInsns && (d.kind == ShInsnKind::kBranchDelayed || d.ends_block != 0u)) {
            engine_.Halt(HaltReason::kUnimplemented,
                         "ShBlockCompiler: instruction 0x%04X at PC 0x%08X right after LDTLB branches conditionally "
                         "or ends the block",
                         d.raw, va);
        }
        insns_[count++] = d;
        if (shadow_left != 0u) --shadow_left;
        if (d.kind == ShInsnKind::kLdtlb) shadow_left = kLdtlbShadowInsns;
        if (slot_next || d.ends_block != 0u) break;
        slot_next = d.delayed_branch != 0u;
    }
    return count;
}

void* ShBlockCompiler::FindBlockNativeStart(uint32_t pc) {
    const IsaBlockSpace&  space = cache_->Space();
    const JumpCacheEntry* e     = space.JumpCacheProbe(ProcessorMode(), pc);
    if (e == nullptr) return nullptr;
    if (e->native != nullptr) return e->native;
    JitBlock* blk = space.JumpCacheBlock(e);
    if (!TailStillMapped(blk)) return nullptr;
    return blk->native_start;
}

void* ShBlockCompiler::Compile(uint32_t pc) {
    ShTranslation start{};
    if (!TranslateFetch(pc, true, pc, &start)) return nullptr;
    const uint32_t phys_start = start.pa;
    const uint8_t  asid       = mmu_->Asid();
    const uint32_t mode       = ProcessorMode();
    IsaBlockSpace& space      = cache_->Space();
    BlockIndexSet& set        = space.indices[mode];
    JitBlockIndex& idx        = set.Index(start.global, asid);

    if (JitBlock* ex = idx.FindExact(pc)) {
        if (ex->native_start != nullptr && ex->phys_start == phys_start && TailStillMapped(ex)) {
            space.JumpCacheInsert(mode, pc, ex->index_split != 0u ? nullptr : ex->native_start, ex);
            return ex->native_start;
        }
        const platform::CodeWriteScope write;
        space.RemoveBlock(ex);
    }

    const uint64_t start_ns = report_compiles_ ? SteadyClockNs() : 0u;
    const uint32_t count    = DecodeBlock(pc, phys_start);
    if (count == 0u) return nullptr;
    const ShDecodedInsn& last = insns_[count - 1u];
    const uint32_t       split_key =
        tail_split_ != 0u ? cache_->BlockIndexKey(tail_page_pa_, pc + tail_split_) : kBlockUnindexed;
    const BlockEmitIndex emit_index{&space,      &set,      &idx,           start.global, asid,
                                    cache_->BlockIndexKey(phys_start, pc), tail_split_, split_key,
                                    tail_page_pa_, predecessor_va_, false};

    struct Target final : BlockEmitTarget {
        ShBlockCompiler* self;
        IsaBlockSpace*   space;
        JitBlockIndex*   idx;
        uint32_t         pc;
        uint32_t         phys_start;
        uint32_t         count;
        uint32_t         mode;
        uint32_t         memory_mode;

        size_t Generate(JitBlock* stored, uint8_t* code, uint8_t* code_end) override {
            cerfir::BlockLink& l = self->link_;
            l.index               = idx;
            l.self                = stored;
            l.key_start           = pc;
            l.phys_start          = phys_start;
            l.native_start        = code;
            l.granule_bytes       = kPageBytes;
            l.jump_cache          = space->jump_cache;
            l.jump_cache_key_disp = JumpCacheKeyDisp(mode);
            l.privilege           = memory_mode;
            return self->codegen_->Generate(self->insns_, count, &l, code, code_end);
        }
        void FlushCache() override { self->cache_->Flush(); }
    } target;
    target.self        = this;
    target.space       = &space;
    target.idx         = &idx;
    target.pc          = pc;
    target.phys_start  = phys_start;
    target.count       = count;
    target.mode        = mode;
    target.memory_mode = MemoryMode(mode, count);

    return emitter_->Emit(BlockEmitRequest{&cache_->Arena(), &emit_index, pc, pc, last.guest_address + 1u,
                                           phys_start, mode, count, start_ns},
                          target);
}

}
