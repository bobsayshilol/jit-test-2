#include "jitlib.h"
#include "internal.h"
#include <stack>

namespace jitlib
{
    namespace
    {
        auto generate_lookups(Ops const &ops)
        {
            std::unordered_map<Label, Value> lookup;
            for (std::size_t i = 0; i < ops.size(); i++)
            {
                auto &op = ops[i];
                if (op.type == OpType::Label)
                {
                    lookup[op.label] = i;
                }
            }
            return lookup;
        }
    }

    void run(Ops const &ops, ExecutionEnvironment &env)
    {
        auto const lookup = generate_lookups(ops);

        // Add the first program counter
        std::stack<Value> pcs;
        pcs.push(env.pc);

        // Keep going until we've returned
        while (!pcs.empty())
        {
            Value &pc = pcs.top();
            Op const op = ops[pc++];
            switch (op.type)
            {
            case OpType::Nop:
                break;
            case OpType::Load:
                env.regs[op.regA] = env.mem[env.regs[op.regB]];
                break;
            case OpType::Store:
                env.mem[env.regs[op.regA]] = env.regs[op.regB];
                break;
            case OpType::SetReg:
                env.regs[op.regA] = env.regs[op.regB];
                break;
            case OpType::SetImm:
                env.regs[op.regA] = op.imm;
                break;
            case OpType::AddReg:
                env.regs[op.regA] += env.regs[op.regB];
                break;
            case OpType::AddImm:
                env.regs[op.regA] += op.imm;
                break;
            case OpType::Negate:
                env.regs[op.regA] = 1 + ~env.regs[op.regA];
                break;
            case OpType::Jump:
                pc = lookup.at(op.label);
                break;
            case OpType::JumpIfZero:
                if (env.regs[op.regA] == 0)
                {
                    pc = lookup.at(op.label);
                }
                break;
            case OpType::Call:
                pcs.push(lookup.at(op.label));
                break;
            case OpType::Return:
                pcs.pop();
                break;
            case OpType::Label:
                break;
            }
        }
    }
}
