#include "lib.h"
#include <stack>

namespace jitlib
{
    void run(Ops const &ops, ExecutionEnvironment &env)
    {
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
                env.regs[op.regA] = env.mem[op.addr];
                break;
            case OpType::Store:
                env.mem[op.addr] = env.regs[op.regA];
                break;
            case OpType::Set:
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
                pc += op.imm;
                break;
            case OpType::JumpIfZero:
                if (env.regs[op.regA] == 0)
                {
                    pc += op.imm;
                }
                break;
            case OpType::Call:
                pcs.push(op.imm);
                break;
            case OpType::Return:
                pcs.pop();
                break;
            }
        }
    }
}
