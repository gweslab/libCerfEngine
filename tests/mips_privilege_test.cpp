#include "mips_privilege_rig.h"

namespace {

namespace O = cerfengine::MipsOp;
namespace S = cerfengine::MipsSpecial;

constexpr uint32_t kKseg3Va = 0xFFFF8000u;
constexpr uint32_t kTlbwi   = 0x42000002u;

struct AccessCase {
    const char* name;
    uint32_t    insn;
    uint32_t    code;
    uint32_t    badvaddr;
    bool        kernel_reads;
    bool        wide;
};

void TestUserAccesses() {
    const AccessCase cases[] = {
        {"lw", MipsGenI(O::kLW, 8u, 9u, 0u), kAdEL, kKseg0Data, true, false},
        {"lw r0", MipsGenI(O::kLW, 8u, 0u, 0u), kAdEL, kKseg0Data, false, false},
        {"lw const", MipsGenI(O::kLW, 0u, 9u, kKseg3Va & 0xFFFFu), kAdEL, kKseg3Va, false, false},
        {"lh", MipsGenI(O::kLH, 8u, 9u, 2u), kAdEL, kKseg0Data + 2u, false, false},
        {"lbu", MipsGenI(O::kLBU, 8u, 9u, 1u), kAdEL, kKseg0Data + 1u, false, false},
        {"sw", MipsGenI(O::kSW, 8u, 9u, 0u), kAdES, kKseg0Data, false, false},
        {"sb", MipsGenI(O::kSB, 8u, 9u, 3u), kAdES, kKseg0Data + 3u, false, false},
        {"lwl", MipsGenI(O::kLWL, 8u, 9u, 0u), kAdEL, kKseg0Data, false, false},
        {"ld", MipsGenI(O::kLD, 8u, 9u, 0u), kAdEL, kKseg0Data, false, true},
        {"sd", MipsGenI(O::kSD, 8u, 9u, 0u), kAdES, kKseg0Data, false, true},
    };
    for (const AccessCase& c : cases) {
        PrivilegeRig rig(false);
        rig.LoadUser(OneInsn(c.insn));
        rig.Start(kUser | (c.wide ? kUx : 0u), kUserPage);
        rig.ExpectException(c.code, c.badvaddr, kUserPage, c.name);
        rig.Start(kSupervisor | (c.wide ? kSx : 0u), kUserPage);
        rig.ExpectException(c.code, c.badvaddr, kUserPage, c.name);
        if (!c.kernel_reads) continue;
        rig.Start(0u, kUserPage);
        rig.RunTo(kLoop);
        TEST_CHECK(rig.State().gpr[9] == kDataValue);
        TEST_CHECK(rig.System().exceptions == 0u);
    }
}

void TestSupervisorSegments() {
    PrivilegeRig rig(false);
    rig.LoadUser(OneInsn(MipsGenI(O::kLW, 10u, 9u, 0u)));
    rig.Start(kSupervisor, kUserPage);
    rig.RunTo(kLoop);
    TEST_CHECK(rig.State().gpr[9] == kSsegValue);
    TEST_CHECK(rig.System().exceptions == 0u);
    rig.Start(kUser, kUserPage);
    rig.ExpectException(kAdEL, kSsegPage, kUserPage, "user sseg");

    PrivilegeRig high(false);
    high.LoadUser(OneInsn(MipsGenI(O::kLW, 11u, 9u, 0u)));
    high.Start(kSupervisor, kUserPage);
    high.ExpectException(kAdEL, kHighVa, kUserPage, "supervisor 0xE0000000");
}

void TestUserFetch() {
    PrivilegeRig rig(false);
    rig.Start(kUser, kKseg0Data);
    rig.ExpectException(kAdEL, kKseg0Data, kKseg0Data, "user fetch");

    PrivilegeRig jump(false);
    jump.LoadUser({MipsGenR(8u, 0u, 0u, 0u, S::kJR), 0u});
    jump.Start(kUser, kUserPage);
    jump.ExpectException(kAdEL, kKseg0Data, kKseg0Data, "user jr");
}

void TestBlocksKeyedByPrivilege() {
    PrivilegeRig rig(false);
    rig.LoadUser(OneInsn(MipsGenI(O::kLW, 8u, 9u, 0u)));
    for (uint32_t pass = 0; pass < 2u; ++pass) {
        rig.Start(0u, kUserPage);
        rig.RunTo(kLoop);
        TEST_CHECK(rig.State().gpr[9] == kDataValue);
        rig.Start(kUser, kUserPage);
        rig.ExpectException(kAdEL, kKseg0Data, kUserPage, "keyed user");
    }
}

void TestCop0Usable() {
    PrivilegeRig rig(false);
    rig.LoadUser(OneInsn(Cop0(cerfengine::MipsCop0Rs::kMFC0, 9u, cerfengine::MipsCp0::kStatus)));
    rig.Start(kUser, kUserPage);
    rig.ExpectException(kCpU, 0u, kUserPage, "user mfc0");
    TEST_CHECK((rig.State().cp0_cause & kCeMask) == 0u);
    rig.Start(kUser | kCu0, kUserPage);
    rig.RunTo(kLoop);
    TEST_CHECK(rig.State().gpr[9] == Sext(kUser | kCu0));
    rig.Start(0u, kUserPage);
    rig.RunTo(kLoop);
    TEST_CHECK(rig.State().gpr[9] == 0u);

    PrivilegeRig tlb(false);
    tlb.LoadUser(OneInsn(kTlbwi));
    tlb.Start(kSupervisor, kUserPage);
    tlb.ExpectException(kCpU, 0u, kUserPage, "supervisor tlbwi");

    PrivilegeRig cache(false);
    cache.LoadUser(OneInsn(MipsGenI(O::kCACHE, 8u, 0u, 0u)));
    cache.Start(kUser, kUserPage);
    cache.ExpectException(kCpU, 0u, kUserPage, "user cache");
    cache.Start(0u, kUserPage);
    cache.RunTo(kLoop);
    TEST_CHECK(cache.System().exceptions == 0u);
}

void TestCop0InDelaySlot() {
    PrivilegeRig rig(false);
    rig.LoadUser({MipsGenI(O::kBEQ, 0u, 0u, 1u), MipsGenI(O::kCACHE, 8u, 0u, 0u), 0u,
                  kBranchSelf, 0u});
    const uint32_t target = kUserPage + 12u;
    for (uint32_t pass = 0; pass < 2u; ++pass) {
        rig.Start(0u, kUserPage);
        rig.RunTo(target);
        TEST_CHECK(rig.System().exceptions == 0u);
        rig.Start(kUser, kUserPage);
        rig.ExpectException(kCpU, 0u, kUserPage, "user cache in delay slot");
        TEST_CHECK((rig.State().cp0_cause & kBd) != 0u);
    }
}

void TestCopUnusableOpcodes() {
    struct CopCase {
        const char* name;
        uint32_t    insn;
        uint32_t    unit;
    };
    const CopCase cases[] = {
        {"cop1", (O::kCOP1 << 26) | (9u << 16), 1u},
        {"cop2", (O::kCOP2 << 26) | (9u << 16), 2u},
        {"lwc1", MipsGenI(O::kLWC1, 8u, 0u, 0u), 1u},
        {"lwc2", MipsGenI(O::kLWC2, 8u, 0u, 0u), 2u},
        {"ldc1", MipsGenI(O::kLDC1, 8u, 0u, 0u), 1u},
        {"ldc2", MipsGenI(O::kLDC2, 8u, 0u, 0u), 2u},
        {"swc1", MipsGenI(O::kSWC1, 8u, 0u, 0u), 1u},
        {"swc2", MipsGenI(O::kSWC2, 8u, 0u, 0u), 2u},
        {"sdc1", MipsGenI(O::kSDC1, 8u, 0u, 0u), 1u},
        {"sdc2", MipsGenI(O::kSDC2, 8u, 0u, 0u), 2u},
    };
    constexpr uint32_t kCu1Cu2 = 3u << 29;
    for (const CopCase& c : cases) {
        PrivilegeRig rig(false, true);
        rig.LoadUser(OneInsn(c.insn));
        for (uint32_t status : {0u, kCu1Cu2, kUser | kCu1Cu2}) {
            rig.Start(status, kUserPage);
            rig.ExpectException(kCpU, 0u, kUserPage, c.name);
            TEST_CHECK((rig.State().cp0_cause & kCeMask) == (c.unit << 28));
        }
    }
}

void TestR3000User() {
    PrivilegeRig rig(true);
    rig.LoadUser(OneInsn(MipsGenI(O::kLW, 8u, 9u, 0u)));
    rig.Start(kKuc, kUserPage);
    rig.ExpectException(kAdEL, kKseg0Data, kUserPage, "r3000 user lw");
    TEST_CHECK((rig.State().cp0_status & 0x3Fu) == (kKuc << 2));
    rig.Start(0u, kUserPage);
    rig.RunTo(kLoop);
    TEST_CHECK(rig.State().gpr[9] == kDataValue);

    PrivilegeRig cop0(true);
    cop0.LoadUser(OneInsn(Cop0(cerfengine::MipsCop0Rs::kMFC0, 9u, cerfengine::MipsCp0::kCause)));
    cop0.Start(kKuc, kUserPage);
    cop0.ExpectException(kCpU, 0u, kUserPage, "r3000 user mfc0");
}

}

int main() {
    TestUserAccesses();
    TestSupervisorSegments();
    TestUserFetch();
    TestBlocksKeyedByPrivilege();
    TestCop0Usable();
    TestCop0InDelaySlot();
    TestCopUnusableOpcodes();
    TestR3000User();
    std::printf("mips_privilege_test passed\n");
    return 0;
}
