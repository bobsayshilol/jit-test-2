#ifndef JIT_EXEC_H
#define JIT_EXEC_H

#include <jitlib/types.h>

namespace jitlib
{
    static inline constexpr std::size_t kNumRegisters = 4;

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
    CompiledCode compile(Ops const &ops);
}

#endif
