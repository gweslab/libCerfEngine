#pragma once

#include <cstdint>

#include <cerfengine/calling_convention.h>
#include <cerfengine/engine.h>

namespace cerfengine {

class ArmUnimplementedHalt : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;

    [[noreturn]] static void CERFENGINE_CDECL InstructionHelper(ArmUnimplementedHalt* self,
                                                                uint32_t pc, uint32_t opcode,
                                                                uint32_t thumb);
    [[noreturn]] static void CERFENGINE_CDECL CoprocRegisterTransferHelper(
            ArmUnimplementedHalt* self, uint32_t pc, uint32_t cp_info, uint32_t thumb);
    [[noreturn]] static void CERFENGINE_CDECL CoprocDataOperationHelper(
            ArmUnimplementedHalt* self, uint32_t pc, uint32_t cp_info, uint32_t thumb);
    [[noreturn]] static void CERFENGINE_CDECL CoprocDataTransferHelper(
            ArmUnimplementedHalt* self, uint32_t pc, uint32_t cp_info, uint32_t thumb);
};

}
