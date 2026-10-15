#include "test_host.h"

#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/guest_engine.h>

#include <string>

namespace {

constexpr uint32_t kRamBase = 0x00000000u;
constexpr uint32_t kRamSize = 0x00010000u;
constexpr uint32_t kMmioPa  = 0x10000000u;

/* ARM DDI 0406C.c A8.8.102 MOV (immediate) A1, A8.8.5 ADD (immediate, ARM) A1,
   A8.8.204 STR (immediate, ARM) A1, A8.8.18 B A1. */
constexpr uint32_t kProgram[] = {
    0xE3A0002Au,
    0xE2801001u,
    0xE3A02201u,
    0xE5821000u,
    0xEAFFFFFEu,
};

}

int main() {
    TestHost      host(kRamBase, kRamSize, kMmioPa);
    TestArmSystem system;
    host.LoadWords(kRamBase, kProgram, sizeof(kProgram) / sizeof(kProgram[0]));

    cerfengine::Setup setup;
    setup.isa = cerfengine::Isa::kArm;
    host.Attach(setup);
    system.Attach(setup);

    cerfengine::Engine engine(setup);
    std::string        error;
    if (!engine.Start(error)) TestFail("Start: %s", error.c_str());
    engine.BindClock(host);

    cerfengine::ArmCpu& cpu = engine.Get<cerfengine::ArmCpu>();
    cpu.SetInitialStackPointer(kRamBase + kRamSize);
    cpu.RaiseResetException(kRamBase, false);

    cerfengine::GuestEngine& guest = engine.Get<cerfengine::GuestEngine>();
    for (int i = 0; i < 64 && !host.MmioWritten(); ++i) {
        TEST_CHECK(guest.Run() == cerfengine::GuestEngine::RunStatus::kRunning);
    }

    const cerfengine::ArmCpuState& state = *cpu.State();
    TEST_CHECK(host.MmioWritten());
    TEST_CHECK(host.MmioValue() == 43u);
    TEST_CHECK(state.gprs[0] == 42u);
    TEST_CHECK(state.gprs[1] == 43u);
    TEST_CHECK(state.gprs[2] == kMmioPa);
    TEST_CHECK(host.BlocksCompiled() > 0u);

    std::printf("arm_run_test passed\n");
    return 0;
}
