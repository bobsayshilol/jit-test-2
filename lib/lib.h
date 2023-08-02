#ifndef JIT_LIB_H
#define JIT_LIB_H

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace jitlib
{
    enum class OpType
    {
        Nop = 0,
        Return,
        Load,       // reg = *addr
        Store,      // *addr = reg
        Set,        // reg = imm
        AddReg,     // reg += reg
        AddImm,     // reg += imm
        Negate,     // reg = -reg
        Jump,       // sp += imm
        JumpIfZero, // if (reg == 0) sp += imm
        Call,       // sp = imm
    };

    using Register = uint8_t;
    using Value = uint8_t;

    static inline constexpr std::size_t kNumRegisters = 4;

    struct Op
    {
        OpType type;
        Register regA;
        union
        {
            Register regB;
            Value imm;
            Value addr;
        };

        static Op make_Return() { return {OpType::Return, 0, {}}; }
        static Op make_Nop() { return {OpType::Nop, 0, {}}; }
        static Op make_Load(Register reg, Value addr) { return {OpType::Load, reg, {addr}}; }
        static Op make_Store(Value addr, Register reg) { return {OpType::Store, reg, {addr}}; }
        static Op make_Set(Register reg, Value imm) { return {OpType::Set, reg, {imm}}; }
        static Op make_AddReg(Register regL, Register regR) { return {OpType::AddReg, regL, {regR}}; }
        static Op make_AddImm(Register reg, Value imm) { return {OpType::AddImm, reg, {imm}}; }
        static Op make_Negate(Register reg) { return {OpType::Negate, reg, {}}; }
        static Op make_Jump(Value offset) { return {OpType::Jump, 0, {offset}}; }
        static Op make_JumpIfZero(Register reg, Value offset) { return {OpType::JumpIfZero, reg, {offset}}; }
        static Op make_Call(Value addr) { return {OpType::Call, 0, {addr}}; }
    };

    using Memory = std::array<Value, 256>;
    using Ops = std::array<Op, 256>;

    struct ExecutionEnvironment
    {
        Memory mem;
        Value regs[kNumRegisters];
        Value pc;
        struct
        {
            bool cmp : 1;
        } flags;
    };

    void run(Ops const &ops, ExecutionEnvironment &env);

    class CompiledCode
    {
        void *m_code;
        std::size_t m_size;

        CompiledCode(const CompiledCode &) = delete;
        CompiledCode &operator=(const CompiledCode &) = delete;

    public:
        CompiledCode();
        CompiledCode(void *buffer, std::size_t size); // takes ownership
        ~CompiledCode();

        CompiledCode(CompiledCode &&);
        CompiledCode &operator=(CompiledCode &&);

        void run(ExecutionEnvironment &env);
    };
    CompiledCode compile(Ops const &ops);
}

#endif
