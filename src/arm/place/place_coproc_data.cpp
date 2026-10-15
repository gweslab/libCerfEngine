#include <cerfengine/engine.h>
#include "../arm_emit_services.h"
#include <cerfengine/arm/arm_system_host.h>
#include <cerfengine/arm/place_fns.h>

namespace cerfengine {

namespace {

using cerfengine::ArmCoprocAction;
using cerfengine::ArmCoprocDecision;
using cerfengine::ArmCoprocKind;

[[noreturn]] void PanicOnShape(BlockContext* ctx, DecodedInsn* d, const char* kind) {
    ctx->emit->Owner().Panic(
        "coprocessor decision for %s p%u CRd=c%u at guest pc=0x%08X is neither "
        "kUndefined nor kUnimplemented", kind, d->cp_num, d->crd, d->guest_address);
}

}

uint8_t* PlaceCoprocDataTransfer(uint8_t*      cursor,
                                 DecodedInsn*  d,
                                 BlockContext* ctx) {
    bool unconditional_und = false;
    if (d->cp_num == 10u || d->cp_num == 11u) {
        cursor = PlaceCoprocessorPermissionCheck(cursor, d, ctx, false, &unconditional_und);
        if (unconditional_und) return cursor;
        return EmitVfpDataTransfer(cursor, d, ctx);
    }
    const ArmCoprocDecision dec = QueryCoprocDecision(d, ctx, ArmCoprocKind::kDataTransfer);
    cursor = PlaceCoprocessorPermissionCheck(cursor, d, ctx, false, &unconditional_und);
    if (unconditional_und) return cursor;
    cursor = EmitCoprocGate(cursor, d, ctx, dec.gate_mask);
    switch (dec.action) {
    case ArmCoprocAction::kUndefined:     return EmitRaiseUndAndReturn(cursor, d, ctx);
    case ArmCoprocAction::kUnimplemented: return EmitCoprocDataTransferUnimplementedHalt(cursor, d, ctx);
    default:                              PanicOnShape(ctx, d, "LDC/STC");
    }
}

uint8_t* PlaceCoprocDataOperation(uint8_t*      cursor,
                                  DecodedInsn*  d,
                                  BlockContext* ctx) {
    bool unconditional_und = false;
    if (d->cp_num == 10u || d->cp_num == 11u) {
        cursor = PlaceCoprocessorPermissionCheck(cursor, d, ctx, false, &unconditional_und);
        if (unconditional_und) return cursor;
        return EmitVfpDataOperation(cursor, d, ctx);
    }
    const ArmCoprocDecision dec = QueryCoprocDecision(d, ctx, ArmCoprocKind::kDataOperation);
    cursor = PlaceCoprocessorPermissionCheck(cursor, d, ctx, false, &unconditional_und);
    if (unconditional_und) return cursor;
    cursor = EmitCoprocGate(cursor, d, ctx, dec.gate_mask);
    switch (dec.action) {
    case ArmCoprocAction::kUndefined:     return EmitRaiseUndAndReturn(cursor, d, ctx);
    case ArmCoprocAction::kUnimplemented: return EmitCoprocDataOperationUnimplementedHalt(cursor, d, ctx);
    default:                              PanicOnShape(ctx, d, "CDP");
    }
}

}
