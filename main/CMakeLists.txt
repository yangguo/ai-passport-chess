idf_component_register(
    SRCS "app_main.c"
         "chess_app.c"
         "chess_ui.c"
         "chess_board_draw.c"
         "chess_nvs_esp.c"
         "chess_ai_task.c"
         "../assets/fonts/chess_zh_18.c"
    INCLUDE_DIRS "."
    REQUIRES bsp chess_core chess_storage chess_ui_model chess_ai chess_i18n chess_power esp_timer nvs_flash
)
