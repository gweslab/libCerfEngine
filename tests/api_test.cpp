#include "test_host.h"

#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_instruction_timing.h>
#include <cerfengine/arm/arm_interrupt_channel.h>
#include <cerfengine/arm/arm_jit.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/arm/arm_system_host.h>
#include <cerfengine/arm/arm_translator.h>
#include <cerfengine/arm/block_context.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/calling_convention.h>
#include <cerfengine/clock_host.h>
#include <cerfengine/cycle_deadline.h>
#include <cerfengine/engine.h>
#include <cerfengine/engine_host.h>
#include <cerfengine/engine_setup.h>
#include <cerfengine/guest_engine.h>
#include <cerfengine/host_request_channel.h>
#include <cerfengine/isa_block_space.h>
#include <cerfengine/jit_block.h>
#include <cerfengine/jit_block_index.h>
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include <cerfengine/mips/mips_exception_delivery.h>
#include <cerfengine/mips/mips_interrupt_channel.h>
#include <cerfengine/mips/mips_jit.h>
#include <cerfengine/mips/mips_mmu.h>
#include <cerfengine/mips/mips_system_host.h>
#include <cerfengine/mips/mips_tlb_context.h>
#include <cerfengine/mmio_bus_host.h>
#include <cerfengine/physical_map.h>
#include <cerfengine/physical_memory_host.h>
#include <cerfengine/wide_multiply.h>

#include <string>

namespace {

void NoHostIsRefused() {
    cerfengine::Setup  setup;
    cerfengine::Engine engine(setup);
    std::string        error;
    TEST_CHECK(!engine.Start(error));
    TEST_CHECK(!error.empty());
}

void ArmSetupStarts() {
    TestHost      host(0x00000000u, 0x00010000u, 0x10000000u);
    TestArmSystem system;

    cerfengine::Setup setup;
    setup.isa = cerfengine::Isa::kArm;
    host.Attach(setup);
    system.Attach(setup);

    cerfengine::Engine engine(setup);
    std::string        error;
    const bool         started = engine.Start(error);
#if CERFENGINE_TEST_BACKEND
    if (!started) TestFail("Start: %s", error.c_str());
#else
    TEST_CHECK(!started);
    TEST_CHECK(error == "this build of libCerfEngine has no execution backend for the host");
#endif
}

void WideMultiply() {
    uint64_t lo = 0;
    uint64_t hi = 0;
    cerfengine::Mulu64(0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, &lo, &hi);
    TEST_CHECK(lo == 0x0000000000000001ull);
    TEST_CHECK(hi == 0xFFFFFFFFFFFFFFFEull);
}

}

int main() {
    NoHostIsRefused();
    ArmSetupStarts();
    WideMultiply();
    std::printf("api_test passed\n");
    return 0;
}
