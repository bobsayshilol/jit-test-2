#ifndef INTERNAL_H
#define INTERNAL_H

#include <jitlib/jitlib.h>
#include <stdexcept>
#include <unordered_map>

#define ASSERT(x)                                           \
    do                                                      \
    {                                                       \
        if (!(x))                                           \
        {                                                   \
            throw std::runtime_error("Assert failed: " #x); \
        }                                                   \
    } while (false)

namespace jitlib
{
    using NativeRegister = std::size_t;
    struct NativeState
    {
        NativeRegister regs[kNumRegisters];
        void *data;
    };
    using NativeFunction = void (*)(NativeState *);
    using LabelToOffsetMap = std::unordered_map<Label, std::size_t>;

    namespace native
    {
        std::size_t preamble(uint8_t *buffer);
        std::size_t encode(Op const &op, uint8_t const *buffer_base, uint8_t *buffer, LabelToOffsetMap const *label_to_offset);
        uint8_t *allocate(std::size_t &size);
        void finalise(uint8_t *buffer, std::size_t used, std::size_t length);
        void deallocate(void *buffer, std::size_t length);
    }
}

#endif
