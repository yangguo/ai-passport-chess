cmake_minimum_required(VERSION 3.16)
project(chess_cli C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Werror)

option(CHESS_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(CHESS_SANITIZERS)
  # -fno-sanitize-recover=all aborts on the first UBSan/ASan report.
  add_compile_options(-fsanitize=address,undefined,alignment -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined,alignment)
endif()

set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(CORE_DIR ${ROOT_DIR}/components/chess_core)
set(STORAGE_DIR ${ROOT_DIR}/components/chess_storage)
set(AI_DIR ${ROOT_DIR}/components/chess_ai)
set(ENGINE_DIR ${ROOT_DIR}/third_party/mcu-max)
set_source_files_properties(${ENGINE_DIR}/mcu-max.c PROPERTIES
  COMPILE_OPTIONS "-Wno-error")

if(NOT DEFINED MCUMAX_HASH_BITS)
  set(MCUMAX_HASH_BITS 12)
endif()

set(CLI_SOURCES
  chess_cli.c
  ${CORE_DIR}/chess_position.c
  ${CORE_DIR}/chess_fen.c
  ${CORE_DIR}/chess_moves.c
  ${CORE_DIR}/chess_game.c
  ${STORAGE_DIR}/chess_codec.c
  ${AI_DIR}/chess_ai.c
  ${AI_DIR}/opening_book.c
  ${ENGINE_DIR}/mcu-max.c
  ${ENGINE_DIR}/mcumax_hash_scramble_table.c
)

function(add_chess_cli target bits)
  add_executable(${target} ${CLI_SOURCES})
  target_compile_definitions(${target} PRIVATE MCUMAX_HASH_BITS=${bits})
  target_include_directories(${target} PRIVATE
    ${CORE_DIR}/include ${STORAGE_DIR}/include
    ${AI_DIR}/include ${AI_DIR} ${ENGINE_DIR})
endfunction()

# Default CLI keeps the requested table (12 on device and in CI).
# chess-cli-nott is the strict-sanitizer companion with the TT compiled out.
add_chess_cli(chess-cli ${MCUMAX_HASH_BITS})
add_chess_cli(chess-cli-nott 0)

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
  # In-process engine: no binary needed, always runs.
  add_test(NAME cli_ai
    COMMAND ${SH_BIN} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check.sh
      ./chess-cli ${CMAKE_CURRENT_SOURCE_DIR}/tests/ai.cmd
      "engine: " "result: ongoing"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(cli_ai PROPERTIES TIMEOUT 300)
  # Out of book, so the engine search (and the TT, when compiled in) runs.
  add_test(NAME cli_search_tt
    COMMAND ${SH_BIN} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check.sh
      ./chess-cli ${CMAKE_CURRENT_SOURCE_DIR}/tests/search_short.cmd
      "ok loaded-fen" "engine: "
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  add_test(NAME cli_search_nott
    COMMAND ${SH_BIN} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check.sh
      ./chess-cli-nott ${CMAKE_CURRENT_SOURCE_DIR}/tests/search_short.cmd
      "ok loaded-fen" "engine: "
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(cli_search_tt cli_search_nott PROPERTIES TIMEOUT 60)
  add_test(NAME cli_movetime
    COMMAND ${SH_BIN} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check.sh
      ./chess-cli ${CMAKE_CURRENT_SOURCE_DIR}/tests/movetime.cmd
      "error bad-arg" "ok loaded-fen" "engine: "
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  add_test(NAME cli_movetime_nott
    COMMAND ${SH_BIN} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check.sh
      ./chess-cli-nott ${CMAKE_CURRENT_SOURCE_DIR}/tests/movetime.cmd
      "error bad-arg" "ok loaded-fen" "engine: "
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(cli_movetime cli_movetime_nott PROPERTIES TIMEOUT 60)
endif()

if(CHESS_SANITIZERS)
  get_property(chess_ctest_targets DIRECTORY PROPERTY TESTS)
  if(chess_ctest_targets)
    set_tests_properties(${chess_ctest_targets} PROPERTIES
      ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
  endif()
endif()
