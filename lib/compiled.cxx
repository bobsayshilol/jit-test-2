#include "jitlib.h"
#include "internal.h"

namespace jitlib
{
    CompiledCode::CompiledCode() : m_code{}, m_size{} {}
    CompiledCode::CompiledCode(void *code, std::size_t size) : m_code{code}, m_size{size} {}
    CompiledCode::~CompiledCode()
    {
        if (m_code != nullptr)
        {
            native::deallocate(m_code, m_size);
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
        // Pass over the code to get the total size and label locations
        LabelToOffsetMap label_to_offset;
        std::size_t size = native::preamble(nullptr);
        for (Op const &op : ops)
        {
            if (op.type == OpType::Label)
            {
                label_to_offset[op.label] = size;
            }
            size += native::encode(op, nullptr, nullptr, nullptr);
        }

        // Allocate a buffer that we can make executable
        auto *const code = native::allocate(size);

        // Copy it over
        std::size_t offset = native::preamble(code);
        for (Op const &op : ops)
        {
            offset += native::encode(op, code, code + offset, &label_to_offset);
        }
        ASSERT(offset <= size);

        // Make the buffer executable
        native::finalise(code, offset, size);

        // Return it ready for us
        return CompiledCode(code, size);
    }
}
