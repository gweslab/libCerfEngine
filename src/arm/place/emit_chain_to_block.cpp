#include <cstddef>

#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/byte_order.h>
#include <cerfengine/arm/block_context.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include <cerfengine/jit_block.h>
#include <cerfengine/jit_block_index.h>
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* QEMU tb_add_jump() (cpu-exec.c), which validates the destination
   before it links and records the link so tb-maint.c tb_jmp_unlink() can
   undo it. */
uint8_t* EmitChainToBlock(uint8_t* cursor, BlockContext* ctx,
                          uint32_t target_va, uint32_t slot) {
    using namespace x86;

    target_va = ArmFcseFold(target_va, ctx->fcse_pid);

    if (target_va == ctx->guest_start) {
        uint8_t* to_dispatcher[2];
        cursor = EmitDispatcherExitPoll(cursor, to_dispatcher);
        EmitJmp32(cursor, ctx->native_start);
        FixupLabel32(to_dispatcher[0], cursor);
        FixupLabel32(to_dispatcher[1], cursor);
        return cursor;
    }

    constexpr uint32_t kGranuleMask = ~(kArmBlockPageBytes - 1u);
    if ((target_va & kGranuleMask) != (ctx->guest_start & kGranuleMask)) {
        return cursor;
    }

    const uint32_t expect_pa =
        ctx->phys_start + (target_va - ctx->guest_start);
    JitBlock* dest = ctx->index->FindExact(target_va);
    if (dest != nullptr &&
        (dest->native_start == nullptr || dest->phys_start != expect_pa)) {
        dest = nullptr;
    }

    uint8_t* to_dispatcher[2];
    cursor = EmitDispatcherExitPoll(cursor, to_dispatcher);
    Emit8(cursor, 0xE9);
    uint8_t* const site = cursor;
    Emit32(cursor, 0u);

    ctx->self->chain_site[slot]       = site;
    ctx->self->chain_fallback[slot]   = cursor;
    ctx->self->chain_pending_va[slot] = target_va;
    if (dest != nullptr) {
        le::Put32(site, static_cast<uint32_t>(dest->native_start - (site + 4)));
        ctx->index->LinkChain(ctx->self, slot, dest, site, cursor);
    } else {
        le::Put32(site, static_cast<uint32_t>(cursor - (site + 4)));
    }
    FixupLabel32(to_dispatcher[0], cursor);
    FixupLabel32(to_dispatcher[1], cursor);
    return cursor;
}

}
