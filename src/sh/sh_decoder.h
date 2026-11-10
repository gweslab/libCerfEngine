#pragma once

#include <cstdint>

#include <cerfengine/engine.h>

#include "sh_decoded_insn.h"

namespace cerfengine {

class ShDecoder : public EngineService {
public:
    using EngineService::EngineService;

    bool ShouldRegister() override { return engine_.GetSetup().isa == Isa::kSh; }
    void OnReady() override;

    ShDecodedInsn Decode(uint16_t raw, uint32_t guest_address) const;

private:
    struct ShControlClass {
        bool sr;
        bool dsp_unit;
        bool bank;
    };

    void    SetSystemTransfer(ShDecodedInsn& d, ShInsnKind kind) const;
    void    SetControlTransfer(ShDecodedInsn& d, ShInsnKind kind) const;
    uint8_t ControlCycles(ShInsnKind kind, const ShControlClass& r) const;

    bool dsp_ = false;
};

}
