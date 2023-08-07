#ifndef JIT_TYPES_H
#define JIT_TYPES_H

#include <array>
#include <cstdint>
#include <string_view>

namespace jitlib
{
    using Register = uint8_t;
    using Value = uint8_t;

    class CompiledCode;
    struct ExecutionEnvironment;
    struct Op;

    struct Label
    {
        template <std::size_t N>
        Label(const char (&str)[N])
        {
            static_assert(N <= 16);
            std::copy(std::begin(str), std::end(str), data.begin());
        }
        std::array<char, 16> data{};

        auto operator<=>(Label const &) const = default;
    };
}

template <>
struct std::hash<jitlib::Label>
{
    std::size_t operator()(jitlib::Label const &label) const noexcept
    {
        return std::hash<std::string_view>{}(label.data.data());
    }
};

#endif
