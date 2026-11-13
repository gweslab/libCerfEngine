#include "sh_dsp_unit.h"

#include <cerfengine/sh/sh_cpu.h>
#include <cerfengine/sh/sh_cpu_state.h>
#include "sh_dsp_alu.h"

namespace cerfengine {

CERFENGINE_REGISTER(ShDspUnit);

namespace {

constexpr uint32_t kXyIncrement  = 2u;
constexpr uint32_t kModuloLimit  = 0xFFFEu;
constexpr uint32_t kLowWord      = 0x0000FFFFu;
constexpr uint32_t kHighWord     = 0xFFFF0000u;
constexpr uint32_t kModuloEndBit = 16u;

}

void ShDspUnit::OnReady() {
    state_ = engine_.Get<ShCpu>().State();
    alu_   = &engine_.Get<ShDspAlu>();
}

void CERFENGINE_CDECL ShDspUnit::ParallelHelper(ShDspUnit* unit, uint32_t code, uint32_t operation) {
    unit->ExecuteParallel(code, operation);
}

void CERFENGINE_CDECL ShDspUnit::TransferHelper(ShDspUnit* unit, uint32_t field_a) {
    unit->UpdatePointer(field_a, true);
    unit->UpdatePointer(field_a, false);
}

uint32_t ShDspUnit::NextPointer(uint32_t pointer, ShDspAddressing mode, bool x) const {
    const uint32_t modulo_bits = state_->sr & (kShSrDmx | kShSrDmy);
    const bool     modulo      = x ? modulo_bits == kShSrDmx : (modulo_bits & kShSrDmy) != 0u;
    if (modulo && (pointer & kModuloLimit) == ((state_->mod >> kModuloEndBit) & kModuloLimit)) {
        return (pointer & kHighWord) | (state_->mod & kLowWord);
    }
    const uint32_t increment = mode == ShDspAddressing::kIncrement ? kXyIncrement
                                                                   : state_->gpr[x ? kShDspIxGpr : kShDspIyGpr];
    if (x) return pointer + increment;
    return (pointer & kHighWord) | ((pointer + increment) & kLowWord);
}

void ShDspUnit::UpdatePointer(uint32_t field_a, bool x) {
    const ShDspAddressing mode = ShDspBusMode(field_a, x);
    if (!ShDspUpdatesPointer(mode)) return;
    uint32_t& pointer = state_->gpr[ShDspPointerGpr(field_a, x)];
    pointer           = NextPointer(pointer, mode, x);
}

bool ShDspUnit::LoadConflicts(uint32_t field_a, bool x, const ShDspOutcome& outcome) const {
    return ShDspBusMode(field_a, x) != ShDspAddressing::kNone && !ShDspTransferStore(field_a, x) &&
           outcome.alu.specified && ShDspTransferReg(field_a, x) == outcome.alu.reg;
}

void ShDspUnit::ExecuteParallel(uint32_t code, uint32_t operation) {
    const uint32_t     field_a = code >> 16;
    const ShDspOutcome outcome = alu_->Evaluate(code & kLowWord, ShDspUnpackOperation(operation));
    const bool         dual    = outcome.alu.specified && outcome.multiply.specified;
    if (LoadConflicts(field_a, true, outcome) || LoadConflicts(field_a, false, outcome) ||
        (dual && outcome.alu.reg == outcome.multiply.reg)) {
        engine_.Halt(HaltReason::kUnimplemented,
                     "ShDspUnit: SH-DSP instruction 0x%08X names one destination register in two parallel operations",
                     code);
    }
    alu_->Apply(outcome);
    UpdatePointer(field_a, true);
    UpdatePointer(field_a, false);
}

}
