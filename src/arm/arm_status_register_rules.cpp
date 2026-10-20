#include "arm_status_register_rules.h"

#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/engine.h>

#include "arm_emit_services.h"

namespace cerfengine {

/* A8.8.109 (p. A8-496): MRS "if d == 15 then UNPREDICTABLE"; B9.3.12
   (p. B9-1998): MSR (register) "if mask == '0000' then UNPREDICTABLE; if
   n == 15 then UNPREDICTABLE". */
bool ArmMrsUnpredictable(const DecodedInsn& d) {
    return d.rd == ArmGpr::kR15;
}

bool ArmMsrRegisterUnpredictable(const DecodedInsn& d, const ArmFeatures& f) {
    return d.rm == ArmGpr::kR15 || (d.crn == 0u && f.arch_v7);
}

/* B9.3.11 (p. B9-1996): "if mask == '0000' && R == '0' then SEE "Related
   encodings""; "if mask == '0000' then UNPREDICTABLE". */
bool ArmMsrImmediateUnpredictable(const DecodedInsn& d, const BlockContext* ctx) {
    if (d.crn != 0u || !ctx->emit->ProcessorConfig()->arch_v7) return false;
    if (d.n == 0u) {
        ctx->emit->Owner().Panic(
            "MSR (immediate): hint encoding (mask==0, R==0) routed here at pc=0x%08X",
            d.guest_address);
    }
    return true;
}

}
