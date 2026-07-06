cmake_minimum_required(VERSION 3.16)
project(matmem_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 COMPONENTS Interpreter)

add_library(matmem_core
    src/dram_model.cpp
    src/energy_model.cpp
    src/scratchpad.cpp
    src/simulator.cpp
    src/tuner.cpp
    src/tiling_strategies.cpp
)

target_include_directories(matmem_core PUBLIC src)

add_executable(matmem-sim src/main.cpp)
target_link_libraries(matmem-sim PRIVATE matmem_core)

enable_testing()

add_executable(test_dram tests/test_dram.cpp)
target_link_libraries(test_dram PRIVATE matmem_core)
add_test(NAME test_dram COMMAND test_dram)

add_executable(test_scratchpad tests/test_scratchpad.cpp)
target_link_libraries(test_scratchpad PRIVATE matmem_core)
add_test(NAME test_scratchpad COMMAND test_scratchpad)

add_executable(test_metrics tests/test_metrics.cpp)
target_link_libraries(test_metrics PRIVATE matmem_core)
add_test(NAME test_metrics COMMAND test_metrics)

add_executable(test_simulation tests/test_simulation.cpp)
target_link_libraries(test_simulation PRIVATE matmem_core)
add_test(NAME test_simulation COMMAND test_simulation)

add_executable(test_smoke tests/test_smoke.cpp)
target_link_libraries(test_smoke PRIVATE matmem_core)
add_test(NAME test_smoke COMMAND test_smoke)

add_executable(test_tuner tests/test_tuner.cpp)
target_link_libraries(test_tuner PRIVATE matmem_core)
add_test(NAME test_tuner COMMAND test_tuner)

if(Python3_FOUND)
    add_test(NAME test_cli_csv
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli_csv.py $<TARGET_FILE:matmem-sim>)
endif()
