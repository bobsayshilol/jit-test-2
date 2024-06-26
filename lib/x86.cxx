#include "internal.h"
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
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
#if 1 // TODO
                (void)reg;
                uint8_t const ins[]{0x90}; // nop
#else
                uint8_t const ins[]{
                    // add reg <imm>
                    0x48, 0x83, uint8_t(0xc0 | reg), op.imm,
                    // and 0xff reg
                    0x48, 0x81, uint8_t(0xe0 | reg), 0xff, 0x00, 0x00, 0x00};
#endif
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            else if (op.type == OpType::AddReg)
            {
#if 1 // TODO
                uint8_t const ins[]{0x90}; // nop
#else
                auto regB = encode_reg(op.regB);
                uint8_t const ins[]{
                    // add reg reg
                    0x48, 0x01, uint8_t(0xc0 | (regB << 3) | reg),
                    // and 0xff reg
                    0x48, 0x81, uint8_t(0xe0 | reg), 0xff, 0x00, 0x00, 0x00};
#endif
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            else if (op.type == OpType::Negate)
            {
#if 1 // TODO
                uint8_t const ins[]{0x90}; // nop
#else
                uint8_t const ins[]{
                    // neg reg
                    0x48, 0xf7, uint8_t(0xd8 | reg),
                    // and 0xff reg
                    0x48, 0x81, uint8_t(0xe0 | reg), 0xff, 0x00, 0x00, 0x00};
#endif
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
            return 0; // TODO

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
            return 0; // TODO

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

                // Store current register values to a |NativeState| on the sack
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
                0x83, 0xec, 0x20, // sub $0x20,%rsp

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

        uint8_t *allocate(std::size_t &size)
        {
            long pagesize = sysconf(_SC_PAGE_SIZE);
            ASSERT(pagesize > 0);
            size = ((size - 1) | (pagesize - 1)) + 1;
            auto *const code = (uint8_t *)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
            ASSERT(code != MAP_FAILED);
            return code;
        }

        void finalise(uint8_t *buffer, std::size_t used, std::size_t length)
        {
            // INT3 out any leftover space
            memset(buffer + used, 0xcc, length - used);

            // Make it executable
            int err = mprotect(buffer, length, PROT_READ | PROT_EXEC);
            ASSERT(err == 0);
        }

        void deallocate(void *buffer, std::size_t length)
        {
            munmap(buffer, length);
        }
    }
}
