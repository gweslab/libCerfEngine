#include "test_host.h"

#include <bit>
#include <cfloat>
#include <cstdint>
#include <cstdio>

#include "host_fp_env.h"
#include "host_fp_op.h"

namespace {

using cerfengine::HostFpEnv;
using cerfengine::HostFpOp;

volatile float  g_one_f   = 1.0f;
volatile float  g_three_f = 3.0f;
volatile float  g_two_f   = 2.0f;
volatile float  g_max_f   = FLT_MAX;
volatile float  g_four_f  = 4.0f;
volatile float  g_big_f   = std::bit_cast<float>(0x68A0213Cu);
volatile double g_two_d   = 2.0;
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

template <HostFpOp kOp, class T>
void CheckRun(T a, T b, uint64_t want_bits, uint32_t want_raised) {
    uint32_t raised = 0xFFFFFFFFu;
    const T  r      = HostFpEnv::Run<kOp>(a, b, &raised);
    uint64_t bits   = 0u;
    if constexpr (sizeof(T) == 4u) bits = std::bit_cast<uint32_t>(r);
    else                           bits = std::bit_cast<uint64_t>(r);
    FP_CHECK(bits == want_bits);
    FP_CHECK(raised == want_raised);
}

void CheckSingle() {
    constexpr uint32_t kInexact = HostFpEnv::kInexact;
    constexpr uint32_t kOverflowInexact = HostFpEnv::kOverflow | HostFpEnv::kInexact;
    CheckRun<HostFpOp::kAdd, float>(g_one_f, g_one_f, 0x40000000u, 0u);
    CheckRun<HostFpOp::kSub, float>(g_one_f, g_three_f, 0xC0000000u, 0u);
    CheckRun<HostFpOp::kDiv, float>(g_one_f, g_three_f, 0x3EAAAAABu, kInexact);
    CheckRun<HostFpOp::kMul, float>(g_max_f, g_two_f, 0x7F800000u, kOverflowInexact);
    CheckRun<HostFpOp::kSqrt, float>(g_two_f, g_two_f, 0x3FB504F3u, kInexact);
    CheckRun<HostFpOp::kSqrt, float>(g_big_f, g_big_f, 0x540F2A99u, kInexact);
    CheckRun<HostFpOp::kSqrt, float>(g_four_f, g_four_f, 0x40000000u, 0u);
}

void CheckDouble() {
    constexpr uint32_t kInexact = HostFpEnv::kInexact;
    constexpr uint32_t kOverflowInexact = HostFpEnv::kOverflow | HostFpEnv::kInexact;
    CheckRun<HostFpOp::kAdd, double>(g_half_d, g_half_d, 0x3FF0000000000000ull, 0u);
    CheckRun<HostFpOp::kSub, double>(g_one_d, g_three_d, 0xC000000000000000ull, 0u);
    CheckRun<HostFpOp::kDiv, double>(g_one_d, g_three_d, 0x3FD5555555555555ull, kInexact);
    CheckRun<HostFpOp::kMul, double>(g_big_d, g_ten_d, 0x7FF0000000000000ull, kOverflowInexact);
    CheckRun<HostFpOp::kSqrt, double>(g_two_d, g_two_d, 0x3FF6A09E667F3BCDull, kInexact);
}

void CheckStaleFlagsAndRounding() {
    CheckRun<HostFpOp::kDiv, float>(g_one_f, g_three_f, 0x3EAAAAABu, HostFpEnv::kInexact);
    CheckRun<HostFpOp::kAdd, float>(g_one_f, g_one_f, 0x40000000u, 0u);

    const float toward_zero = HostFpEnv::RunTowardZero<HostFpOp::kDiv>(
        static_cast<float>(g_one_f), static_cast<float>(g_three_f));
    FP_CHECK(std::bit_cast<uint32_t>(toward_zero) == 0x3EAAAAAAu);
    CheckRun<HostFpOp::kDiv, float>(g_one_f, g_three_f, 0x3EAAAAABu, HostFpEnv::kInexact);
}

}

int main() {
    CheckSingle();
    CheckDouble();
    CheckStaleFlagsAndRounding();
    if (g_failures != 0) TestFail("%d floating-point flag checks failed", g_failures);
    std::printf("host_fp_env_test passed\n");
    return 0;
}
