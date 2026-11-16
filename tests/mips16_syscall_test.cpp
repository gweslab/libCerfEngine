#include "mips_privilege_rig.h"

namespace {

constexpr uint32_t kMips16Syscall    = 0xE809u;
constexpr uint32_t kMips16BranchSelf = 0x17FFu;

cerfengine::MipsFeatures Mips16Features(bool syscall) {
    cerfengine::MipsFeatures f = BaseFeatures(false, false);
    f.mips16                   = true;
    f.mips16_syscall           = syscall;
    return f;
}

void TestSyscallCore() {
    PrivilegeRig rig(Mips16Features(true), false);
    rig.LoadUser({(kMips16BranchSelf << 16) | kMips16Syscall});
    rig.Start16(kUser, kUserPage);
    rig.ExpectException(kSys, 0u, kUserPage, "mips16 syscall");
}

void TestCoreWithoutSyscall() {
    PrivilegeRig rig(Mips16Features(false), false);
    rig.LoadUser({(kMips16BranchSelf << 16) | kMips16Syscall});
    rig.Start16(kUser, kUserPage);
    TEST_CHECK(rig.RunOnce() == PrivilegeRig::RunStatus::kHalted);
    TEST_CHECK(rig.System().exceptions == 0u);
}

}

int main() {
    TestSyscallCore();
    TestCoreWithoutSyscall();
    std::printf("mips16_syscall_test passed\n");
    return 0;
}
