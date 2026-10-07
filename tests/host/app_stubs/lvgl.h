#pragma once
typedef struct lv_obj lv_obj_t;
typedef struct { int unused; } lv_font_t;
extern const lv_font_t lv_font_montserrat_14, lv_font_montserrat_20;

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
typedef uint32_t lv_color_t;
typedef struct { int32_t x, y; } lv_point_t;
typedef struct { int32_t x1, y1, x2, y2; } lv_area_t;
typedef struct { int unused; } lv_layer_t;
typedef struct { lv_layer_t *layer; } lv_event_t;
typedef struct { int bg_opa, border_opa, border_width, radius; lv_color_t bg_color, border_color; } lv_draw_rect_dsc_t;
typedef struct { int width, opa, radius, start_angle, end_angle; lv_point_t center; lv_color_t color; } lv_draw_arc_dsc_t;
typedef struct { int width, opa, round_end, round_start; lv_point_t p1, p2; lv_color_t color; } lv_draw_line_dsc_t;
#define LV_OPA_COVER 255
#define LV_OPA_TRANSP 0
#define LV_OBJ_FLAG_SCROLLABLE 1
#define LV_OBJ_FLAG_CLICKABLE 2
#define LV_EVENT_DRAW_MAIN 1
static inline lv_color_t lv_color_hex(uint32_t color) { return color; }
static inline lv_color_t lv_color_mix(lv_color_t a, lv_color_t b, unsigned weight) {
    uint32_t out = 0;
    for (unsigned i=0; i<3; i++) {
        unsigned shift=i*8;
        out |= (((((a>>shift)&255)*weight+((b>>shift)&255)*(255-weight))/255)&255)<<shift;
    }
    return out;
}
static inline void lv_draw_rect_dsc_init(lv_draw_rect_dsc_t *d) { memset(d,0,sizeof(*d)); }
static inline void lv_draw_arc_dsc_init(lv_draw_arc_dsc_t *d) { memset(d,0,sizeof(*d)); }
static inline void lv_draw_line_dsc_init(lv_draw_line_dsc_t *d) { memset(d,0,sizeof(*d)); }
void lv_draw_rect(lv_layer_t *layer,const lv_draw_rect_dsc_t *d,const lv_area_t *area);
void lv_draw_arc(lv_layer_t *layer,const lv_draw_arc_dsc_t *d);
void lv_draw_line(lv_layer_t *layer,const lv_draw_line_dsc_t *d);
static inline lv_layer_t *lv_event_get_layer(lv_event_t *e) { return e->layer; }
static inline lv_obj_t *lv_obj_create(lv_obj_t *parent) { return parent; }
static inline void lv_obj_set_size(lv_obj_t *o,int w,int h) { (void)o;(void)w;(void)h; }
static inline void lv_obj_set_pos(lv_obj_t *o,int x,int y) { (void)o;(void)x;(void)y; }
static inline void lv_obj_set_style_pad_all(lv_obj_t *o,int v,int sel) { (void)o;(void)v;(void)sel; }
#define lv_obj_set_style_border_width lv_obj_set_style_pad_all
#define lv_obj_set_style_radius lv_obj_set_style_pad_all
#define lv_obj_set_style_bg_opa lv_obj_set_style_pad_all
static inline void lv_obj_remove_flag(lv_obj_t *o,int flag) { (void)o;(void)flag; }
static inline void lv_obj_add_event_cb(lv_obj_t *o,void (*cb)(lv_event_t*),int event,void *user) { (void)o;(void)cb;(void)event;(void)user; }
static inline void lv_obj_invalidate(lv_obj_t *o) { (void)o; }
