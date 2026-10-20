#include "arm_ir_rig.h"

#include "arm/arm_translation_cache.h"

namespace {

constexpr uint32_t kSctlrMmuOn = 0x00000001u;
constexpr uint32_t kSvcMode    = 0x000000D3u;
constexpr uint32_t kOtherAsid  = 5u;

void TestBlockAfterAsidChange(bool global, const char* name) {
    ArmIrRig rig(true, cerfengine::ArmFeatures{});
    rig.System().EnableIdentityWalk(0u, 0u);
    rig.System().SetPagesGlobal(global);
    const std::vector<uint32_t> program = {kRigBranchSelf};
    rig.Run(program, cerfengine::ArmCpuState{}, kSctlrMmuOn, kSvcMode);
    const uint32_t before = rig.Host().BlocksCompiled();

    rig.Engine().Get<cerfengine::ArmMmu>().State()->contextidr = kOtherAsid;
    rig.Engine().Get<cerfengine::ArmTranslationCache>().ContextSwitchFlush();
    rig.Engine().Get<cerfengine::GuestEngine>().Run();
    const uint32_t recompiled = rig.Host().BlocksCompiled() - before;

    std::printf("%s: %u block(s) compiled after the ASID change\n", name, recompiled);
    TEST_CHECK(rig.State().gprs[15] == 0u);
    TEST_CHECK(recompiled == (global ? 0u : 1u));
}

}

int main() {
    for (const bool global : {true, false}) {
        TestBlockAfterAsidChange(global, global ? "global" : "non-global");
    }
    std::printf("arm_block_key_test passed\n");
    return 0;
}
