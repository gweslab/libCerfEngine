#include <cstddef>
#include <cstdint>

#include <cerfengine/engine.h>
#include "../arm_emit_services.h"
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/block_context.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit_alu.h"

namespace cerfengine {

namespace {

constexpr int32_t MonitorAddrDisp() {
    return static_cast<int32_t>(offsetof(ArmCpuState, ldrex_monitor_addr));
}

constexpr int32_t MonitorArmedDisp() {
    return static_cast<int32_t>(offsetof(ArmCpuState, ldrex_monitor_armed));
}

/* DDI 0406C.c A8.8.75 LDREX Operation (p. A8-433) and A8.8.212 STREX
   (p. A8-691): "address = R[n] + imm32". The byte, halfword and doubleword
   forms carry no immediate (A8-434 / A8-438 / A8-436: "address = R[n]"). */
void EmitExclusiveAddr(uint8_t*& cursor, DecodedInsn* d) {
    using namespace x86;
    EmitMovRegBaseDisp32(cursor, kEcx, kStateReg, ArmGprDisp(d->rn));
    if (d->offset != 0) {
        EmitAddRegImm32(cursor, kEcx, static_cast<uint32_t>(d->offset));
    }
}

uint8_t* EmitExclusiveAlignCheck(uint8_t*& cursor, uint32_t bytes) {
    using namespace x86;
    if (bytes < 2u) return nullptr;
    EmitTestRegImm32(cursor, kEcx, bytes - 1u);
    return EmitJnzLabel32(cursor);
}

uint8_t* EmitExclusiveTail(uint8_t* cursor, BlockContext* ctx,
                           uint8_t* align_fault, bool is_write) {
    using namespace x86;
    if (align_fault != nullptr) {
        FixupLabel32(align_fault, cursor);
        EmitMovRegImm32(cursor, kEdx, static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(ctx->emit->Mmu())));
        EmitCall(cursor, is_write
            ? reinterpret_cast<void*>(&ArmMmu::AlignmentFaultWriteHelper)
            : reinterpret_cast<void*>(&ArmMmu::AlignmentFaultReadHelper));
    }
    return cursor;
}

}

/* DDI 0406C.c A8.8.32 CLREX (p. A8-360) Operation (p. A8-361):
   "ClearExclusiveLocal(ProcessorID())", Exceptions None. */
uint8_t* PlaceClrex(uint8_t*      cursor,
                    DecodedInsn*,
                    BlockContext*) {
    using namespace x86;
    EmitMovBaseDisp32Imm32(cursor, kStateReg, MonitorArmedDisp(), 0u);
    return cursor;
}

/* A8.8.75 (p. A8-433) "SetExclusiveMonitors(address,4); R[t] =
   MemA[address,4]"; A8.8.77 (p. A8-437) "R[t] = MemA[address,4]; R[t2] =
   MemA[address+4,4]". */
uint8_t* PlaceLdrex(uint8_t*      cursor,
                    DecodedInsn*  d,
                    BlockContext* ctx) {
    using namespace x86;
    const uint32_t bytes = d->op1;

    EmitExclusiveAddr(cursor, d);
    uint8_t* align_fault = EmitExclusiveAlignCheck(cursor, bytes);

    cursor = EmitTlbFastPath(cursor, ctx, TlbAccess::kRead);
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* abort_label = EmitJzLabel32(cursor);

    switch (bytes) {
    case 1u:
        /* MOVZX EDX, byte [EAX] - 0F B6 /r (SDM Vol. 2B 4-140 MOVZX). */
        Emit8(cursor, 0x0F);
        Emit8(cursor, 0xB6);
        EmitModRmReg(cursor, 0, kEax, kEdx);
        break;
    case 2u:
        /* MOVZX EDX, word [EAX] - 0F B7 /r (SDM Vol. 2B 4-140 MOVZX). */
        Emit8(cursor, 0x0F);
        Emit8(cursor, 0xB7);
        EmitModRmReg(cursor, 0, kEax, kEdx);
        break;
    case 4u:
        EmitMovRegBaseDisp32(cursor, kEdx, kEax, 0);
        break;
    case 8u:
        /* A8.8.77 T1 (p. A8-436) rejects only "t IN {13,15} || t2 IN {13,15}
           || t == t2 || n == 15", so Rn may alias Rt. */
        EmitMovRegBaseDisp32(cursor, kEdi, kEax, 0);
        EmitMovRegBaseDisp32(cursor, kEdx, kEax, 4);
        break;
    default:
        ctx->emit->Owner().Panic("PlaceLdrex got op1=%u outside {1,2,4,8} at guest "
                                 "pc=0x%08X", bytes, d->guest_address);
    }

    if (bytes == 8u) {
        EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd),  kEdi);
        EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd2), kEdx);
    } else {
        EmitMovBaseDisp32Reg(cursor, kStateReg, ArmGprDisp(d->rd),  kEdx);
    }

    EmitMovBaseDisp32Reg  (cursor, kStateReg, MonitorAddrDisp(),  kEcx);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, MonitorArmedDisp(), 1u);

    uint8_t* done_label = EmitJmpLabel32(cursor);

    cursor = EmitExclusiveTail(cursor, ctx, align_fault, false);
    FixupLabel32(abort_label, cursor);
    cursor = EmitIoIrqPreciseBackoutIfIo(cursor, d, ctx);
    cursor = EmitAbortDataTail(cursor, d, ctx);

    FixupLabel32(done_label, cursor);
    return cursor;
}

uint8_t* PlaceStrex(uint8_t*      cursor,
                    DecodedInsn*  d,
                    BlockContext* ctx) {
    using namespace x86;
    const uint32_t bytes = d->op1;

    EmitExclusiveAddr(cursor, d);
    uint8_t* align_fault = EmitExclusiveAlignCheck(cursor, bytes);

    EmitMovRegBaseDisp32(cursor, kEax, kStateReg, MonitorArmedDisp());
    EmitTestRegReg      (cursor, kEax, kEax);
    uint8_t* fail_label_a = EmitJzLabel32(cursor);

    EmitCmpRegBaseDisp32(cursor, kEcx, kStateReg, MonitorAddrDisp());
    uint8_t* fail_label_b = EmitJnzLabel32(cursor);

    cursor = EmitTlbFastPath(cursor, ctx, TlbAccess::kWrite);
    EmitTestRegReg(cursor, kEax, kEax);
    uint8_t* abort_label      = EmitJzLabel32(cursor);
    uint8_t* abort_label_high = nullptr;

    EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, ArmGprDisp(d->rm));
    switch (bytes) {
    case 1u:
        /* MOV [EAX], DL - 88 /r (SDM Vol. 2B 4-35 MOV). */
        Emit8(cursor, 0x88);
        EmitModRmReg(cursor, 0, kEax, kEdx);
        break;
    case 2u:
        /* MOV [EAX], DX - 66 89 /r, the 66H operand-size override prefix
           (SDM Vol. 2A 2-2) over MOV r/m16, r16 (SDM Vol. 2B 4-35). */
        Emit8(cursor, 0x66);
        Emit8(cursor, 0x89);
        EmitModRmReg(cursor, 0, kEax, kEdx);
        break;
    case 4u:
        EmitMovBaseDisp32Reg(cursor, kEax, 0, kEdx);
        break;
    case 8u:
        EmitMovBaseDisp32Reg(cursor, kEax, 0, kEdx);
        EmitAddRegImm32(cursor, kEcx, 4u);
        cursor = EmitTlbFastPath(cursor, ctx, TlbAccess::kWrite);
        EmitTestRegReg(cursor, kEax, kEax);
        abort_label_high = EmitJzLabel32(cursor);
        EmitMovRegBaseDisp32(cursor, kEdx, kStateReg, ArmGprDisp(d->rd2));
        EmitMovBaseDisp32Reg(cursor, kEax, 0, kEdx);
        break;
    default:
        ctx->emit->Owner().Panic("PlaceStrex got op1=%u outside {1,2,4,8} at guest "
                                 "pc=0x%08X", bytes, d->guest_address);
    }

    EmitMovBaseDisp32Imm32(cursor, kStateReg, ArmGprDisp(d->rd),  0u);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, MonitorArmedDisp(), 0u);
    uint8_t* done_label = EmitJmpLabel32(cursor);

    /* DDI 0406C.c Table A3-3 (p. A3-116): StoreExcl(!t) and an Open Access
       StoreExcl(x) both leave the local monitor in Open Access. */
    FixupLabel32(fail_label_a, cursor);
    FixupLabel32(fail_label_b, cursor);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, ArmGprDisp(d->rd),  1u);
    EmitMovBaseDisp32Imm32(cursor, kStateReg, MonitorArmedDisp(), 0u);
    uint8_t* done_label_b = EmitJmpLabel32(cursor);

    cursor = EmitExclusiveTail(cursor, ctx, align_fault, true);
    FixupLabel32(abort_label, cursor);
    if (abort_label_high != nullptr) {
        FixupLabel32(abort_label_high, cursor);
    }
    cursor = EmitIoIrqPreciseBackoutIfIo(cursor, d, ctx);
    cursor = EmitAbortDataTail(cursor, d, ctx);

    FixupLabel32(done_label,   cursor);
    FixupLabel32(done_label_b, cursor);
    return cursor;
}

}
