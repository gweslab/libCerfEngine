#include "cerfir_build_core.h"

#include <cstdio>

#include "../emit_fault.h"

namespace cerfengine::cerfir {

void BuildCore::Reject(const char* what, Op op) {
    char text[128];
    std::snprintf(text, sizeof(text), "CerfIR builder: %s (op %s)", what, InfoOf(op).name);
    throw EmitFault(text);
}

void BuildCore::SlotRefFault(uint32_t disp) {
    char text[96];
    std::snprintf(text, sizeof(text), "CerfIR builder: slot 0x%X has no slot operand form", disp);
    throw EmitFault(text);
}

void BuildCore::SlotHazard(Op op) {
    Reject("slot operand consumed after a write to its slot", op);
}

Ref BuildCore::SubstituteSlotArg(Ref r) {
    return GetSlot(Type::kI32, static_cast<uint32_t>(SlotRefDisp(r)));
}

void BuildCore::NoteFlagWritesSlow(uint8_t store) {
    const FlagSlots& flags = block_.FlagSlotsOf();
    for (uint32_t i = 0; i < 4u; ++i) {
        if ((store & (1u << i)) != 0u) NoteSlotWrite(flags.disp[i], 1u);
    }
}

Ref BuildCore::Snapshot(Ref r) {
    return IsSlotRef(r) ? GetSlot(Type::kI32, static_cast<uint32_t>(SlotRefDisp(r))) : r;
}

void BuildCore::Share(Ref r, uint32_t uses) {
    if (uses == 0u) {
        Drop(r);
        return;
    }
    for (uint32_t i = 1; i < uses; ++i) Dup(r);
}

void BuildCore::DupView(Ref view) {
    const Inst& inst = block_.At(view);
    Dup(inst.args[0]);
    if (inst.op == Op::kPair) Dup(inst.args[1]);
}

}
