#pragma once

#include <cerfengine/engine.h>

#include <cstdint>

namespace cerfengine {

class Thumb32Fatal;
struct DecodedInsn;

class Thumb32MultiplyDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    bool DecodeMultiplyAbsoluteDifference(DecodedInsn* insn, uint32_t op);

private:
    Thumb32Fatal* fatal_ = nullptr;

    bool DecodeMultiplyAccumulate(DecodedInsn* insn, uint32_t op);
    bool DecodeMultiplySubtract(DecodedInsn* insn, uint32_t op);
    bool DecodeHalfwordMultiply(DecodedInsn* insn, uint32_t op);
    bool DecodeWordByHalfwordMultiply(DecodedInsn* insn, uint32_t op);
};

}
