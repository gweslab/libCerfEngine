#pragma once

#include <cerfengine/engine.h>

#include <cstdint>

namespace cerfengine {

struct DecodedInsn;

class Thumb32Fatal : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;

    [[noreturn]] void Unimplemented(const char* what, const DecodedInsn* insn,
                                    uint32_t op);
};

}
