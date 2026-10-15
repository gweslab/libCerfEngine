#include "mips_cp0_emitter.h"
#include <cerfengine/calling_convention.h>

#include <cstdint>

#include "mips_block_context.h"
#include <cerfengine/mips/mips_cpu_state.h>
#include "mips_decoded_insn.h"
#include "mips_gpr_emit.h"
#include "mips_place_fns.h"
#include <cerfengine/mips/mips_system_host.h>
#include "../x86_emit_alu.h"

namespace cerfengine {

CERFENGINE_REGISTER(MipsCp0Emitter);

void MipsCp0Emitter::OnReady() {
    system_ = engine_.GetSetup().mips_system;
}

void CERFENGINE_FASTCALL MipsCp0Emitter::Dmtc0Width64Helper(MipsCp0Emitter* self, uint32_t rd) {
    self->engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                       "MipsCp0Emitter: DMTC0 to cp0[%u] with a non-sign-extended 64-bit "
                       "value - 64-bit CP0 state is not modeled", rd);
}

uint8_t* MipsCp0Emitter::EmitMfc0(uint8_t* cursor, MipsDecodedInsn* d,
                                  MipsBlockContext* ctx) {
    return EmitFromCop0(cursor, d, ctx);
}

uint8_t* MipsCp0Emitter::EmitDmfc0(uint8_t* cursor, MipsDecodedInsn* d,
                                   MipsBlockContext* ctx) {
    return EmitFromCop0(cursor, d, ctx);
}

uint8_t* MipsCp0Emitter::EmitMtc0(uint8_t* cursor, MipsDecodedInsn* d,
                                  MipsBlockContext* ctx) {
    return EmitToCop0(cursor, d, ctx, false);
}

uint8_t* MipsCp0Emitter::EmitDmtc0(uint8_t* cursor, MipsDecodedInsn* d,
                                   MipsBlockContext* ctx) {
    return EmitToCop0(cursor, d, ctx, true);
}

/* gpr[rt] = sext32(cp0[rd]) (QEMU translate.c gen_mfc0_load32: ext_i32_tl). */
uint8_t* MipsCp0Emitter::EmitFromCop0(uint8_t* cursor, MipsDecodedInsn* d,
                                      MipsBlockContext* ctx) {
    using namespace x86;
    if ((d->raw & 0x7u) != 0u) {
        return PlaceMipsUndefined(cursor, d, ctx);
    }
    const cerfengine::MipsCp0Decision dec = system_->MipsCp0(d->rd);
    if (!dec.present) {
        return PlaceMipsUndefined(cursor, d, ctx);
    }
    if (dec.read == nullptr && dec.offset < 0) {
        engine_.Panic("MipsCp0Emitter: the host decision for cp0[%u] has neither a read "
                      "callback nor a state offset", d->rd);
    }
    if (d->rt == 0) {
        return cursor;
    }
    if (dec.read != nullptr) {
        EmitMovRegImm32(cursor, kEcx,
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dec.read_context)));
        EmitCall(cursor, reinterpret_cast<void*>(dec.read));
        mips_emit::EmitStoreGprSextEax(cursor, d->rt);
        return cursor;
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, dec.offset);
    mips_emit::EmitStoreGprSextEax(cursor, d->rt);
    return cursor;
}

uint8_t* MipsCp0Emitter::EmitToCop0(uint8_t* cursor, MipsDecodedInsn* d,
                                    MipsBlockContext* ctx, bool is_dword) {
    using namespace x86;
    if ((d->raw & 0x7u) != 0u) {
        return PlaceMipsUndefined(cursor, d, ctx);
    }
    const cerfengine::MipsCp0Decision dec = system_->MipsCp0(d->rd);
    if (!dec.present) {
        return PlaceMipsUndefined(cursor, d, ctx);
    }
    if (!dec.writable) {
        return cursor;
    }
    if (dec.write == nullptr && dec.offset < 0) {
        engine_.Panic("MipsCp0Emitter: the host decision for writable cp0[%u] has neither "
                      "a write callback nor a state offset", d->rd);
    }
    if (is_dword) {
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rt));
        EmitCdq(cursor);
        EmitCmpRegBaseDisp32(cursor, kEdx, kStateReg, mips_emit::GprHiOff(d->rt));
        uint8_t* ok = EmitJzLabel(cursor);
        EmitMovRegImm32(cursor, kEcx, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this)));
        EmitMovRegImm32(cursor, kEdx, d->rd);
        EmitCall(cursor, reinterpret_cast<void*>(&Dmtc0Width64Helper));
        FixupLabel(ok, cursor);
    }
    if (dec.write != nullptr) {
        EmitMovRegImm32(cursor, kEcx,
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dec.write_context)));
        EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, mips_emit::GprLoOff(d->rt));
        EmitCall(cursor, reinterpret_cast<void*>(dec.write));
        return cursor;
    }
    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, mips_emit::GprLoOff(d->rt));
    EmitMovBaseDisp32Reg(cursor, kStateReg, dec.offset, kEax);
    return cursor;
}

}
