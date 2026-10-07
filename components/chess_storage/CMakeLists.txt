idf_component_register(
    SRCS "chess_codec.c"
         "chess_nvs.c"
    INCLUDE_DIRS "include"
    REQUIRES chess_core
)
