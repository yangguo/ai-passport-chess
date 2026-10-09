#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mcu-max.h"

int main(int argc, char **argv) {
  const char *fen;
  uint32_t nodes;
  unsigned depth;
  struct mcumax_hash_stats st;

  if (argc < 4) {
    fprintf(stderr, "usage: %s <fen> <nodes> <depth>\n", argv[0]);
    return 2;
  }
  fen = argv[1];
  nodes = (uint32_t)strtoul(argv[2], NULL, 10);
  depth = (unsigned)strtoul(argv[3], NULL, 10);

  mcumax_hash_alloc();
  mcumax_set_fen_position(fen);
  mcumax_hash_clear();
  mcumax_set_fen_position(fen);
  (void)mcumax_search_best_move(nodes, depth);
  mcumax_hash_get_stats(&st);

  printf(
      "nodes=%u probes=%llu key_hits=%llu cutoffs=%llu stores=%llu "
      "replace_deeper=%llu hit_rate=%.3f cutoff_rate=%.3f\n",
      (unsigned)mcumax_get_last_search_nodes(), (unsigned long long)st.probes,
      (unsigned long long)st.key_hits, (unsigned long long)st.cutoffs,
      (unsigned long long)st.stores, (unsigned long long)st.replace_deeper_lost,
      st.probes ? (double)st.key_hits / (double)st.probes : 0.0,
      st.probes ? (double)st.cutoffs / (double)st.probes : 0.0);
  return 0;
}
