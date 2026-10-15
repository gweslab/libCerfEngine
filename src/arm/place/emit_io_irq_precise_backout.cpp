#include <cstdint>

#include "../arm_emit_services.h"
#include <cerfengine/arm/arm_interrupt_channel.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/block_context.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* EmitIoIrqPreciseBackout(uint8_t* cursor, DecodedInsn* d,
                                 BlockContext* ctx) {
    using namespace x86;

    EmitPushReg(cursor, kEcx);
    EmitPush32 (cursor, d->guest_address);
    EmitPush32 (cursor, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
        ctx->emit->InterruptChannel())));
    EmitCall(cursor, reinterpret_cast<void*>(
        &ArmInterruptChannel::BackOutForIrqHelper));
    EmitAddRegImm32(cursor, kEsp, 8u);
    EmitPopReg(cursor, kEcx);

    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* proceed = EmitJzLabel32(cursor);

    ArmMmu* mmu = ctx->emit->Mmu();
    EmitMovDwordPtrImm32(cursor, mmu->IoPendingValidPtr(), 0u);
    EmitMovDwordPtrImm32(cursor, mmu->IoPendingAddressPtr(), 0u);
    EmitRet(cursor);

    FixupLabel32(proceed, cursor);
    return cursor;
}

uint8_t* EmitIoIrqPreciseBackoutIfIo(uint8_t* cursor, DecodedInsn* d,
                                     BlockContext* ctx) {
    using namespace x86;

    /* 8B /r mod=00 disp32 (SDM Vol. 2B 4-35 MOV). */
    Emit8(cursor, 0x8B);
    EmitModRmDisp32(cursor, kEax);
    Emit32(cursor, static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(ctx->emit->Mmu()->IoPendingValidPtr())));
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* not_io = EmitJzLabel32(cursor);

    cursor = EmitIoIrqPreciseBackout(cursor, d, ctx);

    FixupLabel32(not_io, cursor);
    return cursor;
}

}
