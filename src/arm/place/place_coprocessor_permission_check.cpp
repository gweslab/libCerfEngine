#include <cstddef>
#include <cstdint>

#include "../arm_emit_services.h"
#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

/* Coproc_Accepted() - ARM DDI 0406C.c A8.7.1 (pp. A8-295/A8-296); cp10/cp11
   access controls - B1.11.1 (p. B1-1229). */
uint8_t* PlaceCoprocessorPermissionCheck(uint8_t*      cursor,
                                         DecodedInsn*  d,
                                         BlockContext* ctx,
                                         bool          user_allowed,
                                         bool*         unconditional_und) {
    using namespace x86;

    const cerfengine::ArmFeatures* config = ctx->emit->ProcessorConfig();
    *unconditional_und = false;

    if (d->cp_num == 14u || d->cp_num == 15u) {
        if (user_allowed) {
            return cursor;
        }
        return EmitRaiseUndIfUserMode(cursor, d, ctx);
    }

    /* A2.9 (p. A2-94): CP8, CP9, CP12 and CP13 are reserved; any
       coprocessor access instruction attempting to access one of these
       coprocessors is UNDEFINED. */
    if (config->arch_v7 &&
        (d->cp_num == 8u || d->cp_num == 9u ||
         d->cp_num == 12u || d->cp_num == 13u)) {
        *unconditional_und = true;
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    /* B1.9.2 (p. B1-1206): a coprocessor instruction that is not
       implemented raises the Undefined Instruction exception. */
    if ((d->cp_num == 10u || d->cp_num == 11u) && !config->vfp && !config->neon) {
        *unconditional_und = true;
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    if (!config->arch_v6) {
        return cursor;
    }

    /* CPACR<2n+1:2n> (B4.1.40, p. B4-1551) per A8.7.1 Coproc_Accepted():
       00 UNDEFINED, 01 UNDEFINED at PL0, 10 UNPREDICTABLE, 11 permitted. */
    EmitMovRegBaseDisp32(cursor, kEax, kMmuReg,
        static_cast<int32_t>(offsetof(ArmMmuState, coprocessor_access)));
    EmitShrReg32Imm(cursor, kEax, static_cast<uint8_t>(2u * d->cp_num));
    EmitAndRegImm32(cursor, kEax, 3u);
    EmitCmpRegImm32(cursor, kEax, 3u);
    uint8_t* pass_label = EmitJzLabel32(cursor);
    EmitCmpRegImm32(cursor, kEax, 1u);
    uint8_t* und_label_field = EmitJnzLabel32(cursor);

    cursor = EmitCpsrUserModeTest(cursor);
    uint8_t* und_label_user = EmitJzLabel32(cursor);

    uint8_t* ok_label = EmitJmpLabel32(cursor);

    FixupLabel32(und_label_field, cursor);
    FixupLabel32(und_label_user, cursor);
    cursor = EmitRaiseUndTail(cursor, d, ctx);

    FixupLabel32(ok_label, cursor);
    FixupLabel32(pass_label, cursor);
    return cursor;
}

}
