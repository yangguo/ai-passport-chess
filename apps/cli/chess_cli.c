/* Host chess CLI (playable frontend): ASCII board, UCI moves, save/load,
 * claims, resignation, and in-process engine replies (`ai`) over the
 * tested core. Every engine suggestion is core-validated before it
 * lands; failures print an error and leave the game untouched.
 *
 * Line protocol (script-testable): `ok <detail>` on success,
 * `error <reason>` on failure; `quit`/EOF exits 0. Reasons:
 * bad-uci, illegal, unknown-command, no-save, corrupt, game-over,
 * no-claim, bad-fen, bad-file, bad-arg, engine-failed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chess_ai.h"
#include "chess_core.h"
#include "chess_storage.h"

#define START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

/* Engine search budget for `ai`: casual strength, seconds per move. */
#define CLI_AI_NODES 200000u
#define CLI_AI_DEPTH 4u

typedef struct cli_state {
  chess_game game;
  chess_settings settings;
  uint64_t seq;
} cli_state;

static const char *status_word(chess_status st) {
  switch (st) {
  case CHESS_STATUS_ONGOING:
    return "ongoing";
  case CHESS_STATUS_INVALID:
    return "invalid";
  case CHESS_STATUS_CHECKMATE_WHITE_WINS:
    return "checkmate-white-wins";
  case CHESS_STATUS_CHECKMATE_BLACK_WINS:
    return "checkmate-black-wins";
  case CHESS_STATUS_STALEMATE:
    return "stalemate";
  case CHESS_STATUS_DRAW_DEAD:
    return "draw-dead";
  case CHESS_STATUS_DRAW_FIVEFOLD:
    return "draw-fivefold";
  case CHESS_STATUS_DRAW_SEVENTY_FIVE:
    return "draw-75";
  case CHESS_STATUS_DRAW_CLAIMED_THREEFOLD:
    return "draw-claimed-threefold";
  case CHESS_STATUS_DRAW_CLAIMED_FIFTY:
    return "draw-claimed-fifty";
  case CHESS_STATUS_DRAW_AGREED:
    return "draw-agreed";
  case CHESS_STATUS_RESIGN_WHITE_WINS:
    return "resign-white-wins";
  case CHESS_STATUS_RESIGN_BLACK_WINS:
    return "resign-black-wins";
  }
  return "unknown";
}

static char piece_char(chess_piece p) {
  static const char names[] = "PNBRQK";
  char c;
  if (p.type == CHESS_EMPTY) {
    return '.';
  }
  c = names[p.type - 1];
  if (p.color == CHESS_BLACK) {
    c = (char)(c - 'A' + 'a');
  }
  return c;
}

static void print_board(const chess_position *pos) {
  int r;
  int f;
  printf("  a b c d e f g h\n");
  for (r = 7; r >= 0; r--) {
    printf("%d", r + 1);
    for (f = 0; f < 8; f++) {
      printf(" %c", piece_char(pos->board[(unsigned)(r * 8 + f)]));
    }
    printf("\n");
  }
  printf("side: %s\n", pos->side_to_move == CHESS_WHITE ? "white" : "black");
}

static void print_status(const cli_state *st) {
  chess_status status = chess_game_status(&st->game);
  printf("result: %s\n", status_word(status));
  if (status == CHESS_STATUS_ONGOING) {
    const chess_position *pos = &st->game.position;
    uint8_t king =
        pos->side_to_move == CHESS_WHITE ? pos->white_king : pos->black_king;
    bool check = chess_is_attacked(pos, king,
                                   (chess_color)(pos->side_to_move ^ 1u));
    printf("check: %s\n", check ? "yes" : "no");
    printf("halfmove: %u\n", pos->halfmove_clock);
    printf("history: %u\n", st->game.history_len);
  }
}

static void move_to_uci(chess_move m, char out[6]) {
  static const char promos[] = "xpnbrqk";
  out[0] = (char)('a' + m.from % 8u);
  out[1] = (char)('1' + m.from / 8u);
  out[2] = (char)('a' + m.to % 8u);
  out[3] = (char)('1' + m.to / 8u);
  if (m.promotion == CHESS_EMPTY) {
    out[4] = '\0';
  } else {
    out[4] = promos[m.promotion];
    out[5] = '\0';
  }
}

static int parse_uci(const char *s, chess_move *m) {
  char p;
  size_t len;
  if (s == NULL || m == NULL) {
    return -1;
  }
  len = strlen(s);
  if ((len != 4 && len != 5) || s[0] < 'a' || s[0] > 'h' || s[2] < 'a' ||
      s[2] > 'h' || s[1] < '1' || s[1] > '8' || s[3] < '1' || s[3] > '8') {
    return -1;
  }
  m->from = (uint8_t)((s[1] - '1') * 8 + (s[0] - 'a'));
  m->to = (uint8_t)((s[3] - '1') * 8 + (s[2] - 'a'));
  m->promotion = CHESS_EMPTY;
  if (len == 5) {
    p = s[4];
    if (p >= 'A' && p <= 'Z') {
      p = (char)(p - 'A' + 'a');
    }
    switch (p) {
    case 'q':
      m->promotion = CHESS_QUEEN;
      break;
    case 'r':
      m->promotion = CHESS_ROOK;
      break;
    case 'b':
      m->promotion = CHESS_BISHOP;
      break;
    case 'n':
      m->promotion = CHESS_KNIGHT;
      break;
    default:
      return -1;
    }
  }
  return 0;
}

static int parse_square(const char *s, uint8_t *sq) {
  if (s == NULL || strlen(s) != 2 || s[0] < 'a' || s[0] > 'h' || s[1] < '1' ||
      s[1] > '8') {
    return -1;
  }
  *sq = (uint8_t)((s[1] - '1') * 8 + (s[0] - 'a'));
  return 0;
}

static const char *move_error(chess_error err) {
  if (err == CHESS_ERR_ILLEGAL_MOVE) {
    return "illegal";
  }
  if (err == CHESS_ERR_GAME_OVER) {
    return "game-over";
  }
  if (err == CHESS_ERR_NO_CLAIM) {
    return "no-claim";
  }
  return "error";
}

static void cmd_moves(cli_state *st, const char *arg) {
  chess_move moves[CHESS_MAX_MOVES];
  size_t count = 0;
  size_t i;
  uint8_t filter = 64;
  bool use_filter = false;
  if (arg != NULL && parse_square(arg, &filter) != 0) {
    printf("error bad-square\n");
    return;
  }
  use_filter = arg != NULL;
  if (chess_generate_legal(&st->game.position, moves, CHESS_MAX_MOVES,
                           &count) != CHESS_OK) {
    printf("error generation\n");
    return;
  }
  printf("moves:");
  for (i = 0; i < count; i++) {
    char uci[6];
    if (use_filter && moves[i].from != filter) {
      continue;
    }
    move_to_uci(moves[i], uci);
    printf(" %s", uci);
  }
  printf("\n");
}

/* Engine move for the side to move, via the in-process adapter. Works
 * from any ongoing position (the engine takes FEN, so no session or
 * attach rules). The adapter core-validates; a second apply can only
 * fail if the game ended between the two calls. Optional budget:
 * `ai [nodes [depth]]`, defaults match casual host play. */
static void cmd_ai(cli_state *st, const char *arg) {
  chess_move reply;
  chess_error err;
  char uci[6];
  uint32_t nodes = CLI_AI_NODES;
  unsigned depth = CLI_AI_DEPTH;
  if (arg != NULL) {
    char *end = NULL;
    unsigned long n = strtoul(arg, &end, 10);
    const char *rest;
    if (end == arg || n == 0 || n > 100000000u) {
      printf("error bad-arg\n");
      return;
    }
    nodes = (uint32_t)n;
    rest = end;
    while (*rest == ' ' || *rest == '\t') {
      rest++;
    }
    if (*rest != '\0') {
      unsigned long d = strtoul(rest, &end, 10);
      if (end == rest || d == 0 || d > 64) {
        printf("error bad-arg\n");
        return;
      }
      depth = (unsigned)d;
    }
  }
  if (chess_ai_suggest(&st->game.position, nodes, depth,
                       &reply) != 0) {
    printf("error engine-failed\n");
    return;
  }
  err = chess_game_apply(&st->game, reply);
  if (err != CHESS_OK) {
    printf("error %s\n", move_error(err));
    return;
  }
  move_to_uci(reply, uci);
  printf("engine: %s\n", uci);
  print_status(st);
}

static void cmd_play(cli_state *st, const char *arg) {
  chess_move m;
  chess_error err;
  char uci[6];
  if (arg == NULL || parse_uci(arg, &m) != 0) {
    printf("error bad-uci\n");
    return;
  }
  err = chess_game_apply(&st->game, m);
  if (err != CHESS_OK) {
    printf("error %s\n", move_error(err));
    return;
  }
  move_to_uci(m, uci);
  printf("ok %s\n", uci);
  print_status(st);
}

static void cmd_claim(cli_state *st, const char *arg) {
  chess_error err;
  if (arg == NULL) {
    err = chess_game_claim_draw(&st->game, NULL);
  } else {
    chess_move m;
    if (parse_uci(arg, &m) != 0) {
      printf("error bad-uci\n");
      return;
    }
    err = chess_game_claim_draw(&st->game, &m);
  }
  if (err != CHESS_OK) {
    printf("error %s\n", move_error(err));
    return;
  }
  printf("ok claimed\n");
  print_status(st);
}

static void cmd_save(cli_state *st, const char *arg) {
  chess_save save;
  uint8_t buf[CHESS_SAVE_SLOT_MAX];
  size_t len = 0;
  FILE *f;
  if (arg == NULL) {
    printf("error bad-file\n");
    return;
  }
  memset(&save, 0, sizeof(save));
  save.game = st->game;
  save.settings = st->settings;
  save.mode = CHESS_MODE_LOCAL;
  save.human_color = CHESS_WHITE;
  save.seq = st->seq;
  if (chess_save_encode(&save, buf, sizeof(buf), &len) != CHESS_OK) {
    printf("error encode\n");
    return;
  }
  f = fopen(arg, "wb");
  if (f == NULL) {
    printf("error bad-file\n");
    return;
  }
  if (fwrite(buf, 1, len, f) != len) {
    fclose(f);
    printf("error bad-file\n");
    return;
  }
  fclose(f);
  st->seq++;
  printf("ok saved %s\n", arg);
}

static void cmd_load(cli_state *st, const char *arg) {
  uint8_t buf[CHESS_SAVE_SLOT_MAX];
  size_t len = 0;
  chess_save save;
  FILE *f;
  if (arg == NULL) {
    printf("error bad-file\n");
    return;
  }
  f = fopen(arg, "rb");
  if (f == NULL) {
    printf("error no-save\n");
    return;
  }
  len = fread(buf, 1, sizeof(buf), f);
  fclose(f);
  if (len == 0 || len == sizeof(buf)) {
    printf("error corrupt\n");
    return;
  }
  memset(&save, 0, sizeof(save));
  if (chess_save_decode(buf, len, &save) != CHESS_OK) {
    printf("error corrupt\n");
    return;
  }
  st->game = save.game;
  st->settings = save.settings;
  st->seq = save.seq + 1;
  printf("ok loaded %s\n", arg);
  print_status(st);
}

static void cmd_new(cli_state *st) {
  chess_game_init_fen(&st->game, START_FEN);
  printf("ok new\n");
}

static void cmd_loadfen(cli_state *st, const char *arg) {
  if (arg == NULL || chess_game_init_fen(&st->game, arg) != CHESS_OK) {
    printf("error bad-fen\n");
    return;
  }
  printf("ok loaded-fen\n");
}

static void cmd_fen(cli_state *st) {
  char buf[CHESS_FEN_MAX];
  if (chess_position_to_fen(&st->game.position, buf, sizeof(buf)) !=
      CHESS_OK) {
    printf("error encode\n");
    return;
  }
  printf("fen: %s\n", buf);
}

static void print_help(void) {
  printf("commands:\n"
         "  board            show board, side and clocks\n"
         "  moves [sq]       list legal UCI moves, optionally from square\n"
         "  play <uci>       play a move (e2e4, e7e8q)\n"
         "  claim [uci]      claim a draw (current or intended move)\n"
         "  resign           resign the game\n"
         "  new              start over from the initial position\n"
         "  loadfen <fen>    start from a FEN position\n"
         "  save <file>      write a versioned save image\n"
         "  load <file>      restore a save (history included)\n"
         "  fen              print the current FEN\n"
         "  status           print the game result\n"
         "  ai               engine moves for the side to move\n"
         "  help             this text\n"
         "  quit             exit\n");
}

/* Split one line into command + single argument (rest of line). */
static void dispatch(cli_state *st, char *line, bool *quit) {
  char *cmd;
  char *arg;
  *quit = false;
  while (*line == ' ' || *line == '\t') {
    line++;
  }
  if (*line == '\0') {
    return;
  }
  cmd = line;
  while (*line != '\0' && *line != ' ' && *line != '\t') {
    line++;
  }
  if (*line != '\0') {
    *line = '\0';
    line++;
  }
  while (*line == ' ' || *line == '\t') {
    line++;
  }
  arg = (*line == '\0') ? NULL : line;

  if (strcmp(cmd, "quit") == 0) {
    *quit = true;
  } else if (strcmp(cmd, "help") == 0) {
    print_help();
  } else if (strcmp(cmd, "board") == 0) {
    print_board(&st->game.position);
  } else if (strcmp(cmd, "moves") == 0) {
    cmd_moves(st, arg);
  } else if (strcmp(cmd, "play") == 0) {
    cmd_play(st, arg);
  } else if (strcmp(cmd, "claim") == 0) {
    cmd_claim(st, arg);
  } else if (strcmp(cmd, "resign") == 0) {
    if (chess_game_resign(&st->game, st->game.position.side_to_move) ==
        CHESS_OK) {
      printf("ok resigned\n");
      print_status(st);
    } else {
      printf("error game-over\n");
    }
  } else if (strcmp(cmd, "new") == 0) {
    cmd_new(st);
  } else if (strcmp(cmd, "loadfen") == 0) {
    cmd_loadfen(st, arg);
  } else if (strcmp(cmd, "save") == 0) {
    cmd_save(st, arg);
  } else if (strcmp(cmd, "load") == 0) {
    cmd_load(st, arg);
  } else if (strcmp(cmd, "fen") == 0) {
    cmd_fen(st);
  } else if (strcmp(cmd, "status") == 0) {
    print_status(st);
  } else if (strcmp(cmd, "ai") == 0) {
    cmd_ai(st, arg);
  } else {
    printf("error unknown-command\n");
  }
}

int main(void) {
  cli_state st;
  char line[1024];
  bool quit = false;
  memset(&st, 0, sizeof(st));
  /* Unbuffered stdout: drivers read us through pipes, where libc
   * would otherwise hold every reply until process exit. */
  setvbuf(stdout, NULL, _IONBF, 0);
  chess_game_init_fen(&st.game, START_FEN);
  st.settings.difficulty = CHESS_DIFF_MEDIUM;
  st.settings.language = 0;
  st.settings.brightness = 80;
  st.seq = 1;
  printf("chess-cli ready (type help)\n");
  while (!quit && fgets(line, sizeof(line), stdin) != NULL) {
    line[strcspn(line, "\r\n")] = '\0';
    dispatch(&st, line, &quit);
  }
  printf("bye\n");
  return 0;
}
