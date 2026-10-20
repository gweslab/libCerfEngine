#include "arm_undefined_encoding_log.h"

#include <cerfengine/byte_order.h>
#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_translator.h>

#include "arm_emit_services.h"

namespace cerfengine {

void LogUndefinedEncoding(const DecodedInsn* d, const BlockContext* ctx) {
    ArmEmitServices*   emit    = ctx->emit;
    const ArmCpuState* cs      = emit->Cpu()->State();
    uint32_t           word    = 0u;
    bool               word_ok = true;
    for (uint32_t off = 0u; off < d->length; off += 2u) {
        uint8_t* p = emit->Translator()->PeekVaToHost(d->actual_guest_address + off);
        if (p == nullptr) {
            word_ok = false;
            word    = 0xFFFFFFFFu;
            break;
        }
        const uint32_t halfword = le::U16(p);
        word |= halfword << (ctx->thumb ? (d->length - 2u - off) * 8u : off * 8u);
    }
    CERFENGINE_LOG(emit->Owner(), kJit,
                   "LogUndefinedEncoding: UNDEFINED encoding at pc=0x%08X (actual=0x%08X) "
                   "word=%s0x%08X T=%u mode=0x%02X\n",
                   d->guest_address, d->actual_guest_address, word_ok ? "" : "UNMAPPED:", word,
                   cs->cpsr.bits.thumb_mode, cs->cpsr.bits.mode);
}

}
