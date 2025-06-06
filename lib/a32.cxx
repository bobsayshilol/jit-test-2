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
            auto regA = encode_reg(op.regA);
            auto regB = encode_reg(op.regB);
            if (op.type == OpType::Load)
            {
                uint32_t const ins[]{
                    0xe7dc0000 | (regA << 12) | regB, // ldrb regA, [r12, regB]
                };
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            else if (op.type == OpType::Store)
            {
                uint32_t const ins[]{
                    0xe7cc0000 | (regB << 12) | regA, // strb regB, [r12, regA]
                };
                if (buffer != nullptr)
                {
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            throw std::logic_error("Unknown mem op");
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
            auto encode_relative_address = [&](auto &ins)
            {
                // Patch call address relative to this instruction.
                auto it = label_to_offset->find(op.label);
                if (it == label_to_offset->end())
                {
                    throw std::logic_error("Unknown label: " + std::string(op.label.data.data()));
                }
                // Offset is the number of instructions, not bytes.
                std::size_t relative_address = it->second / 4 - (buffer + std::size(ins) - buffer_base);
                relative_address -= 1;
                std::end(ins)[-1] |= (relative_address & 0x00ffffff);
            };

            if (op.type == OpType::Jump)
            {
                uint32_t ins[]{
                    0xea000000, // b <offset>
                };
                if (buffer != nullptr)
                {
                    encode_relative_address(ins);
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            else if (op.type == OpType::JumpIfZero)
            {
                auto reg = encode_reg(op.regA);
                uint32_t ins[]{
                    0xe3500000 | (reg << 16), // cmp reg, #0
                    0x0a000000, // beq <offset>
                };
                if (buffer != nullptr)
                {
                    encode_relative_address(ins);
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            else if (op.type == OpType::Call)
            {
                uint32_t ins[]{
                    // Save return address, x86 call style.
                    0xe28fe004, // add r14, pc, #4
                    0xe52de004, // push {r14}
                    0xeb000000, // bl <offset>
                };
                if (buffer != nullptr)
                {
                    encode_relative_address(ins);
                    std::copy(std::begin(ins), std::end(ins), buffer);
                }
                return std::size(ins);
            }
            throw std::logic_error("Unknown jump op");
        }

        std::size_t handle_return(Op const &, uint32_t *buffer)
        {
            if (buffer != nullptr)
            {
                *buffer = 0xe49df004; // pop {pc}
            }
            return 1;
        }

        std::size_t handle_callout(Op const &op, uint32_t *buffer)
        {
            auto helper_thunk = [](NativeState *state, CallOutFunc func)
            {
                auto *env = static_cast<ExecutionEnvironment *>(state->data);
                std::copy(std::begin(state->regs), std::end(state->regs), std::begin(env->regs));
                func(*env);
                std::copy(std::begin(env->regs), std::end(env->regs), std::begin(state->regs));
            };

            uint32_t const enter[]{
                // Give us some stack
                0xe24dd01c, // sub sp, sp, #28

                // Store current register values to a |NativeState| on the stack
                0xe58d0000, // str r0, [sp, #0]
                0xe58d1004, // str r1, [sp, #4]
                0xe58d2008, // str r2, [sp, #8]
                0xe58d300c, // str r3, [sp, #12]
                0xe58dc010, // str r12, [sp, #16]

                // Setup first arg
                0xe1a0000d, // mov r0, sp

                // Setup second arg
                0xe59f1000, // ldr r1, [pc, #0]
                0xea000000, // b call_thunk
                0x00000000, // <callout>
            };
            uint32_t const call_thunk[]{
                // Setup call
                0xe59f2000, // ldr r2, [pc, #0]
                0xea000000, // b leave
                0x00000000, // <thunk>
            };
            uint32_t const leave[]{
                // Call into the helper thunk
                0xe12fff32, // blx r2

                // Read off each register from |NativeState|
                0xe59d0000, // ldr r0, [sp, #0]
                0xe59d1004, // ldr r1, [sp, #4]
                0xe59d2008, // ldr r2, [sp, #8]
                0xe59d300c, // ldr r3, [sp, #12]
                0xe59dc010, // ldr r12, [sp, #16]

                // Restore stack
                0xe28dd01c, // add sp, sp, #28
            };
            if (buffer != nullptr)
            {
                buffer = std::copy(std::begin(enter), std::end(enter), buffer);
                // Patch callout address
                memcpy(buffer - 1, &op.func, 4);

                buffer = std::copy(std::begin(call_thunk), std::end(call_thunk), buffer);
                // Patch thunk address
                auto *thunk_ptr = static_cast<void (*)(NativeState *, CallOutFunc)>(helper_thunk);
                memcpy(buffer - 1, &thunk_ptr, 4);

                buffer = std::copy(std::begin(leave), std::end(leave), buffer);
            }
            return std::size(enter) + std::size(call_thunk) + std::size(leave);
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
