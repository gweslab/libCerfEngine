#include "test_host.h"

#include <bit>
#include <cfloat>
#include <cstdint>
#include <cstdio>

#include "host_fp_env.h"

namespace {

using cerfengine::HostFpEnv;

volatile float  g_one_f   = 1.0f;
volatile float  g_three_f = 3.0f;
volatile float  g_two_f   = 2.0f;
volatile float  g_max_f   = FLT_MAX;
volatile double g_one_d   = 1.0;
volatile double g_three_d = 3.0;
volatile double g_half_d  = 0.5;
volatile double g_big_d   = 1e308;
volatile double g_ten_d   = 10.0;

int g_failures = 0;

#define FP_CHECK(condition)                                                    \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

void CheckFlags() {
    {
        HostFpEnv::BeginClean();
        const float r = g_one_f + g_one_f;
        FP_CHECK(HostFpEnv::Raised() == 0u);
        FP_CHECK(std::bit_cast<uint32_t>(r) == 0x40000000u);
    }
    {
        HostFpEnv::BeginClean();
        const float r = g_one_f / g_three_f;
        FP_CHECK(HostFpEnv::Raised() == HostFpEnv::kInexact);
        FP_CHECK(std::bit_cast<uint32_t>(r) == 0x3EAAAAABu);
    }
    {
        HostFpEnv::BeginClean();
        const float r = g_max_f * g_two_f;
        FP_CHECK((HostFpEnv::Raised() & HostFpEnv::kOverflow) != 0u);
        FP_CHECK(std::bit_cast<uint32_t>(r) == 0x7F800000u);
    }
    {
        HostFpEnv::BeginClean();
        const double r = g_half_d + g_half_d;
        FP_CHECK(HostFpEnv::Raised() == 0u);
        FP_CHECK(std::bit_cast<uint64_t>(r) == 0x3FF0000000000000ull);
    }
    {
        HostFpEnv::BeginClean();
        const double r = g_one_d / g_three_d;
        FP_CHECK(HostFpEnv::Raised() == HostFpEnv::kInexact);
        FP_CHECK(std::bit_cast<uint64_t>(r) == 0x3FD5555555555555ull);
    }
    {
        HostFpEnv::BeginClean();
        const double r = g_big_d * g_ten_d;
        FP_CHECK((HostFpEnv::Raised() & HostFpEnv::kOverflow) != 0u);
        FP_CHECK(std::bit_cast<uint64_t>(r) == 0x7FF0000000000000ull);
    }
    {
        const float stale = g_one_f / g_three_f;
        HostFpEnv::BeginClean();
        const float r = g_one_f + g_one_f;
        FP_CHECK(HostFpEnv::Raised() == 0u);
        FP_CHECK(std::bit_cast<uint32_t>(stale) == 0x3EAAAAABu);
        FP_CHECK(std::bit_cast<uint32_t>(r) == 0x40000000u);
    }
    {
        const HostFpEnv::Saved saved = HostFpEnv::BeginClean();
        HostFpEnv::RoundTowardZero(saved);
        const float toward_zero = g_one_f / g_three_f;
        HostFpEnv::Restore(saved);
        FP_CHECK(HostFpEnv::Raised() == 0u);
        const float nearest = g_one_f / g_three_f;
        FP_CHECK(std::bit_cast<uint32_t>(toward_zero) == 0x3EAAAAAAu);
        FP_CHECK(std::bit_cast<uint32_t>(nearest) == 0x3EAAAAABu);
    }
}

}

int main() {
    CheckFlags();
    if (g_failures != 0) TestFail("%d floating-point flag checks failed", g_failures);
    std::printf("host_fp_env_test passed\n");
    return 0;
}
