/* English UI uses the compact built-in font. Chinese uses a subset of
 * Noto Sans SC and falls back to Montserrat for Latin glyphs. */
#ifndef CHESS_FONT_H
#define CHESS_FONT_H

#include "lvgl.h"
#include "chess_i18n.h"

extern const lv_font_t chess_font_noto_sc_18;

static inline const lv_font_t *chess_font_title(void) {
    return &lv_font_montserrat_20;
}

static inline const lv_font_t *chess_font_ui(void) {
    return &lv_font_montserrat_14;
}

static inline const lv_font_t *chess_font_ui_for(chess_language language) {
    return language == CHESS_LANGUAGE_CHINESE ? &chess_font_noto_sc_18
                                               : &lv_font_montserrat_14;
}

static inline const lv_font_t *chess_font_title_for(chess_language language) {
    return language == CHESS_LANGUAGE_CHINESE ? &chess_font_noto_sc_18
                                               : &lv_font_montserrat_20;
}

#endif /* CHESS_FONT_H */
