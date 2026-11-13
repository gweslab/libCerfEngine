#pragma once

#include <cstdint>

#include <cerfengine/engine.h>

#include "sh_decoded_insn.h"
#include "sh_dsp_encoding.h"

namespace cerfengine {

class ShDecoder : public EngineService {
public:
    using EngineService::EngineService;

    bool ShouldRegister() override { return engine_.GetSetup().isa == Isa::kSh; }
    void OnReady() override;

    bool          Wide(uint16_t raw) const { return dsp_ && ShDspParallelPrefix(raw); }
    ShDecodedInsn Decode(uint16_t raw, uint32_t guest_address) const;
    ShDecodedInsn DecodeWide(uint16_t first, uint16_t second, uint32_t guest_address) const;

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
