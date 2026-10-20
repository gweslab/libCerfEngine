#pragma once

#include <cerfengine/engine.h>
#include <cerfengine/arm/decoded_insn.h>

#include <cstdint>

namespace cerfengine {

class Thumb32Fatal;

class Thumb32PlainImmDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Decode(DecodedInsn* insn, uint32_t op);

private:
    Thumb32Fatal* fatal_ = nullptr;

    bool DecodeAddSubImm12(DecodedInsn* insn, uint32_t op, bool add);
    bool DecodeMoveImm16(DecodedInsn* insn, uint32_t op, ArmInsnKind place);
    bool DecodeBitFieldExtract(DecodedInsn* insn, uint32_t op, ArmInsnKind place);
    bool DecodeBitFieldInsert(DecodedInsn* insn, uint32_t op);
    bool DecodeSaturate(DecodedInsn* insn, uint32_t op);
};

}
