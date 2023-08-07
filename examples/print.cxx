#include <jitlib/jitlib.h>
#include <format>
#include <iostream>

namespace
{
    struct UserData
    {
        const char *prefix;
    };

    void print_regs(jitlib::ExecutionEnvironment &env)
    {
        auto *userdata = static_cast<UserData *>(env.userdata);
        std::cout << std::format("Regs ({}): [{}, {}, {}, {}]", userdata->prefix, env.regs[0], env.regs[1], env.regs[2], env.regs[3]) << std::endl;
    }
}

int main()
{
    jitlib::Ops const program{
        jitlib::Op::make_Label("begin"),

        // r0 += 1
        jitlib::Op::make_AddImm(0, 1),

        // r1 += 10
        jitlib::Op::make_AddImm(1, 10),

        // r2 += 100
        jitlib::Op::make_AddImm(2, 100),

        // r3 += 32
        jitlib::Op::make_AddImm(3, 32),

        // print_regs()
        jitlib::Op::make_CallOut(print_regs),

        // if (r3 == 0) return
        jitlib::Op::make_JumpIfZero(3, "return"),
        jitlib::Op::make_Jump("begin"),
        jitlib::Op::make_Label("return"),
        jitlib::Op::make_Return(),
    };

    {
        UserData userdata{"interpreted"};
        jitlib::ExecutionEnvironment env{};
        env.userdata = &userdata;
        jitlib::run(program, env);
    }

    {
        UserData userdata{"jitted"};
        auto code = jitlib::compile(program);
        jitlib::ExecutionEnvironment env{};
        env.userdata = &userdata;
        code.run(env);
    }
}
