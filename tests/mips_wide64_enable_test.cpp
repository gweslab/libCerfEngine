#include "mips_privilege_rig.h"

namespace {

namespace O = cerfengine::MipsOp;
namespace S = cerfengine::MipsSpecial;

constexpr uint32_t kMips16Dadiupc    = 0xFE01u;
constexpr uint32_t kMips16BranchSelf = 0x17FFu;

void TestWide64ReservedInstruction() {
    struct WideCase {
        const char* name;
        uint32_t    insn;
        bool        runs_when_enabled;
    };
    const WideCase cases[] = {
        {"daddu", MipsGenR(8u, 0u, 9u, 0u, S::kDADDU), true},
        {"daddiu", MipsGenI(O::kDADDIU, 8u, 9u, 0u), true},
        {"dsll32", MipsGenR(0u, 8u, 9u, 0u, S::kDSLL32), true},
        {"dmultu", MipsGenR(8u, 8u, 0u, 0u, S::kDMULTU), true},
        {"ld", MipsGenI(O::kLD, 8u, 9u, 0u), false},
    };
    for (const WideCase& c : cases) {
        PrivilegeRig rig(false);
        rig.LoadUser(OneInsn(c.insn));
        for (uint32_t status : {kUser, kUser | kSx, kSupervisor, kSupervisor | kUx}) {
            rig.Start(status, kUserPage);
            rig.ExpectException(kRi, 0u, kUserPage, c.name);
        }
        if (!c.runs_when_enabled) continue;
        for (uint32_t status : {kUser | kUx, kSupervisor | kSx, 0u}) {
            rig.Start(status, kUserPage);
            rig.RunTo(kLoop);
            TEST_CHECK(rig.System().exceptions == 0u);
        }
    }
}

void TestWide64Cop0Order() {
    PrivilegeRig rig(false);
    rig.LoadUser(OneInsn(Cop0(cerfengine::MipsCop0Rs::kDMFC0, 9u, cerfengine::MipsCp0::kStatus)));
    rig.Start(kUser, kUserPage);
    rig.ExpectException(kCpU, 0u, kUserPage, "user dmfc0 cu0 clear");
    rig.Start(kUser | kCu0, kUserPage);
    rig.ExpectException(kRi, 0u, kUserPage, "user dmfc0 ux clear");
    rig.Start(kUser | kCu0 | kUx, kUserPage);
    rig.RunTo(kLoop);
    TEST_CHECK(rig.State().gpr[9] == Sext(kUser | kCu0 | kUx));
}

void TestWide64InDelaySlot() {
    PrivilegeRig rig(false);
    rig.LoadUser({MipsGenI(O::kBEQ, 0u, 0u, 1u), MipsGenR(8u, 0u, 9u, 0u, S::kDADDU), 0u,
                  kBranchSelf, 0u});
    const uint32_t target = kUserPage + 12u;
    for (uint32_t pass = 0; pass < 2u; ++pass) {
        rig.Start(kUser | kUx, kUserPage);
        rig.RunTo(target);
        TEST_CHECK(rig.System().exceptions == 0u);
        TEST_CHECK(rig.State().gpr[9] == Sext(kKseg0Data));
        rig.Start(kUser, kUserPage);
        rig.ExpectException(kRi, 0u, kUserPage, "user daddu in delay slot");
        TEST_CHECK((rig.State().cp0_cause & kBd) != 0u);
    }
}

void TestMips16Wide64() {
    cerfengine::MipsFeatures f = BaseFeatures(false, false);
    f.mips16                   = true;
    PrivilegeRig rig(f, false);
    rig.LoadUser({(kMips16BranchSelf << 16) | kMips16Dadiupc});
    rig.Start16(kUser, kUserPage);
    rig.ExpectException(kRi, 0u, kUserPage, "user mips16 dadiupc");
    rig.Start16(kUser | kUx, kUserPage);
    rig.RunTo(kUserPage + 2u);
    TEST_CHECK(rig.System().exceptions == 0u);
    TEST_CHECK(rig.State().gpr[16] == Sext(kUserPage + 4u));
}

}

int main() {
    TestWide64ReservedInstruction();
    TestWide64Cop0Order();
    TestWide64InDelaySlot();
    TestMips16Wide64();
    std::printf("mips_wide64_enable_test passed\n");
    return 0;
}
