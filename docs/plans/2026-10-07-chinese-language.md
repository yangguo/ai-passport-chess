# Chinese Language Switch Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add an English/Simplified Chinese switch to the chess home menu and preserve the selected language in NVS saves.

**Architecture:** Keep translations and language normalization in a pure C11 `chess_i18n` component with host tests. Carry the language in `chess_view`; render UI text using a subset LVGL font generated from licensed Noto Sans SC. Reuse the existing settings byte in the save schema and let the single app owner toggle and persist it.

**Tech Stack:** C11, CMake/CTest, LVGL 9.5, ESP-IDF 5.5.3, GitHub Actions.

### Task 1: Translation table and language behavior

**Files:** Create `components/chess_i18n/include/chess_i18n.h`, `components/chess_i18n/chess_i18n.c`, `tests/host/test_i18n.c`; modify `tests/host/CMakeLists.txt`.

1. Write tests for English/Chinese labels, invalid-language normalization, and toggling both ways.
2. Run only the new test and confirm it fails because the component does not exist.
3. Implement fixed translation IDs and pure lookup/toggle helpers.
4. Run the test and existing host suite.

### Task 2: Chinese font asset and renderer localization

**Files:** Create `assets/fonts/chess_zh_18.c`, `assets/fonts/OFL.txt`, `assets/SOURCES.md`; modify `main/chess_font.h`, `main/chess_ui.h`, `main/chess_ui.c`, `main/CMakeLists.txt`.

1. Pin Noto Sans SC source and converter versions; generate only glyphs used by the UI.
2. Select Chinese or English font from `chess_view.language` and translate renderer-owned labels.
3. Keep board piece glyphs and coordinates unchanged; verify generated asset contains all required codepoints.

### Task 3: App menu, save persistence, and verification

**Files:** Modify `main/chess_app.c`, `docs/testing.md`, `docs/storage.md`.

1. Add a fourth home-menu row that toggles language immediately.
2. Normalize loaded language, restore it from NVS, and persist changes without changing save schema.
3. Translate app-generated status, menu, and outcome strings.
4. Run host tests, docs checks, and firmware validation through GitHub Actions; report hardware testing separately.
