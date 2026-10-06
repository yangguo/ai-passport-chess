# BSP vendor record (Task 1, M0 baseline)

- Upstream: https://github.com/FoloToy/ai-passport
- Pinned commit: `33d3d1d93a1125b356b47b6d83a7a60121be801e`
  (per `docs/sources.md`; never follow main without re-review)
- Imported: `components/bsp/` ENTIRE (headers, sources, CMakeLists,
  idf_component.yml) — the reusable board logic. No modifications.
- License: upstream MIT, (c) 2026 FoloToy — full text in
  `components/bsp/LICENSE`, kept verbatim.
- Deliberately NOT imported: `main/*` (demo menu, ui_pixel helpers),
  `assets/*`, docs, CI, skills. Derivative firmware must redesign
  its own UI (upstream AGENTS.md rule); our chess UI lives in
  `main/` and is written from scratch against BSP APIs only.
- What the importer verified by reading (not executing): `bsp_pins.h`
  (C3/8MiB/ST7789P3/ADC-button table), `bsp_display.h` (LVGL lock
  contract), `bsp_button.h` (CLICK/LONG events, non-blocking
  callbacks), `main.c` + `demo.h` (page lifecycle pattern only —
  pattern learned, code not copied).
