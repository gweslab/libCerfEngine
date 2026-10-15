#include <cstddef>
#include <cstdint>

#include <cerfengine/arm/arm_cpu.h>
#include "../arm_emit_services.h"
#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

uint8_t* PlaceCps(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    using namespace x86;
    const cerfengine::ArmFeatures* config = ctx->emit->ProcessorConfig();

    /* CPSRWriteByInstr (p. B1-1153): SCTLR.NMFI gates setting F. */
    if (config->arch_v7) {
        EmitPushBaseDisp32(cursor, kMmuReg,
            static_cast<int32_t>(
                offsetof(ArmMmuState, effective_control_register)));
    } else {
        EmitPush32(cursor, 0u);
    }
    EmitPush32(cursor, 0u);
    EmitPush32(cursor, d->op1);
    EmitPush32(cursor, 0u);
    EmitPush32(cursor, d->immediate);
    EmitPush32(cursor,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->emit->Cpu())));
    EmitCall(cursor,
        reinterpret_cast<void*>(&ArmCpu::WriteCpsrByInstrHelper));
    EmitAddRegImm32(cursor, kEsp, 24);
    return cursor;
}

}
