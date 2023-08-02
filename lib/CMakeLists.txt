add_library(jitlib lib.cxx libx64.cxx)
target_include_directories(jitlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(jitlib PRIVATE -Werror -Wall -Wextra -pedantic)
