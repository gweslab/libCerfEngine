# ARM DDI 0406C.c A2.2.1 (pp. A2-41 to A2-43) and A8.4.3 Shift_C() (p. A8-292).
import itertools
import os

N = 32
MASK = (1 << N) - 1


def uint(x):
    return x & MASK


def sint(x):
    x &= MASK
    return x - (1 << N) if x & (1 << (N - 1)) else x


def add_with_carry(x, y, carry_in):
    unsigned_sum = uint(x) + uint(y) + carry_in
    signed_sum = sint(x) + sint(y) + carry_in
    result = unsigned_sum & MASK
    carry_out = 0 if uint(result) == unsigned_sum else 1
    overflow = 0 if sint(result) == signed_sum else 1
    return result, carry_out, overflow


def bits(x, hi, lo):
    return (x >> lo) & ((1 << (hi - lo + 1)) - 1)


def lsl_c(x, shift):
    extended = x << shift
    return bits(extended, N - 1, 0), bits(extended, N, N)


def lsr_c(x, shift):
    extended = x
    return bits(extended, shift + N - 1, shift), bits(extended, shift - 1, shift - 1)


def asr_c(x, shift):
    extended = sint(x)
    result = (extended >> shift) & MASK
    carry = (extended >> (shift - 1)) & 1
    return result, carry


def ror_c(x, shift):
    m = shift % N
    lsr = bits(x, N - 1, m) if m else x
    lsl = (x << (N - m)) & MASK if m else 0
    result = lsr | lsl
    return result, bits(result, N - 1, N - 1)


def shift_c(kind, value, amount, carry_in):
    if amount == 0:
        return value, carry_in
    return {"ShlC": lsl_c, "LsrC": lsr_c, "AsrC": asr_c, "RorC": ror_c}[kind](value, amount)


def rrx_c(x, carry_in):
    return (carry_in << (N - 1)) | bits(x, N - 1, 1), x & 1


values = [0x00000000, 0x00000001, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x12345678, 0x80000001]
lines = []
for op, (x, y), c in itertools.product(["Add", "Sub", "Adc", "Sbc"],
                                       itertools.product(values, values), [0, 1]):
    if op in ("Add", "Sub") and c == 1:
        continue
    yy = y if op in ("Add", "Adc") else (~y) & MASK
    cin = {"Add": 0, "Sub": 1}.get(op, c)
    r, co, ov = add_with_carry(x, yy, cin)
    n = r >> (N - 1)
    z = 1 if r == 0 else 0
    lines.append("    {Op::k%s, 0x%08Xu, 0x%08Xu, %du, 0x%08Xu, %d, %d, %d, %d}," %
                 (op, x, y, c, r, n, z, co, ov))
amounts = [0, 1, 4, 31, 32, 33, 64, 255]
for op, x, amount, c in itertools.product(["ShlC", "LsrC", "AsrC", "RorC"],
                                          [0x80000001, 0x7FFFFFFE, 0x12345678], amounts, [0, 1]):
    r, co = shift_c(op, x, amount, c)
    n = r >> (N - 1)
    z = 1 if r == 0 else 0
    lines.append("    {Op::k%s, 0x%08Xu, %du, %du, 0x%08Xu, %d, %d, %d, -1}," %
                 (op, x, amount, c, r, n, z, co))
for x, c in itertools.product([0x80000001, 0x7FFFFFFE, 0x00000000], [0, 1]):
    r, co = rrx_c(x, c)
    n = r >> (N - 1)
    z = 1 if r == 0 else 0
    lines.append("    {Op::kRrx, 0x%08Xu, %du, 0u, 0x%08Xu, %d, %d, %d, -1}," % (x, c, r, n, z, co))

with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "cerfir_eval_vectors.inc"),
          "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(lines) + "\n")
print(len(lines))
