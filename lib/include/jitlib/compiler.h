#ifndef JIT_COMPILER_H
#define JIT_COMPILER_H

#include <jitlib/types.h>

namespace jitlib
{
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
