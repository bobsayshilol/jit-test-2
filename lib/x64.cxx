#include "internal.h"
#include <cstring>
#include <array>

// We only use registers that are caller-saved so that we don't have to
// restore anything on exit.
// registers: rax,rcx,rdx,rsi
// r10 - base data ptr / ExecutionEnvironment
// r11 - temporary

namespace jitlib
{
    static_assert(jitlib::kNumRegisters == 4, "Native code will need changing");
    static_assert(std::is_same_v<Value, uint8_t>, "Native code will need changing");
    static_assert(offsetof(ExecutionEnvironment, mem) == 0, "ExecutionEnvironment and data ptr aren't interchangeable");
    static_assert(std::is_same_v<NativeRegister, std::uint64_t>, "Registers are 64bit");

    namespace
    {
        uint8_t encode_reg(Register reg)
        {
            std::array<uint8_t, 4> const regs{
                0x0, // rax
                0x1, // rcx
                0x2, // rdx
                0x6, // rsi
            };
            return regs.at(reg);
        }

        std::size_t handle_nop(Op const &, uint8_t *)
        {
            // Don't need to lower "do nothing"
            return 0;
        }

        std::size_t handle_load_store(Op const &op, uint8_t *buffer)
        {
            auto regA = encode_reg(op.regA);
            auto regB = encode_reg(op.regB);
            if (op.type == OpType::Load)
            {
                uint8_t const ins[]{
                    // mov %r10,%r11
                    0x4d, 0x89, 0xd3,
                    // add reg,%r11
                    0x49, 0x01, uint8_t(0xc3 | (regB << 3)),
                    // movzbl (%r11),reg
                    0x41, 0x0f, 0xb6, uint8_t(0x03 | (regA << 3))};
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            else if (op.type == OpType::Store)
            {
                uint8_t const ins[]{
                    // mov %r10,%r11
                    0x4d, 0x89, 0xd3,
                    // add reg,%r11
                    0x49, 0x01, uint8_t(0xc3 | (regA << 3)),
                    // mov reg,(%r11)
                    0x66, 0x41, 0x89, uint8_t(0x03 | (regB << 3))};
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            throw std::logic_error("Unknown mem op");
        }

        std::size_t handle_set(Op const &op, uint8_t *buffer)
        {
            auto reg = encode_reg(op.regA);
            if (op.type == OpType::SetImm)
            {
                uint8_t const ins[]{0x48, 0xc7, uint8_t(0xc0 | reg), op.imm, 0x00, 0x00, 0x00};
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            else if (op.type == OpType::SetReg)
            {
                auto regB = encode_reg(op.regB);
                uint8_t const ins[]{0x48, 0x89, uint8_t(0xc0 | (regB << 3) | reg)};
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            throw std::logic_error("Unknown set op");
        }

        std::size_t handle_arithmetic(Op const &op, uint8_t *buffer)
        {
            auto reg = encode_reg(op.regA);
            if (op.type == OpType::AddImm)
            {
                uint8_t const ins[]{
                    // add reg <imm>
                    0x48, 0x83, uint8_t(0xc0 | reg), op.imm,
                    // and 0xff reg
                    0x48, 0x81, uint8_t(0xe0 | reg), 0xff, 0x00, 0x00, 0x00};
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            else if (op.type == OpType::AddReg)
            {
                auto regB = encode_reg(op.regB);
                uint8_t const ins[]{
                    // add reg reg
                    0x48, 0x01, uint8_t(0xc0 | (regB << 3) | reg),
                    // and 0xff reg
                    0x48, 0x81, uint8_t(0xe0 | reg), 0xff, 0x00, 0x00, 0x00};
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            else if (op.type == OpType::Negate)
            {
                uint8_t const ins[]{
                    // neg reg
                    0x48, 0xf7, uint8_t(0xd8 | reg),
                    // and 0xff reg
                    0x48, 0x81, uint8_t(0xe0 | reg), 0xff, 0x00, 0x00, 0x00};
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            throw std::logic_error("Unknown arithmetic op");
        }

        std::size_t handle_jump(Op const &op, uint8_t const *buffer_base, uint8_t *buffer, LabelToOffsetMap const *label_to_offset)
        {
            auto patch_addr = [&](auto &ins)
            {
                // Patch call address relative to after execution of this instruction
                auto it = label_to_offset->find(op.label);
                if (it == label_to_offset->end())
                {
                    throw std::logic_error("Unknown label: " + std::string(op.label.data.data()));
                }
                int32_t const relative_address = static_cast<int32_t>(it->second - (buffer + std::size(ins) - buffer_base));
                memcpy(std::end(ins) - 4, &relative_address, 4);
            };

            if (op.type == OpType::Jump)
            {
                uint8_t ins[]{0xe9, 0x00, 0x00, 0x00, 0x00};
                if (buffer != nullptr)
                {
                    patch_addr(ins);
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            else if (op.type == OpType::JumpIfZero)
            {
                auto reg = encode_reg(op.regA);
                uint8_t ins[]{
                    // test reg reg
                    0x48, 0x85, uint8_t(0xc0 | (reg << 3) | reg),
                    // jz <addr>
                    0x0f, 0x84, 0x00, 0x00, 0x00, 0x00};
                if (buffer != nullptr)
                {
                    patch_addr(ins);
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            else if (op.type == OpType::Call)
            {
                uint8_t ins[]{0xe8, 0x00, 0x00, 0x00, 0x00};
                if (buffer != nullptr)
                {
                    patch_addr(ins);
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            throw std::logic_error("Unknown jump op");
        }

        std::size_t handle_return(Op const &, uint8_t *buffer)
        {
            if (buffer != nullptr)
            {
                *buffer = 0xc3;
            }
            return 1;
        }

        std::size_t handle_callout(Op const &op, uint8_t *buffer)
        {
            auto helper_thunk = [](NativeState *state, CallOutFunc func)
            {
                auto *env = static_cast<ExecutionEnvironment *>(state->data);
                std::copy(std::begin(state->regs), std::end(state->regs), std::begin(env->regs));
                func(*env);
                std::copy(std::begin(env->regs), std::end(env->regs), std::begin(state->regs));
            };

            uint8_t const enter[]{
                // Give us some stack
                0x48, 0x83, 0xec, 0x38, // sub $0x38,%rsp

                // Store current register values to a |NativeState| on the stack
                0x48, 0x89, 0x04, 0x24,       // mov %rax,(%rsp)
                0x48, 0x89, 0x4c, 0x24, 0x08, // mov %rcx,0x8(%rsp)
                0x48, 0x89, 0x54, 0x24, 0x10, // mov %rdx,0x10(%rsp)
                0x48, 0x89, 0x74, 0x24, 0x18, // mov %rsi,0x18(%rsp)
                0x4c, 0x89, 0x54, 0x24, 0x20, // mov %r10,0x20(%rsp)

                // Setup first arg
                0x48, 0x89, 0xe7, // mov %rsp,%rdi

                // Setup second arg
                0x48, 0xbe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 // mov callout,%rsi
            };
            uint8_t const call_thunk[]{
                // Setup call
                0x48, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov thunk,%rax
            };
            uint8_t const leave[]{
                // Call into the helper thunk
                // rdi = NativeState
                // rsi = CallOutFunc
                // rax = helper_thunk
                0xff, 0xd0, // call *%rax

                // Read off each register from |NativeState|
                0x48, 0x8b, 0x04, 0x24,       // mov (%rsp),%rax
                0x48, 0x8b, 0x4c, 0x24, 0x08, // mov 0x8(%rsp),%rcx
                0x48, 0x8b, 0x54, 0x24, 0x10, // mov 0x10(%rsp),%rdx
                0x48, 0x8b, 0x74, 0x24, 0x18, // mov 0x18(%rsp),%rsi
                0x4c, 0x8b, 0x54, 0x24, 0x20, // mov 0x20(%rsp),%r10

                // Restore stack
                0x48, 0x83, 0xc4, 0x38, // add $0x38,%rsp
            };
            if (buffer != nullptr)
            {
                buffer = std::copy(std::begin(enter), std::end(enter), buffer);
                // Patch callout address
                memcpy(buffer - 8, &op.func, 8);

                buffer = std::copy(std::begin(call_thunk), std::end(call_thunk), buffer);
                // Patch thunk address
                auto *thunk_ptr = static_cast<void (*)(NativeState *, CallOutFunc)>(helper_thunk);
                memcpy(buffer - 8, &thunk_ptr, 8);

                buffer = std::copy(std::begin(leave), std::end(leave), buffer);
            }
            return std::size(enter) + std::size(call_thunk) + std::size(leave);
        }
    }

    namespace native
    {
        std::size_t preamble(uint8_t *buffer)
        {
            uint8_t const enter[]{
                // Give us some stack
                0x48, 0x83, 0xec, 0x38, // sub $0x38,%rsp

                // Store address of |NativeState| to the stack
                0x48, 0x89, 0x7c, 0x24, 0x08, // mov %rdi,0x8(%rsp)

                // Read off each register from |NativeState|
                0x48, 0x8b, 0x07,       // mov (%rdi),%rax
                0x48, 0x8b, 0x4f, 0x08, // mov 0x8(%rdi),%rcx
                0x48, 0x8b, 0x57, 0x10, // mov 0x10(%rdi),%rdx
                0x48, 0x8b, 0x77, 0x18, // mov 0x18(%rdi),%rsi
                0x4c, 0x8b, 0x57, 0x20, // mov 0x20(%rdi),%r10

                // Call into the rest of the code
                0xe8, 0x00, 0x00, 0x00, 0x00, // call <addr>
            };
            uint8_t const leave[]{
                // Load |NativeState| address from stack
                0x48, 0x8b, 0x7c, 0x24, 0x08, // mov 0x8(%rsp),%rdi

                // Store new register values back to |NativeState|
                0x48, 0x89, 0x07,       // mov %rax,(%rdi)
                0x48, 0x89, 0x4f, 0x08, // mov %rcx,0x8(%rdi)
                0x48, 0x89, 0x57, 0x10, // mov %rdx,0x10(%rdi)
                0x48, 0x89, 0x77, 0x18, // mov %rsi,0x18(%rdi)
                0x4c, 0x89, 0x57, 0x20, // mov %r10,0x20(%rdi)

                // Return
                0x48, 0x83, 0xc4, 0x38, // add $0x38,%rsp
                0xc3,                   // ret

                // Safety guard
                0xcc, // int3
                0xcc, // int3
                0xcc, // int3
            };
            if (buffer != nullptr)
            {
                buffer = std::copy(std::begin(enter), std::end(enter), buffer);
                // Patch call address
                uint32_t const relative_address = std::size(leave);
                memcpy(buffer - 4, &relative_address, 4);
                buffer = std::copy(std::begin(leave), std::end(leave), buffer);
            }
            return std::size(enter) + std::size(leave);
        }

        std::size_t encode(Op const &op, uint8_t const *buffer_base, uint8_t *buffer, LabelToOffsetMap const *label_to_offset)
        {
            switch (op.type)
            {
            case OpType::Nop:
                return handle_nop(op, buffer);

            case OpType::Load:
            case OpType::Store:
                return handle_load_store(op, buffer);

            case OpType::SetReg:
            case OpType::SetImm:
                return handle_set(op, buffer);

            case OpType::AddReg:
            case OpType::AddImm:
            case OpType::Negate:
                return handle_arithmetic(op, buffer);

            case OpType::Jump:
            case OpType::JumpIfZero:
            case OpType::Call:
                return handle_jump(op, buffer_base, buffer, label_to_offset);

            case OpType::Return:
                return handle_return(op, buffer);

            case OpType::Label:
                return 0;

            case OpType::CallOut:
                return handle_callout(op, buffer);
            }
            return 0;
        }
    }
}
