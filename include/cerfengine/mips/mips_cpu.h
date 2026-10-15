#pragma once

#include "../calling_convention.h"
#include "../engine.h"
#include "mips_cpu_state.h"

namespace cerfengine {

class MipsCpu : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kMips;
    }

    MipsCpuState* State() { return &state_; }

    void ResetState();

    /* VR4102 UM ch.27 p587 HIBERNATE: the internal and system interface clocks
       shut down, and a Cold Reset sequence exits the mode. */
    static void CERFENGINE_FASTCALL HibernateHelper(uint32_t next_pc, MipsCpu* cpu);

private:
    MipsCpuState state_{};
};

}
