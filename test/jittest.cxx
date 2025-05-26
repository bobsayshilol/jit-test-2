#include <jitlib/jitlib.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <source_location>
#include <string>
#include <vector>

namespace tests
{
    struct TestArgs
    {
        std::vector<std::string> errors;
        bool jit;
    };
    struct TestCase
    {
        using Fn = void (*)(TestArgs &);
        TestCase(Fn func, const char *name) : func(func), name(name) { s_tests.push_back(this); }
        Fn func;
        const char *name;
        static inline std::vector<TestCase const *> s_tests;
    };

#define TEST_CASE(name)                                          \
    static void _test_func_##name(tests::TestArgs &);            \
    tests::TestCase _test_case_##name(_test_func_##name, #name); \
    static void _test_func_##name([[maybe_unused]] tests::TestArgs &_test_args)

#define RUN_OPS(ops, env) \
    tests::run_ops(_test_args.jit, ops, env)

#define CHECK_IMPL(lhs, rhs, op, fail)                                                                                                                                        \
    do                                                                                                                                                                        \
    {                                                                                                                                                                         \
        auto &&lhs_ = lhs;                                                                                                                                                    \
        auto &&rhs_ = rhs;                                                                                                                                                    \
        if (lhs_ op rhs_)                                                                                                                                                     \
        {                                                                                                                                                                     \
            _test_args.errors.push_back("Fail (" + std::to_string(__LINE__) + ") : " #lhs " != " #rhs " : (" + std::to_string(lhs_) + ") != (" + std::to_string(rhs_) + ")"); \
            if constexpr (fail)                                                                                                                                               \
            {                                                                                                                                                                 \
                return;                                                                                                                                                       \
            }                                                                                                                                                                 \
        }                                                                                                                                                                     \
    } while (false)

#define CHECK_EQ(lhs, rhs) CHECK_IMPL(lhs, rhs, !=, false)
#define REQUIRE_EQ(lhs, rhs) CHECK_IMPL(lhs, rhs, !=, true)

    bool run_tests()
    {
        bool success = true;
        for (TestCase const *test : TestCase::s_tests)
        {
            for (bool jit : {false, true})
            {
                TestArgs args;
                args.jit = jit;
                try
                {
                    test->func(args);
                }
                catch (...)
                {
                    args.errors.push_back("Exception thrown");
                }
                if (!args.errors.empty())
                {
                    printf("Test %s failed (%s):\n", test->name, jit ? "jitter" : "interpreter");
                    for (auto const &error : args.errors)
                    {
                        printf("  %s\n", error.c_str());
                    }
                    success = false;
                }
            }
        }
        return success;
    }

    void run_ops(bool jit, jitlib::Ops const &ops, jitlib::ExecutionEnvironment &env)
    {
        if (jit)
        {
            auto code = jitlib::compile(ops);
            code.run(env);
        }
        else
        {
            jitlib::run(ops, env);
        }
    }
}

TEST_CASE(test_basic)
{
    jitlib::Ops const ops{
        jitlib::Op::make_Return(),
    };

    jitlib::ExecutionEnvironment env{};
    RUN_OPS(ops, env);
    CHECK_EQ(env.regs[0], 0);
    CHECK_EQ(env.regs[1], 0);
    CHECK_EQ(env.regs[2], 0);
    CHECK_EQ(env.regs[3], 0);
}

TEST_CASE(test_reg_order)
{
    jitlib::Ops const ops{
        jitlib::Op::make_SetImm(0, 1),
        jitlib::Op::make_SetImm(1, 2),
        jitlib::Op::make_SetImm(2, 3),
        jitlib::Op::make_SetImm(3, 4),
        jitlib::Op::make_Return(),
    };

    jitlib::ExecutionEnvironment env{};
    RUN_OPS(ops, env);
    CHECK_EQ(env.regs[0], 1);
    CHECK_EQ(env.regs[1], 2);
    CHECK_EQ(env.regs[2], 3);
    CHECK_EQ(env.regs[3], 4);
}

TEST_CASE(test_reg_input)
{
    jitlib::Ops const ops{
        jitlib::Op::make_Return(),
    };

    jitlib::ExecutionEnvironment env{};
    env.regs[0] = 1;
    env.regs[1] = 2;
    env.regs[2] = 3;
    env.regs[3] = 4;
    RUN_OPS(ops, env);
    CHECK_EQ(env.regs[0], 1);
    CHECK_EQ(env.regs[1], 2);
    CHECK_EQ(env.regs[2], 3);
    CHECK_EQ(env.regs[3], 4);
}

TEST_CASE(test_set_copy)
{
    jitlib::Ops const ops{
        jitlib::Op::make_SetImm(0, 2),
        jitlib::Op::make_SetReg(1, 0),
        jitlib::Op::make_Return(),
    };

    jitlib::ExecutionEnvironment env{};
    RUN_OPS(ops, env);
    CHECK_EQ(env.regs[0], 2);
    CHECK_EQ(env.regs[1], 2);
}

TEST_CASE(test_set_all)
{
    for (int i = 0; i <= 255; i++)
    {
        jitlib::Ops const ops{
            jitlib::Op::make_SetImm(0, i),
            jitlib::Op::make_Return(),
        };

        jitlib::ExecutionEnvironment env{};
        RUN_OPS(ops, env);
        CHECK_EQ(env.regs[0], i);
    }
}

TEST_CASE(test_load_store)
{
    jitlib::Ops const ops{
        jitlib::Op::make_Load(2, 0),  // r2 = m[r0]
        jitlib::Op::make_Store(1, 3), // m[r1] = r3
        jitlib::Op::make_Return(),
    };

    jitlib::ExecutionEnvironment env{};
    env.regs[0] = 4;
    env.regs[1] = 10;
    env.regs[3] = 9;
    env.mem[4] = 7;
    RUN_OPS(ops, env);
    CHECK_EQ(env.regs[2], 7);
    CHECK_EQ(env.mem[10], 9);
}

TEST_CASE(test_add)
{
    jitlib::Ops const ops{
        jitlib::Op::make_SetImm(2, 1), // r2 = 1
        jitlib::Op::make_AddReg(1, 2), // r1 += r2
        jitlib::Op::make_AddImm(2, 3), // r2 += 3
        jitlib::Op::make_Return(),
    };

    jitlib::ExecutionEnvironment env{};
    RUN_OPS(ops, env);
    CHECK_EQ(env.regs[1], 1);
    CHECK_EQ(env.regs[2], 4);
}

TEST_CASE(test_add_all)
{
    for (int i = 0; i <= 255; i++)
    {
        jitlib::Ops const ops{
            jitlib::Op::make_AddImm(0, i),
            jitlib::Op::make_Return(),
        };

        jitlib::ExecutionEnvironment env{};
        RUN_OPS(ops, env);
        CHECK_EQ(env.regs[0], i);
    }
}

TEST_CASE(test_add_wrap)
{
    jitlib::Ops const ops{
        jitlib::Op::make_SetImm(1, 255), // r1 = 255
        jitlib::Op::make_AddImm(1, 1),   // r1 += 1
        jitlib::Op::make_Return(),
    };

    jitlib::ExecutionEnvironment env{};
    RUN_OPS(ops, env);
    CHECK_EQ(env.regs[1], 0);
}

TEST_CASE(test_neg)
{
    jitlib::Ops const ops{
        jitlib::Op::make_SetImm(1, 255), // r1 = 255
        jitlib::Op::make_Negate(1),      // r1 = -r1
        jitlib::Op::make_Return(),
    };

    jitlib::ExecutionEnvironment env{};
    RUN_OPS(ops, env);
    CHECK_EQ(env.regs[1], 1);
}

TEST_CASE(test_jump)
{
    jitlib::Ops const ops{
        jitlib::Op::make_SetImm(1, 7),  // r1 = 7
        jitlib::Op::make_Jump("test"),  // jmp over
        jitlib::Op::make_AddImm(1, 1),  // r1 += 1
        jitlib::Op::make_Label("test"), //
        jitlib::Op::make_AddImm(1, 2),  // r1 += 2
        jitlib::Op::make_Return(),
    };

    jitlib::ExecutionEnvironment env{};
    RUN_OPS(ops, env);
    CHECK_EQ(env.regs[1], 9);
}

TEST_CASE(test_jump_if_zero)
{
    jitlib::Ops const ops{
        jitlib::Op::make_SetImm(0, 3),          // r0 = 3
        jitlib::Op::make_SetImm(1, 3),          // r1 = 3
        jitlib::Op::make_Negate(0),             // r0 = -r0
        jitlib::Op::make_AddReg(0, 1),          // r0 += r1
        jitlib::Op::make_JumpIfZero(0, "test"), // r0 == 0, jmp over
        jitlib::Op::make_AddImm(2, 1),          // r2 += 1
        jitlib::Op::make_Label("test"),         //
        jitlib::Op::make_AddImm(2, 2),          // r2 += 2
        jitlib::Op::make_Return(),
    };

    jitlib::ExecutionEnvironment env{};
    RUN_OPS(ops, env);
    CHECK_EQ(env.regs[0], 0);
    CHECK_EQ(env.regs[2], 2);
}

TEST_CASE(test_call)
{
    jitlib::Ops const ops{
        jitlib::Op::make_Call("test"), // call 4
        jitlib::Op::make_AddImm(1, 5), // r1 += 5
        jitlib::Op::make_Return(),
        jitlib::Op::make_Nop(),
        jitlib::Op::make_Label("test"),
        jitlib::Op::make_SetImm(1, 3), // r1 = 3
        jitlib::Op::make_Return(),
    };

    jitlib::ExecutionEnvironment env{};
    RUN_OPS(ops, env);
    CHECK_EQ(env.regs[1], 8);
}

TEST_CASE(test_call_out)
{
    using UserData = int;
    auto func = [](jitlib::ExecutionEnvironment &env)
    {
        *static_cast<UserData *>(env.userdata) += env.mem[0];
        env.mem[0] = 3;
        env.regs[0] += 1;
        env.regs[1] += 2;
        env.regs[2] += 3;
        env.regs[3] += 4;
    };

    jitlib::Ops const ops{
        jitlib::Op::make_CallOut(func),
        jitlib::Op::make_AddImm(2, 5),
        jitlib::Op::make_Return(),
    };

    jitlib::ExecutionEnvironment env{};
    env.mem[0] = 10;
    env.regs[0] = 1;
    env.regs[1] = 2;
    env.regs[2] = 3;
    env.regs[3] = 4;
    UserData userdata = 7;
    env.userdata = &userdata;
    RUN_OPS(ops, env);
    CHECK_EQ(userdata, 17);
    CHECK_EQ(env.mem[0], 3);
    CHECK_EQ(env.mem[1], 0);
    CHECK_EQ(env.regs[0], 2);
    CHECK_EQ(env.regs[1], 4);
    CHECK_EQ(env.regs[2], 11);
    CHECK_EQ(env.regs[3], 8);
}

int main()
{
    return tests::run_tests() ? EXIT_SUCCESS : EXIT_FAILURE;
}
