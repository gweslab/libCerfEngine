#include "arm_vfp_register_form.h"

namespace cerfengine {

ArmVfpRegisterTransfer ArmVfpRegisterTransferOf(const DecodedInsn& d, const ArmFeatures& features) {
    ArmVfpRegisterTransfer t;
    /* ARM DDI 0406C.c Table A7-22 (p. A7-278): L=0, C=1, A=1xx, B=0x is
       VDUP (ARM core register). */
    if (features.neon && d.cp_num == 11u && d.l == 0u && (d.cp_opc & 4u) && (d.cp & 2u) == 0u) {
        t.form = ArmVfpRegisterForm::kNeonVdupCore;
        return t;
    }

    if (features.neon && d.cp_num == 11u) {
        const bool esize_8  = (d.cp_opc & 2u) != 0u;
        const bool esize_16 = ((d.cp_opc & 2u) == 0u) && ((d.cp & 1u) != 0u);
        if (esize_8 || esize_16) {
            t.form = d.l == 0u ? ArmVfpRegisterForm::kNeonCoreToScalar
                               : ArmVfpRegisterForm::kNeonScalarToCore;
            return t;
        }
    }

    /* B9.3.21 VMRS (p. B9-2014) and B9.3.22 VMSR (p. B9-2016), encoding
       T1/A1: bits[27:20] = 1110 111L, coproc = 1010, opc2 = (0)(0)(0),
       CRm = (0)(0)(0)(0). */
    if (d.cp_num == 10u && d.cp_opc == 7u && d.crm == 0u && d.cp == 0u) {
        t.form = ArmVfpRegisterForm::kSystemRegister;
        return t;
    }

    if (d.cp_num == 11u && (d.cp_opc & 6u) == 0u && d.crm == 0u && (d.cp & 3u) == 0u) {
        const uint32_t dn = (((d.cp >> 2) & 1u) << 4) | (d.crn & 0xFu);
        t.form   = ArmVfpRegisterForm::kSingleMove;
        t.single = (dn << 1) | (d.cp_opc & 1u);
        return t;
    }

    /* VMOV Sn <-> Rt (DDI 0406C.c A8.8.343, p. A8-944): "n = UInt(Vn:N)". */
    if (d.cp_opc == 0u && d.crm == 0u && (d.cp & 3u) == 0u) {
        t.form   = ArmVfpRegisterForm::kSingleMove;
        t.single = ((d.crn & 0xFu) << 1) | ((d.cp >> 2) & 1u);
        return t;
    }
    return t;
}

}
