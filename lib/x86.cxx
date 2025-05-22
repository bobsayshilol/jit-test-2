#include "internal.h"
#include <cstring>
#include <array>

// Only eax,ecx,edx are caller-saved.
// registers: eax,ecx,edx,ebx
// edi - base data ptr / ExecutionEnvironment
// esi - temporary

namespace jitlib
{
    static_assert(jitlib::kNumRegisters == 4, "Native code will need changing");
    static_assert(std::is_same_v<Value, uint8_t>, "Native code will need changing");
    static_assert(offsetof(ExecutionEnvironment, mem) == 0, "ExecutionEnvironment and data ptr aren't interchangeable");
    static_assert(std::is_same_v<NativeRegister, std::uint32_t>, "Registers are 32bit");

    namespace
    {
        uint8_t encode_reg(Register reg)
        {
            std::array<uint8_t, 4> const regs{
                0x0, // eax
                0x1, // ecx
                0x2, // edx
                0x3, // ebx
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
                    // mov %edi,%esi
                    0x89, 0xfe,
                    // add reg,%esi
                    0x01, uint8_t(0xc6 | (regB << 3)),
                    // mov (%esi),reg
                    0x8b, uint8_t(0x06 | (regA << 3)),
                };
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            else if (op.type == OpType::Store)
            {
                uint8_t const ins[]{
                    // mov %edi,%esi
                    0x89, 0xfe,
                    // add reg,%esi
                    0x01, uint8_t(0xc6 | (regA << 3)),
                    // mov reg,(%esi)
                    0x89, uint8_t(0x06 | (regB << 3)),
                };
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
                uint8_t const ins[]{uint8_t(0xb8 | reg), op.imm, 0x00, 0x00, 0x00};
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            else if (op.type == OpType::SetReg)
            {
                auto regB = encode_reg(op.regB);
                uint8_t const ins[]{0x89, uint8_t(0xc0 | (regB << 3) | reg)};
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
                    0x81, uint8_t(0xc0 | reg), op.imm, 0x00, 0x00, 0x00,
                    // and 0xff reg
                    0x81, uint8_t(0xe0 | reg), 0xff, 0x00, 0x00, 0x00,
                };
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
                    0x01, uint8_t(0xc0 | (regB << 3) | reg),
                    // and 0xff reg
                    0x81, uint8_t(0xe0 | reg), 0xff, 0x00, 0x00, 0x00,
                };
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
                    0xf7, uint8_t(0xd8 | reg),
                    // and 0xff reg
                    0x81, uint8_t(0xe0 | reg), 0xff, 0x00, 0x00, 0x00,
                };
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
                    0x85, uint8_t(0xc0 | (reg << 3) | reg),
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
                0x83, 0xec, 0x3a, // sub $0x3a,%esp

                // Store current register values to a |NativeState| on the stack
                0x89, 0x44, 0x24, 0x18, // mov %eax,0x18(%esp)
                0x89, 0x4c, 0x24, 0x1c, // mov %ecx,0x1c(%esp)
                0x89, 0x54, 0x24, 0x20, // mov %edx,0x20(%esp)
                0x89, 0x5c, 0x24, 0x24, // mov %ebx,0x24(%esp)
                0x89, 0x7c, 0x24, 0x28, // mov %edi,0x28(%esp)

                // Setup first arg
                0x8d, 0x4c, 0x24, 0x18, // lea 0x18(%esp),%ecx

                // Setup second arg
                0xb8, 0x00, 0x00, 0x00, 0x00, // mov callout,%eax
            };
            uint8_t const call_thunk[]{
                // Push args for thunk
                0x50, // push %eax (func)
                0x51, // push %ecx (state)

                // Setup call
                0xb8, 0x00, 0x00, 0x00, 0x00, // mov thunk,%eax
            };
            uint8_t const leave[]{
                // Call into the helper thunk
                0xff, 0xd0, // call *%eax

                // Pop args
                0x58, // pop %eax
                0x58, // pop %eax

                // Read off each register from |NativeState|
                0x8b, 0x44, 0x24, 0x18, // mov 0x18(%esp),%eax
                0x8b, 0x4c, 0x24, 0x1c, // mov 0x1c(%esp),%ecx
                0x8b, 0x54, 0x24, 0x20, // mov 0x20(%esp),%edx
                0x8b, 0x5c, 0x24, 0x24, // mov 0x24(%esp),%ebx
                0x8b, 0x7c, 0x24, 0x28, // mov 0x28(%esp),%edi

                // Restore stack
                0x83, 0xc4, 0x3a, // add $0x3a,%esp
            };
            if (buffer != nullptr)
            {
                buffer = std::copy(std::begin(enter), std::end(enter), buffer);
                // Patch callout address
                memcpy(buffer - 4, &op.func, 4);

                buffer = std::copy(std::begin(call_thunk), std::end(call_thunk), buffer);
                // Patch thunk address
                auto *thunk_ptr = static_cast<void (*)(NativeState *, CallOutFunc)>(helper_thunk);
                memcpy(buffer - 4, &thunk_ptr, 4);

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
                // Save registers that we will trample.
                0x53, // push %ebx
                0x57, // push %edi
                0x56, // push %esi

                // Give us some stack
                0x83, 0xec, 0x20, // sub $0x20,%esp

                // Load |NativeState| address from caller's stack (0x20 + 3*push + 4)
                0x8b, 0x74, 0x24, 0x30, // mov 0x30(%esp),%esi

                // Read off each register from |NativeState|
                0x8b, 0x06,             // mov (%esi),%eax
                0x8b, 0x4e, 0x04,       // mov 0x4(%esi),%ecx
                0x8b, 0x56, 0x08,       // mov 0x8(%esi),%edx
                0x8b, 0x5e, 0x0c,       // mov 0xc(%esi),%ebx
                0x8b, 0x7e, 0x10,       // mov 0x10(%esi),%edi

                // Call into the rest of the code
                0xe8, 0x00, 0x00, 0x00, 0x00, // call <addr>
            };
            uint8_t const leave[]{
                // Load |NativeState| address from caller's stack (0x20 + 3*push + 4)
                0x8b, 0x74, 0x24, 0x30, // mov 0x30(%esp),%esi

                // Store new register values back to |NativeState|
                0x89, 0x06,       // mov %eax,(%esi)
                0x89, 0x4e, 0x04, // mov %ecx,0x4(%esi)
                0x89, 0x56, 0x08, // mov %edx,0x8(%esi)
                0x89, 0x5e, 0x0c, // mov %ebx,0xc(%esi)
                0x89, 0x7e, 0x10, // mov %edi,0x10(%esi)

                // Return
                0x83, 0xc4, 0x20, // add $0x20,%esp
                0x5e,             // pop %esi
                0x5f,             // pop %edi
                0x5b,             // pop %ebx
                0xc3,             // ret

                // Saftey guard
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
