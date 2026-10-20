#include "mips_ir_fns.h"

#include <cstdio>

#include <cerfengine/mips/mips_cpu.h>

#include "../../emit_fault.h"
#include "../mips_emit_services.h"
#include "mips_ir_codegen.h"

namespace cerfengine {

namespace {

using cerfir::kNoRef;
using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

[[noreturn]] void DecisionFault(uint32_t rd, const char* what) {
    char text[160];
    std::snprintf(text, sizeof(text),
                  "CerfIR MIPS frontend: the host decision for cp0[%u] has %s", rd, what);
    throw EmitFault(text);
}

/* QEMU MIPS translate.c gen_mfc0_load32: gpr[rt] = sext32(cp0[rd]). */
void FromCop0(MipsIrContext& c) {
    const MipsDecodedInsn& d = *c.d;
    if ((d.raw & 0x7u) != 0u) {
        MipsIrUndefined(c);
        return;
    }
    const MipsCp0Decision dec = c.codegen->System()->MipsCp0(d.rd);
    if (!dec.present) {
        MipsIrUndefined(c);
        return;
    }
    if (dec.read == nullptr && dec.offset < 0) {
        DecisionFault(d.rd, "neither a read callback nor a state offset");
    }
    if (d.rt == 0u) return;
    const Ref value = dec.read != nullptr
                          ? MipsIrFastcall(c, reinterpret_cast<const void*>(dec.read), Type::kI32,
                                           c.b->ConstPtr(dec.read_context), kNoRef)
                          : c.b->GetSlot(Type::kI32, static_cast<uint32_t>(dec.offset));
    MipsIrWriteSext(c, d.rt, value);
}

void ToCop0(MipsIrContext& c, bool is_dword) {
    const MipsDecodedInsn& d = *c.d;
    cerfir::Builder&       b = *c.b;
    if ((d.raw & 0x7u) != 0u) {
        MipsIrUndefined(c);
        return;
    }
    const MipsCp0Decision dec = c.codegen->System()->MipsCp0(d.rd);
    if (!dec.present) {
        MipsIrUndefined(c);
        return;
    }
    if (!dec.writable) return;
    if (dec.write == nullptr && dec.offset < 0) {
        DecisionFault(d.rd, "neither a write callback nor a state offset");
    }
    if (is_dword) {
        const uint32_t ok   = b.NewLabel();
        const Ref      sext = b.Op2(Op::kAsr, MipsIrReadLo(c, d.rt), b.Const32(31u));
        b.Branch(b.Compare(Op::kCmpEq, MipsIrReadHi(c, d.rt), sext), ok, true);
        const bool stored = c.pc_stored;
        MipsIrFastcall(c, reinterpret_cast<const void*>(&MipsCpu::Dmtc0WidthHelper),
                       Type::kVoid, b.ConstPtr(c.block->emit->Cpu()), b.Const32(d.rd));
        c.pc_stored = stored;
        b.Label(ok);
    }
    if (dec.write != nullptr) {
        MipsIrFastcall(c, reinterpret_cast<const void*>(dec.write), Type::kVoid,
                       b.ConstPtr(dec.write_context), MipsIrReadLo(c, d.rt));
        return;
    }
    b.SetSlot(static_cast<uint32_t>(dec.offset), MipsIrReadLo(c, d.rt));
}

}

void MipsIrMfc0(MipsIrContext& c) { FromCop0(c); }
void MipsIrDmfc0(MipsIrContext& c) { FromCop0(c); }
void MipsIrMtc0(MipsIrContext& c) { ToCop0(c, false); }
void MipsIrDmtc0(MipsIrContext& c) { ToCop0(c, true); }

}
