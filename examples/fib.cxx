#include <jitlib/jitlib.h>
#include <chrono>
#include <string>

namespace
{
    void run_interpreter(jitlib::Ops const &program, jitlib::ExecutionEnvironment &env)
    {
        jitlib::run(program, env);
    }

    void run_compiled(jitlib::CompiledCode const &code, jitlib::ExecutionEnvironment &env)
    {
        code.run(env);
    }

    template <typename Func, typename... Args>
    auto profile(Func func, Args &...args)
    {
        constexpr std::size_t num_times = 1'000;
        auto start = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < num_times; i++)
        {
            func(args...);
        }
        auto end = std::chrono::high_resolution_clock::now();
        return (end - start).count() / double(num_times);
    }

    std::string to_string(jitlib::ExecutionEnvironment const &env)
    {
        std::string str;
        char buf[8];
        for (std::size_t y = 0; y < 16; y++)
        {
            for (std::size_t x = 0; x < 16; x++)
            {
                snprintf(buf, sizeof(buf), "%03u ", env.mem[y * 16 + x]);
                str += buf;
            }
            str += '\n';
        }
        return str;
    }
}

int main()
{
    jitlib::Ops const program{
        // r3 = 0
        jitlib::Op::make_SetImm(3, 0),

        // r0 = 1, r1 = 1
        jitlib::Op::make_SetImm(0, 1),
        jitlib::Op::make_SetImm(1, 1),

        jitlib::Op::make_Label("begin"),

        // r2 = r1 + r0
        jitlib::Op::make_SetReg(2, 1),
        jitlib::Op::make_AddReg(2, 0),

        // (r0, r1) = (r1, r2)
        jitlib::Op::make_SetReg(0, 1),
        jitlib::Op::make_SetReg(1, 2),

        // mem[r3] = r0
        jitlib::Op::make_Store(3, 0),

        // r3++
        jitlib::Op::make_AddImm(3, 1),

        // if (r3 == 0) return
        jitlib::Op::make_JumpIfZero(3, "return"),
        jitlib::Op::make_Jump("begin"),
        jitlib::Op::make_Label("return"),
        jitlib::Op::make_Return(),
    };

    {
        jitlib::ExecutionEnvironment env{};
        run_interpreter(program, env);
        auto interpreter_mem = to_string(env);
        auto interpreter_time = profile(run_interpreter, program, env);

        printf("Interpretted: %fns\n%s\n", interpreter_time, interpreter_mem.c_str());
    }

    {
        auto code = jitlib::compile(program);

        jitlib::ExecutionEnvironment env{};
        run_compiled(code, env);
        auto compiled_mem = to_string(env);
        auto compiled_time = profile(run_compiled, code, env);

        printf("Compiled: %fns\n%s\n", compiled_time, compiled_mem.c_str());
    }
}
