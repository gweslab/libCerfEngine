#include <cerfengine/host_request_channel.h>

#include <cerfengine/engine.h>
#include <cerfengine/mips/mips_interrupt_channel.h>

namespace cerfengine {

namespace {

class MipsHostRequestChannel : public HostRequestChannel {
public:
    using HostRequestChannel::HostRequestChannel;

    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kMips;
    }

protected:
    void Kick() override {
        engine_.Get<MipsInterruptChannel>().RequestDispatch(MipsInterruptChannel::kDispatchHostClock);
    }
};

}

CERFENGINE_REGISTER_AS(MipsHostRequestChannel, HostRequestChannel);

}
