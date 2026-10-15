#pragma once

#include <cerfengine/engine.h>

#include <cstdint>

namespace cerfengine {

class Thumb32Fatal;
struct DecodedInsn;

class Thumb32LongMultiplyDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    bool DecodeLongMultiplyDivide(DecodedInsn* insn, uint32_t op);

private:
    Thumb32Fatal* fatal_               = nullptr;
    bool          has_integer_divide_  = false;

    bool RegistersValid(uint32_t op) const;
    bool DecodeLongMultiply(DecodedInsn* insn, uint32_t op, uint32_t row);
    bool DecodeHalfwordLongMultiply(DecodedInsn* insn, uint32_t op);
    bool DecodeDivide(DecodedInsn* insn, uint32_t op, bool is_signed);
};

}
