/* Screen builder: delete-and-rebuild per render keeps lifecycle
 * trivial (no stale objects across game states). Caller holds the
 * LVGL lock. */
#include "chess_ui.h"

#include <stdio.h>

#include "chess_font.h"

static lv_obj_t *s_screen;

static void clear_screen(void) {
    if (s_screen != NULL) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, 240, 320);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
}

static lv_obj_t *add_label(lv_obj_t *parent, const char *text, int y,
                           const lv_font_t *font, uint32_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, 240);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, 0, y);
    return label;
}

static void render_board(const chess_view *view) {
    clear_screen();
    add_label(s_screen, view->header, 6, chess_font_ui(), 0xFAFAFA);
    chess_board_create(s_screen, &view->snapshot);
    add_label(s_screen, view->footer, 274, chess_font_ui(), 0xFAFAFA);
    lv_screen_load(s_screen);
}

static void render_menu(const chess_view *view, const char *title) {
    unsigned i;
    clear_screen();
    add_label(s_screen, title, 40, chess_font_title(), 0xFAFAFA);
    for (i = 0; i < view->nmenu && i < CHESS_MENU_MAX; i++) {
        lv_obj_t *row = add_label(s_screen, view->menu[i],
                                  90 + (int)i * 30, chess_font_ui(),
                                  i == view->menu_idx ? 0xFFB300 : 0xFAFAFA);
        (void)row;
    }
    lv_screen_load(s_screen);
}

static void render_confirm(const chess_view *view) {
    clear_screen();
    add_label(s_screen, view->confirm_action, 90, chess_font_ui(), 0xFAFAFA);
    add_label(s_screen, view->confirm_yes ? "> Confirm" : "  Confirm", 130,
              chess_font_ui(), view->confirm_yes ? 0xFFB300 : 0xFAFAFA);
    add_label(s_screen, !view->confirm_yes ? "> Cancel" : "  Cancel", 160,
              chess_font_ui(), !view->confirm_yes ? 0xFFB300 : 0xFAFAFA);
    lv_screen_load(s_screen);
}

static void render_over(const chess_view *view) {
    clear_screen();
    add_label(s_screen, view->over, 100, chess_font_title(), 0xFAFAFA);
    add_label(s_screen, "OK: new game", 160, chess_font_ui(), 0xFAFAFA);
    add_label(s_screen, "LONG: home", 190, chess_font_ui(), 0xFAFAFA);
    lv_screen_load(s_screen);
}

static void render_error(const chess_view *view) {
    char line[64];
    clear_screen();
    snprintf(line, sizeof(line), "Error 0x%04X", view->error_code);
    add_label(s_screen, line, 90, chess_font_title(), 0xC62828);
    add_label(s_screen, "OK: retry   LONG: home", 150, chess_font_ui(),
              0xFAFAFA);
    lv_screen_load(s_screen);
}

static void render_home(const chess_view *view) {
    (void)view;
    clear_screen();
    add_label(s_screen, "CHESS", 90, chess_font_title(), 0xFAFAFA);
    add_label(s_screen, "OK: continue   LONG: new", 150, chess_font_ui(),
              0xFAFAFA);
    lv_screen_load(s_screen);
}

void chess_ui_render(const chess_view *view) {
    switch (view->screen) {
    case CHESS_VIEW_BOARD:
        render_board(view);
        break;
    case CHESS_VIEW_PAUSE:
        render_menu(view, "PAUSE");
        break;
    case CHESS_VIEW_CONFIRM:
        render_confirm(view);
        break;
    case CHESS_VIEW_OVER:
        render_over(view);
        break;
    case CHESS_VIEW_ERROR:
        render_error(view);
        break;
    case CHESS_VIEW_HOME:
        render_home(view);
        break;
    }
}
