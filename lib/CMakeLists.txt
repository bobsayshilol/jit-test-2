add_library(jitlib jitlib.cxx compiled.cxx x64.cxx)
target_include_directories(jitlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(jitlib PRIVATE -Werror -Wall -Wextra -pedantic)
