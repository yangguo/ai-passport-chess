# Agent working agreement

- This repository is currently a documentation-first design baseline, not working chess firmware. Start with README.md, docs/plans/2026-10-06-chess-design.md and the implementation plan.
- Inspect git status and preserve unrelated changes. Use main for the initial design; implement on feature/* branches. Do not push to FoloToy upstream.
- Keep core pure C11, fixed-capacity and host-testable. No ESP-IDF/LVGL dependencies in core/model/codec. One application owner mutates the real game.
- Never reduce rules to fit the AI engine. All four promotions are required; AI results must be legal and belong to the current generation.
- Follow docs/sources.md for pinned upstream versions. Read imported BSP AGENTS.md/applicable development guidance when importing or editing upstream code. Keep third-party licenses.
- Run python3 tools/check_docs.py for documentation changes. Once implemented, run relevant host/perft/sanitizer and clean firmware checks. Do not call docs CI a chess/perft/firmware pass.
- Hardware flashing and community publishing require explicit task authorization. Document-only tasks do not authorize either. Do not wipe shared NVS to mask errors.
- Report local/host, build, CI, device and publication evidence separately. Leave NOT RUN visible. Budget values are targets until measured.
- Do not commit firmware readbacks, credentials, private device identity, build caches or managed_components. Future release binaries are packaged artifacts.
