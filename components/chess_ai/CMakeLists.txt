idf_component_register(
    SRCS "chess_ai.c"
         "opening_book.c"
         "../../third_party/mcu-max/mcu-max.c"
         "../../third_party/mcu-max/mcumax_hash_scramble_table.c"
    INCLUDE_DIRS "include"
                 "."
                 "../../third_party/mcu-max"
    REQUIRES chess_core
)

# 4096-entry TT when heap allows (see chess_ai_engine_init); never 1024.
target_compile_definitions(${COMPONENT_LIB} PRIVATE MCUMAX_HASH_BITS=12)
# Vendored engine: warnings shown but never fatal here.
# -mstrict-align keeps a memcpy/byte scramble load from becoming a
# misaligned lw on the ESP32-C3 (rv32imc).
set(_mcumax_opts -w)
if(CONFIG_IDF_TARGET_ESP32C3)
  list(APPEND _mcumax_opts -mstrict-align)
endif()
set_source_files_properties(
    ../../third_party/mcu-max/mcu-max.c
    PROPERTIES COMPILE_OPTIONS "${_mcumax_opts}"
)
