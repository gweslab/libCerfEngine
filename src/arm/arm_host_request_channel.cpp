#include <cerfengine/host_request_channel.h>

#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_interrupt_channel.h>

namespace cerfengine {

namespace {

class ArmHostRequestChannel : public HostRequestChannel {
public:
    using HostRequestChannel::HostRequestChannel;

    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kArm;
    }

protected:
    void Kick() override { engine_.Get<ArmInterruptChannel>().RequestHostService(); }
};

}

CERFENGINE_REGISTER_AS(ArmHostRequestChannel, HostRequestChannel);

}
