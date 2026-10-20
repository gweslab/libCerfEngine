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

    void OnReady() override;

    MipsCpuState* State() { return &state_; }

    uint32_t Privilege() const { return privilege_of_status_[(state_.cp0_status >> 1) & 0xFu]; }

    void ResetState();

    /* VR4102 UM ch.27 p587 HIBERNATE: the internal and system interface clocks
       shut down, and a Cold Reset sequence exits the mode. */
    static void CERFENGINE_FASTCALL HibernateHelper(uint32_t next_pc, MipsCpu* cpu);

    static void CERFENGINE_FASTCALL Dmtc0WidthHelper(MipsCpu* cpu, uint32_t rd);

private:
    MipsCpuState state_{};
    uint8_t      privilege_of_status_[16]{};
};

}
