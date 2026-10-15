#include "test_host.h"

#include <cerfengine/guest_engine.h>
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_cpu_state.h>

#include <string>

namespace {

constexpr uint32_t kRamBase = 0x1FC00000u;
constexpr uint32_t kRamSize = 0x00010000u;
constexpr uint32_t kEntryVa = 0xBFC00000u;
constexpr uint32_t kMmioPa  = 0x10000000u;

constexpr uint32_t kProgram[] = {
    0x2402002Au,
    0x24430001u,
    0x3C04B000u,
    0xAC830000u,
    0x1000FFFFu,
    0x00000000u,
};

}

int main() {
    TestHost       host(kRamBase, kRamSize, kMmioPa);
    TestMipsSystem system(kEntryVa);
    host.LoadWords(kRamBase, kProgram, sizeof(kProgram) / sizeof(kProgram[0]));

    cerfengine::Setup setup;
    setup.isa = cerfengine::Isa::kMips;
    host.Attach(setup);
    system.Attach(setup);

    cerfengine::Engine engine(setup);
    std::string        error;
    if (!engine.Start(error)) TestFail("Start: %s", error.c_str());
    engine.BindClock(host);

    cerfengine::MipsCpu& cpu = engine.Get<cerfengine::MipsCpu>();
    cpu.ResetState();

    cerfengine::GuestEngine& guest = engine.Get<cerfengine::GuestEngine>();
    for (int i = 0; i < 64 && !host.MmioWritten(); ++i) {
        TEST_CHECK(guest.Run() == cerfengine::GuestEngine::RunStatus::kRunning);
    }

    const cerfengine::MipsCpuState& state = *cpu.State();
    TEST_CHECK(host.MmioWritten());
    TEST_CHECK(host.MmioValue() == 43u);
    TEST_CHECK(state.gpr[2] == 42u);
    TEST_CHECK(state.gpr[3] == 43u);
    TEST_CHECK(static_cast<uint32_t>(state.gpr[4]) == 0xB0000000u);
    TEST_CHECK(host.BlocksCompiled() > 0u);

    std::printf("mips_run_test passed\n");
    return 0;
}
