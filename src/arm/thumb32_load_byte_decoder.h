#pragma once

#include <cerfengine/engine.h>

#include <cstdint>

namespace cerfengine {

struct DecodedInsn;

class Thumb32LoadByteDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;

    bool DecodeLoadByteMemoryHints(DecodedInsn* insn, uint32_t op);

private:
    bool DecodePreloadRegister(DecodedInsn* insn, uint32_t op);
    bool DecodeByteLiteral(DecodedInsn* insn, uint32_t op);
    bool DecodeByteImmediate12(DecodedInsn* insn, uint32_t op);
    bool DecodeByteImmediate8(DecodedInsn* insn, uint32_t op);
    bool DecodeByteRegister(DecodedInsn* insn, uint32_t op);
    bool DecodeByteUnprivileged(DecodedInsn* insn, uint32_t op);
    bool DecodeSignedByteLiteral(DecodedInsn* insn, uint32_t op);
    bool DecodeSignedByteRegister(DecodedInsn* insn, uint32_t op);
    bool DecodeSignedByteImmediate12(DecodedInsn* insn, uint32_t op);
    bool DecodeSignedByteImmediate8(DecodedInsn* insn, uint32_t op);
    bool DecodeSignedByteUnprivileged(DecodedInsn* insn, uint32_t op);
};

}
