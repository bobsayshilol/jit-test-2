#ifndef JIT_OPS_H
#define JIT_OPS_H

#include <jitlib/types.h>

namespace jitlib
{
    enum class OpType
    {
        Nop = 0,
        Return,
        Load,       // regA = *regB
        Store,      // *regA = regB
        SetReg,     // reg = reg
        SetImm,     // reg = imm
        AddReg,     // reg += reg
        AddImm,     // reg += imm
        Negate,     // reg = -reg
        Jump,       // sp = label
        JumpIfZero, // if (reg == 0) sp = label
        Call,       // sp = label
        Label,      // label:
        CallOut,    // call func
    };

    using CallOutFunc = void (*)(ExecutionEnvironment &);

    struct Op
    {
        OpType type;
        Register regA;
        union
        {
            Register regB;
            Value imm;
            Label label;
            CallOutFunc func;
        };

        static Op make_Return() { return {OpType::Return, 0, {}}; }
        static Op make_Nop() { return {OpType::Nop, 0, {}}; }
        static Op make_Load(Register reg, Register regR) { return {OpType::Load, reg, {.regB = regR}}; }
        static Op make_Store(Register reg, Register regR) { return {OpType::Store, reg, {.regB = regR}}; }
        static Op make_SetReg(Register reg, Register regR) { return {OpType::SetReg, reg, {.regB = regR}}; }
        static Op make_SetImm(Register reg, Value imm) { return {OpType::SetImm, reg, {.imm = imm}}; }
        static Op make_AddReg(Register regL, Register regR) { return {OpType::AddReg, regL, {.regB = regR}}; }
        static Op make_AddImm(Register reg, Value imm) { return {OpType::AddImm, reg, {.imm = imm}}; }
        static Op make_Negate(Register reg) { return {OpType::Negate, reg, {}}; }
        static Op make_Jump(Label label) { return {OpType::Jump, 0, {.label = label}}; }
        static Op make_JumpIfZero(Register reg, Label label) { return {OpType::JumpIfZero, reg, {.label = label}}; }
        static Op make_Call(Label label) { return {OpType::Call, 0, {.label = label}}; }
        static Op make_Label(Label label) { return {OpType::Label, 0, {.label = label}}; }
        static Op make_CallOut(CallOutFunc func) { return {OpType::CallOut, 0, {.func = func}}; }
    };
}

#endif
