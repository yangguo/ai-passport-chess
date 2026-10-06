/* Engine black-box driver (host CLI): spawn the console engine, feed
 * coordinate moves, read exact 72-byte board prints, extract the reply
 * by diff, and validate everything against the core before applying.
 *
 * Protocol (observed from umax4_8.c, NOT assumed):
 * - one exchange = one printed board (8 lines x 8 chars + '\n');
 * - feeding our move echoes it; feeding an empty line makes the side
 *   to move play (up to ~1M nodes, i.e. seconds);
 * - stdin EOF makes the engine loop forever: never EOF the pipe,
 *   always kill the child on detach/quit, always read with timeout.
 * Board alphabet: white RNBQKBN + '*' pawn, black rnbqkbn + '+'
 * pawn, '.' empty, rank 8 first. Anything else is a desync.
 *
 * Transport note: the engine never flushes except per newline, so a
 * pipe keeps every board print stuck in its block buffer (observed:
 * 120 s reads with zero bytes). Spawn uses a pty in raw mode instead:
 * the engine then sees a tty, line-buffers, and every byte arrives
 * untranslated. Never EOF the child side; always kill on detach.
 */
#ifndef CHESS_ENGINE_H
#define CHESS_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "chess_core.h"

#define CHESS_ENGINE_BOARD_LEN 72u
#define CHESS_ENGINE_READ_TIMEOUT_SEC 120

typedef struct chess_engine {
  pid_t pid;
  int fd; /* pty master, bidirectional */
  bool live;
} chess_engine;

/* Parse one 72-byte board print into core pieces (a1 = index 0).
 * Strict alphabet; anything else is an error (likely desync). */
int chess_engine_parse_board(const char *text, size_t len,
                             chess_piece board[64]);

/* Extract the mover's move between two placements: normal, capture,
 * castling (king stride 2), en passant (3 changed squares), and
 * promotion (always queen: the engine never underpromotes). The
 * result is UNVALIDATED: dry-run it with chess_engine_try_apply. */
int chess_engine_diff_move(const chess_piece prev[64],
                           const chess_piece cur[64], chess_color mover,
                           chess_move *out);

/* Dry-run a move on a position copy and check the resulting placement
 * equals `want`. 0 = applicable and matching, nonzero otherwise. */
int chess_engine_try_apply(const chess_position *pos, chess_move m,
                           const chess_piece want[64]);

/* Process plumbing (POSIX). read_board reads exactly one board print
 * or fails (timeout/crash/short). kill reaps the child. */
int chess_engine_spawn(chess_engine *e, const char *path);
int chess_engine_send(chess_engine *e, const char *line);
int chess_engine_read_board(chess_engine *e, char text[80]);
void chess_engine_kill(chess_engine *e);

#endif /* CHESS_ENGINE_H */
