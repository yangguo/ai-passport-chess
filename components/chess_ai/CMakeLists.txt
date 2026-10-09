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

target_compile_definitions(${COMPONENT_LIB} PRIVATE MCUMAX_HASH_BITS=10)
# Vendored engine: warnings shown but never fatal here.
set_source_files_properties(
    ../../third_party/mcu-max/mcu-max.c
    PROPERTIES COMPILE_OPTIONS "-w"
)
