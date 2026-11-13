#pragma once

#include <cerfengine/sh/sh_cpu_state.h>
#include <cerfengine/sh/sh_interrupt_channel.h>
#include <cerfengine/sh/sh_interrupt_host.h>
#include <cerfengine/sh/sh_sleep_host.h>

#include <cstdint>

constexpr uint32_t kShRigFakeIntevt = 0x400u;

class ShRigFakeHosts final : public cerfengine::ShInterruptHost, public cerfengine::ShSleepHost {
public:
    uint32_t AcceptInterrupt(uint32_t sr) override {
        if (!cerfengine::ShSrAcceptsInterrupt(sr, level)) return 0u;
        ++accepted;
        return kShRigFakeIntevt;
    }

    uint32_t StandbyWakeLevel() override { return standby_level; }

    bool StandbySelected() const override { return standby; }

    void EnterStandby() override {
        ++standby_entries;
        exited = false;
    }

    bool StandbyExited() const override { return exited; }

    void Raise(cerfengine::ShInterruptChannel& channel, uint32_t value) {
        level = value;
        channel.PublishLevel(value);
    }

    uint32_t level           = 0u;
    uint32_t standby_level   = 0u;
    uint32_t accepted        = 0u;
    uint32_t standby_entries = 0u;
    bool     standby         = false;
    bool     exited          = false;
};
