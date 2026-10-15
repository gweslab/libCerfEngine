#pragma once

#include <cerfengine/calling_convention.h>
#include <cstdint>

#include <cerfengine/engine.h>

namespace cerfengine {

struct MipsDecodedInsn;
struct MipsBlockContext;

class MipsSystemHost;

class MipsCp0Emitter : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kMips;
    }

    uint8_t* EmitMfc0(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
    uint8_t* EmitMtc0(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
    uint8_t* EmitDmfc0(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
    uint8_t* EmitDmtc0(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);

    static void CERFENGINE_FASTCALL Dmtc0Width64Helper(MipsCp0Emitter* self, uint32_t rd);

private:
    uint8_t* EmitFromCop0(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx);
    uint8_t* EmitToCop0(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx,
                        bool is_dword);

    cerfengine::MipsSystemHost* system_ = nullptr;
};

}
