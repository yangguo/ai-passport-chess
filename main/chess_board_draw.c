/* Procedural board renderer. Colors: light cream, slate-blue dark;
 * white pieces bright fill + dark outline, black pieces dark fill +
 * bright outline; focus = high-contrast frame, targets = dots,
 * captures = rings, checked king = warning frame (state is also in
 * the footer text, never color-only). */
#include "chess_board_draw.h"

#include "chess_font.h"

#define LIGHT_SQ 0xEDE6D0
#define DARK_SQ 0x5B7290
#define PIECE_W 0xFAFAFA
#define PIECE_W_LINE 0x1A1A1A
#define PIECE_B 0x1A1A1A
#define PIECE_B_LINE 0xFAFAFA
#define FOCUS_COLOR 0xFFB300
#define TARGET_COLOR 0x223344
#define CHECK_COLOR 0xC62828
#define LASTMOVE_TINT 0xFFF176

static lv_obj_t *s_board;
static chess_snapshot s_snap;

static int cell_x(unsigned sq) {
    return CHESS_BOARD_X + (int)(sq % 8u) * CHESS_BOARD_CELL;
}

static int cell_y(unsigned sq) {
    return CHESS_BOARD_Y + (int)(7u - sq / 8u) * CHESS_BOARD_CELL;
}

static void draw_square(lv_layer_t *layer, unsigned sq) {
    lv_draw_rect_dsc_t dsc;
    lv_area_t area;
    bool light = ((sq % 8u) + (sq / 8u)) % 2u == 0u;
    bool last = (sq == s_snap.last_from || sq == s_snap.last_to) &&
                s_snap.last_from != CHESS_NO_SQUARE;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.bg_color = lv_color_hex(light ? LIGHT_SQ : DARK_SQ);
    if (last) {
        dsc.bg_color = lv_color_mix(lv_color_hex(LASTMOVE_TINT),
                                    dsc.bg_color, 110);
    }
    area.x1 = (int32_t)cell_x(sq);
    area.y1 = (int32_t)cell_y(sq);
    area.x2 = area.x1 + CHESS_BOARD_CELL - 1;
    area.y2 = area.y1 + CHESS_BOARD_CELL - 1;
    lv_draw_rect(layer, &dsc, &area);
}

static void draw_frame(lv_layer_t *layer, unsigned sq, uint32_t color,
                       int32_t width) {
    lv_draw_rect_dsc_t dsc;
    lv_area_t area;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_TRANSP;
    dsc.border_opa = LV_OPA_COVER;
    dsc.border_color = lv_color_hex(color);
    dsc.border_width = width;
    dsc.radius = 2;
    area.x1 = (int32_t)cell_x(sq) + 2;
    area.y1 = (int32_t)cell_y(sq) + 2;
    area.x2 = area.x1 + CHESS_BOARD_CELL - 5;
    area.y2 = area.y1 + CHESS_BOARD_CELL - 5;
    lv_draw_rect(layer, &dsc, &area);
}

static void draw_dot(lv_layer_t *layer, unsigned sq) {
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = lv_color_hex(TARGET_COLOR);
    dsc.width = 5;
    dsc.opa = LV_OPA_COVER;
    dsc.center.x = (int32_t)cell_x(sq) + CHESS_BOARD_CELL / 2;
    dsc.center.y = (int32_t)cell_y(sq) + CHESS_BOARD_CELL / 2;
    dsc.radius = 3;
    dsc.start_angle = 0;
    dsc.end_angle = 360;
    lv_draw_arc(layer, &dsc);
}

static void draw_ring(lv_layer_t *layer, unsigned sq) {
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = lv_color_hex(TARGET_COLOR);
    dsc.width = 3;
    dsc.opa = LV_OPA_COVER;
    dsc.center.x = (int32_t)cell_x(sq) + CHESS_BOARD_CELL / 2;
    dsc.center.y = (int32_t)cell_y(sq) + CHESS_BOARD_CELL / 2;
    dsc.radius = 11;
    dsc.start_angle = 0;
    dsc.end_angle = 360;
    lv_draw_arc(layer, &dsc);
}

static void draw_line(lv_layer_t *layer, int x1, int y1, int x2, int y2,
                      uint32_t color, int32_t width) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_hex(color);
    dsc.width = width;
    dsc.round_end = 1;
    dsc.round_start = 1;
    dsc.opa = LV_OPA_COVER;
    dsc.p1.x = x1;
    dsc.p1.y = y1;
    dsc.p2.x = x2;
    dsc.p2.y = y2;
    lv_draw_line(layer, &dsc);
}

static void draw_disc(lv_layer_t *layer, int cx, int cy, int r,
                       uint32_t color) {
    lv_draw_arc_dsc_t dsc;
    int32_t width;
    if (r < 2) {
        r = 2;
    }
    width = (int32_t)(r * 2 - 1);
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = lv_color_hex(color);
    dsc.width = width;
    dsc.opa = LV_OPA_COVER;
    dsc.center.x = cx;
    dsc.center.y = cy;
    dsc.radius = (uint16_t)(r - 1);
    dsc.start_angle = 0;
    dsc.end_angle = 360;
    lv_draw_arc(layer, &dsc);
}

static void draw_ball(lv_layer_t *layer, int cx, int cy, int r, uint32_t fill,
                      uint32_t line) {
    if (r < 3) {
        draw_disc(layer, cx, cy, r, fill);
        return;
    }
    draw_disc(layer, cx, cy, r, line);
    draw_disc(layer, cx, cy, r - 1, fill);
}

/* Compact Staunton-inspired silhouettes sized for one 30px cell. */
static void draw_piece(lv_layer_t *layer, unsigned sq) {
    chess_piece p = s_snap.cells[sq];
    uint32_t fill;
    uint32_t line;
    int ox = cell_x(sq);
    int oy = cell_y(sq);
    int cx = ox + 15;
    if (p.type == CHESS_EMPTY) {
        return;
    }
    if (p.color == CHESS_WHITE) {
        fill = PIECE_W;
        line = PIECE_W_LINE;
    } else {
        fill = PIECE_B;
        line = PIECE_B_LINE;
    }
    switch (p.type) {
    case CHESS_PAWN: {
        /* Rounded head, collar, tapered stem and stepped pedestal. */
        draw_ball(layer, cx, oy + 8, 4, fill, line);
        draw_line(layer, cx - 3, oy + 13, cx + 3, oy + 13, line, 3);
        draw_line(layer, cx - 2, oy + 15, cx + 2, oy + 15, line, 3);
        draw_line(layer, cx - 2, oy + 15, cx - 4, oy + 21, line, 3);
        draw_line(layer, cx + 2, oy + 15, cx + 4, oy + 21, line, 3);
        draw_line(layer, cx - 6, oy + 21, cx + 6, oy + 21, line, 3);
        draw_line(layer, cx - 7, oy + 24, cx + 7, oy + 24, line, 3);
        break;
    }
    case CHESS_KNIGHT: {
        /* Horse profile facing right: ears, brow, muzzle, jaw and neck. */
        draw_line(layer, cx - 5, oy + 21, cx - 5, oy + 13, line, 4);
        draw_line(layer, cx - 5, oy + 13, cx - 8, oy + 8, line, 4);
        draw_line(layer, cx - 8, oy + 8, cx - 4, oy + 5, line, 4);
        draw_line(layer, cx - 4, oy + 5, cx - 1, oy + 8, line, 4);
        draw_line(layer, cx - 1, oy + 8, cx + 5, oy + 8, line, 4);
        draw_line(layer, cx + 5, oy + 8, cx + 8, oy + 10, line, 4);
        draw_line(layer, cx + 8, oy + 10, cx + 6, oy + 13, line, 4);
        draw_line(layer, cx + 6, oy + 13, cx + 2, oy + 14, line, 4);
        draw_line(layer, cx + 2, oy + 14, cx + 1, oy + 18, line, 4);
        draw_line(layer, cx + 1, oy + 18, cx + 5, oy + 21, line, 4);
        draw_line(layer, cx - 5, oy + 15, cx + 1, oy + 19, fill, 2);
        draw_disc(layer, cx + 2, oy + 10, 1, line);
        draw_line(layer, cx - 7, oy + 22, cx + 7, oy + 22, line, 3);
        draw_line(layer, cx - 8, oy + 25, cx + 8, oy + 25, line, 3);
        break;
    }
    case CHESS_BISHOP: {
        /* Mitre point, rounded shoulders and the traditional diagonal slit. */
        draw_ball(layer, cx, oy + 5, 2, fill, line);
        draw_line(layer, cx, oy + 7, cx - 6, oy + 14, line, 4);
        draw_line(layer, cx, oy + 7, cx + 6, oy + 14, line, 4);
        draw_line(layer, cx - 6, oy + 14, cx - 3, oy + 19, line, 4);
        draw_line(layer, cx + 6, oy + 14, cx + 3, oy + 19, line, 4);
        draw_line(layer, cx - 3, oy + 19, cx + 3, oy + 19, line, 4);
        draw_line(layer, cx - 2, oy + 11, cx + 2, oy + 15, fill, 2);
        draw_line(layer, cx - 6, oy + 21, cx + 6, oy + 21, line, 3);
        draw_line(layer, cx - 7, oy + 24, cx + 7, oy + 24, line, 3);
        break;
    }
    case CHESS_ROOK: {
        /* Castle tower with three merlons, narrowing shaft and broad base. */
        draw_line(layer, cx - 7, oy + 5, cx - 3, oy + 5, line, 4);
        draw_line(layer, cx + 3, oy + 5, cx + 7, oy + 5, line, 4);
        draw_line(layer, cx - 1, oy + 5, cx + 1, oy + 5, line, 4);
        draw_line(layer, cx - 7, oy + 5, cx - 7, oy + 9, line, 3);
        draw_line(layer, cx - 2, oy + 5, cx - 2, oy + 9, line, 3);
        draw_line(layer, cx + 2, oy + 5, cx + 2, oy + 9, line, 3);
        draw_line(layer, cx + 7, oy + 5, cx + 7, oy + 9, line, 3);
        draw_line(layer, cx - 7, oy + 9, cx - 5, oy + 19, line, 3);
        draw_line(layer, cx + 7, oy + 9, cx + 5, oy + 19, line, 3);
        draw_line(layer, cx - 5, oy + 14, cx + 5, oy + 14, fill, 2);
        draw_line(layer, cx - 6, oy + 20, cx + 6, oy + 20, line, 3);
        draw_line(layer, cx - 8, oy + 24, cx + 8, oy + 24, line, 3);
        break;
    }
    case CHESS_QUEEN: {
        /* Crown with five points and a flared, ringed coronet. */
        draw_ball(layer, cx - 7, oy + 6, 2, fill, line);
        draw_ball(layer, cx, oy + 4, 2, fill, line);
        draw_ball(layer, cx + 7, oy + 6, 2, fill, line);
        draw_line(layer, cx - 7, oy + 8, cx - 5, oy + 17, line, 3);
        draw_line(layer, cx, oy + 6, cx - 3, oy + 17, line, 3);
        draw_line(layer, cx, oy + 6, cx + 3, oy + 17, line, 3);
        draw_line(layer, cx + 7, oy + 8, cx + 5, oy + 17, line, 3);
        draw_line(layer, cx - 5, oy + 17, cx + 5, oy + 17, line, 3);
        draw_line(layer, cx - 6, oy + 20, cx + 6, oy + 20, line, 3);
        draw_line(layer, cx - 8, oy + 24, cx + 8, oy + 24, line, 3);
        break;
    }
    case CHESS_KING: {
        /* Cross atop a taller, rounded crown and stepped royal base. */
        draw_line(layer, cx, oy + 4, cx, oy + 11, line, 3);
        draw_line(layer, cx - 3, oy + 7, cx + 3, oy + 7, line, 3);
        draw_line(layer, cx - 4, oy + 13, cx - 6, oy + 19, line, 3);
        draw_line(layer, cx + 4, oy + 13, cx + 6, oy + 19, line, 3);
        draw_line(layer, cx - 4, oy + 13, cx + 4, oy + 13, line, 3);
        draw_line(layer, cx - 6, oy + 19, cx + 6, oy + 19, line, 3);
        draw_line(layer, cx - 7, oy + 22, cx + 7, oy + 22, line, 3);
        draw_line(layer, cx - 8, oy + 25, cx + 8, oy + 25, line, 3);
        break;
    }
    default:
        break;
    }
}

static bool is_target(unsigned sq) {
    unsigned i;
    for (i = 0; i < s_snap.ntargets; i++) {
        if (s_snap.targets[i] == sq) {
            return true;
        }
    }
    return false;
}

static void board_draw_cb(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    unsigned sq;
    (void)e;
    for (sq = 0; sq < 64u; sq++) {
        draw_square(layer, sq);
    }
    for (sq = 0; sq < 64u; sq++) {
        draw_piece(layer, sq);
    }
    for (sq = 0; sq < 64u; sq++) {
        if (is_target(sq)) {
            if (s_snap.cells[sq].type == CHESS_EMPTY) {
                draw_dot(layer, sq);
            } else {
                draw_ring(layer, sq);
            }
        }
    }
    if (s_snap.selected != CHESS_NO_SQUARE) {
        draw_frame(layer, s_snap.selected, FOCUS_COLOR, 2);
    }
    if (s_snap.hover != CHESS_NO_SQUARE) {
        draw_frame(layer, s_snap.hover, FOCUS_COLOR, 3);
    }
    if (s_snap.check != CHESS_NO_SQUARE) {
        draw_frame(layer, s_snap.check, CHECK_COLOR, 3);
    }
}

lv_obj_t *chess_board_create(lv_obj_t *parent, const chess_snapshot *snap) {
    s_snap = *snap;
    s_board = lv_obj_create(parent);
    lv_obj_set_size(s_board, 240, 240);
    lv_obj_set_pos(s_board, CHESS_BOARD_X, CHESS_BOARD_Y);
    lv_obj_set_style_pad_all(s_board, 0, 0);
    lv_obj_set_style_border_width(s_board, 0, 0);
    lv_obj_set_style_radius(s_board, 0, 0);
    lv_obj_set_style_bg_opa(s_board, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(s_board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_board, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_board, board_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    return s_board;
}

void chess_board_update(const chess_snapshot *snap) {
    s_snap = *snap;
    if (s_board != NULL) {
        lv_obj_invalidate(s_board);
    }
}
