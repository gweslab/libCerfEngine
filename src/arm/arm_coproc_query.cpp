#include "arm_coproc_query.h"

#include "arm_emit_services.h"

namespace cerfengine {

ArmCoprocDecision QueryCoprocDecision(const DecodedInsn* d, BlockContext* ctx, ArmCoprocKind kind) {
    ArmCoprocAccess access;
    access.kind   = kind;
    access.cp_num = d->cp_num;
    access.read   = d->l != 0u;
    switch (kind) {
    case ArmCoprocKind::kRegisterTransfer:
        access.opc1 = d->cp_opc;
        access.crn  = d->crn;
        access.crm  = d->crm;
        access.opc2 = d->cp;
        access.rt   = d->rd;
        break;
    case ArmCoprocKind::kRegisterTransferDouble:
        access.opc1 = (static_cast<uint32_t>(d->offset) >> 4) & 0xFu;
        access.crm  = static_cast<uint32_t>(d->offset) & 0xFu;
        access.rt   = d->crd;
        access.rt2  = d->rn;
        break;
    case ArmCoprocKind::kDataTransfer:
        access.crd = d->crd;
        break;
    case ArmCoprocKind::kDataOperation:
        access.opc1 = d->cp_opc;
        access.crn  = d->crn;
        access.crm  = d->crm;
        access.opc2 = d->cp;
        access.crd  = d->crd;
        break;
    }
    return ctx->emit->System()->ArmCoprocessor(access);
}

uint32_t CoprocRegisterTransferHaltInfo(const DecodedInsn* d) {
    return (d->cp_num << 24) | (d->crn << 16) | (d->crm << 8) | (d->cp_opc << 4) |
           (d->cp << 1) | (d->l ? 1u : 0u);
}

uint32_t CoprocDataOperationHaltInfo(const DecodedInsn* d) {
    return (d->cp_num << 20) | (d->crd << 16) | (d->crn << 12) | (d->crm << 8) |
           (d->cp_opc << 4) | (d->cp << 1);
}

uint32_t CoprocDataTransferHaltInfo(const DecodedInsn* d) {
    return (d->cp_num << 16) | (d->crd << 12) | (d->rn << 8) | ((d->p & 1u) << 4) |
           ((d->u & 1u) << 3) | ((d->n & 1u) << 2) | ((d->w & 1u) << 1) | (d->l ? 1u : 0u);
}

}
