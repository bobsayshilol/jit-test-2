#ifndef JIT_COMPILER_H
#define JIT_COMPILER_H

#include <span>
#include <variant>
#include <vector>
#include <string>

#include <jitlib/types.h>
#include <jitlib/ops.h>

namespace jitlib
{
    class ExecutionEnvironment;

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
}

#endif
