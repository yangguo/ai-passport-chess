/* Application owner (Task 8): exactly one chess_game, one UI model,
 * one LVGL snapshot family, one NVS save slot pair. Button callbacks
 * only enqueue; this task serializes game transitions, model reduce,
 * result checks and saves. BSP LONG followed by a release CLICK is
 * suppressed here (one physical press, one logical action).
 */
#ifndef CHESS_APP_H
#define CHESS_APP_H

#include "bsp_button.h"

/* Enter the chess application; called once from app_main with LVGL
 * ready. Does not return while a game session lives. */
void chess_app_start(void);

/* ISR-safe button entry for bsp_button_init: enqueue only. */
void chess_app_button(bsp_btn_t btn, bsp_btn_ev_t ev, void *user);

#endif /* CHESS_APP_H */
