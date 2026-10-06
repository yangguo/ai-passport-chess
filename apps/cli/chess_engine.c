/* Engine black-box driver: strict parsing, validated diffs, POSIX
 * plumbing with timeouts. Live I/O (spawn/read) is covered by the
 * gated ai-live scripted test; parsing/diff/dry-run are unit-tested. */
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

#include "chess_core.h"
#include "chess_engine.h"

static int glyph_to_piece(char c, chess_piece *out) {
  out->color = CHESS_WHITE;
  switch (c) {
  case '.':
    out->type = CHESS_EMPTY;
    return 0;
  case '*':
    out->type = CHESS_PAWN;
    return 0;
  case 'P':
  case 'R':
  case 'N':
  case 'B':
  case 'Q':
  case 'K': {
    static const chess_piece_type kinds[26] = {
        ['B' - 'A'] = CHESS_BISHOP, ['K' - 'A'] = CHESS_KING,
        ['N' - 'A'] = CHESS_KNIGHT, ['P' - 'A'] = CHESS_PAWN,
        ['Q' - 'A'] = CHESS_QUEEN,  ['R' - 'A'] = CHESS_ROOK,
    };
    out->type = kinds[(unsigned)(c - 'A')];
    if (out->type == CHESS_EMPTY) {
      return -1;
    }
    return 0;
  }
  case '+':
    out->type = CHESS_PAWN;
    out->color = CHESS_BLACK;
    return 0;
  case 'p':
  case 'r':
  case 'n':
  case 'b':
  case 'q':
  case 'k': {
    chess_piece_type t = CHESS_EMPTY;
    switch (c) {
    case 'p':
      t = CHESS_PAWN;
      break;
    case 'r':
      t = CHESS_ROOK;
      break;
    case 'n':
      t = CHESS_KNIGHT;
      break;
    case 'b':
      t = CHESS_BISHOP;
      break;
    case 'q':
      t = CHESS_QUEEN;
      break;
    case 'k':
      t = CHESS_KING;
      break;
    default:
      break;
    }
    out->type = t;
    out->color = CHESS_BLACK;
    return 0;
  }
  default:
    return -1;
  }
}

int chess_engine_parse_board(const char *text, size_t len,
                             chess_piece board[64]) {
  unsigned row;
  unsigned f;
  if (text == NULL || board == NULL || len != CHESS_ENGINE_BOARD_LEN) {
    return -1;
  }
  for (row = 0; row < 8u; row++) {
    if (text[row * 9u + 8u] != '\n') {
      return -1;
    }
    for (f = 0; f < 8u; f++) {
      unsigned sq = (7u - row) * 8u + f;
      if (glyph_to_piece(text[row * 9u + f], &board[sq]) != 0) {
        return -1;
      }
    }
  }
  return 0;
}

static bool same_piece(chess_piece a, chess_piece b) {
  if (a.type != b.type) {
    return false;
  }
  return a.type == CHESS_EMPTY || a.color == b.color;
}

int chess_engine_diff_move(const chess_piece prev[64],
                           const chess_piece cur[64], chess_color mover,
                           chess_move *out) {
  unsigned changed[4];
  unsigned n = 0;
  unsigned sq;
  unsigned emptied = 0;
  unsigned from = 64;
  unsigned to = 64;
  unsigned i;
  if (prev == NULL || cur == NULL || out == NULL) {
    return -1;
  }
  for (sq = 0; sq < 64u; sq++) {
    if (!same_piece(prev[sq], cur[sq])) {
      if (n >= 4u) {
        return -1;
      }
      changed[n++] = sq;
    }
  }
  for (i = 0; i < n; i++) {
    if (cur[changed[i]].type == CHESS_EMPTY) {
      emptied++;
      from = changed[i];
    } else {
      to = changed[i];
    }
  }
  if (n == 2u && emptied == 1u) {
    /* Quiet move, capture, or promotion (engine always queens). */
    unsigned last = (mover == CHESS_WHITE) ? 7u : 0u;
    out->from = (uint8_t)from;
    out->to = (uint8_t)to;
    out->promotion = CHESS_EMPTY;
    if (prev[from].type == CHESS_PAWN && to / 8u == last &&
        cur[to].type == CHESS_QUEEN && cur[to].color == mover) {
      out->promotion = CHESS_QUEEN;
    }
    return 0;
  }
  if (n == 3u && emptied == 2u) {
    /* En passant: two squares emptied (pawn + victim), one new pawn.
     * `to` is already the single non-emptied square; the dry run is
     * the real check. */
    if (prev[from].type != CHESS_PAWN || cur[to].type != CHESS_PAWN ||
        cur[to].color != mover) {
      return -1;
    }
    out->from = (uint8_t)from;
    out->to = (uint8_t)to;
    out->promotion = CHESS_EMPTY;
    return 0;
  }
  if (n == 4u) {
    /* Castling: the king strides two squares. */
    for (i = 0; i < n; i++) {
      if (prev[changed[i]].type == CHESS_KING &&
          prev[changed[i]].color == mover &&
          cur[changed[i]].type == CHESS_EMPTY) {
        from = changed[i];
      }
      if (cur[changed[i]].type == CHESS_KING &&
          cur[changed[i]].color == mover) {
        to = changed[i];
      }
    }
    if (from < 64u && to < 64u &&
        (to == from + 2u || to + 2u == from)) {
      out->from = (uint8_t)from;
      out->to = (uint8_t)to;
      out->promotion = CHESS_EMPTY;
      return 0;
    }
    return -1;
  }
  return -1;
}

int chess_engine_try_apply(const chess_position *pos, chess_move m,
                           const chess_piece want[64]) {
  chess_position probe;
  chess_undo undo;
  unsigned sq;
  if (pos == NULL || want == NULL) {
    return -1;
  }
  probe = *pos;
  if (chess_make(&probe, m, &undo) != CHESS_OK) {
    return -1;
  }
  for (sq = 0; sq < 64u; sq++) {
    if (!same_piece(probe.board[sq], want[sq])) {
      return -1;
    }
  }
  return 0;
}

int chess_engine_spawn(chess_engine *e, const char *path) {
  pid_t pid;
  int fd = -1;
  if (e == NULL || path == NULL) {
    return -1;
  }
  e->live = false;
  pid = forkpty(&fd, NULL, NULL, NULL);
  if (pid < 0) {
    return -1;
  }
  if (pid == 0) {
    /* Raw slave: no echo (else our input pollutes reads), no
     * output postprocessing (else \n becomes \r\n). */
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t) == 0) {
      cfmakeraw(&t);
      tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
    execl(path, path, (char *)NULL);
    _exit(127);
  }
  e->pid = pid;
  e->fd = fd;
  e->live = true;
  return 0;
}

int chess_engine_send(chess_engine *e, const char *line) {
  size_t left;
  const char *p;
  if (e == NULL || !e->live) {
    return -1;
  }
  p = (line == NULL) ? "" : line;
  left = strlen(p);
  while (left > 0) {
    ssize_t w = write(e->fd, p, left);
    if (w <= 0) {
      return -1;
    }
    p += (size_t)w;
    left -= (size_t)w;
  }
  if (write(e->fd, "\n", 1) != 1) {
    return -1;
  }
  return 0;
}

int chess_engine_read_board(chess_engine *e, char text[80]) {
  size_t got = 0;
  if (e == NULL || !e->live || text == NULL) {
    return -1;
  }
  while (got < CHESS_ENGINE_BOARD_LEN) {
    fd_set set;
    struct timeval deadline;
    ssize_t r;
    FD_ZERO(&set);
    FD_SET(e->fd, &set);
    deadline.tv_sec = CHESS_ENGINE_READ_TIMEOUT_SEC;
    deadline.tv_usec = 0;
    if (select(e->fd + 1, &set, NULL, NULL, &deadline) <= 0) {
      return -1; /* timeout or error: caller kills the child */
    }
    r = read(e->fd, text + got, CHESS_ENGINE_BOARD_LEN - got);
    if (r <= 0) {
      return -1; /* EOF or crash */
    }
    got += (size_t)r;
  }
  text[CHESS_ENGINE_BOARD_LEN] = '\0';
  return 0;
}

void chess_engine_kill(chess_engine *e) {
  if (e == NULL || !e->live) {
    return;
  }
  e->live = false;
  kill(e->pid, SIGKILL);
  waitpid(e->pid, NULL, 0);
  close(e->fd);
}
