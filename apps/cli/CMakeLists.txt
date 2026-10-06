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
  chess_engine.c
  ${CORE_DIR}/chess_position.c
  ${CORE_DIR}/chess_fen.c
  ${CORE_DIR}/chess_moves.c
  ${CORE_DIR}/chess_game.c
  ${STORAGE_DIR}/chess_codec.c
)
target_include_directories(chess-cli PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CORE_DIR}/include ${STORAGE_DIR}/include)

enable_testing()
find_program(SH_BIN sh)

# NOTE (RED): chess_engine.c is still a placeholder; link must fail.
add_executable(test_engine_parse
  tests/test_engine_parse.c
  chess_engine.c
  ${CORE_DIR}/chess_position.c
  ${CORE_DIR}/chess_fen.c
  ${CORE_DIR}/chess_moves.c
  ${CORE_DIR}/chess_game.c
)
target_include_directories(test_engine_parse PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CORE_DIR}/include ${STORAGE_DIR}/include)
add_test(NAME engine_parse COMMAND test_engine_parse)

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
  # No-engine gate: must not depend on the ambient environment.
  add_test(NAME cli_noengine
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_noenv.sh
      ./chess-cli ${CMAKE_CURRENT_SOURCE_DIR}/tests/noengine.cmd
      "error no-engine"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  # Live engine test: needs a console engine binary (see
  # third_party/mcu-max/SOURCES.md). Opt-in via CHESS_ENGINE_BIN.
  if(DEFINED ENV{CHESS_ENGINE_BIN})
    add_test(NAME cli_ai_live
      COMMAND ${SH_BIN} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check.sh
        ./chess-cli ${CMAKE_CURRENT_SOURCE_DIR}/tests/ai_live.cmd
        "ok ai attached black" "engine: " "result: ongoing"
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cli_ai_live PROPERTIES TIMEOUT 300)
  endif()
endif()
