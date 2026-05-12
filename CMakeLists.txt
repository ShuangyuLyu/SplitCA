cmake_minimum_required(VERSION 3.22)
include(FetchContent)

project(SplitCA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    add_compile_definitions(_LIBCPP_ENABLE_EXPERIMENTAL)
endif()

# 这个会在非 Release 模式下打开 vector 等容器的边界检查
string(REPLACE "-DNDEBUG" "-D_GLIBCXX_ASSERTIONS" CMAKE_CXX_FLAGS_RELWITHDEBINFO ${CMAKE_CXX_FLAGS_RELWITHDEBINFO})
string(APPEND CMAKE_CXX_FLAGS_DEBUG " -D_GLIBCXX_ASSERTIONS")

include_directories("src")
file(GLOB_RECURSE SRC_DIR "src/*.cpp")
set_property(SOURCE ${SRC_DIR} PROPERTY COMPILE_FLAGS "-Wall -Werror -Wno-sign-compare")
add_executable(${PROJECT_NAME} ${SRC_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE pthread)

# clipp
FetchContent_Declare(clipp
    URL https://github.com/muellan/clipp/tarball/2c32b2f1f7cc530b1ec1f62c92f698643bb368db
)
FetchContent_MakeAvailable(clipp)
# Replace `std::result_of<Fn(Args...)>::type`
execute_process(COMMAND sed -i "s/std::result_of<Fn\(Args...\)>::type/std::invoke_result_t<Fn, Args...>/g" ${CMAKE_BINARY_DIR}/_deps/clipp-src/include/clipp.h)
execute_process(COMMAND sed -i "s/std::result_of<Fn\(\)>::type/std::invoke_result_t<Fn>/g" ${CMAKE_BINARY_DIR}/_deps/clipp-src/include/clipp.h)
# Add [[maybe_unused]] attribute to disable warning.
execute_process(COMMAND sed -i "1409c [[maybe_unused]] int i = 0;" ${CMAKE_BINARY_DIR}/_deps/clipp-src/include/clipp.h)
target_link_libraries(${PROJECT_NAME} PRIVATE clipp::clipp)

# minisat (with extension)
add_subdirectory(3rd/minisat)
target_link_libraries(${PROJECT_NAME} PRIVATE minisat-lib-static)

# dbg-macro
if (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    FetchContent_Declare(dbg_macro
        URL https://github.com/Toby-Shi-cloud/dbg-macro-reflection/tarball/732f02931b5ec65fddd79bf94a0f1a2f801c0bc5
    )
    FetchContent_MakeAvailable(dbg_macro)
    target_link_libraries(${PROJECT_NAME} PRIVATE dbg_macro)
endif()

# ftxui
FetchContent_Declare(ftxui
    URL https://github.com/ArthurSonzogni/FTXUI/archive/refs/tags/v6.1.9.tar.gz
)
FetchContent_MakeAvailable(ftxui)
target_link_libraries(${PROJECT_NAME} PRIVATE ftxui::component ftxui::dom ftxui::screen)

# testing
enable_testing()
add_test(
    NAME axtls-3wise-test
    COMMAND ${CMAKE_SOURCE_DIR}/test/test3.sh ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/check_tuples
)
add_test(
    NAME axtls-4wise-test
    COMMAND ${CMAKE_SOURCE_DIR}/test/test4.sh ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/check_tuples
)

# tools
add_executable(check_tuples tools/check_tuples.cpp)
