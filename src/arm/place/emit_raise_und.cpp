#include <cstddef>
#include <cstdint>

#include <cerfengine/byte_order.h>
#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>
#include "../arm_emit_services.h"
#include <cerfengine/arm/arm_translator.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* EmitRaiseUndTail(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->emit->Cpu())));
    EmitCall(cursor,
        reinterpret_cast<void*>(&ArmCpu::RaiseUndefinedExceptionHelper));
    EmitAddRegImm32(cursor, kEsp, 8);
    EmitRet(cursor);
    return cursor;
}

/* DDI 0406C.c B1.3.3 "Format of the CPSR and SPSRs" (p. B1-1148) places
   M[4:0] at bits[4:0]; Table B1-1 (B1.3.1, p. B1-1139) gives User = 10000,
   its Encoding column being "the corresponding CPSR.M field". */
uint8_t* EmitCpsrUserModeTest(uint8_t* cursor) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, ArmCpsrDisp());
    EmitAndRegImm32(cursor, kEax, kArmCpsrModeMask);
    EmitCmpRegImm32(cursor, kEax, ArmMode::kUser);
    return cursor;
}

uint8_t* EmitRaiseUndIfUserMode(uint8_t* cursor, DecodedInsn* d,
                                BlockContext* ctx) {
    using namespace x86;
    cursor = EmitCpsrUserModeTest(cursor);
    uint8_t* not_user = EmitJnzLabel32(cursor);
    cursor = EmitRaiseUndTail(cursor, d, ctx);
    FixupLabel32(not_user, cursor);
    return cursor;
}

uint8_t* EmitRaiseUndAndReturn(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    ArmEmitServices* emit = ctx->emit;
    const ArmCpuState* cs = emit->Cpu()->State();
    uint32_t word = 0u;
    bool word_ok = true;
    for (uint32_t off = 0u; off < d->length; off += 2u) {
        uint8_t* p =
            emit->Translator()->PeekVaToHost(d->actual_guest_address + off);
        if (p == nullptr) {
            word_ok = false;
            word = 0xFFFFFFFFu;
            break;
        }
        const uint32_t halfword = le::U16(p);
        word |= halfword
                << (ctx->thumb ? (d->length - 2u - off) * 8u : off * 8u);
    }
    CERFENGINE_LOG(emit->Owner(), kJit,
                   "EmitRaiseUndAndReturn: UNDEFINED encoding at pc=0x%08X (actual=0x%08X) "
                   "word=%s0x%08X T=%u mode=0x%02X\n",
                   d->guest_address, d->actual_guest_address,
                   word_ok ? "" : "UNMAPPED:", word,
                   cs->cpsr.bits.thumb_mode, cs->cpsr.bits.mode);
    return EmitRaiseUndTail(cursor, d, ctx);
}

}
