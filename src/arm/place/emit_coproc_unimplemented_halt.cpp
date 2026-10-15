#include <cstdint>

#include "../arm_emit_services.h"
#include "../arm_unimplemented_halt.h"
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit.h"

namespace cerfengine {

namespace {

uint8_t* EmitHaltCall(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx,
                      uint32_t cp_info, void* helper) {
    using namespace x86;
    EmitPush32(cursor, ctx->thumb ? 1u : 0u);
    EmitPush32(cursor, cp_info);
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor, static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(ctx->emit->UnimplementedHalt())));
    EmitCall(cursor, helper);
    return cursor;
}

}

uint8_t* EmitCoprocUnimplementedHalt(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) {
    const uint32_t cp_info = (d->cp_num << 24) | (d->crn << 16) | (d->crm << 8) |
                             (d->cp_opc << 4) | (d->cp << 1) | (d->l ? 1u : 0u);
    return EmitHaltCall(cursor, d, ctx, cp_info,
                        reinterpret_cast<void*>(
                            &ArmUnimplementedHalt::CoprocRegisterTransferHelper));
}

uint8_t* EmitCoprocDataOperationUnimplementedHalt(uint8_t* cursor, DecodedInsn* d,
                                                  BlockContext* ctx) {
    const uint32_t cp_info = (d->cp_num << 20) | (d->crd << 16) |
                             (d->crn << 12) | (d->crm << 8) |
                             (d->cp_opc << 4) | (d->cp << 1);
    return EmitHaltCall(cursor, d, ctx, cp_info,
                        reinterpret_cast<void*>(
                            &ArmUnimplementedHalt::CoprocDataOperationHelper));
}

uint8_t* EmitCoprocDataTransferUnimplementedHalt(uint8_t* cursor, DecodedInsn* d,
                                                 BlockContext* ctx) {
    const uint32_t cp_info = (d->cp_num << 16) | (d->crd << 12) | (d->rn << 8) |
                             ((d->p & 1u) << 4) | ((d->u & 1u) << 3) |
                             ((d->n & 1u) << 2) | ((d->w & 1u) << 1) |
                             (d->l ? 1u : 0u);
    return EmitHaltCall(cursor, d, ctx, cp_info,
                        reinterpret_cast<void*>(
                            &ArmUnimplementedHalt::CoprocDataTransferHelper));
}

}
