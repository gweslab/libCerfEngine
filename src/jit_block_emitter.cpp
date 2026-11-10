#include "jit_block_emitter.h"

#include "cerfir/cerfir_host_lowering.h"
#include "emit_fault.h"
#include "host_platform.h"
#include "jit_code_arena.h"
#include "jit_predecessor_link.h"
#include "steady_clock_ns.h"

namespace cerfengine {

CERFENGINE_REGISTER(JitBlockEmitter);

void JitBlockEmitter::OnReady() {
    host_            = &engine_.Host();
    report_compiles_ = engine_.GetSetup().report_block_compiles;
}

size_t JitBlockEmitter::GenerateChecked(const BlockEmitRequest& request, BlockEmitTarget& target, JitBlock* stored,
                                        uint8_t* code, uint8_t* code_end) {
    try {
        return target.Generate(stored, code, code_end);
    } catch (const cerfir::HostArenaFull&) {
        return 0u;
    } catch (const EmitFault& fault) {
        engine_.Panic("%s (block at guest PC 0x%08X)", fault.what(), request.guest_pc);
    }
}

uint8_t* JitBlockEmitter::Emit(const BlockEmitRequest& request, BlockEmitTarget& target) {
    JitCodeArena&                  arena = *request.arena;
    const BlockEmitIndex* const    index = request.index;
    const platform::CodeWriteScope write;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const size_t slab_size = arena.Remaining();
        uint8_t*     slab      = arena.Allocate(slab_size);
        if (slab_size > JitBlockIndex::OuterEntrySize()) {
            uint8_t*  code   = slab + JitBlockIndex::OuterEntrySize();
            JitBlock* stored = nullptr;
            if (index != nullptr) {
                stored = index->idx->PlaceOuterAt(slab, request.key_start, request.guest_end, request.phys_start,
                                                  index->split != 0u ? index->split_pa : 0u, code);
                index->space->IndexInsert(stored, index->idx, index->key, index->split,
                                          index->split != 0u ? index->split_key : kBlockUnindexed);
                if (!index->outer_global) index->set->MarkPopulated(index->asid);
                LinkPredecessor(*index->idx, index->predecessor_va, stored);
            }
            const size_t code_size = GenerateChecked(request, target, stored, code, slab + slab_size);
            if (code_size != 0u) {
                arena.FreeUnusedTail(code + code_size);
                platform::FlushInstructionCache(code, code_size);
                if (index != nullptr) {
                    const bool cache_native = index->cache_split_native || stored->index_split == 0u;
                    index->space->JumpCacheInsert(request.privilege, request.key_start,
                                                  cache_native ? code : nullptr, stored);
                }
                if (report_compiles_) {
                    host_->OnBlockCompiled(BlockCompileStats{request.num_insns,
                                                             static_cast<uint32_t>(code_size),
                                                             SteadyClockNs() - request.start_ns});
                }
                return code;
            }
            if (index != nullptr) index->space->RemoveBlock(stored);
        }
        target.FlushCache();
    }
    engine_.Panic("JitBlockEmitter: %u-insn block at 0x%08X does not fit a flushed %zu-byte arena",
                  request.num_insns, request.key_start, arena.MaxSize());
}

}
