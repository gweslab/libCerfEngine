#include "arm_unimplemented_halt.h"

namespace cerfengine {

CERFENGINE_REGISTER(ArmUnimplementedHalt);

bool ArmUnimplementedHalt::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

void CERFENGINE_CDECL ArmUnimplementedHalt::InstructionHelper(ArmUnimplementedHalt* self,
                                                              uint32_t pc, uint32_t opcode,
                                                              uint32_t thumb) {
    self->engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                       "unimplemented %s instruction 0x%08X executed at guest pc=0x%08X",
                       thumb != 0u ? "Thumb" : "ARM", opcode, pc);
}

void CERFENGINE_CDECL ArmUnimplementedHalt::CoprocRegisterTransferHelper(
        ArmUnimplementedHalt* self, uint32_t pc, uint32_t cp_info, uint32_t thumb) {
    self->engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                       "unimplemented %s coprocessor instruction at guest "
                       "pc=0x%08X - p%u CRn=c%u CRm=c%u opc1=%u opc2=%u %s",
                       thumb != 0u ? "Thumb" : "ARM",
                       pc, (cp_info >> 24) & 0xFu, (cp_info >> 16) & 0xFu,
                       (cp_info >> 8) & 0xFu, (cp_info >> 4) & 0xFu, (cp_info >> 1) & 0x7u,
                       (cp_info & 1u) ? "MRC/read" : "MCR/write");
}

void CERFENGINE_CDECL ArmUnimplementedHalt::CoprocDataOperationHelper(
        ArmUnimplementedHalt* self, uint32_t pc, uint32_t cp_info, uint32_t thumb) {
    self->engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                       "unimplemented %s coprocessor instruction at guest "
                       "pc=0x%08X - CDP p%u CRd=c%u CRn=c%u CRm=c%u opc1=%u opc2=%u",
                       thumb != 0u ? "Thumb" : "ARM",
                       pc, (cp_info >> 20) & 0xFu, (cp_info >> 16) & 0xFu,
                       (cp_info >> 12) & 0xFu, (cp_info >> 8) & 0xFu, (cp_info >> 4) & 0xFu,
                       (cp_info >> 1) & 0x7u);
}

void CERFENGINE_CDECL ArmUnimplementedHalt::CoprocDataTransferHelper(
        ArmUnimplementedHalt* self, uint32_t pc, uint32_t cp_info, uint32_t thumb) {
    self->engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                       "unimplemented %s coprocessor data transfer at guest "
                       "pc=0x%08X - %s p%u CRd=c%u Rn=r%u P=%u U=%u D=%u W=%u",
                       thumb != 0u ? "Thumb" : "ARM",
                       pc, (cp_info & 1u) ? "LDC" : "STC", (cp_info >> 16) & 0xFu,
                       (cp_info >> 12) & 0xFu, (cp_info >> 8) & 0xFu, (cp_info >> 4) & 0x1u,
                       (cp_info >> 3) & 0x1u, (cp_info >> 2) & 0x1u, (cp_info >> 1) & 0x1u);
}

}
