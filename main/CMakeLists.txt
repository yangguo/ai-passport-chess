idf_component_register(
    SRCS "app_main.c"
         "chess_app.c"
         "chess_ui.c"
         "chess_board_draw.c"
         "chess_nvs_esp.c"
    INCLUDE_DIRS "."
    REQUIRES bsp chess_core chess_storage chess_ui_model esp_timer nvs_flash
)
