#pragma once

#include <cerfengine/engine.h>

#include <cstdint>

namespace cerfengine {

struct DecodedInsn;

class Thumb32LoadStoreDualDecoder : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Decode(DecodedInsn* insn, uint32_t op);

private:
    bool has_cp15_v7_ = false;

    bool DecodeDual(DecodedInsn* insn, uint32_t op);
    bool DecodeExclusiveOrTableBranch(DecodedInsn* insn, uint32_t op);
    bool DecodeTableBranchByte(DecodedInsn* insn, uint32_t op);
    bool DecodeLoadExclusive(DecodedInsn* insn, uint32_t op, uint32_t bytes);
    bool DecodeStoreExclusive(DecodedInsn* insn, uint32_t op, uint32_t bytes);
};

}
