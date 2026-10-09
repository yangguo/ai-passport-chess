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
set_source_files_properties(
    ../../third_party/mcu-max/mcu-max.c
    PROPERTIES COMPILE_OPTIONS "-w"
)
