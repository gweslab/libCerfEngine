#include "cerfir/cerfir_builder.h"
#include "cerfir/cerfir_dump.h"
#include "cerfir/cerfir_eval.h"
#include "cerfir/cerfir_verify.h"
#include "emit_fault.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using cerfengine::cerfir::Block;
using cerfengine::cerfir::Builder;
using cerfengine::cerfir::EvalArgs;
using cerfengine::cerfir::FlagSet;
using cerfengine::cerfir::Inst;
using cerfengine::cerfir::Op;
using cerfengine::cerfir::Ref;
using cerfengine::cerfir::Type;
using cerfengine::cerfir::kNoRef;

namespace {

[[noreturn]] void Fail(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::printf("TEST FAILED: ");
    std::vprintf(fmt, args);
    std::printf("\n");
    va_end(args);
    std::fflush(stdout);
    std::exit(1);
}

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) Fail("%s:%d: %s", __FILE__, __LINE__, #condition); \
    } while (0)

struct Harness {
    Harness() {
        model.cycle_slot = 0x100;
        target.model     = &model;
        lowering.Begin(block, code.data(), code.data() + code.size(), target);
    }

    Block                               block;
    cerfengine::cerfir::HostLowering    lowering;
    cerfengine::cerfir::GuestModel      model;
    cerfengine::cerfir::HostGuestTarget target;
    std::vector<uint8_t>                code = std::vector<uint8_t>(0x10000u);
};

struct Vector {
    Op       op;
    uint32_t x;
    uint32_t y;
    uint32_t c;
    uint32_t result;
    int      n;
    int      z;
    int      carry;
    int      v;
};

constexpr Vector kVectors[] = {
#include "cerfir_eval_vectors.inc"
};

void CheckFlag(Op flag, const Vector& vec, const EvalArgs& args, int want) {
    uint64_t bit = 0;
    if (want < 0) {
        CHECK(!cerfengine::cerfir::EvalFlag(flag, vec.op, Type::kI32, args, &bit));
        return;
    }
    if (!cerfengine::cerfir::EvalFlag(flag, vec.op, Type::kI32, args, &bit) ||
        bit != static_cast<uint64_t>(want)) {
        Fail("%s flag %s x=0x%08X y=0x%08X c=%u: got %llu want %d",
             cerfengine::cerfir::InfoOf(vec.op).name, cerfengine::cerfir::InfoOf(flag).name,
             vec.x, vec.y, vec.c, static_cast<unsigned long long>(bit), want);
    }
}

void TestEvalVectors() {
    for (const Vector& vec : kVectors) {
        const bool carry_arg = vec.op == Op::kAdc || vec.op == Op::kSbc ||
                               vec.op == Op::kShlC || vec.op == Op::kLsrC ||
                               vec.op == Op::kAsrC || vec.op == Op::kRorC;
        EvalArgs args{};
        args.value[0] = vec.x;
        args.type[0]  = Type::kI32;
        args.value[1] = vec.y;
        args.type[1]  = vec.op == Op::kRrx ? Type::kI1 : Type::kI32;
        if (carry_arg) {
            args.value[2] = vec.c;
            args.type[2]  = Type::kI1;
        }
        uint64_t out = 0;
        if (!cerfengine::cerfir::EvalPure(vec.op, Type::kI32, args, &out) || out != vec.result) {
            Fail("%s x=0x%08X y=0x%08X c=%u: got 0x%llX want 0x%08X",
                 cerfengine::cerfir::InfoOf(vec.op).name, vec.x, vec.y, vec.c,
                 static_cast<unsigned long long>(out), vec.result);
        }
        CheckFlag(Op::kNFlag, vec, args, vec.n);
        CheckFlag(Op::kZFlag, vec, args, vec.z);
        CheckFlag(Op::kCFlag, vec, args, vec.carry);
        CheckFlag(Op::kVFlag, vec, args, vec.v);
    }
}

void TestOrderedCompares() {
    struct Case {
        Op       op;
        Type     type;
        uint64_t x;
        uint64_t y;
        uint64_t want;
    };
    constexpr Case kCases[] = {
        {Op::kCmpLtS, Type::kI32, 0xFFFFFFFFu, 0u, 1u},
        {Op::kCmpLtU, Type::kI32, 0xFFFFFFFFu, 0u, 0u},
        {Op::kCmpLeS, Type::kI32, 5u, 5u, 1u},
        {Op::kCmpLeS, Type::kI32, 0x80000000u, 0x7FFFFFFFu, 1u},
        {Op::kCmpLtS, Type::kI64, 0xFFFFFFFF00000000ull, 1u, 1u},
        {Op::kCmpLtU, Type::kI64, 0xFFFFFFFF00000000ull, 1u, 0u},
        {Op::kCmpLeS, Type::kI64, 0x8000000000000000ull, 0x7FFFFFFFFFFFFFFFull, 1u},
        {Op::kCmpLeS, Type::kI64, 0x7FFFFFFFFFFFFFFFull, 0x8000000000000000ull, 0u},
    };
    for (const Case& k : kCases) {
        EvalArgs args{};
        args.value[0] = k.x;
        args.type[0]  = k.type;
        args.value[1] = k.y;
        args.type[1]  = k.type;
        uint64_t out  = 2u;
        if (!cerfengine::cerfir::EvalPure(k.op, Type::kI1, args, &out) || out != k.want) {
            Fail("%s x=0x%llX y=0x%llX: got %llu want %llu", cerfengine::cerfir::InfoOf(k.op).name,
                 static_cast<unsigned long long>(k.x), static_cast<unsigned long long>(k.y),
                 static_cast<unsigned long long>(out), static_cast<unsigned long long>(k.want));
        }
    }
}

void TestDivision() {
    struct Case {
        Op       op;
        uint32_t x;
        uint32_t y;
        uint64_t want;
    };
    constexpr Case kCases[] = {
        {Op::kDivRemU32, 7u, 2u, 0x0000000100000003ull},
        {Op::kDivRemU32, 0xFFFFFFFFu, 2u, 0x000000017FFFFFFFull},
        {Op::kDivRemU32, 5u, 0u, 0x0000000000000005ull},
        {Op::kDivRemU32, 0u, 7u, 0x0000000000000000ull},
        {Op::kDivRemS32, 0xFFFFFFF9u, 2u, 0xFFFFFFFFFFFFFFFDull},
        {Op::kDivRemS32, 7u, 0xFFFFFFFEu, 0x00000001FFFFFFFDull},
        {Op::kDivRemS32, 0xFFFFFFF9u, 0xFFFFFFFEu, 0xFFFFFFFF00000003ull},
        {Op::kDivRemS32, 0x80000000u, 0xFFFFFFFFu, 0x0000000080000000ull},
        {Op::kDivRemS32, 9u, 0u, 0x0000000000000009ull},
        {Op::kDivRemS32, 0x80000000u, 1u, 0x0000000080000000ull},
        {Op::kDivRemS32, 0x80000000u, 0u, 0x0000000080000000ull},
    };
    for (const Case& k : kCases) {
        EvalArgs args{};
        args.value[0] = k.x;
        args.type[0]  = Type::kI32;
        args.value[1] = k.y;
        args.type[1]  = Type::kI32;
        uint64_t out  = 0u;
        if (!cerfengine::cerfir::EvalPure(k.op, Type::kI64, args, &out) || out != k.want) {
            Fail("%s x=0x%08X y=0x%08X: got 0x%016llX want 0x%016llX",
                 cerfengine::cerfir::InfoOf(k.op).name, k.x, k.y,
                 static_cast<unsigned long long>(out), static_cast<unsigned long long>(k.want));
        }
    }
}

void TestBuilderFolding() {
    Harness h;
    Block&  block = h.block;
    Builder b(block, h.lowering, true);
    FlagSet f;
    const uint8_t all = cerfengine::cerfir::kWantN | cerfengine::cerfir::kWantZ |
                        cerfengine::cerfir::kWantC | cerfengine::cerfir::kWantV;

    const Ref sum = b.Compute(Op::kAdd, b.Const32(0x7FFFFFFFu), b.Const32(1u), kNoRef, all, &f);
    CHECK(cerfengine::cerfir::IsConstRef(sum) && block.ConstValue(sum) == 0x80000000u);
    CHECK(block.ConstValue(f.n) == 1u && block.ConstValue(f.z) == 0u);
    CHECK(block.ConstValue(f.c) == 0u && block.ConstValue(f.v) == 1u);
    CHECK(block.Size() == 0u);

    const Ref x = b.GetSlot(Type::kI32, 0u);
    CHECK(b.Op2(Op::kAdd, x, b.Const32(0u)) == x);
    CHECK(b.Op2(Op::kAdd, b.Const32(0u), x) == x);
    CHECK(b.Op2(Op::kAnd, x, b.Const32(0xFFFFFFFFu)) == x);
    CHECK(b.Op3(Op::kSelect, b.Const1(true), x, b.Const32(5u)) == x);

    const Ref diff = b.Compute(Op::kSub, x, b.Const32(0u), kNoRef, cerfengine::cerfir::kWantC, &f);
    CHECK(!cerfengine::cerfir::IsConstRef(diff) && block.At(diff).op == Op::kSub);
    CHECK(block.At(f.c).op == Op::kCFlag && block.At(f.c).args[0] == diff);
    CHECK(block.Info(x).uses == 0u);
    CHECK(block.Info(diff).uses == 1u);

    bool threw = false;
    try {
        b.Compute(Op::kAnd, x, x, kNoRef, cerfengine::cerfir::kWantC, &f);
    } catch (const cerfengine::EmitFault&) {
        threw = true;
    }
    CHECK(threw);
}

void TestVerifyAndDump() {
    Harness h;
    Block&  block = h.block;
    Builder b(block, h.lowering, true);
    b.CycleAdd(1u);
    const Ref      z    = b.GetSlot(Type::kI1, 0x41u);
    const uint32_t skip = b.NewLabel();
    b.Branch(z, skip, false);
    b.SetSlot(0u, b.Op2(Op::kAdd, b.GetSlot(Type::kI32, 0u), b.Const32(1u)));
    b.Label(skip);
    b.SetSlot(0x3Cu, b.Const32(0x1004u));
    b.ExitDirect(0x1004u, 1u, false);

    std::string error;
    if (!cerfengine::cerfir::Verify(block, &error)) Fail("%s", error.c_str());
    const std::string want =
        "%0 = GetSlot.i1 @0x41\n"
        "BranchIfNot %0 L0\n"
        "%2 = GetSlot.i32 @0x0\n"
        "%3 = Add.i32 %2, #0x1\n"
        "SetSlot %3 @0x0\n"
        "Label L0\n"
        "SetSlot #0x1004 @0x3C\n"
        "ExitDirect pc 0x00001004 slot 1\n";
    const std::string got = cerfengine::cerfir::Dump(block);
    if (got != want) Fail("dump mismatch:\n%s", got.c_str());
    CHECK(h.lowering.Finish() != 0u);
}

template <typename F>
void ExpectBuildFault(F build, const char* fragment) {
    Harness h;
    Builder b(h.block, h.lowering, true);
    try {
        build(b);
    } catch (const cerfengine::EmitFault& fault) {
        if (std::string(fault.what()).find(fragment) == std::string::npos) {
            Fail("build fault \"%s\" lacks \"%s\"", fault.what(), fragment);
        }
        return;
    }
    Fail("no build fault, expected \"%s\"", fragment);
}

void ExpectVerifyError(const Block& block, const char* fragment) {
    std::string error;
    CHECK(!cerfengine::cerfir::Verify(block, &error));
    if (error.find(fragment) == std::string::npos) {
        Fail("verify error \"%s\" lacks \"%s\"", error.c_str(), fragment);
    }
}

void TestVerifyRejects() {
    ExpectBuildFault(
        [](Builder& b) {
            const Ref      v = b.GetSlot(Type::kI32, 0u);
            const uint32_t l = b.NewLabel();
            b.Label(l);
            b.SetSlot(4u, v);
        },
        "live at a label");
    ExpectBuildFault(
        [](Builder& b) {
            const Ref v = b.GetSlot(Type::kI32, 0u);
            b.SetSlot(4u, v);
            b.SetSlot(8u, v);
        },
        "not available");
    {
        Harness h;
        Builder b(h.block, h.lowering, true);
        b.GetSlot(Type::kI32, 0u);
        b.ExitDispatcher();
        ExpectVerifyError(h.block, "never consumed");
    }
    {
        Harness h;
        Builder b(h.block, h.lowering, true);
        FlagSet f;
        const Ref v = b.GetSlot(Type::kI32, 0u);
        b.Dup(v);
        b.Dup(v);
        b.ComputeOnlyFlags(Op::kAdd, v, v, kNoRef, cerfengine::cerfir::kWantZ, &f);
        b.SetSlot(4u, v);
        b.SetSlot(8u, f.z);
        h.block.Append(Op::kNFlag, Type::kI1, h.block.At(f.z).args[0], kNoRef, kNoRef, 0u, 0u,
                       cerfengine::cerfir::kValueIsValueOp, 0u);
        b.ExitDispatcher();
        ExpectVerifyError(h.block, "does not follow its producer");
    }
    {
        Harness h;
        Builder b(h.block, h.lowering, true);
        b.SetSlot(0u, b.Const32(1u));
        ExpectVerifyError(h.block, "does not end in a terminal");
    }
    {
        Harness h;
        Builder b(h.block, h.lowering, true);
        const uint32_t l = b.NewLabel();
        b.Label(l);
        b.Branch(b.GetSlot(Type::kI1, 0x41u), l, true);
        b.ExitDispatcher();
        ExpectVerifyError(h.block, "backward branch");
    }
    {
        Harness h;
        Builder b(h.block, h.lowering, true);
        b.ExitDispatcher();
        b.SetSlot(0u, b.Const32(1u));
        b.ExitDispatcher();
        ExpectVerifyError(h.block, "code after a terminal");
    }
}

}

int main() {
    TestEvalVectors();
    TestOrderedCompares();
    TestDivision();
    TestBuilderFolding();
    TestVerifyAndDump();
    TestVerifyRejects();
    std::printf("cerfir_test passed (%zu vectors)\n", sizeof(kVectors) / sizeof(kVectors[0]));
    return 0;
}
