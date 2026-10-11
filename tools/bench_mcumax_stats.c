/* Host benchmark: nodes and completed root iteration depth per search. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "mcu-max.h"

int main(int argc, char **argv) {
  const char *fen =
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  uint32_t node_max = 200000u;
  unsigned depth_max = 4u;
  int runs = 5;
  double total_sec = 0.0;
  uint64_t total_nodes = 0;
  uint64_t total_depth = 0;

  if (argc > 1) {
    node_max = (uint32_t)strtoul(argv[1], NULL, 10);
  }
  if (argc > 2) {
    depth_max = (unsigned)strtoul(argv[2], NULL, 10);
  }

  for (int i = 0; i < runs; i++) {
    uint32_t nodes = 0;
    uint32_t depth = 0;
    clock_t t0 = clock();
    mcumax_set_fen_position(fen);
    mcumax_search_best_move(node_max, depth_max);
    clock_t t1 = clock();
    mcumax_get_last_search_stats(&nodes, &depth);
    if (nodes == 0u) {
      nodes = node_max;
    }
    total_sec += (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    total_nodes += nodes;
    total_depth += depth;
  }

  printf("budget %u nodes depth %u\n", node_max, depth_max);
  printf("avg_nodes %.0f avg_iter_depth %.2f sec %.4f nps %.0f\n",
         (double)total_nodes / runs, (double)total_depth / runs, total_sec / runs,
         (double)total_nodes / total_sec);
  return 0;
}
