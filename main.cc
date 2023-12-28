#include <cstdint>
#include <vector>
#include <string>
#include <iostream>
#include <unordered_map>
#include <functional>
#include <fstream>
#include <unistd.h>
#include <cstring>

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
};

struct Operand {
    OperandKind kind;

    // register
    int regi_bits;

    int imm;

    // shift|extend|cond val
    int val;
    // shift|extend amount
    int amount;
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

std::unordered_map<std::string, uint32_t> regi_bits = {
    // 64bit registers
    {"x0", 0}, {"x1", 1}, {"x2", 2}, {"x3", 3}, {"x4", 4}, {"x5", 5}, {"x6", 6}, {"x7", 7},
    {"x8", 8}, {"x9", 9}, {"x10", 10}, {"x11", 11}, {"x12", 12}, {"x13", 13}, {"x14", 14}, {"x15", 15},
    {"x16", 16}, {"x17", 17}, {"x18", 18}, {"x19", 19}, {"x20", 20}, {"x21", 21}, {"x22", 22}, {"x23", 23},
    {"x24", 24}, {"x25", 25}, {"x26", 26}, {"x27", 27}, {"x28", 28}, {"x29", 29}, {"x30", 30}, {"sp", 31},

    // 32bit registers
    {"w0", 0}, {"w1", 1}, {"w2", 2}, {"w3", 3}, {"w4", 4}, {"w5", 5}, {"w6", 6}, {"w7", 7}, 
    {"w8", 8}, {"w9", 9}, {"w10", 10}, {"w11", 11}, {"w12", 12}, {"w13", 13}, {"w14", 14}, {"w15", 15}, 
    {"w16", 16}, {"w17", 17}, {"w18", 18}, {"w19", 19}, {"w20", 20}, {"w21", 21}, {"w22", 22}, {"w23", 23}, 
    {"w24", 24}, {"w25", 25}, {"w26", 26}, {"w27", 27}, {"w28", 28}, {"w29", 29}, {"w30", 30}, {"wsp", 31},
};

Operand *new_regi(OperandKind kind, std::string regi_name) {
    Operand *op = new Operand;
    op->kind = kind;
    op->regi_bits = regi_bits[regi_name];

    return op;
}

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

#define is_xr(operands, i)        (operands[i]->kind == XR)
#define is_wr(operands, i)        (operands[i]->kind == WR)
#define is_xr_or_xsp(operands, i) (operands[i]->kind == XR || operands[i]->kind == XSP)
#define is_wr_or_wsp(operands, i) (operands[i]->kind == WR || operands[i]->kind == WSP)
#define is_shift(operands, i)     (operands[i]->kind == SHIFT)
#define is_extend(operands, i)    (operands[i]->kind == EXTEND)
#define is_imm(operands, i)       (operands[i]->kind == IMM)
#define is_cond(operands, i)      (operands[i]->kind == COND)

#define next_op_shift(operands, i)  ((operand_length > i+1) ? is_shift(operands, i+1) : true)
#define next_op_extend(operands, i) ((operand_length > i+1) ? is_extend(operands, i+1) : true)

#define is_xr_shift(operands, i)   (is_xr(operands, i) && next_op_shift(operands, i))
#define is_wr_shift(operands, i)   (is_wr(operands, i) && next_op_shift(operands, i))
#define is_imm_shift(operands, i)  (is_imm(operands, i) && next_op_shift(operands, i))
#define is_xr_extend(operands, i)  (is_xr(operands, i) && next_op_extend(operands, i))
#define is_wr_extend(operands, i)  (is_wr(operands, i) && next_op_extend(operands, i))

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

#define ENCODE_REGI(operand_idx, b)               (operands[operand_idx]->regi_bits << b)
#define ENCODE_SHIFTS(operand_idx, b1, b2)        ((operand_length > operand_idx) ? (operands[operand_idx]->val << b1) | (operands[operand_idx]->amount << b2) : 0)

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
        // ADD (shifted register)
        if (pattern3(xr, xr, xr_shift))                 return (uint32_t)0b10001011000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_SHIFTS(3, 22, 10); // #2
        if (pattern3(wr, wr, wr_shift))                 return (uint32_t)0b00001011000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_SHIFTS(3, 22, 10); // #2
        //// ADD (immediate)
        if (pattern3(xr_or_xsp, xr_or_xsp, imm_shift))  return (uint32_t)0b10010001000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_IMM12(2, 10) | ENCODE_LSL_SHIFTS(3, 12, 22); // #3
        if (pattern3(wr_or_wsp, wr_or_wsp, imm_shift))  return (uint32_t)0b00010001000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_IMM12(2, 10) | ENCODE_LSL_SHIFTS(3, 12, 22); // #3
        //// ADD (extened register)
        if (pattern3(wr_or_wsp, wr_or_wsp, wr_extend))  return (uint32_t)0b00001011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_EXTENDW(3, 13, 10); // #4
        if (pattern3(xr_or_xsp, xr_or_xsp, xr_extend))  return (uint32_t)0b10001011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_EXTENDX(3, 13, 10); // #5
        if (pattern3(xr_or_xsp, xr_or_xsp, wr_extend))  return (uint32_t)0b10001011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_EXTENDW(3, 13, 10); // #4
        unreachable();
    }},
    {"adds", [](Operand** operands, int operand_length) {
        // ADD (shifted register)
        if (pattern3(xr, xr, xr_shift))                 return (uint32_t)0b10101011000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_SHIFTS(3, 22, 10); // #2
        if (pattern3(wr, wr, wr_shift))                 return (uint32_t)0b00101011000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_SHIFTS(3, 22, 10); // #2
        // ADD (immediate)
        if (pattern3(xr, xr_or_xsp, imm_shift))         return (uint32_t)0b10110001000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_IMM12(2, 10) | ENCODE_LSL_SHIFTS(3, 12, 22); // #3
        if (pattern3(wr, wr_or_wsp, imm_shift))         return (uint32_t)0b00110001000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_IMM12(2, 10) | ENCODE_LSL_SHIFTS(3, 12, 22); // #3
        // ADD (extened register)
        if (pattern3(wr, wr_or_wsp, wr_extend))         return (uint32_t)0b00101011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_EXTENDW(3, 13, 10); // #4
        if (pattern3(xr, xr_or_xsp, xr_extend))         return (uint32_t)0b10101011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_EXTENDX(3, 13, 10); // #5
        if (pattern3(xr, xr_or_xsp, wr_extend))         return (uint32_t)0b10101011001000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16) | ENCODE_EXTENDX(3, 13, 10); // #5
        unreachable();
    }},
    {"asr", [](Operand** operands, int operand_length) {
        // (register)
        if (pattern3(xr, xr, xr))                       return (uint32_t)0b10011010110000000010100000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        if (pattern3(wr, wr, wr))                       return (uint32_t)0b00011010110000000010100000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5) | ENCODE_REGI(2, 16); // #1
        // (immediate)
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
        // MOV (register)
        if (pattern2(wr, wr))                           return (uint32_t)0b00101010000000000000001111100000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 16); // #23
        if (pattern2(xr, xr))                           return (uint32_t)0b10101010000000000000001111100000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 16); // #23
        // MOV (to/from SP)
        if (pattern2(wr_or_wsp, wr_or_wsp))             return (uint32_t)0b00010001000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        if (pattern2(xr_or_xsp, xr_or_xsp))             return (uint32_t)0b10010001000000000000000000000000 | ENCODE_REGI(0, 0) | ENCODE_REGI(1, 5); // #8
        // MOV (inverted wide immediate)
        // MOV (wide immediate)
        // MOV (bitmask immediate)
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

// TODO
// std::unordered_map<std::string, std::vector<uint8_t>> sections;
// current_section = sections[section_name];
// current_section.push_back(...);

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

int main() {
    code.reserve(10000000);

    for (int i = 0; i < 10; i++) {
        Operand** operands = new Operand*[5];
        std::string instr = "add";
        operands[0] = (new_regi(XR, "x1"));
        operands[1] = (new_regi(XR, "x2"));
        operands[2] = (new_imm(5));

        code.push_back(instr_table[instr](operands, 3));
    }

    generate_elf();
    return 0;
}

