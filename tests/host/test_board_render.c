/* Capture the real renderer's draw commands into a small host raster.
 * This checks our drawing/color contract, not LVGL's antialiasing. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../../main/chess_board_draw.c"

static uint32_t pixels[320][240];
static bool touched[320][240];
static void pixel(int x,int y,uint32_t color) {
    assert(x>=0 && x<240 && y>=0 && y<320);
    pixels[y][x]=color; touched[y][x]=true;
}
void lv_draw_rect(lv_layer_t *layer,const lv_draw_rect_dsc_t *d,const lv_area_t *a) {
    (void)layer;
    for(int y=a->y1;y<=a->y2;y++) for(int x=a->x1;x<=a->x2;x++) {
        bool border=d->border_opa && (x<a->x1+d->border_width || x>a->x2-d->border_width || y<a->y1+d->border_width || y>a->y2-d->border_width);
        if(border) pixel(x,y,d->border_color);
        else if(d->bg_opa) pixel(x,y,d->bg_color);
    }
}
void lv_draw_arc(lv_layer_t *layer,const lv_draw_arc_dsc_t *d) {
    (void)layer; int r=d->radius;int inner=r-d->width;if(inner<0)inner=0;
    for(int y=-r;y<=r;y++) for(int x=-r;x<=r;x++) {
        int dist=x*x+y*y;
        if(dist<=r*r && dist>=inner*inner) pixel(d->center.x+x,d->center.y+y,d->color);
    }
}
void lv_draw_line(lv_layer_t *layer,const lv_draw_line_dsc_t *d) {
    (void)layer;
    int dx=d->p2.x-d->p1.x,dy=d->p2.y-d->p1.y;
    int steps=abs(dx)>abs(dy)?abs(dx):abs(dy);if(!steps)steps=1;
    for(int i=0;i<=steps;i++) {
        int cx=d->p1.x+dx*i/steps,cy=d->p1.y+dy*i/steps;
        for(int y=-(d->width/2);y<=(d->width-1)/2;y++)
            for(int x=-(d->width/2);x<=(d->width-1)/2;x++) pixel(cx+x,cy+y,d->color);
    }
}
static void test_color_dominates_every_piece(void) {
    for(unsigned color=CHESS_WHITE;color<=CHESS_BLACK;color++)
        for(unsigned piece=CHESS_PAWN;piece<=CHESS_KING;piece++) {
            memset(touched,0,sizeof(touched));memset(&s_snap,0,sizeof(s_snap));
            s_snap.cells[0]=(chess_piece){.type=piece,.color=color};
            draw_piece(NULL,0);
            unsigned body=0,edge=0;
            for(int y=240;y<270;y++)for(int x=0;x<30;x++) if(touched[y][x]) {
                if(pixels[y][x]==(color==CHESS_WHITE?PIECE_W:PIECE_B)) body++;
                else edge++;
            }
            fprintf(stderr,"piece=%u color=%u body=%u edge=%u\n",piece,color,body,edge);
            assert(body>edge); /* White must look white; Black must look black. */
        }
}
static void preview(const char *path,chess_color bottom) {
    chess_position pos;
    assert(chess_position_from_fen(&pos,"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")==CHESS_OK);
    memset(&s_snap,0,sizeof(s_snap));memcpy(s_snap.cells,pos.board,sizeof(pos.board));
    s_snap.bottom=bottom;s_snap.selected=CHESS_NO_SQUARE;s_snap.hover=CHESS_NO_SQUARE;s_snap.check=CHESS_NO_SQUARE;s_snap.last_from=CHESS_NO_SQUARE;
    memset(pixels,0,sizeof(pixels));lv_event_t event={0};board_draw_cb(&event);
    FILE *file=fopen(path,"wb");assert(file);fprintf(file,"P6\n240 320\n255\n");
    for(int y=0;y<320;y++)for(int x=0;x<240;x++) {
        unsigned char rgb[3]={pixels[y][x]>>16,pixels[y][x]>>8,pixels[y][x]};fwrite(rgb,1,3,file);
    }
    fclose(file);
}
int main(int argc,char **argv) {
    if(argc==3) { preview(argv[1],CHESS_WHITE);preview(argv[2],CHESS_BLACK); }
    test_color_dominates_every_piece();
    puts("PASS: all six piece bodies have the correct dominant color");return 0;
}
