/* Chess UI font abstraction (Task 8).
 * English Montserrat now; a Chinese subset font plugs in here before
 * the V0 acceptance gate (TODO i18n). All UI text goes through these
 * accessors so the swap is one file + assets, never a UI rewrite. */
#ifndef CHESS_FONT_H
#define CHESS_FONT_H

#include "lvgl.h"

static inline const lv_font_t *chess_font_title(void) {
    return &lv_font_montserrat_20;
}

static inline const lv_font_t *chess_font_ui(void) {
    return &lv_font_montserrat_14;
}

#endif /* CHESS_FONT_H */
