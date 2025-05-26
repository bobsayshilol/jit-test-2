#include "internal.h"
#include <cstring>
#include <array>

// We only use registers that are caller-saved so that we don't have to
// restore anything on exit.
// registers: r0,r1,r2,r3
// r12 - base data ptr / ExecutionEnvironment
// r14 - temporary
//
// Fake link register is pushed before branch, emulating x86 call.
// Return is then simply a pop{pc}.

namespace jitlib
{
    static_assert(jitlib::kNumRegisters == 4, "Native code will need changing");
    static_assert(std::is_same_v<Value, uint8_t>, "Native code will need changing");
    static_assert(offsetof(ExecutionEnvironment, mem) == 0, "ExecutionEnvironment and data ptr aren't interchangeable");
    static_assert(std::is_same_v<NativeRegister, std::uint32_t>, "Registers are 32bit");

    namespace
    {
        uint32_t encode_reg(Register reg)
        {
            std::array<uint8_t, 4> const regs{
                0x0, // r0
                0x1, // r1
                0x2, // r2
                0x3, // r3
            };
            return regs.at(reg);
        }

        std::size_t handle_nop(Op const &, uint32_t *)
        {
            // Don't need to lower "do nothing"
            return 0;
        }

        std::size_t handle_load_store(Op const &op, uint32_t *buffer)
        {
            (void)op;
            (void)buffer;
            return 0; // TODO
        }

        std::size_t handle_set(Op const &op, uint32_t *buffer)
        {
            auto reg = encode_reg(op.regA);
            if (op.type == OpType::SetImm)
            {
                uint32_t const ins[]{
                    0xe3a00000 | (reg << 12) | op.imm // mov reg, imm
                };
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            else if (op.type == OpType::SetReg)
            {
                auto regB = encode_reg(op.regB);
                uint32_t const ins[]{
                    0xe1a00000 | (reg << 12) | regB // mov reg, regB
                };
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            throw std::logic_error("Unknown set op");
        }

        std::size_t handle_arithmetic(Op const &op, uint32_t *buffer)
        {
            auto reg = encode_reg(op.regA);
            if (op.type == OpType::AddImm)
            {
                uint32_t const ins[]{
                    0xe3a00000 | (0xe << 12) | op.imm, // mov r14, imm
                    0xe0800000 | (reg << 16) | (reg << 12) | 0xe, // add reg, reg, r14
                    0xe2000000 | (reg << 16) | (reg << 12) | 0xff, // and reg, reg, #255
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
                uint32_t const ins[]{
                    0xe0800000 | (reg << 16) | (reg << 12) | regB, // add reg, reg, regB
                    0xe2000000 | (reg << 16) | (reg << 12) | 0xff, // and reg, reg, #255
                };
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            else if (op.type == OpType::Negate)
            {
                uint32_t const ins[]{
                    0xe2600000 | (reg << 16) | (reg << 12), // neg reg, reg
                    0xe2000000 | (reg << 16) | (reg << 12) | 0xff, // and reg, reg, #255
                };
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            throw std::logic_error("Unknown arithmetic op");
        }

        std::size_t handle_jump(Op const &op, uint32_t const *buffer_base, uint32_t *buffer, LabelToOffsetMap const *label_to_offset)
        {
            (void)op;
            (void)buffer_base;
            (void)buffer;
            (void)label_to_offset;
            return 0; // TODO
        }

        std::size_t handle_return(Op const &, uint32_t *buffer)
        {
            (void)buffer;
            if (buffer != nullptr)
            {
                *buffer = 0xe49df004; // pop {pc}
            }
            return 1;
        }

        std::size_t handle_callout(Op const &op, uint32_t *buffer)
        {
            (void)op;
            (void)buffer;
            return 0; // TODO
        }

        std::size_t preamble32(uint32_t *buffer)
        {
            uint32_t const enter[]{
                // Store return address.
                0xe52de004, // push {r14}

                // Store address of |NativeState| to the stack
                0xe52d0004, // push {r0}

                // Read off each register from |NativeState|
                0xe5901004, // ldr r1, [r0, #4]
                0xe5902008, // ldr r2, [r0, #8]
                0xe590300c, // ldr r3, [r0, #12]
                0xe590c010, // ldr r12, [r0, #16]
                0xe5900000, // ldr r0, [r0, #0]

                // Save return address, x86 call style.
                0xe28fe004, // add r14, pc, #4
                0xe52de004, // push {r14}
                // Call into the rest of the code
                0xe1a00000, // nop
            };
            uint32_t const leave[]{
                // Load |NativeState| address from stack
                0xe49dc004, // pop {r12}

                // Store new register values back to |NativeState|
                0xe58c0000, // str r0, [r12, #0]
                0xe58c1004, // str r1, [r12, #4]
                0xe58c2008, // str r2, [r12, #8]
                0xe58c300c, // str r3, [r12, #12]

                // Return
                0xe49df004, // pop {pc}

                // Safety guard
                0xe7f000f0, // udf
                0xe7f000f0, // udf
                0xe7f000f0, // udf
            };
            if (buffer != nullptr)
            {
                buffer = std::copy(std::begin(enter), std::end(enter), buffer);
                // Patch call address
                uint32_t const relative_address = std::size(leave);
                buffer[-1] = 0xea000000 + relative_address - 1, // b <offset>
                buffer = std::copy(std::begin(leave), std::end(leave), buffer);
            }
            return std::size(enter) + std::size(leave);
        }

        std::size_t encode32(Op const &op, uint32_t const *buffer_base, uint32_t *buffer, LabelToOffsetMap const *label_to_offset)
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

    namespace native
    {
        std::size_t preamble(uint8_t *buffer)
        {
            uint32_t *buffer32 = reinterpret_cast<uint32_t *>(buffer);
            return preamble32(buffer32) * 4;
        }

        std::size_t encode(Op const &op, uint8_t const *buffer_base, uint8_t *buffer, LabelToOffsetMap const *label_to_offset) {
            uint32_t const *buffer_base32 = reinterpret_cast<uint32_t const*>(buffer_base);
            uint32_t *buffer32 = reinterpret_cast<uint32_t *>(buffer);
            return encode32(op, buffer_base32, buffer32, label_to_offset) * 4;
        }
    }
}
