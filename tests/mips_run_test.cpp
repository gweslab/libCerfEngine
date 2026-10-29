#include "mmio_store_program.h"
#include "test_host.h"

#include <cerfengine/guest_engine.h>
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_cpu_state.h>

#include <string>

int main() {
    TestHost       host(kMipsStoreRamBase, kMipsStoreRamSize, kStoreMmioPa);
    TestMipsSystem system(kMipsStoreEntryVa);
    host.LoadWords(kMipsStoreRamBase, kMipsStoreProgram, sizeof(kMipsStoreProgram) / sizeof(kMipsStoreProgram[0]));

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
