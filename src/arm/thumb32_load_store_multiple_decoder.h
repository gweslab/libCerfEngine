#pragma once

#include <cerfengine/engine.h>

#include <cstdint>

namespace cerfengine {

struct DecodedInsn;

class Thumb32LoadStoreMultipleDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;

    bool Decode(DecodedInsn* insn, uint32_t op);

private:
    bool DecodeBlockTransfer(DecodedInsn* insn, uint32_t op, bool increment);
    bool DecodeReturnFromException(DecodedInsn* insn, uint32_t op,
                                   bool increment);
    bool DecodeStoreReturnState(DecodedInsn* insn, uint32_t op, bool increment);
};

}
