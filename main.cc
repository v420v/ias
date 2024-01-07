#include <cstdint>
#include <vector>
#include <string>
#include <iostream>
#include <unordered_map>
#include <functional>
#include <fstream>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <sstream>

// --------------------------------------------------------------------
// --------------------------------------------------------------------
// A64 encoder
// --------------------------------------------------------------------
// --------------------------------------------------------------------

// ARM ® A64 Instruction Set Architecture ARMv8, for ARMv8-A architecture profile
// https://student.cs.uwaterloo.ca/~cs452/docs/rpi4b/ISA_A64_xml_v88A-2021-12_OPT.pdf

enum OperandKind {
    XR,
    WR,
    XSP,
    WSP,
    IMM,
    SHIFT,
    EXTEND,
    COND,
    MEM_OP_BASE,
    MEM_OP_BASE_PRE,
    MEM_OP_IMM_OFFSET,
    MEM_OP_REGI_OFFSET,
    MEM_OP_IMM_OFFSET_PRE,
};

struct Operand {
    OperandKind kind;
    int regi_bits;
    int imm;
    int val; // shift|extend|cond val
    int amount; // shift|extend amount
    // ref
    Operand* base_register;
    Operand* offset; // offset takes one of three formats { Immediate | Register }
    Operand* extend_offset;
};

enum ShiftType {
    LSL      = 0b00,
    LSR      = 0b01,
    ASR      = 0b10,
    RESERVED = 0b11,
    ROR      = 0b11,
};

enum ExtendType {
    UXTB = 0b000,
    UXTH = 0b001,
    UXTW = 0b010,
    UXTX = 0b011,
    SXTB = 0b100,
    SXTH = 0b101,
    SXTW = 0b110,
    SXTX = 0b111,
};

enum CondType {
    EQ = 0b0000,
    NE = 0b0001,
    HS = 0b0010,
    LO = 0b0011,
    MI = 0b0100,
    PL = 0b0101,
    VS = 0b0110,
    VC = 0b0111,
    HI = 0b1000,
    LS = 0b1001,
    GE = 0b1010,
    LT = 0b1011,
    GT = 0b1100,
    LE = 0b1101,
    AL = 0b1110,
};

Operand* new_regi(OperandKind kind, int regi_bits) {
    Operand *op = new Operand;
    op->kind = kind;
    op->regi_bits = regi_bits;

    return op;
}

std::unordered_map<std::string, Operand*> registers = {
    // 64bit registers
    {"x0",  new_regi(XR, 0)},  {"x1",  new_regi(XR, 1)},  {"x2",  new_regi(XR, 2)},  {"x3",  new_regi(XR, 3)},
    {"x4",  new_regi(XR, 4)},  {"x5",  new_regi(XR, 5)},  {"x6",  new_regi(XR, 6)},  {"x7",  new_regi(XR, 7)},
    {"x8",  new_regi(XR, 8)},  {"x9",  new_regi(XR, 9)},  {"x10", new_regi(XR, 10)}, {"x11", new_regi(XR, 11)},
    {"x12", new_regi(XR, 12)}, {"x13", new_regi(XR, 13)}, {"x14", new_regi(XR, 14)}, {"x15", new_regi(XR, 15)},
    {"x16", new_regi(XR, 16)}, {"x17", new_regi(XR, 17)}, {"x18", new_regi(XR, 18)}, {"x19", new_regi(XR, 19)},
    {"x20", new_regi(XR, 20)}, {"x21", new_regi(XR, 21)}, {"x22", new_regi(XR, 22)}, {"x23", new_regi(XR, 23)},
    {"x24", new_regi(XR, 24)}, {"x25", new_regi(XR, 25)}, {"x26", new_regi(XR, 26)}, {"x27", new_regi(XR, 27)},
    {"x28", new_regi(XR, 28)}, {"x29", new_regi(XR, 29)}, {"x30", new_regi(XR, 30)}, {"sp",  new_regi(XSP, 31)},

    // 32bit registers
    {"w0",  new_regi(WR, 0)},  {"w1",  new_regi(WR, 1)},  {"w2",  new_regi(WR, 2)},  {"w3",  new_regi(WR, 3)},
    {"w4",  new_regi(WR, 4)},  {"w5",  new_regi(WR, 5)},  {"w6",  new_regi(WR, 6)},  {"w7",  new_regi(WR, 7)},
    {"w8",  new_regi(WR, 8)},  {"w9",  new_regi(WR, 9)},  {"w10", new_regi(WR, 10)}, {"w11", new_regi(WR, 11)},
    {"w12", new_regi(WR, 12)}, {"w13", new_regi(WR, 13)}, {"w14", new_regi(WR, 14)}, {"w15", new_regi(WR, 15)},
    {"w16", new_regi(WR, 16)}, {"w17", new_regi(WR, 17)}, {"w18", new_regi(WR, 18)}, {"w19", new_regi(WR, 19)},
    {"w20", new_regi(WR, 20)}, {"w21", new_regi(WR, 21)}, {"w22", new_regi(WR, 22)}, {"w23", new_regi(WR, 23)},
    {"w24", new_regi(WR, 24)}, {"w25", new_regi(WR, 25)}, {"w26", new_regi(WR, 26)}, {"w27", new_regi(WR, 27)},
    {"w28", new_regi(WR, 28)}, {"w29", new_regi(WR, 29)}, {"w30", new_regi(WR, 30)}, {"wsp", new_regi(WSP, 31) }
};

std::unordered_map<std::string, ShiftType> shift_types = {
    {"LSL", LSL},
    {"LSR", LSR},
    {"ASR", ASR},
    {"RESERVED", RESERVED},
    {"ROR", ROR},
};

std::unordered_map<std::string, ExtendType> extend_types = {
    {"UXTB", UXTB},
    {"UXTH", UXTH},
    {"UXTW", UXTW},
    {"UXTX", UXTX},
    {"SXTB", SXTB},
    {"SXTH", SXTH},
    {"SXTW", SXTW},
    {"SXTX", SXTX},
};

Operand *new_shift(ShiftType shift_type, int amount) {
    Operand *op = new Operand;
    op->kind = SHIFT;
    op->val = shift_type;
    op->amount = amount;

    return op;
}

Operand *new_extend(ExtendType extend_type, int amount) {
    Operand *op = new Operand;
    op->kind = EXTEND;
    op->val = extend_type;
    op->amount = amount;

    return op;
}

Operand *new_cond(CondType cond_type) {
    Operand *op = new Operand;
    op->kind = COND;
    op->val = cond_type;

    return op;
}

Operand *new_imm(int imm) {
    Operand *op = new Operand;
    op->kind = IMM;
    op->imm = imm;

    return op;
}

[[noreturn]] void unreachable() {
    std::cerr << "unreachable" << std::endl;
    exit(1);
}

#define is_xr(operands, i)                   (operands[i]->kind == XR)
#define is_wr(operands, i)                   (operands[i]->kind == WR)
#define is_xr_or_xsp(operands, i)            (operands[i]->kind == XR || operands[i]->kind == XSP)
#define is_wr_or_wsp(operands, i)            (operands[i]->kind == WR || operands[i]->kind == WSP)
#define is_shift(operands, i)                (operands[i]->kind == SHIFT)
#define is_extend(operands, i)               (operands[i]->kind == EXTEND)
#define is_imm(operands, i)                  (operands[i]->kind == IMM)
#define is_cond(operands, i)                 (operands[i]->kind == COND)
#define is_mem_op_base(operands, i)             (operands[i]->kind == MEM_OP_BASE || (operands[i]->kind == MEM_OP_IMM_OFFSET && operands[i]->offset->imm == 0))
#define is_mem_op_imm_offset(operands, i)       (operands[i]->kind == MEM_OP_IMM_OFFSET)
#define is_mem_op_imm_offset_pre(operands, i)   (operands[i]->kind == MEM_OP_IMM_OFFSET_PRE)
#define is_mem_op_regi_offset(operands, i)      (operands[i]->kind == MEM_OP_REGI_OFFSET)

#define next_op_shift(operands, i)           ((operand_length > i+1) ? is_shift(operands, i+1) : true)
#define next_op_extend(operands, i)          ((operand_length > i+1) ? is_extend(operands, i+1) : true)

#define is_xr_shift(operands, i)             (is_xr(operands, i) && next_op_shift(operands, i))
#define is_wr_shift(operands, i)             (is_wr(operands, i) && next_op_shift(operands, i))
#define is_imm_shift(operands, i)            (is_imm(operands, i) && next_op_shift(operands, i))
#define is_xr_extend(operands, i)            (is_xr(operands, i) && next_op_extend(operands, i))
#define is_wr_extend(operands, i)            (is_wr(operands, i) && next_op_extend(operands, i))

#define pattern1(A)             (operand_length >= 1) && \
                                is_##A(operands, 0)

#define pattern2(A, B)          (operand_length >= 2) && \
                                is_##A(operands, 0) && \
                                is_##B(operands, 1)

#define pattern3(A, B, C)       (operand_length >= 3) && \
                                is_##A(operands, 0) && \
                                is_##B(operands, 1) && \
                                is_##C(operands, 2)

#define pattern4(A, B, C, D)    (operand_length >= 4) && \
                                is_##A(operands, 0) && \
                                is_##B(operands, 1) && \
                                is_##C(operands, 2) && \
                                is_##D(operands, 3)

#define pattern5(A, B, C, D, E) (operand_length >= 5) && \
                                is_##A(operands, 0) && \
                                is_##B(operands, 1) && \
                                is_##C(operands, 2) && \
                                is_##D(operands, 3) && \
                                is_##E(operands, 4)

CondType invert_cond(CondType cond) {
    static std::unordered_map<CondType, CondType> inverted_condtype = {
        {EQ, NE}, {NE, EQ}, {HS, LO}, {LO, HS}, {MI, PL}, {PL, MI}, {VS, VC}, {VC, VS}, {HI, LS}, {LS, HI}, {GE, LT}, {LT, GE}, {GT, LE}, {LE, GT},
    };
    return inverted_condtype[cond];
}

#define ENCODE_REGI(operand_idx, b)                 (operands[operand_idx]->regi_bits << b)
#define ENCODE_SHIFTS(operand_idx, b1, b2)          ((operand_length > operand_idx) ? (operands[operand_idx]->val << b1) | (operands[operand_idx]->amount << b2) : 0)

#define ENCODE_MEM_OP_BASE(operand_idx, b)             (operands[operand_idx]->base_register->regi_bits << b)

#define ENCODE_MEM_OP_IMM9_OFFSET(operand_idx, b1, b2)  (operands[operand_idx]->base_register->regi_bits << b1) | ((operands[operand_idx]->offset->imm & 0b111111111) << b2)
#define ENCODE_MEM_OP_IMM12_OFFSET(operand_idx, b1, b2) (operands[operand_idx]->base_register->regi_bits << b1) | ((operands[operand_idx]->offset->imm & 0b111111111111) << b2)

#define ENCODE_MEM_OP_DIV_IMM7_OFFSET(operand_idx, b1, b2, div)  (operands[operand_idx]->base_register->regi_bits << b1) | (((operands[operand_idx]->offset->imm / div) & 0b1111111) << b2)
#define ENCODE_MEM_OP_DIV_IMM9_OFFSET(operand_idx, b1, b2, div)  (operands[operand_idx]->base_register->regi_bits << b1) | (((operands[operand_idx]->offset->imm / div) & 0b111111111) << b2)
#define ENCODE_MEM_OP_DIV_IMM12_OFFSET(operand_idx, b1, b2, div) (operands[operand_idx]->base_register->regi_bits << b1) | (((operands[operand_idx]->offset->imm / div) & 0b111111111111) << b2)

#define ENCODE_MEM_OP_REGI_OFFSET(operand_idx, b1, b2, b3, b4, div_amount)   (operands[operand_idx]->base_register->regi_bits << b1) | (operands[operand_idx]->offset->regi_bits << b2) | (operands[operand_idx]->extend_offset->val << b3) | ((operands[operand_idx]->extend_offset->amount / div_amount) << b4)

// imm
#define ENCODE_IMM4(operand_idx, b)               (operands[operand_idx]->imm & 0b1111) << b
#define ENCODE_IMM5(operand_idx, b)               (operands[operand_idx]->imm & 0b11111) << b
#define ENCODE_IMM6(operand_idx, b)               (operands[operand_idx]->imm & 0b111111) << b
#define ENCODE_IMM7(operand_idx, b)               (operands[operand_idx]->imm & 0b1111111) << b
#define ENCODE_IMM8(operand_idx, b)               (operands[operand_idx]->imm & 0b11111111) << b
#define ENCODE_IMM9(operand_idx, b)               (operands[operand_idx]->imm & 0b111111111) << b
#define ENCODE_IMM10(operand_idx, b)              (operands[operand_idx]->imm & 0b1111111111) << b
#define ENCODE_IMM11(operand_idx, b)              (operands[operand_idx]->imm & 0b11111111111) << b
#define ENCODE_IMM12(operand_idx, b)              (operands[operand_idx]->imm & 0b111111111111) << b
#define ENCODE_IMM16(operand_idx, b)              (operands[operand_idx]->imm & 0b1111111111111111) << b

#define ENCODE_DIV_IMM7(operand_idx, b, div)      ((operands[operand_idx]->imm / div) & 0b1111111) << b

#define ENCODE_SUB_IMM6(operand_idx, sub, b)      ((sub - operands[operand_idx]->imm) & 0b111111) << b
#define ENCODE_NEG_MOD_IMM6(operand_idx, mod, b)  (((-operands[operand_idx]->imm) % mod) & 0b111111) << b

// cond
#define ENCODE_COND(operand_idx, b)               (operands[operand_idx]->val << b)
#define ENCODE_INV_COND(operand_idx, b)           (invert_cond((CondType)operands[operand_idx]->val) << b)

// extend
#define ENCODE_EXTENDX(operand_idx, b1, b2)       ((operand_length > operand_idx) ? ((operands[operand_idx]->val << b1) | (operands[operand_idx]->amount << b2)) : UXTX << b1)
#define ENCODE_EXTENDW(operand_idx, b1, b2)       ((operand_length > operand_idx) ? ((operands[operand_idx]->val << b1) | (operands[operand_idx]->amount << b2)) : UXTW << b1)
#define ENCODE_LSL_SHIFTS(operand_idx, div, b)    ((operand_length > operand_idx) ? ((operands[operand_idx]->amount / div) << b) : 0)

// Base instructions in alphabetic order
// https://student.cs.uwaterloo.ca/~cs452/docs/rpi4b/ISA_A64_xml_v88A-2021-12_OPT.pdf

static std::unordered_map<std::string, std::function<uint32_t(Operand**, int)>> instr_table = {
    {"adc", [](Operand** operands, int operand_length) {
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b10011010000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011010000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"adcs", [](Operand** operands, int operand_length) {
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b10111010000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00111010000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"add", [](Operand** operands, int operand_length) {
        if (pattern3(xr, xr, xr_shift))                 return (uint32_t)0b10001011000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_SHIFTS(3, 22, 10); // #2
        if (pattern3(wr, wr, wr_shift))                 return (uint32_t)0b00001011000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_SHIFTS(3, 22, 10); // #2
        if (pattern3(xr_or_xsp, xr_or_xsp, imm_shift))  return (uint32_t)0b10010001000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_IMM12(2, 10) | ENCODE_LSL_SHIFTS(3, 12, 22); // #3
        if (pattern3(wr_or_wsp, wr_or_wsp, imm_shift))  return (uint32_t)0b00010001000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_IMM12(2, 10) | ENCODE_LSL_SHIFTS(3, 12, 22); // #3
        if (pattern3(wr_or_wsp, wr_or_wsp, wr_extend))  return (uint32_t)0b00001011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_EXTENDW(3, 13, 10); // #4
        if (pattern3(xr_or_xsp, xr_or_xsp, xr_extend))  return (uint32_t)0b10001011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_EXTENDX(3, 13, 10); // #5
        if (pattern3(xr_or_xsp, xr_or_xsp, wr_extend))  return (uint32_t)0b10001011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_EXTENDW(3, 13, 10); // #4
        unreachable();
    }},
    {"adds", [](Operand** operands, int operand_length) {
        if (pattern3(xr, xr, xr_shift))                 return (uint32_t)0b10101011000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_SHIFTS(3, 22, 10); // #2
        if (pattern3(wr, wr, wr_shift))                 return (uint32_t)0b00101011000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_SHIFTS(3, 22, 10); // #2
        if (pattern3(xr, xr_or_xsp, imm_shift))         return (uint32_t)0b10110001000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_IMM12(2, 10) | ENCODE_LSL_SHIFTS(3, 12, 22); // #3
        if (pattern3(wr, wr_or_wsp, imm_shift))         return (uint32_t)0b00110001000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_IMM12(2, 10) | ENCODE_LSL_SHIFTS(3, 12, 22); // #3
        if (pattern3(wr, wr_or_wsp, wr_extend))         return (uint32_t)0b00101011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_EXTENDW(3, 13, 10); // #4
        if (pattern3(xr, xr_or_xsp, xr_extend))         return (uint32_t)0b10101011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_EXTENDX(3, 13, 10); // #5
        if (pattern3(xr, xr_or_xsp, wr_extend))         return (uint32_t)0b10101011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_EXTENDX(3, 13, 10); // #5
        unreachable();
    }},
    {"asr", [](Operand** operands, int operand_length) {
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b10011010110000000010100000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011010110000000010100000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        if (pattern3(xr, xr, imm))                      return (uint32_t)0b10010011010000001111110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_IMM6(2, 16); // #6
        if (pattern3(wr, wr, imm))                      return (uint32_t)0b00010011010000001111110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_IMM6(2, 16); // #6
        unreachable();
    }},
    {"asrv", [](Operand** operands, int operand_length) {
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b10011010110000000010100000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011010110000000010100000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"autda", [](Operand** operands, int operand_length) {
        if (pattern2(xr, xr_or_xsp))                    return (uint32_t)0b11011010110000010001100000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"autdb", [](Operand** operands, int operand_length) {
        if (pattern2(xr, xr_or_xsp))                    return (uint32_t)0b11011010110000010001110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"autdza", [](Operand** operands, int operand_length) {
        if (pattern1(xr))                               return (uint32_t)0b11011010110000010011101111100000 | ENCODE_REGI(0, 0); // #7
        unreachable();
    }},
    {"autdzb", [](Operand** operands, int operand_length) {
        if (pattern1(xr))                               return (uint32_t)0b11011010110000010011111111100000 | ENCODE_REGI(0, 0); // #7
        unreachable();
    }},
    {"autia", [](Operand** operands, int operand_length) {
        if (pattern2(xr, xr_or_xsp))                    return (uint32_t)0b11011010110000010001000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"autia1716", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010000110011111;
        unreachable();
    }},
    {"autiasp", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010001110111111;
        unreachable();
    }},
    {"autiaz", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010001110011111;
        unreachable();
    }},
    {"autiza", [](Operand** operands, int operand_length) {
        if (pattern1(xr))                               return (uint32_t)0b11011010110000010011001111100000 | ENCODE_REGI(0, 0); // #7
        unreachable();
    }},
    {"autib", [](Operand** operands, int operand_length) {
        if (pattern2(xr, xr_or_xsp))                    return (uint32_t)0b11011010110000010001010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"autib1716", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010000111011111;
        unreachable();
    }},
    {"autibsp", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010001111111111;
        unreachable();
    }},
    {"autibz", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010001111011111;
        unreachable();
    }},
    {"autizb", [](Operand** operands, int operand_length) {
        if (pattern1(xr))                               return (uint32_t)0b11011010110000010011011111100000 | ENCODE_REGI(0, 0); // #7
        unreachable();
    }},
    {"cas", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10001000101000000111110000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11001000101000000111110000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"casa", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10001000111000000111110000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11001000111000000111110000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"casab", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00001000111000000111110000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"casah", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01001000111000000111110000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"casal", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10001000111000001111110000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11001000111000001111110000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"casalb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00001000111000001111110000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"casalh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01001000111000001111110000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"casb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00001000101000000111110000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"cash", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01001000101000000111110000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ccmn", [](Operand** operands, int operand_length) {
        // immediate
        if (pattern4(xr, imm, imm, cond))               return (uint32_t)0b10111010010000000000100000000000 | ENCODE_REGI(0, 5) | ENCODE_IMM5(1, 16) | ENCODE_IMM4(2, 0) | ENCODE_COND(3, 12); // #9
        if (pattern4(wr, imm, imm, cond))               return (uint32_t)0b00111010010000000000100000000000 | ENCODE_REGI(0, 5) | ENCODE_IMM5(1, 16) | ENCODE_IMM4(2, 0) | ENCODE_COND(3, 12); // #9
        // register
        if (pattern4(xr, xr, imm, cond))                return (uint32_t)0b10111010010000000000000000000000 | ENCODE_REGI(0, 5) | ENCODE_REGI(1, 16) | ENCODE_IMM4(2, 0) | ENCODE_COND(3, 12); // #10
        if (pattern4(wr, wr, imm, cond))                return (uint32_t)0b00111010010000000000000000000000 | ENCODE_REGI(0, 5) | ENCODE_REGI(1, 16) | ENCODE_IMM4(2, 0) | ENCODE_COND(3, 12); // #10
        unreachable();
    }},
    {"ccmp", [](Operand** operands, int operand_length) {
        // immediate
        if (pattern4(xr, imm, imm, cond))               return (uint32_t)0b11111010010000000000100000000000 | ENCODE_REGI(0, 5) | ENCODE_IMM5(1, 16) | ENCODE_IMM4(2, 0) | ENCODE_COND(3, 12); // #9
        if (pattern4(wr, imm, imm, cond))               return (uint32_t)0b01111010010000000000100000000000 | ENCODE_REGI(0, 5) | ENCODE_IMM5(1, 16) | ENCODE_IMM4(2, 0) | ENCODE_COND(3, 12); // #9
        // register
        if (pattern4(xr, xr, imm, cond))                return (uint32_t)0b11111010010000000000000000000000 | ENCODE_REGI(0, 5) | ENCODE_REGI(1, 16) | ENCODE_IMM4(2, 0) | ENCODE_COND(3, 12); // #10
        if (pattern4(wr, wr, imm, cond))                return (uint32_t)0b01111010010000000000000000000000 | ENCODE_REGI(0, 5) | ENCODE_REGI(1, 16) | ENCODE_IMM4(2, 0) | ENCODE_COND(3, 12); // #10
        unreachable();
    }},
    {"cfinv", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000000100000000011111;
        unreachable();
    }},
    {"cinc", [](Operand** operands, int operand_length) {
        if (pattern3(xr, xr, cond))                     return (uint32_t)0b10011010100000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(1, 16) | ENCODE_INV_COND(2, 12); // #15
        if (pattern3(wr, wr, cond))                     return (uint32_t)0b00011010100000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(1, 16) | ENCODE_INV_COND(2, 12); // #15
        unreachable();
    }},
    {"cinv", [](Operand** operands, int operand_length) {
        if (pattern3(xr, xr, cond))                     return (uint32_t)0b11011010100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(1, 16) | ENCODE_INV_COND(2, 12); // #15
        if (pattern3(wr, wr, cond))                     return (uint32_t)0b01011010100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(1, 16) | ENCODE_INV_COND(2, 12); // #15
        unreachable();
    }},
    {"clrex", [](Operand** operands, int operand_length) {
        if (pattern1(imm))                              return (uint32_t)0b11010101000000110011000001011111 | ENCODE_IMM4(0, 8);
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110011111101011111;
        unreachable();
    }},
    {"cls", [](Operand** operands, int operand_length) {
        if (pattern2(xr, xr))                           return (uint32_t)0b11011010110000000001010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        if (pattern2(wr, wr))                           return (uint32_t)0b01011010110000000001010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"clz", [](Operand** operands, int operand_length) {
        if (pattern2(xr, xr))                           return (uint32_t)0b11011010110000000001000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        if (pattern2(wr, wr))                           return (uint32_t)0b01011010110000000001000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"cmn", [](Operand** operands, int operand_length) {
        // (shiftted register)
        if (pattern2(xr, xr_shift))                     return (uint32_t)0b10101011000000000000000000011111 | ENCODE_REGI(0, 5) | ENCODE_REGI(1, 16) | ENCODE_SHIFTS(2, 22, 10); // #11
        if (pattern2(wr, wr_shift))                     return (uint32_t)0b00101011000000000000000000011111 | ENCODE_REGI(0, 5) | ENCODE_REGI(1, 16) | ENCODE_SHIFTS(2, 22, 10); // #11
        // (extened register)
        if (pattern2(xr_or_xsp, xr_extend))             return (uint32_t)0b10101011001000000000000000011111 | ENCODE_REGI(0, 5) | ENCODE_REGI(1, 16) | ENCODE_EXTENDX(2, 13, 10); // #12
        if (pattern3(xr_or_xsp, wr, extend))            return (uint32_t)0b10101011001000000000000000011111 | ENCODE_REGI(0, 5) | ENCODE_REGI(1, 16) | ENCODE_EXTENDX(2, 13, 10); // #12
        if (pattern2(wr_or_wsp, wr_extend))             return (uint32_t)0b00101011001000000000000000011111 | ENCODE_REGI(0, 5) | ENCODE_REGI(1, 16) | ENCODE_EXTENDW(2, 13, 10); // #13
        // (immediate)
        if (pattern2(xr_or_xsp, imm_shift))             return (uint32_t)0b10110001000000000000000000011111 | ENCODE_REGI(0, 5) | ENCODE_IMM12(1, 10) | ENCODE_LSL_SHIFTS(2, 12, 22); // #14
        if (pattern2(wr_or_wsp, imm_shift))             return (uint32_t)0b00110001000000000000000000011111 | ENCODE_REGI(0, 5) | ENCODE_IMM12(1, 10) | ENCODE_LSL_SHIFTS(2, 12, 22); // #14
        unreachable();
    }},
    {"cmp", [](Operand** operands, int operand_length) {
        if (pattern2(xr, xr_shift))                     return (uint32_t)0b11101011000000000000000000011111 | ENCODE_REGI(0, 5) | ENCODE_REGI(1, 16) | ENCODE_SHIFTS(2, 22, 10); // #11
        if (pattern2(wr, wr_shift))                     return (uint32_t)0b01101011000000000000000000011111 | ENCODE_REGI(0, 5) | ENCODE_REGI(1, 16) | ENCODE_SHIFTS(2, 22, 10); // #11
        if (pattern2(xr_or_xsp, xr_extend))             return (uint32_t)0b11101011001000000000000000011111 | ENCODE_REGI(0, 5) | ENCODE_REGI(1, 16) | ENCODE_EXTENDX(2, 13, 10); // #12
        if (pattern3(xr_or_xsp, wr, extend))            return (uint32_t)0b11101011001000000000000000011111 | ENCODE_REGI(0, 5) | ENCODE_REGI(1, 16) | ENCODE_EXTENDW(2, 13, 10); // #13
        if (pattern2(wr_or_wsp, wr_extend))             return (uint32_t)0b01101011001000000000000000011111 | ENCODE_REGI(0, 5) | ENCODE_REGI(1, 16) | ENCODE_EXTENDW(2, 13, 10); // #13
        if (pattern2(xr_or_xsp, imm_shift))             return (uint32_t)0b11110001000000000000000000011111 | ENCODE_REGI(0, 5) | ENCODE_IMM12(1, 10) | ENCODE_LSL_SHIFTS(2, 12, 22); // #14
        if (pattern2(wr_or_wsp, imm_shift))             return (uint32_t)0b01110001000000000000000000011111 | ENCODE_REGI(0, 5) | ENCODE_IMM12(1, 10) | ENCODE_LSL_SHIFTS(2, 12, 22); // #14
        unreachable();
    }},
    {"cneg", [](Operand** operands, int operand_length) {
        if (pattern3(xr, xr, cond))                     return (uint32_t)0b11011010100000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(1, 16) | ENCODE_INV_COND(2, 12); // #15
        if (pattern3(wr, wr, cond))                     return (uint32_t)0b01011010100000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(1, 16) | ENCODE_INV_COND(2, 12); // #15
        unreachable();
    }},
    {"crc32b", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011010110000000100000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"crc32cb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011010110000000101000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"crc32ch", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011010110000000101010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"crc32cw", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011010110000000101100000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"crc32cx", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b10011010110000000101110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"crc32h", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011010110000000100010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"crc32w", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011010110000000100100000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"crc32x", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b10011010110000000100110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"csdb", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010001010011111;
        unreachable();
    }},
    {"csel", [](Operand** operands, int operand_length) {
        if (pattern4(xr, xr, xr, cond))                 return (uint32_t)0b10011010100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_COND(3, 12); // #16
        if (pattern4(wr, wr, wr, cond))                 return (uint32_t)0b00011010100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_COND(3, 12); // #16
        unreachable();
    }},
    {"cset", [](Operand** operands, int operand_length) {
        if (pattern2(xr, cond))                         return (uint32_t)0b10011010100111110000011111100000 | ENCODE_REGI(0, 0) | ENCODE_INV_COND(1, 12); // #27
        if (pattern2(wr, cond))                         return (uint32_t)0b00011010100111110000011111100000 | ENCODE_REGI(0, 0) | ENCODE_INV_COND(1, 12); // #27
        unreachable();
    }},
    {"csetm", [](Operand** operands, int operand_length) {
        if (pattern2(xr, cond))                          return (uint32_t)0b11011010100111110000001111100000 | ENCODE_REGI(0, 0) | ENCODE_INV_COND(1, 12); // #27
        if (pattern2(wr, cond))                          return (uint32_t)0b01011010100111110000001111100000 | ENCODE_REGI(0, 0) | ENCODE_INV_COND(1, 12); // #27
        unreachable();
    }},
    {"csinc", [](Operand** operands, int operand_length) {
        if (pattern4(xr, xr, xr, cond))                 return (uint32_t)0b10011010100000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_COND(3, 12); // #16
        if (pattern4(wr, wr, wr, cond))                 return (uint32_t)0b00011010100000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_COND(3, 12); // #16
        unreachable();
    }},
    {"csinv", [](Operand** operands, int operand_length) {
        if (pattern4(xr, xr, xr, cond))                 return (uint32_t)0b11011010100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_COND(3, 12); // #16
        if (pattern4(wr, wr, wr, cond))                 return (uint32_t)0b01011010100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_COND(3, 12); // #16
        unreachable();
    }},
    {"csneg", [](Operand** operands, int operand_length) {
        if (pattern4(xr, xr, xr, cond))                 return (uint32_t)0b11011010100000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_COND(3, 12); // #16
        if (pattern4(wr, wr, wr, cond))                 return (uint32_t)0b01011010100000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_COND(3, 12); // #16
        unreachable();
    }},
    {"dcps1", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010100101000000000000000000001;
        if (pattern1(imm))                              return (uint32_t)0b11010100101000000000000000000001 | ENCODE_IMM16(0, 5); // #17
        unreachable();
    }},
    {"dcps2", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010100101000000000000000000010;
        if (pattern1(imm))                              return (uint32_t)0b11010100101000000000000000000010 | ENCODE_IMM16(0, 5); // #17
        unreachable();
    }},
    {"dcps3", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010100101000000000000000000011;
        if (pattern1(imm))                              return (uint32_t)0b11010100101000000000000000000011 | ENCODE_IMM16(0, 5); // #17
        unreachable();
    }},
    {"drps", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010110101111110000001111100000;
        unreachable();
    }},
    {"eret", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010110100111110000001111100000;
        unreachable();
    }},
    {"eretaa", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010110100111110000101111111111;
        unreachable();
    }},
    {"eretab", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010110100111110000111111111111;
        unreachable();
    }},
    {"esb", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010001000011111;
        unreachable();
    }},
    {"extr", [](Operand** operands, int operand_length) {
        if (pattern4(wr, wr, wr, imm))                  return (uint32_t)0b00010011100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_IMM6(3, 10); // #18
        if (pattern4(wr, wr, wr, imm))                  return (uint32_t)0b10010011110000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_IMM6(3, 10); // #18
        unreachable();
    }},
    {"hint", [](Operand** operands, int operand_length) {
        if (pattern1(imm))                              return (uint32_t)0b11010101000000110010000000011111 | ENCODE_IMM7(0, 5);
        unreachable();
    }},
    {"hlt", [](Operand** operands, int operand_length) {
        if (pattern1(imm))                              return (uint32_t)0b11010100010000000000000000000000 | ENCODE_IMM16(0, 5); // #17
        unreachable();
    }},
    {"hvc", [](Operand** operands, int operand_length) {
        if (pattern1(imm))                              return (uint32_t)0b11010100000000000000000000000010 | ENCODE_IMM16(0, 5); // #17
        unreachable();
    }},
    {"ldadd", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000001000000000000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000001000000000000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldadda", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000101000000000000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000101000000000000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldaddab", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000101000000000000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldaddah", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000101000000000000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldaddal", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000111000000000000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000111000000000000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldaddalb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000111000000000000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldaddalh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000111000000000000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldaddb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000001000000000000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldaddh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000001000000000000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldaddl", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000011000000000000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000011000000000000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldaddlb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000011000000000000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldaddlh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000011000000000000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldapr", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_base))                  return (uint32_t)0b10111000101111111100000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        if (pattern2(xr, mem_op_base))                  return (uint32_t)0b11111000101111111100000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        unreachable();
    }},
    {"ldaprb", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_base))                  return (uint32_t)0b00111000101111111100000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        unreachable();
    }},
    {"ldaprh", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_base))                  return (uint32_t)0b01111000101111111100000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        unreachable();
    }},
    {"ldapur", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b10011001010000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        if (pattern2(xr, mem_op_imm_offset))            return (uint32_t)0b11011001010000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        unreachable();
    }},
    {"ldapurb", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b00011001010000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        unreachable();
    }},
    {"ldapurh", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b01011001010000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        unreachable();
    }},
    {"ldapursb", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b00011001110000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        if (pattern2(xr, mem_op_imm_offset))            return (uint32_t)0b00011001100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        unreachable();
    }},
    {"ldapursh", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b01011001110000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        if (pattern2(xr, mem_op_imm_offset))            return (uint32_t)0b01011001100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        unreachable();
    }},
    {"ldapursw", [](Operand** operands, int operand_length) {
        if (pattern2(xr, mem_op_imm_offset))            return (uint32_t)0b10011001100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        unreachable();
    }},
    {"ldar", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_base))                  return (uint32_t)0b10001000110111111111110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        if (pattern2(xr, mem_op_base))                  return (uint32_t)0b11001000110111111111110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        unreachable();
    }},
    {"ldarb", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_base))                  return (uint32_t)0b00001000110111111111110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        unreachable();
    }},
    {"ldarh", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_base))                  return (uint32_t)0b01001000110111111111110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        unreachable();
    }},
    {"ldaxp", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10001000011111111000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 10) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11001000011111111000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 10) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldaxr", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_base))                  return (uint32_t)0b10001000010111111111110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        if (pattern2(xr, mem_op_base))                  return (uint32_t)0b11001000010111111111110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        unreachable();
    }},
    {"ldaxrb", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_base))                  return (uint32_t)0b00001000010111111111110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        unreachable();
    }},
    {"ldaxrh", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_base))                  return (uint32_t)0b01001000010111111111110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        unreachable();
    }},
    {"ldclr", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000001000000001000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000001000000001000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldclra", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000101000000001000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000101000000001000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldclrab", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000101000000001000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldclrah", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000101000000001000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldclral", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000111000000001000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000111000000001000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldclralb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000111000000001000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldclralh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000111000000001000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldclrb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000001000000001000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldclrh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000001000000001000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldclrl", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000011000000001000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000011000000001000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldclrlb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000011000000001000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldclrlh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000011000000001000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldeor", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000001000000010000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000001000000010000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldeora", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000101000000010000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000101000000010000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldeorab", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000101000000010000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldeorah", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000101000000010000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldeoral", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000111000000010000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000111000000010000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldeoralb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000111000000010000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldeoralh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000111000000010000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldeorb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000001000000010000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldeorh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000001000000010000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldeorl", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000011000000010000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000011000000010000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldeorlb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000011000000010000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldeorlh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000011000000010000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldlar", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_base))                  return (uint32_t)0b10001000110111110111110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        if (pattern2(xr, mem_op_base))                  return (uint32_t)0b11001000110111110111110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        unreachable();
    }},
    {"ldlarb", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_base))                  return (uint32_t)0b00001000110111110111110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        unreachable();
    }},
    {"ldlarh", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_base))                  return (uint32_t)0b01001000110111110111110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        unreachable();
    }},
    {"ldnp", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_imm_offset))        return (uint32_t)0b00101000010000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 10) | ENCODE_MEM_OP_DIV_IMM7_OFFSET(2, 5, 15, 4);
        if (pattern3(xr, xr, mem_op_imm_offset))        return (uint32_t)0b10101000010000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 10) | ENCODE_MEM_OP_DIV_IMM7_OFFSET(2, 5, 15, 8);
        unreachable();
    }},
    {"ldp", [](Operand** operands, int operand_length) {
        if (pattern4(wr, wr, mem_op_base, imm))         return (uint32_t)0b00101000110000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 10) | ENCODE_MEM_OP_BASE(2, 5) | ENCODE_DIV_IMM7(3, 15, 4);
        if (pattern4(xr, xr, mem_op_base, imm))         return (uint32_t)0b10101000110000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 10) | ENCODE_MEM_OP_BASE(2, 5) | ENCODE_DIV_IMM7(3, 15, 8);
        if (pattern3(wr, wr, mem_op_imm_offset_pre))    return (uint32_t)0b00101001110000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 10) | ENCODE_MEM_OP_DIV_IMM7_OFFSET(2, 5, 15, 4);
        if (pattern3(xr, xr, mem_op_imm_offset_pre))    return (uint32_t)0b10101001110000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 10) | ENCODE_MEM_OP_DIV_IMM7_OFFSET(2, 5, 15, 8);
        if (pattern3(wr, wr, mem_op_imm_offset))        return (uint32_t)0b00101001010000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 10) | ENCODE_MEM_OP_DIV_IMM7_OFFSET(2, 5, 15, 4);
        if (pattern3(xr, xr, mem_op_imm_offset))        return (uint32_t)0b10101001010000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 10) | ENCODE_MEM_OP_DIV_IMM7_OFFSET(2, 5, 15, 8);
        unreachable();
    }},
    {"ldpsw", [](Operand** operands, int operand_length) {
        if (pattern4(xr, xr, mem_op_base, imm))         return (uint32_t)0b01101000110000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 10) | ENCODE_MEM_OP_BASE(2, 5) | ENCODE_DIV_IMM7(3, 15, 4);
        if (pattern3(xr, xr, mem_op_imm_offset_pre))    return (uint32_t)0b01101001110000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 10) | ENCODE_MEM_OP_DIV_IMM7_OFFSET(2, 5, 15, 4);
        if (pattern3(xr, xr, mem_op_imm_offset))        return (uint32_t)0b01101001010000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 10) | ENCODE_MEM_OP_DIV_IMM7_OFFSET(2, 5, 15, 4);
        unreachable();
    }},
    {"ldr", [](Operand** operands, int operand_length) {
        // LDR (immediate)
        if (pattern3(wr, mem_op_base, imm))             return (uint32_t)0b10111000010000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5) | ENCODE_IMM9(2, 12);
        if (pattern3(xr, mem_op_base, imm))             return (uint32_t)0b11111000010000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5) | ENCODE_IMM9(2, 12);
        if (pattern2(wr, mem_op_imm_offset_pre))        return (uint32_t)0b10111000010000000000110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        if (pattern2(xr, mem_op_imm_offset_pre))        return (uint32_t)0b11111000010000000000110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b10111001010000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_DIV_IMM12_OFFSET(1, 5, 10, 4);
        if (pattern2(xr, mem_op_imm_offset))            return (uint32_t)0b11111001010000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_DIV_IMM12_OFFSET(1, 5, 10, 8);
        if (pattern2(wr, mem_op_regi_offset))           return (uint32_t)0b10111000011000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_REGI_OFFSET(1, 5, 16, 13, 12, 2);
        if (pattern2(xr, mem_op_regi_offset))           return (uint32_t)0b11111000011000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_REGI_OFFSET(1, 5, 16, 13, 12, 3);
        unreachable();
    }},
    {"ldraa", [](Operand** operands, int operand_length) {
        if (pattern2(xr, mem_op_imm_offset))            return (uint32_t)0b11111000001000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_DIV_IMM9_OFFSET(1, 5, 12, 8);
        if (pattern2(xr, mem_op_imm_offset_pre))        return (uint32_t)0b11111000001000000000110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_DIV_IMM9_OFFSET(1, 5, 12, 8);
        unreachable();
    }},
    {"ldrab", [](Operand** operands, int operand_length) {
        if (pattern2(xr, mem_op_imm_offset))            return (uint32_t)0b11111000101000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_DIV_IMM9_OFFSET(1, 5, 12, 8);
        if (pattern2(xr, mem_op_imm_offset_pre))        return (uint32_t)0b11111000101000000000110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_DIV_IMM9_OFFSET(1, 5, 12, 8);
        unreachable();
    }},
    {"ldrb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, mem_op_base, imm))             return (uint32_t)0b00111000010000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5) | ENCODE_IMM9(2, 12);
        if (pattern2(wr, mem_op_imm_offset_pre))        return (uint32_t)0b00111000010000000000110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b00111001010000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM12_OFFSET(1, 5, 10);
        if (pattern2(wr, mem_op_regi_offset))           return (uint32_t)0b00111000011000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_REGI_OFFSET(1, 5, 16, 13, 12, 1);
        unreachable();
    }},
    {"ldrh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, mem_op_base, imm))             return (uint32_t)0b01111000010000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5) | ENCODE_IMM9(2, 12);
        if (pattern2(wr, mem_op_imm_offset_pre))        return (uint32_t)0b01111000010000000000110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b01111001010000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_DIV_IMM12_OFFSET(1, 5, 10, 2);
        if (pattern2(wr, mem_op_regi_offset))           return (uint32_t)0b01111000011000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_REGI_OFFSET(1, 5, 16, 13, 12, 1);
        unreachable();
    }},
    {"ldrsb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, mem_op_base, imm))             return (uint32_t)0b00111000110000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5) | ENCODE_IMM9(2, 12);
        if (pattern3(xr, mem_op_base, imm))             return (uint32_t)0b00111000100000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5) | ENCODE_IMM9(2, 12);
        if (pattern2(wr, mem_op_imm_offset_pre))        return (uint32_t)0b00111000110000000000110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        if (pattern2(xr, mem_op_imm_offset_pre))        return (uint32_t)0b00111000100000000000110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b00111001110000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM12_OFFSET(1, 5, 10);
        if (pattern2(xr, mem_op_imm_offset))            return (uint32_t)0b00111001100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM12_OFFSET(1, 5, 10);
        if (pattern2(wr, mem_op_regi_offset))           return (uint32_t)0b00111000111000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_REGI_OFFSET(1, 5, 16, 13, 12, 1);
        if (pattern2(xr, mem_op_regi_offset))           return (uint32_t)0b00111000101000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_REGI_OFFSET(1, 5, 16, 13, 12, 1);
        unreachable();
    }},
    {"ldrsh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, mem_op_base, imm))             return (uint32_t)0b01111000110000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5) | ENCODE_IMM9(2, 12);
        if (pattern3(xr, mem_op_base, imm))             return (uint32_t)0b01111000100000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5) | ENCODE_IMM9(2, 12);
        if (pattern2(wr, mem_op_imm_offset_pre))        return (uint32_t)0b01111000110000000000110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        if (pattern2(xr, mem_op_imm_offset_pre))        return (uint32_t)0b01111000100000000000110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b01111001110000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_DIV_IMM12_OFFSET(1, 5, 10, 2);
        if (pattern2(xr, mem_op_imm_offset))            return (uint32_t)0b01111001100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_DIV_IMM12_OFFSET(1, 5, 10, 2);
        if (pattern2(wr, mem_op_regi_offset))           return (uint32_t)0b01111000111000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_REGI_OFFSET(1, 5, 16, 13, 12, 1);
        if (pattern2(xr, mem_op_regi_offset))           return (uint32_t)0b01111000101000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_REGI_OFFSET(1, 5, 16, 13, 12, 1);
        unreachable();
    }},
    {"ldrsw", [](Operand** operands, int operand_length) {
        if (pattern3(xr, mem_op_base, imm))             return (uint32_t)0b10111000100000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5) | ENCODE_IMM9(2, 12);
        if (pattern2(xr, mem_op_imm_offset_pre))        return (uint32_t)0b10111000100000000000110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        if (pattern2(xr, mem_op_imm_offset))            return (uint32_t)0b10111001100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_DIV_IMM9_OFFSET(1, 5, 10, 4);
        if (pattern2(xr, mem_op_regi_offset))           return (uint32_t)0b10111000101000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_REGI_OFFSET(1, 5, 16, 13, 12, 2);
        unreachable();
    }},
    {"ldset", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000001000000011000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000001000000011000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldseta", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000101000000011000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000101000000011000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsetab", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000101000000011000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsetah", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000101000000011000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsetal", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000111000000011000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000111000000011000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsetalb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000111000000011000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsetalh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000111000000011000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsetb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000001000000011000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldseth", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000001000000011000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsetl", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000011000000011000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000011000000011000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsetlb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000011000000011000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsetlh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000011000000011000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsmax", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000001000000100000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000001000000100000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsmaxa", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000101000000100000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000101000000100000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsmaxab", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000101000000100000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsmaxah", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000101000000100000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsmaxal", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000111000000100000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000111000000100000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsmaxalb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000111000000100000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsmaxalh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000111000000100000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsmaxb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000001000000100000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsmaxh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000001000000100000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsmaxl", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000011000000100000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000011000000100000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsmaxlb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000011000000100000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsmaxlh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000011000000100000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsmin", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000001000000101000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000001000000101000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsmina", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000101000000101000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000101000000101000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsminab", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000101000000101000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsminah", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000101000000101000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsminal", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000111000000101000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000111000000101000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsminalb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000111000000101000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsminalh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000111000000101000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsminb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000001000000101000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsminh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000001000000101000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsminl", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000011000000101000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000011000000101000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsminlb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000011000000101000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldsminlh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000011000000101000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldtr", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b10111000010000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        if (pattern2(xr, mem_op_imm_offset))            return (uint32_t)0b11111000010000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        unreachable();
    }},
    {"ldtrb", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b00111000010000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        unreachable();
    }},
    {"ldtrh", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b01111000010000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        unreachable();
    }},
    {"ldtrsb", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b00111000110000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        if (pattern2(xr, mem_op_imm_offset))            return (uint32_t)0b00111000100000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        unreachable();
    }},
    {"ldtrsh", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b01111000110000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        if (pattern2(xr, mem_op_imm_offset))            return (uint32_t)0b01111000100000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        unreachable();
    }},
    {"ldtrsw", [](Operand** operands, int operand_length) {
        if (pattern2(xr, mem_op_imm_offset))            return (uint32_t)0b10111000100000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        unreachable();
    }},
    {"ldumax", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000001000000110000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000001000000110000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldumaxa", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000101000000110000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000101000000110000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldumaxab", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000101000000110000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldumaxah", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000101000000110000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldumaxal", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000111000000110000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000111000000110000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldumaxalb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000111000000110000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldumaxalh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000111000000110000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldumaxb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000001000000110000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldumaxh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000001000000110000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldumaxl", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000011000000110000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000011000000110000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldumaxlb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000011000000110000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldumaxlh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000011000000110000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldumin", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000001000000111000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000001000000111000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldumina", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000101000000111000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000101000000111000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"lduminab", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000101000000111000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"lduminah", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000101000000111000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"lduminal", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000111000000111000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000111000000111000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"lduminalb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000111000000111000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"lduminalh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000111000000111000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"lduminb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000001000000111000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"lduminh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000001000000111000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"lduminl", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b10111000011000000111000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return (uint32_t)0b11111000011000000111000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"lduminlb", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b00111000011000000111000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"lduminlh", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return (uint32_t)0b01111000011000000111000000000000 | ENCODE_REGI(0, 16) | ENCODE_REGI(1, 0) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldur", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b10111000010000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        if (pattern2(xr, mem_op_imm_offset))            return (uint32_t)0b11111000010000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        unreachable();
    }},
    {"ldurb", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b00111000010000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        unreachable();
    }},
    {"ldurh", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b01111000010000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        unreachable();
    }},
    {"ldursb", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b00111000110000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        if (pattern2(xr, mem_op_imm_offset))            return (uint32_t)0b00111000100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        unreachable();
    }},
    {"ldursh", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_imm_offset))            return (uint32_t)0b01111000110000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        if (pattern2(xr, mem_op_imm_offset))            return (uint32_t)0b01111000100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        unreachable();
    }},
    {"ldursw", [](Operand** operands, int operand_length) {
        if (pattern2(xr, mem_op_imm_offset))            return (uint32_t)0b10111000100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_IMM9_OFFSET(1, 5, 12);
        unreachable();
    }},
    {"ldxp", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, mem_op_base))              return  (uint32_t)0b10001000011111110000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 10) | ENCODE_MEM_OP_BASE(2, 5);
        if (pattern3(xr, xr, mem_op_base))              return  (uint32_t)0b11001000011111110000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 10) | ENCODE_MEM_OP_BASE(2, 5);
        unreachable();
    }},
    {"ldxr", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_base))                  return (uint32_t)0b10001000010111110111110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        if (pattern2(xr, mem_op_base))                  return (uint32_t)0b11001000010111110111110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        unreachable();
    }},
    {"ldxrb", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_base))                  return (uint32_t)0b00001000010111110111110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        unreachable();
    }},
    {"ldxrh", [](Operand** operands, int operand_length) {
        if (pattern2(wr, mem_op_base))                  return (uint32_t)0b01001000010111110111110000000000 | ENCODE_REGI(0, 0) | ENCODE_MEM_OP_BASE(1, 5);
        unreachable();
    }},
    {"lsl", [](Operand** operands, int operand_length) {
        // LSL (register)
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011010110000000010000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b10011010110000000010000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        // LSL (immediate)
        if (pattern3(wr, wr, imm))                      return (uint32_t)0b01010011000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_NEG_MOD_IMM6(2, 32, 16) | ENCODE_SUB_IMM6(2, 31, 10); // #19
        if (pattern3(xr, xr, imm))                      return (uint32_t)0b11010011010000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_NEG_MOD_IMM6(2, 64, 16) | ENCODE_SUB_IMM6(2, 63, 10); // #19
        unreachable();
    }},
    {"lslv", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011010110000000010000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b10011010110000000010000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"lsr", [](Operand** operands, int operand_length) {
        // LSR (register)
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011010110000000010010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b10011010110000000010010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        // LSR (immediate)
        if (pattern3(wr, wr, imm))                      return (uint32_t)0b01010011000000000111110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_IMM6(2, 16); // #6
        if (pattern3(xr, xr, imm))                      return (uint32_t)0b11010011010000001111110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_IMM6(2, 16); // #6
        unreachable();
    }},
    {"lsrv", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011010110000000010010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b10011010110000000010010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"madd", [](Operand** operands, int operand_length) {
        if (pattern4(wr, wr, wr, wr))                   return (uint32_t)0b00011011000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_REGI(3, 10); // #20
        if (pattern4(xr, xr, xr, xr))                   return (uint32_t)0b10011011000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_REGI(3, 10); // #20
        unreachable();
    }},
    {"mneg", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011011000000001111110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b10011011000000001111110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"mov", [](Operand** operands, int operand_length) {
        if (pattern2(wr, wr))                           return (uint32_t)0b00101010000000000000001111100000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 16); // #23
        if (pattern2(xr, xr))                           return (uint32_t)0b10101010000000000000001111100000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 16); // #23
        if (pattern2(wr_or_wsp, wr_or_wsp))             return (uint32_t)0b00010001000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        if (pattern2(xr_or_xsp, xr_or_xsp))             return (uint32_t)0b10010001000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        // TODO: MOV (inverted wide immediate)
        if (pattern2(wr, imm))                          return (uint32_t)0b01010010100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_IMM16(1, 5);
        if (pattern2(xr, imm))                          return (uint32_t)0b11010010100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_IMM16(1, 5);
        // TODO: MOV (bitmask immediate)
        unreachable();
    }},
    {"movk", [](Operand** operands, int operand_length) {
        if (pattern2(wr, imm_shift))                    return (uint32_t)0b01110010100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_IMM16(1, 5) | ENCODE_LSL_SHIFTS(2, 16, 21); // #21
        if (pattern2(xr, imm_shift))                    return (uint32_t)0b11110010100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_IMM16(1, 5) | ENCODE_LSL_SHIFTS(2, 16, 21); // #21
        unreachable();
    }},
    {"movn", [](Operand** operands, int operand_length) {
        if (pattern2(wr, imm_shift))                    return (uint32_t)0b00010010100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_IMM16(1, 5) | ENCODE_LSL_SHIFTS(2, 16, 21); // #21
        if (pattern2(xr, imm_shift))                    return (uint32_t)0b10010010100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_IMM16(1, 5) | ENCODE_LSL_SHIFTS(2, 16, 21); // #21
        unreachable();
    }},
    {"movz", [](Operand** operands, int operand_length) {
        if (pattern2(wr, imm_shift))                    return (uint32_t)0b01010010100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_IMM16(1, 5) | ENCODE_LSL_SHIFTS(2, 16, 21); // #21
        if (pattern2(xr, imm_shift))                    return (uint32_t)0b11010010100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_IMM16(1, 5) | ENCODE_LSL_SHIFTS(2, 16, 21); // #21
        unreachable();
    }},
    {"msub", [](Operand** operands, int operand_length) {
        if (pattern4(wr, wr, wr, wr))                   return (uint32_t)0b00011011000000001000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_REGI(3, 10); // #20
        if (pattern4(xr, xr, xr, xr))                   return (uint32_t)0b10011011000000001000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_REGI(3, 10); // #20
        unreachable();
    }},
    {"mul", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011011000000000111110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b10011011000000000111110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"mvn", [](Operand** operands, int operand_length) {
        if (pattern2(wr, wr_shift))                     return (uint32_t)0b00101010001000000000001111100000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 16) | ENCODE_SHIFTS(2, 22, 10); // #22
        if (pattern2(xr, xr_shift))                     return (uint32_t)0b10101010001000000000001111100000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 16) | ENCODE_SHIFTS(2, 22, 10); // #22
        unreachable();
    }},
    {"neg", [](Operand** operands, int operand_length) {
        if (pattern2(wr, wr_shift))                     return (uint32_t)0b01001011000000000000001111100000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 16) | ENCODE_SHIFTS(2, 22, 10); // #22
        if (pattern2(xr, xr_shift))                     return (uint32_t)0b11001011000000000000001111100000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 16) | ENCODE_SHIFTS(2, 22, 10); // #22
        unreachable();
    }},
    {"negs", [](Operand** operands, int operand_length) {
        if (pattern2(wr, wr_shift))                     return (uint32_t)0b01101011000000000000001111100000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 16) | ENCODE_SHIFTS(2, 22, 10); // #22
        if (pattern2(xr, xr_shift))                     return (uint32_t)0b11101011000000000000001111100000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 16) | ENCODE_SHIFTS(2, 22, 10); // #22
        unreachable();
    }},
    {"ngc", [](Operand** operands, int operand_length) {
        if (pattern2(wr, wr))                           return (uint32_t)0b01011010000000000000001111100000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 16); // #23
        if (pattern2(xr, xr))                           return (uint32_t)0b11011010000000000000001111100000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 16); // #23
        unreachable();
    }},
    {"ngcs", [](Operand** operands, int operand_length) {
        if (pattern2(wr, wr))                           return (uint32_t)0b01111010000000000000001111100000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 16); // #23
        if (pattern2(xr, xr))                           return (uint32_t)0b11111010000000000000001111100000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 16); // #23
        unreachable();
    }},
    {"nop", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010000000011111;
        unreachable();
    }},
    {"orn", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, wr_shift))                 return (uint32_t)0b00101010001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_SHIFTS(3, 22, 10); // #2
        if (pattern3(xr, xr, xr_shift))                 return (uint32_t)0b10101010001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_SHIFTS(3, 22, 10); // #2
        unreachable();
    }},
    {"orr", [](Operand** operands, int operand_length) {
        // orr (shifted register)
        if (pattern3(wr, wr, wr_shift))                 return (uint32_t)0b00101010000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_SHIFTS(3, 22, 10); // #2
        if (pattern3(xr, xr, xr_shift))                 return (uint32_t)0b10101010000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_SHIFTS(3, 22, 10); // #2
        // TODO: orr (immidiate)
        unreachable();
    }},
    {"pacda", [](Operand** operands, int operand_length) {
        if (pattern2(xr, xr_or_xsp))                    return (uint32_t)0b11011010110000010000100000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"pacdb", [](Operand** operands, int operand_length) {
        if (pattern2(xr, xr_or_xsp))                    return (uint32_t)0b11011010110000010000110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"pacdza", [](Operand** operands, int operand_length) {
        if (pattern1(xr))                               return (uint32_t)0b11011010110000010010101111100000 | ENCODE_REGI(0, 0); // #7
        unreachable();
    }},
    {"pacdzb", [](Operand** operands, int operand_length) {
        if (pattern1(xr))                               return (uint32_t)0b11011010110000010010111111100000 | ENCODE_REGI(0, 0); // #7
        unreachable();
    }},
    {"pacga", [](Operand** operands, int operand_length) {
        if (pattern3(xr, xr, xr_or_xsp))                return (uint32_t)0b10011010110000000011000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"pacia", [](Operand** operands, int operand_length) {
        if (pattern2(xr, xr_or_xsp))                    return (uint32_t)0b11011010110000010000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"pacia1716", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010000100011111;
        unreachable();
    }},
    {"paciasp", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010001100111111;
        unreachable();
    }},
    {"paciaz", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010001100011111;
        unreachable();
    }},
    {"pacib", [](Operand** operands, int operand_length) {
        if (pattern2(xr, xr_or_xsp))                    return (uint32_t)0b11011010110000010000010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"pacib1716", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010000101011111;
        unreachable();
    }},
    {"pacibsp", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010001101111111;
        unreachable();
    }},
    {"pacibz", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010001101011111;
        unreachable();
    }},
    {"paciza", [](Operand** operands, int operand_length) {
        if (pattern1(xr))                               return (uint32_t)0b11011010110000010010001111100000 | ENCODE_REGI(0, 0); // #7
        unreachable();
    }},
    {"pacizb", [](Operand** operands, int operand_length) {
        if (pattern1(xr))                               return (uint32_t)0b11011010110000010010011111100000 | ENCODE_REGI(0, 0); // #7
        unreachable();
    }},
    {"pssbb", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110011010010011111;
        unreachable();
    }},
    {"rbit", [](Operand** operands, int operand_length) {
        if (pattern2(wr, wr))                           return (uint32_t)0b01011010110000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        if (pattern2(xr, xr))                           return (uint32_t)0b11011010110000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"ret", [](Operand** operands, int operand_length) {
        if (pattern1(xr))                               return (uint32_t)0b11010110010111110000000000000000 | ENCODE_REGI(0, 5); // #27
        if (operand_length == 0)                        return (uint32_t)0b11010110010111110000001111000000;
        unreachable();
    }},
    {"retaa", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010110010111110000101111111111;
        unreachable();
    }},
    {"retab", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010110010111110000111111111111;
        unreachable();
    }},
    {"rev", [](Operand** operands, int operand_length) {
        if (pattern2(wr, wr))                           return (uint32_t)0b01011010110000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        if (pattern2(xr, xr))                           return (uint32_t)0b11011010110000000000110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"rev16", [](Operand** operands, int operand_length) {
        if (pattern2(wr, wr))                           return (uint32_t)0b01011010110000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        if (pattern2(xr, xr))                           return (uint32_t)0b11011010110000000000010000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"rev32", [](Operand** operands, int operand_length) {
        if (pattern2(xr, xr))                           return (uint32_t)0b11011010110000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"rev64", [](Operand** operands, int operand_length) {
        if (pattern2(xr, xr))                           return (uint32_t)0b11011010110000000000110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"rmif", [](Operand** operands, int operand_length) {
        if (pattern3(xr, imm, imm))                     return (uint32_t)0b10111010000000000000010000000000 | ENCODE_REGI(0, 5) | ENCODE_IMM6(1, 15) | ENCODE_IMM4(2, 0); // #26
        unreachable();
    }},
    {"ror", [](Operand** operands, int operand_length) {
        // ROR (immediate)
        if (pattern3(wr, wr, imm))                      return (uint32_t)0b00010011100000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(1, 16) | ENCODE_IMM6(2, 10); // #25
        if (pattern3(xr, xr, imm))                      return (uint32_t)0b10010011110000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(1, 16) | ENCODE_IMM6(2, 10); // #25
        // ROR (register)
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011010110000000010110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b10011010110000000010110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"rorv", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011010110000000010110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b10011010110000000010110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"sb", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110011000011111111;
        unreachable();
    }},
    {"sbc", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b01011010000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b11011010000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"sbcs", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b01111010000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b11111010000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"sdiv", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011010110000000000110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b10011010110000000000110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"setf16", [](Operand** operands, int operand_length) {
        if (pattern1(wr))                               return (uint32_t)0b00111010000000000100100000001101 | ENCODE_REGI(0, 5); // #27
        unreachable();
    }},
    {"setf8", [](Operand** operands, int operand_length) {
        if (pattern1(wr))                               return (uint32_t)0b00111010000000000000100000001101 | ENCODE_REGI(0, 5); // #27
        unreachable();
    }},
    {"sev", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010000010011111;
        unreachable();
    }},
    {"sevl", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010000010111111;
        unreachable();
    }},
    {"smaddl", [](Operand** operands, int operand_length) {
        if (pattern4(xr, wr, wr, xr))                   return (uint32_t)0b10011011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_REGI(3, 10); // #20
        unreachable();
    }},
    {"smc", [](Operand** operands, int operand_length) {
        if (pattern1(imm))                              return (uint32_t)0b11010100000000000000000000000011 | ENCODE_IMM16(0, 5); // #17
        unreachable();
    }},
    {"smnegl", [](Operand** operands, int operand_length) {
        if (pattern3(xr, wr, wr))                       return (uint32_t)0b10011011001000001111110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"smsubl", [](Operand** operands, int operand_length) {
        if (pattern4(xr, wr, wr, xr))                   return (uint32_t)0b10011011001000001000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_REGI(3, 10); // #20
        unreachable();
    }},
    {"smulh", [](Operand** operands, int operand_length) {
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b10011011010000000111110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"smull", [](Operand** operands, int operand_length) {
        if (pattern3(xr, wr, wr))                       return (uint32_t)0b10011011001000000111110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"ssbb", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110011000010011111;
        unreachable();
    }},
    {"sub", [](Operand** operands, int operand_length) {
        // SUB (shifted register)
        if (pattern3(xr, xr, xr_shift))                 return (uint32_t)0b11001011000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_SHIFTS(3, 22, 10); // #2
        if (pattern3(wr, wr, wr_shift))                 return (uint32_t)0b01001011000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_SHIFTS(3, 22, 10); // #2
        // SUB (immediate)
        if (pattern3(xr_or_xsp, xr_or_xsp, imm_shift))  return (uint32_t)0b11010001000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_IMM12(2, 10) | ENCODE_LSL_SHIFTS(3, 12, 22); // #3
        if (pattern3(wr_or_wsp, wr_or_wsp, imm_shift))  return (uint32_t)0b01010001000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_IMM12(2, 10) | ENCODE_LSL_SHIFTS(3, 12, 22); // #3
        // SUB (extened register)
        if (pattern3(wr_or_wsp, wr_or_wsp, wr_extend))  return (uint32_t)0b01001011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_EXTENDW(3, 13, 10); // #4
        if (pattern3(xr_or_xsp, xr_or_xsp, xr_extend))  return (uint32_t)0b11001011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_EXTENDX(3, 13, 10); // #5
        if (pattern3(xr_or_xsp, xr_or_xsp, wr_extend))  return (uint32_t)0b11001011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_EXTENDW(3, 13, 10); // #4
        unreachable();
    }},
    {"subs", [](Operand** operands, int operand_length) {
        // ADD (shifted register)
        if (pattern3(xr, xr, xr_shift))                 return (uint32_t)0b11101011000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_SHIFTS(3, 22, 10); // #2
        if (pattern3(wr, wr, wr_shift))                 return (uint32_t)0b01101011000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_SHIFTS(3, 22, 10); // #2
        // ADD (immediate)
        if (pattern3(xr, xr_or_xsp, imm_shift))         return (uint32_t)0b11110001000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_IMM12(2, 10) | ENCODE_LSL_SHIFTS(3, 12, 22); // #3
        if (pattern3(wr, wr_or_wsp, imm_shift))         return (uint32_t)0b01110001000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_IMM12(2, 10) | ENCODE_LSL_SHIFTS(3, 12, 22); // #3
        // ADD (extened register)
        if (pattern3(wr, wr_or_wsp, wr_extend))         return (uint32_t)0b01101011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_EXTENDW(3, 13, 10); // #4
        if (pattern3(xr, xr_or_xsp, xr_extend))         return (uint32_t)0b11101011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_EXTENDX(3, 13, 10); // #5
        if (pattern3(xr, xr_or_xsp, wr_extend))         return (uint32_t)0b11101011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_EXTENDX(3, 13, 10); // #5
        unreachable();
    }},
    {"svc", [](Operand** operands, int operand_length) {
        if (pattern1(imm))                              return (uint32_t)0b11010100000000000000000000000001 | ENCODE_IMM16(0, 5); // #17
        unreachable();
    }},
    {"sxtb", [](Operand** operands, int operand_length) {
        if (pattern2(wr, wr))                           return (uint32_t)0b00010011000000000001110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        if (pattern2(xr, wr))                           return (uint32_t)0b10010011010000000001110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"sxth", [](Operand** operands, int operand_length) {
        if (pattern2(wr, wr))                           return (uint32_t)0b00010011000000000011110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        if (pattern2(xr, wr))                           return (uint32_t)0b10010011010000000011110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"sxtw", [](Operand** operands, int operand_length) {
        if (pattern2(xr, wr))                           return (uint32_t)0b10010011010000000111110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"udf", [](Operand** operands, int operand_length) {
        if (pattern1(imm))                              return (uint32_t)0b00000000000000000000000000000000 | ENCODE_IMM16(0, 0);
        unreachable();
    }},
    {"udiv", [](Operand** operands, int operand_length) {
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011010110000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b10011010110000000000100000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"umaddl", [](Operand** operands, int operand_length) {
        if (pattern4(xr, wr, wr, xr))                   return (uint32_t)0b10011011101000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_REGI(3, 10); // #20
        unreachable();
    }},
    {"umnegl", [](Operand** operands, int operand_length) {
        if (pattern3(xr, wr, wr))                       return (uint32_t)0b10011011101000001111110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"umsubl", [](Operand** operands, int operand_length) {
        if (pattern4(xr, wr, wr, xr))                   return (uint32_t)0b10011011101000001000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_REGI(3, 10); // #20
        unreachable();
    }},
    {"umulh", [](Operand** operands, int operand_length) {
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b10011011110000000111110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"umull", [](Operand** operands, int operand_length) {
        if (pattern3(xr, wr, wr))                       return (uint32_t)0b10011011101000000111110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        unreachable();
    }},
    {"uxtb", [](Operand** operands, int operand_length) {
        if (pattern2(wr, wr))                           return (uint32_t)0b01010011000000000001110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"uxth", [](Operand** operands, int operand_length) {
        if (pattern2(wr, wr))                           return (uint32_t)0b01010011000000000011110000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        unreachable();
    }},
    {"wfe", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010000001011111;
        unreachable();
    }},
    {"wfi", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010000001111111;
        unreachable();
    }},
    {"xpacd", [](Operand** operands, int operand_length) {
        if (pattern1(xr))                               return (uint32_t)0b11011010110000010100011111100000 | ENCODE_REGI(0, 0); // #7
        unreachable();
    }},
    {"xpaci", [](Operand** operands, int operand_length) {
        if (pattern1(xr))                               return (uint32_t)0b11011010110000010100001111100000 | ENCODE_REGI(0, 0); // #7
        unreachable();
    }},
    {"xpaclri", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010000011111111;
        unreachable();
    }},
    {"yield", [](Operand** operands, int operand_length) {
        if (operand_length == 0)                        return (uint32_t)0b11010101000000110010000000111111;
        unreachable();
    }},
};

// --------------------------------------------------------------------
// --------------------------------------------------------------------
// Elf file Generator
// --------------------------------------------------------------------
// --------------------------------------------------------------------

struct Elf64_Ehdr {
	uint8_t    e_ident[16];
	uint16_t   e_type;     
	uint16_t   e_machine;  
	uint32_t   e_version;
	uintptr_t  e_entry;
	uintptr_t  e_phoff;
	uintptr_t  e_shoff;
	uint32_t   e_flags;
	uint16_t   e_ehsize;   
	uint16_t   e_phentsize;
	uint16_t   e_phnum;    
	uint16_t   e_shentsize;
	uint16_t   e_shnum;    
	uint16_t   e_shstrndx; 
};

struct Elf64_Sym {
	uint32_t   st_name;
	uint8_t    st_info;
	uint8_t    st_other;
	uint16_t   st_shndx;
	uintptr_t  st_value;
	uint64_t   st_size;
};

struct Elf64_Shdr {
	uint32_t   sh_name;
	uint32_t   sh_type;
	uintptr_t  sh_flags;
	uintptr_t  sh_addr;
	uintptr_t  sh_offset;
	uintptr_t  sh_size;
	uint32_t   sh_link;
	uint32_t   sh_info;
	uintptr_t  sh_addralign;
	uintptr_t  sh_entsize;
};

struct Elf64_Phdr {
	uint32_t  ph_type;
	uint32_t  ph_flags;
	uint64_t  ph_off;
	uint64_t  ph_vaddr;
	uint64_t  ph_paddr;
	uint64_t  ph_filesz;
	uint64_t  ph_memsz;
	uint64_t  ph_align;
};

#define EM_AARCH64 0xb7

#define STB_LOCAL 0
#define STB_GLOBAL 1

#define STT_NOTYPE 0
#define STT_SECTION 3

#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3

#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4

std::vector<uint32_t> code;
uint8_t rodata[16] = {};

void generate_elf() {
    uint8_t strtab[16] = {
        0x0,
        '_', 's', 't', 'a', 'r', 't', '\0',
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

	int null_nameofs = 0;

    int start_nameofs = null_nameofs + strlen("") + 1;

    Elf64_Sym symtab[4] = {
        Elf64_Sym { // null
            st_name: (uint32_t)null_nameofs,
            st_info: ((STB_LOCAL << 4) + (STT_NOTYPE & 0xf)),
        },
        Elf64_Sym { // .rodata
            st_name: 0,
            st_info: ((STB_LOCAL << 4) + (STT_SECTION & 0xf)),
			st_shndx: 2,
        },
        Elf64_Sym { // .text
            st_name: 0,
            st_info: ((STB_LOCAL << 4) + (STT_SECTION & 0xf)),
			st_shndx: 1,
        },
        Elf64_Sym { // _start
			st_name: (uint32_t)start_nameofs,
			st_info: ((STB_GLOBAL << 4) + (STT_NOTYPE & 0xf)),
			st_shndx: 1,
			st_value: 0,
		},
    };

	uint8_t shstrtab[64] = {
		'\0',
		'.', 't', 'e', 'x', 't', '\0',
		'.', 'r', 'o', 'd', 'a', 't', 'a', '\0',
		'.', 's', 't', 'r', 't', 'a', 'b', '\0',
		'.', 's', 'y', 'm', 't', 'a', 'b', '\0',
		'.', 's', 'h', 's', 't', 'r', 't', 'a', 'b', '\0',
		// padding
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

	int text_nameofs = null_nameofs + strlen("") + 1;
	int rodata_nameofs = text_nameofs + strlen(".text") + 1;
	int strtab_nameofs = rodata_nameofs + strlen(".rodata") + 1;
	int symtab_nameofs = strtab_nameofs + strlen(".strtab") + 1;
	int shstrtab_nameofs = symtab_nameofs + strlen(".symtab") + 1;

	int code_ofs = sizeof(Elf64_Ehdr);
	int code_size = code.size() * sizeof(uint32_t);

	int rodata_ofs = code_ofs + code_size;
	int rodata_size = sizeof(rodata);

	int strtab_ofs = rodata_ofs + rodata_size;
	int strtab_size = sizeof(strtab);

	int symtab_ofs = strtab_ofs + strtab_size;
	int symtab_size = sizeof(symtab);

	int shstrtab_ofs = symtab_ofs + symtab_size;
	int shstrtab_size = sizeof(shstrtab);

	int sectionheader_ofs = shstrtab_ofs + shstrtab_size;

	Elf64_Shdr section_headers[6] = {
		// NULL
		Elf64_Shdr {
			sh_name: (uint32_t)null_nameofs,
			sh_type: SHT_NULL,
		},
		// .text
		Elf64_Shdr {
			sh_name: (uint32_t)text_nameofs,
			sh_type: SHT_PROGBITS,
			sh_flags: (uintptr_t)(SHF_ALLOC | SHF_EXECINSTR),
			sh_addr: 0,
			sh_offset: (uintptr_t)code_ofs,
			sh_size: (uintptr_t)code_size,
			sh_link: 0,
			sh_info: 0,
			sh_addralign: (uintptr_t)1,
			sh_entsize: 0,
		},
		// .rodata
		Elf64_Shdr {
			sh_name: (uint32_t)rodata_nameofs,
			sh_type: SHT_PROGBITS,
			sh_flags: (uintptr_t)SHF_ALLOC,
			sh_addr: 0,
			sh_offset: (uintptr_t)rodata_ofs,
			sh_size: (uintptr_t)rodata_size,
			sh_link: 0,
			sh_info: 0,
			sh_addralign: (uintptr_t)1,
			sh_entsize: 0,
		},
		// .strtab
		Elf64_Shdr {
			sh_name: (uint32_t)strtab_nameofs,
			sh_type: SHT_STRTAB,
			sh_flags: (uintptr_t)0,
			sh_addr: 0,
			sh_offset: (uintptr_t)strtab_ofs,
			sh_size: (uintptr_t)strtab_size,
			sh_link: 0,
			sh_info: 0,
			sh_addralign: (uintptr_t)1,
			sh_entsize: 0,
		},
		// .symtab
		Elf64_Shdr {
			sh_name: (uint32_t)symtab_nameofs,
			sh_type: SHT_SYMTAB,
			sh_flags: (uintptr_t)0,
			sh_addr: 0,
			sh_offset: (uintptr_t)symtab_ofs,
			sh_size: (uintptr_t)symtab_size,
			sh_link: 3, // section number of .strtab
			sh_info: 3, // Number of local symbols
			sh_addralign: (uintptr_t)8,
			sh_entsize: (uintptr_t)sizeof(Elf64_Sym),
		},
		// .shstrtab
		Elf64_Shdr {
			sh_name: (uint32_t)shstrtab_nameofs,
			sh_type: SHT_STRTAB,
			sh_flags: 0,
			sh_addr: 0,
			sh_offset: (uintptr_t)shstrtab_ofs,
			sh_size: (uintptr_t)shstrtab_size,
			sh_link: 0,
			sh_info: 0,
			sh_addralign: (uintptr_t)1,
			sh_entsize: 0,
		},
    };

    // https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst#elf-header

	Elf64_Ehdr ehdr = Elf64_Ehdr {
		e_ident: {
			0x7f, 0x45, 0x4c, 0x46, // Magic number ' ELF' in ascii format
			0x02, // 2 = 64-bit
			0x01, // 1 = little endian
			0x01,
			0x00,
			0x00,
			0x00,
			0x00,
			0x00,
			0x00,
			0x00,
			0x00,
			0x00,
        },
		e_type: 1, // 1 = realocatable
		e_machine: EM_AARCH64, 
		e_version: 1,
		e_entry: 0,
		e_phoff: 0,
		e_shoff: (uintptr_t)sectionheader_ofs,
		e_flags: 0x0,
		e_ehsize: sizeof(Elf64_Ehdr),
		e_phentsize: sizeof(Elf64_Phdr),
		e_phnum: 0,
		e_shentsize: sizeof(Elf64_Shdr),
		e_shnum: sizeof(section_headers) / sizeof(Elf64_Shdr),
		e_shstrndx: sizeof(section_headers) / sizeof(Elf64_Shdr) - 1,
	};

    // elf header
    write(1, reinterpret_cast<char*>(&ehdr), sizeof(Elf64_Ehdr));

    // .text
    write(1, code.data(), code.size() * sizeof(uint32_t));

    // .rodata
    write(1, reinterpret_cast<char*>(&rodata), sizeof(rodata));

    // .strtab
    write(1, reinterpret_cast<char*>(&strtab), sizeof(strtab));

    // .symtab
    for (int i = 0; i < sizeof(symtab)/sizeof(Elf64_Sym); i++) {
        write(1, reinterpret_cast<char*>(&(symtab[i])), sizeof(Elf64_Sym));
    }

    // .shstrtab
    write(1, reinterpret_cast<char*>(&shstrtab), sizeof(shstrtab));

    // section headers
    for (int i = 0; i < sizeof(section_headers)/sizeof(Elf64_Shdr); i++) {
        write(1, reinterpret_cast<char*>(&(section_headers[i])), sizeof(Elf64_Shdr));
    }
}

struct Parser {
    int idx;
    int line;
    std::string file_path;
    std::string program;
};

Parser* new_parser(std::string file_path, std::string program) {
    Parser *p = new Parser;
    p->program = program;
    p->file_path = file_path;
    p->idx = 0;
    p->line = 1;
    return p;
}

[[noreturn]] void syntax_error(Parser* p, std::string msg) {
    std::cerr << "\u001b[1m" << p->file_path << ":" << p->line << ": \x1b[91merror:\x1b[0m\u001b[1m " << msg << "\033[0m" << std::endl;
    exit(1);
}

inline bool at_eof(Parser *p) {
    return p->idx >= p->program.size();
}

inline void parser_advance(Parser *p, int n) {
    p->idx += n;
}

void skip_white_space(Parser *p) {
    while (!at_eof(p)) {
        char c = p->program[p->idx];
        if (c == ' ' || c == '\t') {
            parser_advance(p, 1);
        } else {
            break;
        }
    }
}

std::string read_ident(Parser* p) {
    skip_white_space(p);

    int start = p->idx;
    while (std::isalpha(p->program[p->idx]) || std::isdigit(p->program[p->idx])) {
        parser_advance(p, 1);
    }

    std::string str = p->program.substr(start, p->idx-start);

    skip_white_space(p);

    return str;
}

int read_number(Parser* p) {
    skip_white_space(p);

    int imm_val = 0;
    while (std::isdigit(p->program[p->idx])) {
        imm_val = imm_val * 10 + p->program[p->idx] - '0';
        parser_advance(p, 1);
    }

    skip_white_space(p);

    return imm_val;
}

inline Operand* parse_register(Parser* p) {

    std::string ident = read_ident(p);
    if (registers.find(ident) != registers.end()) {
        skip_white_space(p);
        return registers[ident];
    }

    syntax_error(p, "expected register operand");
}

inline Operand* parse_extend(Parser* p) {
    std::string ident = read_ident(p);

    if (extend_types.find(ident) != extend_types.end()) {
        int extend_amout = 0;
        if (p->program[p->idx] == '#') {
            parser_advance(p, 1);
            extend_amout = read_number(p);
        }
        return new_extend(extend_types[ident], extend_amout);
    }

    syntax_error(p, "expected extend operand");
}

inline Operand* parse_shift(Parser* p) {
    std::string ident = read_ident(p);

    if (shift_types.find(ident) != shift_types.end()) {
        int shift_amout = 0;
        if (p->program[p->idx] == '#') {
            parser_advance(p, 1);
            shift_amout = read_number(p);
        }
        return new_shift(shift_types[ident], shift_amout);
    }

    syntax_error(p, "expected shift operand");
}

/*
    MEM_OP_BASE            -> [ register ]

    MEM_OP_IMM_OFFSET      -> [ register, immediate ]

    MEM_OP_IMM_OFFSET_PRE  -> [ register, immediate ]!

    MEM_OP_REGI_OFFSET     -> [ register, register ]
    MEM_OP_REGI_OFFSET     -> [ register, register, LSL #0 ]
*/

Operand* parse_operand(Parser* p) {
    skip_white_space(p);

    if (p->program[p->idx] == '#') {
        parser_advance(p, 1);

        int imm_val = read_number(p);

        return new_imm(imm_val);
    }

    if (p->program[p->idx] == '[') {
        Operand* mem_op = new Operand;
        parser_advance(p, 1); // skip `[`
        mem_op->base_register = parse_register(p);
        switch (p->program[p->idx]) {
            case ']':
                mem_op->kind = MEM_OP_BASE;
                break;
            case ',':
                parser_advance(p, 1); // skip `,`
                skip_white_space(p);
                if (p->program[p->idx] == '#') { // imm offset
                    mem_op->kind = MEM_OP_IMM_OFFSET;
                    parser_advance(p, 1); // skip `#`
                    int imm_offset_val = read_number(p);
                    mem_op->offset = new_imm(imm_offset_val);
                } else { // register offset
                    mem_op->kind = MEM_OP_REGI_OFFSET;
                    mem_op->offset = parse_register(p);
                    if (p->program[p->idx] == ',') {
                        parser_advance(p, 1); // skip `,`
                        mem_op->extend_offset = parse_extend(p);
                    } else {
                        mem_op->extend_offset = new_extend((ExtendType)0, 0);
                    }
                }
                break;
        }
        if (p->program[p->idx] != ']') {
            syntax_error(p, "expected `]`");
        }
        parser_advance(p, 1); // skip `]`
        skip_white_space(p);
        if (p->program[p->idx] == '!') {
            switch (mem_op->kind) {
                case MEM_OP_BASE:
                    mem_op->kind = MEM_OP_BASE_PRE;
                    break;
                case MEM_OP_IMM_OFFSET:
                    mem_op->kind = MEM_OP_IMM_OFFSET_PRE;
                    break;
                default:
                    syntax_error(p, "expected `!`");
            }
            parser_advance(p, 1); // skip `!`
        }
        return mem_op;
    }

    std::string ident = read_ident(p);

    if (registers.find(ident) != registers.end()) {
        return registers[ident];
    }

    if (shift_types.find(ident) != shift_types.end()) {
        int shift_amout = 0;
        if (p->program[p->idx] == '#') {
            parser_advance(p, 1);
            shift_amout = read_number(p);
        }
        return new_shift(shift_types[ident], shift_amout);
    }

    if (extend_types.find(ident) != extend_types.end()) {
        int extend_amout = 0;
        if (p->program[p->idx] == '#') {
            parser_advance(p, 1);
            extend_amout = read_number(p);
        }
        return new_extend(extend_types[ident], extend_amout);
    }

    syntax_error(p, "unkown operand found");
}

void parse_program(Parser* p) {
    while (!at_eof(p)) {
        skip_white_space(p);

        if (p->program[p->idx] == '\n') {
            parser_advance(p, 1);
            p->line++;
            continue;
        }

        std::string instr_name = read_ident(p);

        Operand** operands = new Operand*[5];

        int operand_length = 0;
        while (true) {
            if (operand_length > 4) {
                break;
            }

            operands[operand_length++] = parse_operand(p);
            skip_white_space(p);
            if (p->program[p->idx] == ',') {
                parser_advance(p, 1);
            } else {
                break;
            }
        }

        char c = p->program[p->idx];

        if (c == '\n' || c == '\0') {
            parser_advance(p, 1);
            p->line++;
        } else {
            syntax_error(p, "expected a new line or EOF");
        }

        code.push_back(instr_table[instr_name](operands, operand_length));
    }
}

std::string read_file(char* file_path) {
    std::ifstream input_file(file_path);

    if (!input_file.is_open()) {
        std::cerr << "error: failed to open file: " << file_path << std::endl;
        exit(1);
    }

    std::ostringstream file_content_stream;
    file_content_stream << input_file.rdbuf();

    input_file.close();

    std::string file_content = file_content_stream.str();

    return file_content;
}

int main(int argc, char** argv) {
    if (argc < 1) {
        std::cerr << "error: no input file" << std::endl;
        return 1;
    }

    char* file_path = argv[1];

    std::string file_content = read_file(file_path);

    code.reserve(5000000);

    Parser* p = new_parser(file_path, file_content);
    parse_program(p);

    generate_elf();
    return 0;
}

