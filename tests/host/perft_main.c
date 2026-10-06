/* perft CLI (Task 3, step 4): leaf-node counter for move-gen verification.
 * Usage: perft --fen "<fen>" --depth N [--divide]
 * Prints the total on stdout; with --divide also prints one "<uci> <sub>"
 * line per root move. Statistics count fixed-depth leaves only: no
 * repetition or fifty-move pruning (see docs/testing.md). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chess_core.h"
#include "chess_core_internal.h"

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

static uint64_t perft(chess_position *pos, unsigned depth) {
  chess_move moves[CHESS_MAX_MOVES];
  size_t count = 0;
  size_t i;
  uint64_t nodes = 0;

  if (depth == 0) {
    return 1;
  }
  if (chess_generate_legal(pos, moves, CHESS_MAX_MOVES, &count) != CHESS_OK) {
    fprintf(stderr, "perft: move generation failed\n");
    exit(1);
  }
  for (i = 0; i < count; i++) {
    chess_undo undo;
    chess_make_unchecked(pos, moves[i], &undo);
    nodes += perft(pos, depth - 1);
    chess_unmake(pos, &undo);
  }
  return nodes;
}

static void usage(const char *argv0) {
  fprintf(stderr, "usage: %s --fen \"<fen>\" --depth N [--divide]\n", argv0);
}

int main(int argc, char **argv) {
  const char *fen = NULL;
  const char *depth_arg = NULL;
  int divide = 0;
  int i;
  chess_position pos;
  unsigned long depth;
  char *end = NULL;

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--fen") == 0 && i + 1 < argc) {
      fen = argv[++i];
    } else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc) {
      depth_arg = argv[++i];
    } else if (strcmp(argv[i], "--divide") == 0) {
      divide = 1;
    } else {
      usage(argv[0]);
      return 2;
    }
  }
  if (fen == NULL || depth_arg == NULL) {
    usage(argv[0]);
    return 2;
  }
  depth = strtoul(depth_arg, &end, 10);
  if (end == depth_arg || *end != '\0' || depth > 16) {
    fprintf(stderr, "perft: bad depth '%s'\n", depth_arg);
    return 2;
  }
  if (chess_position_from_fen(&pos, fen) != CHESS_OK) {
    fprintf(stderr, "perft: bad FEN '%s'\n", fen);
    return 1;
  }

  if (depth == 0) {
    printf("1\n");
    return 0;
  }
  if (divide) {
    chess_move moves[CHESS_MAX_MOVES];
    size_t count = 0;
    size_t k;
    uint64_t total = 0;
    if (chess_generate_legal(&pos, moves, CHESS_MAX_MOVES, &count) !=
        CHESS_OK) {
      fprintf(stderr, "perft: move generation failed\n");
      return 1;
    }
    for (k = 0; k < count; k++) {
      chess_undo undo;
      uint64_t sub;
      char uci[6];
      chess_make_unchecked(&pos, moves[k], &undo);
      sub = perft(&pos, (unsigned)depth - 1u);
      chess_unmake(&pos, &undo);
      move_to_uci(moves[k], uci);
      printf("%s %llu\n", uci, (unsigned long long)sub);
      total += sub;
    }
    printf("%llu\n", (unsigned long long)total);
  } else {
    printf("%llu\n", (unsigned long long)perft(&pos, (unsigned)depth));
  }
  return 0;
}
