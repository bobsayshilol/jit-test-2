add_executable(jittest jittest.cxx)
target_link_libraries(jittest jitlib)
target_compile_options(jittest PRIVATE -Werror -Wall -Wextra -pedantic)

add_test(
	NAME run-jittest
	COMMAND jittest
)
