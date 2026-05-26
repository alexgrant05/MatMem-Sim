cmake_minimum_required(VERSION 3.16)
project(matmem_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(matmem_core
    src/dram_model.cpp
    src/scratchpad.cpp
    src/simulator.cpp
    src/tiling_strategies.cpp
)

target_include_directories(matmem_core PUBLIC src)

add_executable(matmem-sim src/main.cpp)
target_link_libraries(matmem-sim PRIVATE matmem_core)

enable_testing()
add_executable(smoke_tests tests/test_smoke.cpp)
target_link_libraries(smoke_tests PRIVATE matmem_core)
add_test(NAME smoke_tests COMMAND smoke_tests)
