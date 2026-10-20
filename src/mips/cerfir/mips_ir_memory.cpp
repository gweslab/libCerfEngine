#include "mips_ir_fns.h"

#include "../mips_emit_services.h"
#include "../mips_memory_access.h"

namespace cerfengine {

namespace {

using cerfir::kNoRef;
using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

Ref Address(MipsIrContext& c) {
    return c.b->Op2(Op::kAdd, MipsIrReadLo(c, c.d->rs), c.b->Const32(MipsSignedImm(*c.d)));
}

cerfir::MemAttr& Attr(MipsIrContext& c, uint8_t width, bool sign, uint32_t* index) {
    const MipsDecodedInsn& d = *c.d;
    cerfir::MemAttr&       m = c.b->NewMemAttr(index);
    m.width_bytes            = width;
    m.sign                   = sign;
    m.guest_pc               = d.guest_address;
    if (d.rs != 0u) {
        m.base_slot = static_cast<int32_t>(MipsGprLoDisp(d.rs));
        m.offset    = static_cast<int32_t>(MipsSignedImm(d));
    }
    return m;
}

void LoadInto(MipsIrContext& c, uint8_t width, bool sign, Ref address) {
    const MipsDecodedInsn& d = *c.d;
    if (address == kNoRef && d.rs != 0u && d.rt != 0u &&
        c.b->SimpleLoad(width, sign, MipsGprLoDisp(d.rs), MipsSignedImm(d), MipsGprLoDisp(d.rt),
                        MipsGprHiDisp(d.rt), d.guest_address)) {
        c.pc_stored = true;
        return;
    }
    uint32_t index = 0;
    cerfir::MemAttr&       m     = Attr(c, width, sign, &index);
    if (d.rt != 0u) {
        m.result_slot  = static_cast<int32_t>(MipsGprLoDisp(d.rt));
        m.result_slot2 = static_cast<int32_t>(MipsGprHiDisp(d.rt));
    }
    Ref value = kNoRef;
    if (address != kNoRef) {
        m.base_slot = cerfir::kNoSlot;
        m.offset    = 0;
        value       = c.b->Load(Type::kI32, address, kNoRef, index);
    } else if (m.base_slot == cerfir::kNoSlot) {
        value = c.b->Load(Type::kI32, c.b->Const32(MipsSignedImm(d)), kNoRef, index);
    } else {
        value = c.b->LoadBased(Type::kI32, index, m);
    }
    if (d.rt == 0u) c.b->Drop(value);
    c.pc_stored = true;
}

void StoreFrom(MipsIrContext& c, uint8_t width) {
    const MipsDecodedInsn& d = *c.d;
    if (d.rs != 0u && c.b->SimpleStore(width, MipsGprLoDisp(d.rs), MipsSignedImm(d),
                                       MipsIrReadLo(c, d.rt), d.guest_address)) {
        c.pc_stored = true;
        return;
    }
    uint32_t index = 0;
    const cerfir::MemAttr& m     = Attr(c, width, false, &index);
    const Ref              value = MipsIrReadLo(c, d.rt);
    if (m.base_slot == cerfir::kNoSlot) {
        c.b->Store(c.b->Const32(MipsSignedImm(d)), value, kNoRef, index);
    } else {
        c.b->StoreBased(value, index, m);
    }
    c.pc_stored = true;
}

const MipsMemoryHelpers& Helpers(const MipsIrContext& c) {
    return MipsMemoryAccess::HelpersFor(c.privilege);
}

Ref Load(MipsIrContext& c, const void* helper, Type result, Ref address) {
    return MipsIrFastcall(c, helper, result, address, c.b->ConstPtr(c.block->emit->Memory()));
}

void LoadDword(MipsIrContext& c, Ref address) {
    const Ref v =
        Load(c, reinterpret_cast<const void*>(Helpers(c).load_dword), Type::kI64, address);
    if (c.d->rt == 0u) {
        c.b->Drop(v);
        return;
    }
    MipsIrWrite64(c, c.d->rt, c.b->Lo32(v), c.b->Hi32(v));
}

void ByIndex(MipsIrContext& c, const void* helper) {
    const Ref address = Address(c);
    MipsIrFastcall(c, helper, Type::kVoid, address, c.b->Const32(c.d->rt),
                   c.b->ConstPtr(c.block->emit->Memory()));
}

const void* Fn(MipsMemoryHelpers::IndexFn f) { return reinterpret_cast<const void*>(f); }

}

void MipsIrLb(MipsIrContext& c) { LoadInto(c, 1u, true, kNoRef); }
void MipsIrLh(MipsIrContext& c) { LoadInto(c, 2u, true, kNoRef); }
void MipsIrLw(MipsIrContext& c) { LoadInto(c, 4u, true, kNoRef); }
void MipsIrLbu(MipsIrContext& c) { LoadInto(c, 1u, false, kNoRef); }
void MipsIrLhu(MipsIrContext& c) { LoadInto(c, 2u, false, kNoRef); }
void MipsIrLwu(MipsIrContext& c) { LoadInto(c, 4u, false, kNoRef); }
void MipsIrLd(MipsIrContext& c) { LoadDword(c, Address(c)); }

void MipsIrSb(MipsIrContext& c) { StoreFrom(c, 1u); }
void MipsIrSh(MipsIrContext& c) { StoreFrom(c, 2u); }
void MipsIrSw(MipsIrContext& c) { StoreFrom(c, 4u); }

void MipsIrSd(MipsIrContext& c) { ByIndex(c, Fn(Helpers(c).store_dword)); }
void MipsIrLwl(MipsIrContext& c) { ByIndex(c, Fn(Helpers(c).lwl)); }
void MipsIrLwr(MipsIrContext& c) { ByIndex(c, Fn(Helpers(c).lwr)); }
void MipsIrLdl(MipsIrContext& c) { ByIndex(c, Fn(Helpers(c).ldl)); }
void MipsIrLdr(MipsIrContext& c) { ByIndex(c, Fn(Helpers(c).ldr)); }
void MipsIrSwl(MipsIrContext& c) { ByIndex(c, Fn(Helpers(c).swl)); }
void MipsIrSwr(MipsIrContext& c) { ByIndex(c, Fn(Helpers(c).swr)); }
void MipsIrSdl(MipsIrContext& c) { ByIndex(c, Fn(Helpers(c).sdl)); }
void MipsIrSdr(MipsIrContext& c) { ByIndex(c, Fn(Helpers(c).sdr)); }

void MipsIrMips16Lwpc(MipsIrContext& c) { LoadInto(c, 4u, true, c.b->Const32(c.d->target)); }

void MipsIrMips16Ldpc(MipsIrContext& c) { LoadDword(c, c.b->Const32(c.d->target)); }

}
