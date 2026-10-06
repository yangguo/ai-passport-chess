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
set(CORE_SOURCES
  ${CORE_DIR}/chess_position.c
  ${CORE_DIR}/chess_fen.c
  ${CORE_DIR}/chess_moves.c
)

add_executable(test_core test_core.c ${CORE_SOURCES})
target_include_directories(test_core PRIVATE ${CORE_DIR}/include)

add_executable(test_game test_game.c ${CORE_SOURCES} ${CORE_DIR}/chess_game.c)
target_include_directories(test_game PRIVATE ${CORE_DIR}/include)

set(MODEL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../components/chess_ui_model)
# NOTE (RED): chess_ui_model.c does not exist yet; link must fail for now.
add_executable(test_ui_model test_ui_model.c ${CORE_SOURCES}
  ${MODEL_DIR}/chess_ui_model.c)
target_include_directories(test_ui_model PRIVATE
  ${CORE_DIR}/include ${MODEL_DIR}/include)

set(STORAGE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../components/chess_storage)
add_executable(test_codec test_codec.c ${CORE_SOURCES}
  ${CORE_DIR}/chess_game.c
  ${STORAGE_DIR}/chess_codec.c ${STORAGE_DIR}/chess_nvs.c)
target_include_directories(test_codec PRIVATE
  ${CORE_DIR}/include ${STORAGE_DIR}/include)

add_executable(perft perft_main.c ${CORE_SOURCES})
target_include_directories(perft PRIVATE ${CORE_DIR}/include)

enable_testing()
add_test(NAME core COMMAND test_core)
add_test(NAME game COMMAND test_game)
add_test(NAME ui_model COMMAND test_ui_model)
add_test(NAME codec COMMAND test_codec)

# Startpos perft gate (Task 3): no castling/ep-capture/promotion occurs
# within 4 plies, so the ordinary-move generator must hit exact counts.
set(START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")
add_test(NAME perft_start_d1 COMMAND perft --fen "${START_FEN}" --depth 1)
add_test(NAME perft_start_d2 COMMAND perft --fen "${START_FEN}" --depth 2)
add_test(NAME perft_start_d3 COMMAND perft --fen "${START_FEN}" --depth 3)
add_test(NAME perft_start_d4 COMMAND perft --fen "${START_FEN}" --depth 4)
set_tests_properties(perft_start_d1 PROPERTIES PASS_REGULAR_EXPRESSION "^20")
set_tests_properties(perft_start_d2 PROPERTIES PASS_REGULAR_EXPRESSION "^400")
set_tests_properties(perft_start_d3 PROPERTIES PASS_REGULAR_EXPRESSION "^8902")
set_tests_properties(perft_start_d4 PROPERTIES PASS_REGULAR_EXPRESSION "^197281")

# Full perft gate over tests/perft/cases.json via the host-only Python
# runner (the C core itself never parses JSON).
find_program(PYTHON3_BIN python3)
if(PYTHON3_BIN)
  add_test(NAME perft_cases
    COMMAND ${PYTHON3_BIN}
      ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/check_perft_cases.py
      --perft ./perft
      --cases ${CMAKE_CURRENT_SOURCE_DIR}/../perft/cases.json
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()
