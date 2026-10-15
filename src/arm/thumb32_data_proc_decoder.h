#pragma once

#include <cerfengine/engine.h>

#include <cstdint>

namespace cerfengine {

class Thumb32Fatal;
struct DecodedInsn;

class Thumb32DataProcDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    bool DecodeDataProcessingModifiedImmediate(DecodedInsn* insn, uint32_t op);
    bool DecodeDataProcessingShiftedRegister(DecodedInsn* insn, uint32_t op);
    bool DecodeDataProcessingRegister(DecodedInsn* insn, uint32_t op);

private:
    Thumb32Fatal* fatal_ = nullptr;

    bool DecodeMiscellaneous(DecodedInsn* insn, uint32_t op);
    bool DecodeMoveRegisterImmediateShifts(DecodedInsn* insn, uint32_t op);
    bool MapDataProcessingOpcode(uint32_t o, uint32_t rn, uint32_t rd,
                                 uint32_t s, uint32_t* opcode,
                                 bool* test) const;
    bool DataProcRegistersValid(uint32_t o, uint32_t rn, uint32_t rd, bool test,
                                bool* sp_form) const;

    uint32_t ThumbExpandImm(uint32_t key, uint32_t imm8) const;
};

}
