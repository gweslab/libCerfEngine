#pragma once

#include "mips_ir_rig.h"

constexpr uint16_t kMips16Nop = 0x6500u;

struct Mips16Program {
    uint32_t              load_va = 0;
    std::vector<uint32_t> words;
    std::vector<uint32_t> start_vas;
};

class Mips16ProgramGenerator {
public:
    Mips16ProgramGenerator(std::mt19937& rng, bool wide) : rng_(rng), wide_(wide) {}

    Mips16Program Program(uint32_t start_va, uint32_t length, bool first_simple) {
        hw_.clear();
        starts_.clear();
        fixups_.clear();
        start_va_ = start_va;
        if (first_simple) Emit16(Simple());
        while (starts_.size() < length) {
            const uint32_t pick = Pick(100u);
            if (pick < 52u) {
                Emit16(Simple());
            } else if (pick < 66u) {
                Extended();
            } else if (pick < 77u) {
                Branch();
            } else if (pick < 81u) {
                Jal();
            } else if (pick < 85u) {
                RegisterJump();
            } else if (pick < 87u) {
                Emit16(static_cast<uint16_t>((0x1Du << 11) | (1u << 5)));
                Emit16(Simple());
            } else if (pick < 93u) {
                PcRelative();
            } else {
                BasedAccess();
            }
        }
        EmitJalx(kMipsRigEndVa);
        Emit16(kMips16Nop);
        Resolve();
        Mips16Program p;
        p.load_va = start_va_ & ~3u;
        if ((start_va_ & 2u) != 0u) hw_.insert(hw_.begin(), kMips16Nop);
        if ((hw_.size() & 1u) != 0u) hw_.push_back(kMips16Nop);
        for (size_t i = 0; i < hw_.size(); i += 2u) {
            p.words.push_back(static_cast<uint32_t>(hw_[i]) | (static_cast<uint32_t>(hw_[i + 1u]) << 16));
        }
        for (uint32_t s : starts_) p.start_vas.push_back(start_va_ + 2u * s);
        return p;
    }

private:
    enum class FixKind : uint8_t { kB16, kBz16, kBExt, kJal, kAddiupc };

    struct Fixup {
        uint32_t at;
        uint32_t first;
        FixKind  kind;
    };

    uint32_t Pick(uint32_t n) { return MipsRigPick(rng_, n); }
    uint32_t X() { return Pick(8u); }
    uint32_t Imm8() { return Pick(256u); }

    void Emit16(uint16_t h) {
        starts_.push_back(static_cast<uint32_t>(hw_.size()));
        hw_.push_back(h);
    }

    void Emit32(uint16_t h0, uint16_t h1) {
        starts_.push_back(static_cast<uint32_t>(hw_.size()));
        hw_.push_back(h0);
        hw_.push_back(h1);
    }

    void EmitJalx(uint32_t target) {
        const uint32_t imm26 = (target >> 2) & 0x03FFFFFFu;
        Emit32(static_cast<uint16_t>((0x03u << 11) | (1u << 10) | (((imm26 >> 16) & 0x1Fu) << 5) |
                                     ((imm26 >> 21) & 0x1Fu)),
               static_cast<uint16_t>(imm26));
    }

    static uint16_t Op(uint32_t op, uint32_t rest) { return static_cast<uint16_t>((op << 11) | rest); }

    uint32_t ShiftFunct() {
        static constexpr uint32_t kFunct[] = {0u, 2u, 3u, 1u};
        return kFunct[Pick(wide_ ? 4u : 3u)];
    }

    uint16_t Rr() {
        static constexpr uint32_t kNarrow[] = {0x02u, 0x03u, 0x04u, 0x06u, 0x07u, 0x0Au, 0x0Bu,
                                               0x0Cu, 0x0Du, 0x0Eu, 0x0Fu, 0x10u, 0x12u, 0x18u,
                                               0x19u, 0x1Au, 0x1Bu};
        static constexpr uint32_t kWide[]   = {0x08u, 0x13u, 0x14u, 0x16u, 0x17u,
                                               0x1Cu, 0x1Du, 0x1Eu, 0x1Fu};
        static constexpr uint32_t kRare[]   = {0x05u, 0x09u, 0x01u, 0x11u, 0x15u};
        uint32_t funct = kNarrow[Pick(17u)];
        if (wide_ && Pick(3u) == 0u) funct = kWide[Pick(9u)];
        if (Pick(40u) == 0u) funct = kRare[Pick(5u)];
        const uint32_t rx = X();
        const uint32_t ry = X();
        return Op(0x1Du, (rx << 8) | (ry << 5) | funct);
    }

    uint16_t Simple() {
        const uint32_t x = X();
        const uint32_t y = X();
        switch (Pick(wide_ ? 18u : 15u)) {
        case 0u: return Op(0x00u, (x << 8) | Imm8());
        case 1u: {
            const uint32_t sa    = Pick(8u);
            const uint32_t funct = ShiftFunct();
            return Op(0x06u, (x << 8) | (y << 5) | (sa << 2) | funct);
        }
        case 2u: {
            const uint32_t wide = wide_ ? Pick(2u) : 0u;
            const uint32_t imm  = Pick(16u);
            return Op(0x08u, (x << 8) | (y << 5) | (wide << 4) | imm);
        }
        case 3u: return Op(0x09u, (x << 8) | Imm8());
        case 4u: {
            const uint32_t op = Pick(2u) == 0u ? 0x0Au : 0x0Bu;
            return Op(op, (x << 8) | Imm8());
        }
        case 5u: {
            static constexpr uint32_t kMinor[] = {2u, 3u, 5u, 5u, 7u, 7u, 4u, 6u};
            const uint32_t minor = kMinor[Pick(Pick(8u) == 0u ? 8u : 6u)];
            uint32_t       imm   = Imm8();
            if (minor == 5u) {
                const uint32_t r32 = 1u + Pick(25u);
                imm = ((r32 >> 3) << 3) | ((r32 & 7u) << 5) | Pick(8u);
            }
            if (minor == 7u) imm = (y << 5) | Pick(32u);
            return Op(0x0Cu, (minor << 8) | imm);
        }
        case 6u: {
            const uint32_t op = Pick(2u) == 0u ? 0x0Du : 0x0Eu;
            return Op(op, (x << 8) | Imm8());
        }
        case 7u:
        case 8u:
        case 9u: {
            static constexpr uint32_t kOps[] = {0x10u, 0x11u, 0x13u, 0x14u, 0x15u,
                                                0x18u, 0x19u, 0x1Bu};
            const uint32_t op  = kOps[Pick(8u)];
            const uint32_t imm = Pick(32u);
            return Op(op, (x << 8) | (y << 5) | imm);
        }
        case 10u: {
            const uint32_t op = Pick(2u) == 0u ? 0x12u : 0x1Au;
            return Op(op, (x << 8) | Imm8());
        }
        case 11u: {
            const uint32_t rz = X();
            const uint32_t f  = wide_ ? Pick(4u) : 1u + 2u * Pick(2u);
            return Op(0x1Cu, (x << 8) | (y << 5) | (rz << 2) | f);
        }
        case 12u:
        case 13u:
        case 14u: return Rr();
        case 15u: {
            static constexpr uint32_t kOps[] = {0x07u, 0x0Fu, 0x17u};
            const uint32_t op  = kOps[Pick(3u)];
            const uint32_t imm = Pick(32u);
            return Op(op, (x << 8) | (y << 5) | imm);
        }
        default: {
            static constexpr uint32_t kFunct[] = {0u, 1u, 2u, 5u, 7u, 3u};
            const uint32_t funct = kFunct[Pick(Pick(8u) == 0u ? 6u : 5u)];
            return Op(0x1Fu, (funct << 8) | (y << 5) | Pick(32u));
        }
        }
    }

    void Extended() {
        static constexpr uint32_t kNarrow[] = {0x00u, 0x06u, 0x08u, 0x09u, 0x0Au, 0x0Bu, 0x0Cu,
                                               0x0Du, 0x0Eu, 0x10u, 0x11u, 0x12u, 0x13u, 0x14u,
                                               0x15u, 0x18u, 0x19u, 0x1Au, 0x1Bu};
        static constexpr uint32_t kWide[]   = {0x07u, 0x0Fu, 0x17u, 0x1Du, 0x1Fu};
        const uint32_t op  = wide_ && Pick(4u) == 0u ? kWide[Pick(5u)] : kNarrow[Pick(19u)];
        const uint32_t rx  = X();
        const uint32_t ry  = X();
        uint32_t       mid = (rx << 8) | (ry << 5);
        if (op == 0x06u) mid |= ShiftFunct();
        if (op == 0x0Cu) {
            const uint32_t minor = 2u + Pick(Pick(8u) == 0u ? 6u : 2u);
            const uint32_t r     = X();
            mid = (minor << 8) | (r << 5);
        }
        if (op == 0x1Du) {
            const uint32_t hi    = Pick(8u) == 0u ? (X() << 8) : 0u;
            const uint32_t r     = X();
            const uint32_t funct = Pick(2u) == 0u ? 0x08u : 0x13u;
            mid = hi | (r << 5) | funct;
        }
        if (op == 0x1Fu) {
            const uint32_t minor = Pick(8u);
            const uint32_t r     = X();
            mid = (minor << 8) | (r << 5);
        }
        const uint32_t ext = static_cast<uint32_t>(rng_()) & 0x7FFu;
        const uint32_t low = op == 0x1Du ? (mid & 0x1Fu) : Pick(32u);
        Emit32(static_cast<uint16_t>(0xF000u | ext),
               Op(op, (mid & ~0x1Fu) | low));
    }

    void Branch() {
        const uint32_t at = static_cast<uint32_t>(hw_.size());
        switch (Pick(4u)) {
        case 0u:
            Emit16(Op(0x02u, 0u));
            fixups_.push_back(Fixup{at, static_cast<uint32_t>(starts_.size()), FixKind::kB16});
            return;
        case 1u: {
            const bool bt = Pick(2u) == 0u;
            if (bt) {
                Emit16(Op(0x0Cu, Pick(2u) << 8));
            } else {
                const uint32_t op = 0x04u + Pick(2u);
                Emit16(Op(op, X() << 8));
            }
            fixups_.push_back(Fixup{at, static_cast<uint32_t>(starts_.size()), FixKind::kBz16});
            return;
        }
        default: {
            static constexpr uint32_t kOps[] = {0x02u, 0x04u, 0x05u, 0x0Cu};
            const uint32_t op  = kOps[Pick(4u)];
            const uint32_t mid = op == 0x0Cu ? (Pick(2u) << 8) : (X() << 8);
            Emit32(0xF000u, Op(op, mid));
            fixups_.push_back(Fixup{at, static_cast<uint32_t>(starts_.size()), FixKind::kBExt});
            return;
        }
        }
    }

    void Jal() {
        if (Pick(3u) == 0u) {
            EmitJalx(Pick(2u) == 0u ? kMipsRigEndVa : kMipsRigEnd16Va);
        } else {
            const uint32_t at = static_cast<uint32_t>(hw_.size());
            Emit32(Op(0x03u, 0u), 0u);
            fixups_.push_back(Fixup{at, static_cast<uint32_t>(starts_.size()) + 1u, FixKind::kJal});
        }
        Emit16(Simple());
    }

    void RegisterJump() {
        const uint32_t x  = X();
        const uint32_t at = static_cast<uint32_t>(hw_.size());
        Emit16(Op(0x01u, x << 8));
        fixups_.push_back(Fixup{at, static_cast<uint32_t>(starts_.size()) + 3u, FixKind::kAddiupc});
        Emit16(Op(0x09u, (x << 8) | 1u));
        Emit16(Op(0x1Du, (x << 8) | ((Pick(2u) == 0u ? 0u : 2u) << 5)));
        Emit16(Simple());
    }

    void PcRelative() {
        const uint32_t x = X();
        switch (Pick(wide_ ? 4u : 2u)) {
        case 0u: Emit16(Op(0x01u, (x << 8) | Imm8())); return;
        case 1u: Emit16(Op(0x16u, (x << 8) | Imm8())); return;
        case 2u: Emit16(Op(0x1Fu, (4u << 8) | (x << 5) | Pick(32u))); return;
        default: Emit16(Op(0x1Fu, (6u << 8) | (x << 5) | Pick(32u))); return;
        }
    }

    void BasedAccess() {
        const uint32_t base = X();
        const uint32_t r32  = Pick(2u) == 0u ? kMipsGenBaseData : kMipsGenBaseMmio;
        Emit16(Op(0x0Cu, (7u << 8) | (base << 5) | r32));
        static constexpr uint32_t kOps[] = {0x10u, 0x11u, 0x13u, 0x14u, 0x15u,
                                            0x18u, 0x19u, 0x1Bu};
        const uint32_t op    = kOps[Pick(8u)];
        const uint32_t scale = op == 0x10u || op == 0x14u || op == 0x18u ? 1u
                               : op == 0x13u || op == 0x1Bu               ? 4u
                                                                         : 2u;
        const uint32_t ry  = X();
        const uint32_t imm = Pick(32u) & ~(scale - 1u);
        Emit16(Op(op, (base << 8) | (ry << 5) | imm));
    }

    static constexpr uint32_t kMipsGenBaseData = 28u;
    static constexpr uint32_t kMipsGenBaseMmio = 27u;

    uint32_t StartVa(uint32_t index) const { return start_va_ + 2u * starts_[index]; }

    uint32_t ForwardStart(uint32_t first, bool word_aligned) {
        std::vector<uint32_t> options;
        for (uint32_t i = first; i < starts_.size(); ++i) {
            if (!word_aligned || (StartVa(i) & 3u) == 0u) options.push_back(i);
        }
        if (options.empty()) return UINT32_MAX;
        return options[Pick(static_cast<uint32_t>(options.size()))];
    }

    void Resolve() {
        for (const Fixup& f : fixups_) {
            const uint32_t pc = start_va_ + 2u * f.at;
            switch (f.kind) {
            case FixKind::kB16: {
                const uint32_t t   = StartVa(ForwardStart(f.first, false));
                const uint32_t off = (t - pc - 2u) >> 1;
                hw_[f.at] = static_cast<uint16_t>(hw_[f.at] | (off & 0x7FFu));
                break;
            }
            case FixKind::kBz16: {
                const uint32_t t   = StartVa(ForwardStart(f.first, false));
                const uint32_t off = (t - pc - 2u) >> 1;
                hw_[f.at] = static_cast<uint16_t>(hw_[f.at] | (off & 0xFFu));
                break;
            }
            case FixKind::kBExt: {
                const uint32_t t   = StartVa(ForwardStart(f.first, false));
                const uint32_t imm = (t - pc - 4u) >> 1;
                hw_[f.at] = static_cast<uint16_t>(0xF000u | ((imm >> 11) & 0x1Fu) | (((imm >> 5) & 0x3Fu) << 5));
                hw_[f.at + 1u] = static_cast<uint16_t>(hw_[f.at + 1u] | (imm & 0x1Fu));
                break;
            }
            case FixKind::kJal: {
                const uint32_t i     = ForwardStart(f.first, true);
                const uint32_t t     = i == UINT32_MAX ? kMipsRigEndVa : StartVa(i);
                const uint32_t imm26 = (t >> 2) & 0x03FFFFFFu;
                const uint32_t x     = i == UINT32_MAX ? 1u : 0u;
                hw_[f.at] = static_cast<uint16_t>((0x03u << 11) | (x << 10) |
                                                  (((imm26 >> 16) & 0x1Fu) << 5) | ((imm26 >> 21) & 0x1Fu));
                hw_[f.at + 1u] = static_cast<uint16_t>(imm26);
                break;
            }
            case FixKind::kAddiupc: {
                const uint32_t base = pc & ~3u;
                uint32_t       i    = ForwardStart(f.first, true);
                if (i != UINT32_MAX && StartVa(i) - base > 0x3FCu) i = UINT32_MAX;
                const uint32_t t    = i == UINT32_MAX ? base : StartVa(i);
                hw_[f.at] = static_cast<uint16_t>(hw_[f.at] | (((t - base) >> 2) & 0xFFu));
                break;
            }
            }
        }
    }

    std::mt19937&         rng_;
    const bool            wide_;
    std::vector<uint16_t> hw_;
    std::vector<uint32_t> starts_;
    std::vector<Fixup>    fixups_;
    uint32_t              start_va_ = 0;
};
