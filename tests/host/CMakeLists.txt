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
set(I18N_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../components/chess_i18n)
set(AI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../components/chess_ai)
set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/mcu-max)
set_source_files_properties(${ENGINE_DIR}/mcu-max.c PROPERTIES
  COMPILE_OPTIONS "-Wno-error")

set(AI_SOURCES ${AI_DIR}/chess_ai.c ${AI_DIR}/opening_book.c ${ENGINE_DIR}/mcu-max.c
  ${ENGINE_DIR}/mcumax_hash_scramble_table.c)
set(AI_INCLUDES ${CORE_DIR}/include ${AI_DIR}/include ${AI_DIR} ${ENGINE_DIR})

function(chess_ai_target_hash_bits target bits)
  target_compile_definitions(${target} PRIVATE MCUMAX_HASH_BITS=${bits})
endfunction()

add_executable(test_ai_adapter
  test_ai_adapter.c
  ${CORE_SOURCES}
  ${CORE_DIR}/chess_game.c
  ${AI_SOURCES}
)
target_include_directories(test_ai_adapter PRIVATE ${AI_INCLUDES})
chess_ai_target_hash_bits(test_ai_adapter 12)
add_test(NAME ai_adapter COMMAND test_ai_adapter)

add_executable(test_ai_worker
  test_ai_worker.c
  ${CORE_SOURCES}
  ${CORE_DIR}/chess_game.c
  ${AI_SOURCES}
)
target_include_directories(test_ai_worker PRIVATE ${AI_INCLUDES})
chess_ai_target_hash_bits(test_ai_worker 12)
add_test(NAME ai_worker COMMAND test_ai_worker)

add_executable(test_pst
  test_pst.c
  ${CORE_SOURCES}
  ${ENGINE_DIR}/mcu-max.c
  ${ENGINE_DIR}/mcumax_hash_scramble_table.c
)
target_include_directories(test_pst PRIVATE ${CORE_DIR}/include ${ENGINE_DIR})
target_compile_definitions(test_pst PRIVATE MCUMAX_EXPOSE_EVAL)
chess_ai_target_hash_bits(test_pst 12)
add_test(NAME pst COMMAND test_pst)

add_executable(test_opening_book
  test_opening_book.c
  ${CORE_SOURCES}
  ${CORE_DIR}/chess_game.c
  ${AI_SOURCES}
)
target_include_directories(test_opening_book PRIVATE ${AI_INCLUDES})
chess_ai_target_hash_bits(test_opening_book 12)
add_test(NAME opening_book COMMAND test_opening_book)
set_tests_properties(opening_book PROPERTIES TIMEOUT 600)

add_executable(test_mcumax_hash
  test_mcumax_hash.c
  ${CORE_SOURCES}
  ${CORE_DIR}/chess_game.c
  ${AI_SOURCES}
)
target_include_directories(test_mcumax_hash PRIVATE ${AI_INCLUDES})
chess_ai_target_hash_bits(test_mcumax_hash 12)
add_test(NAME mcumax_hash COMMAND test_mcumax_hash)

add_executable(test_mcumax_hash_off
  test_mcumax_hash.c
  ${CORE_SOURCES}
  ${CORE_DIR}/chess_game.c
  ${AI_SOURCES}
)
target_include_directories(test_mcumax_hash_off PRIVATE ${AI_INCLUDES})
chess_ai_target_hash_bits(test_mcumax_hash_off 0)
add_test(NAME mcumax_hash_off COMMAND test_mcumax_hash_off)

add_executable(test_ai_fen_reload
  test_ai_fen_reload.c
  ${CORE_SOURCES}
  ${CORE_DIR}/chess_game.c
  ${AI_SOURCES}
)
target_include_directories(test_ai_fen_reload PRIVATE ${AI_INCLUDES})
chess_ai_target_hash_bits(test_ai_fen_reload 12)
add_test(NAME ai_fen_reload COMMAND test_ai_fen_reload)
add_executable(test_ui_model test_ui_model.c ${CORE_SOURCES}
  ${MODEL_DIR}/chess_ui_model.c)
target_include_directories(test_ui_model PRIVATE
  ${CORE_DIR}/include ${MODEL_DIR}/include)

add_executable(test_i18n test_i18n.c ${I18N_DIR}/chess_i18n.c)
target_include_directories(test_i18n PRIVATE ${I18N_DIR}/include)

set(POWER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../components/chess_power)
add_executable(test_power test_power.c ${POWER_DIR}/chess_power.c)
target_include_directories(test_power PRIVATE ${POWER_DIR}/include)
add_test(NAME power COMMAND test_power)

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
add_test(NAME i18n COMMAND test_i18n)
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

# Real application state transitions; doubles only at ESP/LVGL/task boundaries.
add_executable(test_app test_app.c ${CORE_SOURCES} ${CORE_DIR}/chess_game.c
  ${MODEL_DIR}/chess_ui_model.c ${I18N_DIR}/chess_i18n.c
  ${POWER_DIR}/chess_power.c
  ${STORAGE_DIR}/chess_codec.c ${STORAGE_DIR}/chess_nvs.c
  ${AI_SOURCES})
target_include_directories(test_app PRIVATE app_stubs
  ../../main ../../components/bsp/include ${CORE_DIR}/include
  ${POWER_DIR}/include
  ${MODEL_DIR}/include ${I18N_DIR}/include ${STORAGE_DIR}/include
  ${AI_INCLUDES})
chess_ai_target_hash_bits(test_app 12)
add_test(NAME app COMMAND test_app)

add_executable(test_board_layout test_board_layout.c)
target_include_directories(test_board_layout PRIVATE ../../main ${CORE_DIR}/include)
add_test(NAME board_layout COMMAND test_board_layout)

add_executable(test_board_render test_board_render.c ${CORE_SOURCES})
target_include_directories(test_board_render PRIVATE app_stubs ../../main
  ${CORE_DIR}/include ${I18N_DIR}/include)
add_test(NAME board_render COMMAND test_board_render)

# ADC wake-pad preparation before the hardware deep-sleep transition.
add_executable(test_bsp_power test_bsp_power.c)
target_include_directories(test_bsp_power PRIVATE power_stubs app_stubs
  ../../components/bsp/include)
add_test(NAME bsp_power COMMAND test_bsp_power)
