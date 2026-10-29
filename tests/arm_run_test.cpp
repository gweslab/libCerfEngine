#include "mmio_store_program.h"
#include "test_host.h"

#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/guest_engine.h>

#include <string>

int main() {
    TestHost      host(kArmStoreRamBase, kArmStoreRamSize, kStoreMmioPa);
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

    cerfengine::GuestEngine& guest = engine.Get<cerfengine::GuestEngine>();
    for (int i = 0; i < 64 && !host.MmioWritten(); ++i) {
        TEST_CHECK(guest.Run() == cerfengine::GuestEngine::RunStatus::kRunning);
    }

    const cerfengine::ArmCpuState& state = *cpu.State();
    TEST_CHECK(host.MmioWritten());
    TEST_CHECK(host.MmioValue() == 43u);
    TEST_CHECK(state.gprs[0] == 42u);
    TEST_CHECK(state.gprs[1] == 43u);
    TEST_CHECK(state.gprs[2] == kStoreMmioPa);
    TEST_CHECK(host.BlocksCompiled() > 0u);

    std::printf("arm_run_test passed\n");
    return 0;
}
