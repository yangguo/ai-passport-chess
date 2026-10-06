idf_component_register(
    SRCS "chess_ai.c"
         "../../third_party/mcu-max/mcu-max.c"
    INCLUDE_DIRS "include"
                 "../../third_party/mcu-max"
    REQUIRES chess_core
)
# Vendored engine: warnings shown but never fatal here.
set_source_files_properties(
    ../../third_party/mcu-max/mcu-max.c
    PROPERTIES COMPILE_OPTIONS "-w"
)
