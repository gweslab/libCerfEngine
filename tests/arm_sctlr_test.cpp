#include "arm_ir_rig.h"

#include <cstring>

namespace {

struct SctlrConfig {
    const char* name;
    uint32_t    mask;
    uint32_t    value;
};

constexpr SctlrConfig kConfigs[] = {
    {"armv7a",    0x8CDD87F8u, 0x00C50078u},
    {"cortex_a8", 0x8DFFC7F8u, 0x00C50078u},
    {"imx31",     0xCC1F0478u, 0x00050078u},
    {"s3c2410",   0x3FFF8C78u, 0x00000078u},
    {"xscale",    0xFFFFC478u, 0x00000078u},
    {"iop13xx",   0xFBFFC478u, 0x00000078u},
    {"strongarm", 0xFFFFEC70u, 0x00000070u},
    {"sa1100",    0xFFFFCC70u, 0x00000070u},
    {"sa1110",    0xFFFFCC70u, 0x00000070u},
    {"arm720t",   0xFFFFDC70u, 0x00000070u},
};

struct HaltBit {
    const char* name;
    uint32_t    bit;
};

constexpr HaltBit kHaltBits[] = {
    {"AFE", 1u << 29}, {"EE", 1u << 25}, {"VE", 1u << 24}, {"HA", 1u << 17}, {"L4", 1u << 15}, {"B", 1u << 7},
};

constexpr uint32_t kSvcMode    = 0x000000D3u;
constexpr uint32_t kMcrSctlrR0 = 0xEE010F10u;
constexpr uint32_t kMrcSctlrR1 = 0xEE111F10u;
constexpr uint32_t kSctlrM     = 1u << 0;

class SystemControlCoprocessor : public TestCoprocessor {
public:
    cerfengine::ArmCoprocDecision Decide(const cerfengine::ArmCoprocAccess& a) override {
        cerfengine::ArmCoprocDecision d;
        if (a.cp_num == 15u && a.opc1 == 0u && a.crn == 1u && a.crm == 0u && a.opc2 == 0u) {
            d.action = cerfengine::ArmCoprocAction::kSystemControl;
        }
        return d;
    }
};

cerfengine::ArmFeatures Features(const SctlrConfig& config) {
    cerfengine::ArmFeatures f;
    f.sctlr_read_only_mask  = config.mask;
    f.sctlr_read_only_value = config.value;
    return f;
}

uint32_t HaltSet() {
    uint32_t set = 0;
    for (const HaltBit& halt : kHaltBits) set |= halt.bit;
    return set;
}

struct WriteResult {
    cerfengine::GuestEngine::RunStatus status;
    uint32_t                           read;
    bool                               halt_names_sctlr;
};

WriteResult WriteThenRead(const SctlrConfig& config, uint32_t value) {
    SystemControlCoprocessor cp;
    ArmIrRig rig(true, Features(config));
    rig.System().SetCoprocessor(&cp);
    const std::vector<uint32_t> program = {kMcrSctlrR0, kMrcSctlrR1, kRigBranchSelf};
    cerfengine::ArmCpuState initial{};
    initial.gprs[0] = value;
    initial.gprs[1] = 0xA5A5A5A5u;
    WriteResult result{};
    result.status = rig.Run(program, initial, 0u, kSvcMode);
    result.read   = rig.State().gprs[1];
    result.halt_names_sctlr = result.status == cerfengine::GuestEngine::RunStatus::kHalted &&
                              std::strstr(rig.Engine().LastHalt().detail, "SCTLR") != nullptr;
    return result;
}

void TestReset(const SctlrConfig& config) {
    SystemControlCoprocessor cp;
    ArmIrRig rig(true, Features(config));
    rig.System().SetCoprocessor(&cp);
    const uint32_t sctlr = rig.Engine().Get<cerfengine::ArmMmu>().State()->control_register.word;
    std::printf("%s reset: SCTLR 0x%08X\n", config.name, sctlr);
    TEST_CHECK(sctlr == config.value);
}

void TestWrite(const SctlrConfig& config, uint32_t value, const char* label) {
    const WriteResult result = WriteThenRead(config, value);
    const uint32_t expected = (value & ~config.mask) | config.value;
    std::printf("%s %s: status %u r1 0x%08X expected 0x%08X\n", config.name, label,
                static_cast<unsigned>(result.status), result.read, expected);
    TEST_CHECK(result.status != cerfengine::GuestEngine::RunStatus::kHalted);
    TEST_CHECK(result.read == expected);
}

void TestHaltBits(const SctlrConfig& config) {
    for (const HaltBit& halt : kHaltBits) {
        const WriteResult result = WriteThenRead(config, halt.bit);
        const bool writable = (config.mask & halt.bit) == 0u;
        std::printf("%s %s: writable %d status %u r1 0x%08X\n", config.name, halt.name, writable ? 1 : 0,
                    static_cast<unsigned>(result.status), result.read);
        if (writable) {
            TEST_CHECK(result.halt_names_sctlr);
        } else {
            TEST_CHECK(result.status != cerfengine::GuestEngine::RunStatus::kHalted);
            TEST_CHECK(result.read == config.value);
        }
    }
}

}

int main() {
    const uint32_t ones = 0xFFFFFFFFu & ~kSctlrM & ~HaltSet();
    for (const SctlrConfig& config : kConfigs) {
        TestReset(config);
        TestWrite(config, 0u, "zero");
        TestWrite(config, ones, "ones");
        TestHaltBits(config);
    }
    std::printf("arm_sctlr_test passed\n");
    return 0;
}
