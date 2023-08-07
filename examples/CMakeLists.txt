add_executable(jitfib fib.cxx)
target_link_libraries(jitfib jitlib)
target_compile_options(jitfib PRIVATE -Werror -Wall -Wextra -pedantic)

add_executable(jitprint print.cxx)
target_link_libraries(jitprint jitlib)
target_compile_options(jitprint PRIVATE -Werror -Wall -Wextra -pedantic)

