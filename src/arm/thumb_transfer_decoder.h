#pragma once

#include <cerfengine/engine.h>

#include <cstdint>

namespace cerfengine {

struct DecodedInsn;

class ThumbTransferDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;

    bool DecodeHalfwordOffsetTransfer(DecodedInsn* insn, uint16_t op);
    bool DecodeImmediateOffsetTransfer(DecodedInsn* insn, uint16_t op);
    bool DecodeLoadLiteral(DecodedInsn* insn, uint16_t op);
    bool DecodeLoadStoreMultiple(DecodedInsn* insn, uint16_t op);
    bool DecodeRegisterOffsetTransfer(DecodedInsn* insn, uint16_t op);
    bool DecodeStackRelativeTransfer(DecodedInsn* insn, uint16_t op);
};

}
