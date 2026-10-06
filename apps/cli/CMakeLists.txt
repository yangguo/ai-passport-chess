cmake_minimum_required(VERSION 3.16)
project(chess_cli C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Werror)

option(CHESS_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(CHESS_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(CORE_DIR ${ROOT_DIR}/components/chess_core)
set(STORAGE_DIR ${ROOT_DIR}/components/chess_storage)

add_executable(chess-cli
  chess_cli.c
  ${CORE_DIR}/chess_position.c
  ${CORE_DIR}/chess_fen.c
  ${CORE_DIR}/chess_moves.c
  ${CORE_DIR}/chess_game.c
  ${STORAGE_DIR}/chess_codec.c
)
target_include_directories(chess-cli PRIVATE
  ${CORE_DIR}/include ${STORAGE_DIR}/include)

enable_testing()
find_program(SH_BIN sh)
if(SH_BIN)
  add_test(NAME cli_fools
    COMMAND ${SH_BIN} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check.sh
      ./chess-cli ${CMAKE_CURRENT_SOURCE_DIR}/tests/fools.cmd
      "ok d8h4" "result: checkmate-black-wins"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  add_test(NAME cli_saving
    COMMAND ${SH_BIN} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check.sh
      ./chess-cli ${CMAKE_CURRENT_SOURCE_DIR}/tests/saving.cmd
      "ok saved" "ok loaded" "4P3" "result: ongoing"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  add_test(NAME cli_bad
    COMMAND ${SH_BIN} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check.sh
      ./chess-cli ${CMAKE_CURRENT_SOURCE_DIR}/tests/bad.cmd
      "error illegal" "error bad-uci" "error unknown-command"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  add_test(NAME cli_promo
    COMMAND ${SH_BIN} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check.sh
      ./chess-cli ${CMAKE_CURRENT_SOURCE_DIR}/tests/promo.cmd
      "b7b8n" "ok b7b8n" "1N2k3"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()
