#include <cstdint>

#include <cerfengine/engine.h>
#include "../arm_emit_services.h"
#include "../arm_vfp.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* EmitVfpDataOperation(uint8_t*      cursor,
                              DecodedInsn*  d,
                              BlockContext* ctx) {
    using namespace x86;
    ArmEmitServices* emit = ctx->emit;

    const uint32_t packed =
        ((d->cp_opc & 0xFu) << 19) |
        ((d->crn    & 0xFu) << 15) |
        ((d->crd    & 0xFu) << 11) |
        ((d->cp_num & 0xFu) <<  7) |
        ((d->cp     & 0x7u) <<  4) |
         (d->crm    & 0xFu);

    EmitPush32(cursor, packed);
    EmitPush32(cursor, d->guest_address);
    EmitMovRegImm32(cursor, kEax,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emit->Vfp())));
    EmitPushReg(cursor, kEax);
    EmitCall(cursor,
        reinterpret_cast<void*>(&ArmVfp::ExecuteCdpHelper));
    EmitAddRegImm32(cursor, kEsp, 12);
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* fall_through = EmitJzLabel(cursor);
    EmitRet(cursor);
    FixupLabel(fall_through, cursor);
    return cursor;
}

}
