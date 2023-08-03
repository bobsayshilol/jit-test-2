#ifndef JIT_LIB_H
#define JIT_LIB_H

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>
#include <string>

namespace jitlib
{
    enum class OpType
    {
        Nop = 0,
        Return,
        Load,       // reg = *addr
        Store,      // *addr = reg
        SetReg,     // reg = reg
        SetImm,     // reg = imm
        AddReg,     // reg += reg
        AddImm,     // reg += imm
        Negate,     // reg = -reg
        Jump,       // sp = label
        JumpIfZero, // if (reg == 0) sp = label
        Call,       // sp = label
        Label,
    };

    using Register = uint8_t;
    using Value = uint8_t;
    struct Label
    {
        template <std::size_t N>
        Label(const char (&str)[N])
        {
            static_assert(N <= 16);
            std::copy(std::begin(str), std::end(str), data.begin());
        }
        std::array<char, 16> data{};

        auto operator<=>(Label const &) const = default;
    };

    static inline constexpr std::size_t kNumRegisters = 4;

    struct Op
    {
        OpType type;
        Register regA;
        union
        {
            Register regB;
            Value imm;
            Label label;
        };

        static Op make_Return() { return {OpType::Return, 0, {}}; }
        static Op make_Nop() { return {OpType::Nop, 0, {}}; }
        static Op make_Load(Register reg, Value addr) { return {OpType::Load, reg, {.imm = addr}}; }
        static Op make_Store(Value addr, Register reg) { return {OpType::Store, reg, {.imm = addr}}; }
        static Op make_SetReg(Register reg, Register regR) { return {OpType::SetReg, reg, {.regB = regR}}; }
        static Op make_SetImm(Register reg, Value imm) { return {OpType::SetImm, reg, {.imm = imm}}; }
        static Op make_AddReg(Register regL, Register regR) { return {OpType::AddReg, regL, {.regB = regR}}; }
        static Op make_AddImm(Register reg, Value imm) { return {OpType::AddImm, reg, {.imm = imm}}; }
        static Op make_Negate(Register reg) { return {OpType::Negate, reg, {}}; }
        static Op make_Jump(Label label) { return {OpType::Jump, 0, {.label = label}}; }
        static Op make_JumpIfZero(Register reg, Label label) { return {OpType::JumpIfZero, reg, {.label = label}}; }
        static Op make_Call(Label label) { return {OpType::Call, 0, {.label = label}}; }
        static Op make_Label(Label label) { return {OpType::Label, 0, {.label = label}}; }
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

        void run(ExecutionEnvironment &env) const;
    };
    CompiledCode compile(Ops const &ops);
}

template <>
struct std::hash<jitlib::Label>
{
    std::size_t operator()(jitlib::Label const &label) const noexcept
    {
        return std::hash<std::string_view>{}(label.data.data());
    }
};

#endif
