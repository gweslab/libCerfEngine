#include "mmio_store_program.h"
#include "test_host.h"

#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/guest_engine.h>
#include <cerfengine/mips/mips_cpu.h>

#include <string>

namespace {

class NeverRegistered : public cerfengine::EngineService {
public:
    using EngineService::EngineService;
};

template <class Run>
uint32_t ThrownValue(Run&& run) {
    try {
        for (int i = 0; i < 64; ++i) run();
    } catch (const HostThrow& thrown) {
        return thrown.value;
    }
    TestFail("no host exception left Run()");
}

template <class Body>
void WithArmStoreEngine(TestHost& host, Body&& body) {
    TestArmSystem system;
    host.LoadWords(kArmStoreRamBase, kArmStoreProgram, sizeof(kArmStoreProgram) / sizeof(kArmStoreProgram[0]));

    cerfengine::Setup setup;
    setup.isa = cerfengine::Isa::kArm;
    host.Attach(setup);
    system.Attach(setup);

    cerfengine::Engine engine(setup);
    std::string        error;
    if (!engine.Start(error)) TestFail("Start: %s", error.c_str());
    engine.BindClock(host);

    cerfengine::ArmCpu& cpu = engine.Get<cerfengine::ArmCpu>();
    cpu.SetInitialStackPointer(kArmStoreRamBase + kArmStoreRamSize);
    cpu.RaiseResetException(kArmStoreRamBase, false);

    body(engine);
}

uint32_t RunArm(TestHost& host) {
    uint32_t value = 0;
    WithArmStoreEngine(host, [&](cerfengine::Engine& engine) {
        cerfengine::GuestEngine& guest = engine.Get<cerfengine::GuestEngine>();
        value = ThrownValue([&] { guest.Run(); });
    });
    return value;
}

void ArmMmioThrowLeavesRun() {
    TestHost host(kArmStoreRamBase, kArmStoreRamSize, kStoreMmioPa);
    host.OnMmioWrite([](uint32_t value) { ThrowFromHost(value); });
    TEST_CHECK(RunArm(host) == 43u);
}

void ArmClockThrowLeavesRun() {
    TestHost host(kArmStoreRamBase, kArmStoreRamSize, kStoreMmioPa);
    host.OnClockDispatch([] { ThrowFromHost(7u); });
    TEST_CHECK(RunArm(host) == 7u);
}

struct HostPanic {
    std::string message;
};

void ArmLocatorErrorPanicsAndLeavesRun() {
    TestHost host(kArmStoreRamBase, kArmStoreRamSize, kStoreMmioPa);
    host.OnPanic([](const char* message) {
        cerfengine::RaiseHostFailure(std::make_exception_ptr(HostPanic{message}));
    });
    WithArmStoreEngine(host, [&](cerfengine::Engine& engine) {
        host.OnMmioWrite([&engine](uint32_t) { engine.Get<NeverRegistered>(); });
        cerfengine::GuestEngine& guest = engine.Get<cerfengine::GuestEngine>();
        std::string              panic;
        try {
            for (int i = 0; i < 64 && panic.empty(); ++i) guest.Run();
        } catch (const HostPanic& raised) {
            panic = raised.message;
        }
        TEST_CHECK(panic.find("Get<") == 0);
    });
}

void MipsMmioThrowLeavesRun() {
    TestHost       host(kMipsStoreRamBase, kMipsStoreRamSize, kStoreMmioPa);
    TestMipsSystem system(kMipsStoreEntryVa);
    host.LoadWords(kMipsStoreRamBase, kMipsStoreProgram, sizeof(kMipsStoreProgram) / sizeof(kMipsStoreProgram[0]));
    host.OnMmioWrite([](uint32_t value) { ThrowFromHost(value); });

    cerfengine::Setup setup;
    setup.isa = cerfengine::Isa::kMips;
    host.Attach(setup);
    system.Attach(setup);

    cerfengine::Engine engine(setup);
    std::string        error;
    if (!engine.Start(error)) TestFail("Start: %s", error.c_str());
    engine.BindClock(host);

    engine.Get<cerfengine::MipsCpu>().ResetState();

    cerfengine::GuestEngine& guest = engine.Get<cerfengine::GuestEngine>();
    TEST_CHECK(ThrownValue([&] { guest.Run(); }) == 43u);
}

}

int main() {
    ArmMmioThrowLeavesRun();
    ArmClockThrowLeavesRun();
    ArmLocatorErrorPanicsAndLeavesRun();
    MipsMmioThrowLeavesRun();
    std::printf("host_exception_test passed\n");
    return 0;
}
