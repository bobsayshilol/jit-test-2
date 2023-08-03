#include "lib.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <array>

// We only use registers that are caller-saved so that we don't have to
// restore anything on exit.
// registers: rax,rcx,rdx,rsi
// r10 - base data ptr
// r11 - temporary

namespace jitlib
{
    namespace
    {
        struct NativeState
        {
            std::uint64_t regs[4];
            void *data;
        };
        using NativeFunction = void (*)(NativeState *);

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

        std::size_t handle_nop(Op const &, uint8_t *)
        {
            // Don't need to lower "do nothing"
            return 0;
        }

        std::size_t handle_load_store(Op const &op, uint8_t *buffer)
        {
            (void)op;
            (void)buffer;
            return 0;
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
            return 0;
        }

        std::size_t handle_arithmetic(Op const &op, uint8_t *buffer)
        {
            auto reg = encode_reg(op.regA);
            if (op.type == OpType::AddImm)
            {
                uint8_t const ins[]{0x48, 0x83, uint8_t(0xc0 | reg), op.imm};
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            else if (op.type == OpType::AddReg)
            {
                auto regB = encode_reg(op.regB);
                uint8_t const ins[]{0x48, 0x01, uint8_t(0xc0 | (regB << 3) | reg)};
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            return 0;
        }

        std::size_t handle_jump(Op const &op, uint8_t *buffer)
        {
            (void)op;
            (void)buffer;
            return 0;
        }

        std::size_t handle_call(Op const &op, uint8_t *buffer)
        {
            // call *%rdx
            (void)op;
            (void)buffer;
            return 0;
        }

        std::size_t handle_return(Op const &, uint8_t *buffer)
        {
            if (buffer != nullptr)
            {
                *buffer = 0xc3;
            }
            return 1;
        }

        std::size_t encode(Op const &op, uint8_t *buffer)
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
                return handle_jump(op, buffer);

            case OpType::Call:
                return handle_call(op, buffer);

            case OpType::Return:
                return handle_return(op, buffer);

            case OpType::Label:
                // TODO
                return 0;
            }
            return 0;
        }
    }

    CompiledCode::CompiledCode() : m_code{}, m_size{} {}
    CompiledCode::CompiledCode(void *code, std::size_t size) : m_code{code}, m_size{size} {}
    CompiledCode::~CompiledCode()
    {
        if (m_code != nullptr)
        {
            munmap(m_code, m_size);
        }
    }
    CompiledCode::CompiledCode(CompiledCode &&o) : CompiledCode() { operator=(std::move(o)); }
    CompiledCode &CompiledCode::operator=(CompiledCode &&o)
    {
        std::swap(m_code, o.m_code);
        std::swap(m_size, o.m_size);
        return *this;
    }

    void CompiledCode::run(ExecutionEnvironment &env) const
    {
        // Setup registers
        NativeState state{};
        std::copy(std::begin(env.regs), std::end(env.regs), std::begin(state.regs));
        state.data = env.mem.data();

        // Call the function
        reinterpret_cast<NativeFunction>(m_code)(&state);

        // Copy back registers
        std::copy(std::begin(state.regs), std::end(state.regs), std::begin(env.regs));
    }

    CompiledCode compile(Ops const &ops)
    {
        // Pass over the code to get the total size
        std::size_t size = preamble(nullptr);
        for (Op const &op : ops)
        {
            size += encode(op, nullptr);
        }

        // Allocate enough space for it
        long pagesize = sysconf(_SC_PAGE_SIZE);
        assert(pagesize > 0);
        size = ((size - 1) | (pagesize - 1)) + 1;
        auto *const code = (uint8_t *)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
        assert(code != MAP_FAILED);

        // Copy it over
        std::size_t offset = preamble(code);
        for (Op const &op : ops)
        {
            offset += encode(op, code + offset);
        }
        assert(offset <= size);

        // INT3 out any leftover space
        memset(code + offset, 0xCC, size - offset);

        // Make it executable
        int err = mprotect(code, size, PROT_READ | PROT_EXEC);
        assert(err == 0);
        return CompiledCode(code, size);
    }
}
