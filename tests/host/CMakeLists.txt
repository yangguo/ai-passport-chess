cmake_minimum_required(VERSION 3.16)
project(chess_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Werror)

option(CHESS_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(CHESS_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../components/chess_core)

add_executable(test_core
  test_core.c
  ${CORE_DIR}/chess_position.c
  ${CORE_DIR}/chess_fen.c
)
target_include_directories(test_core PRIVATE ${CORE_DIR}/include)

enable_testing()
add_test(NAME core COMMAND test_core)
