#include "chess_i18n.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expr)                                                           \
  do {                                                                        \
    if (!(expr)) {                                                            \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);            \
      failures++;                                                             \
    }                                                                         \
  } while (0)

static void test_language_normalization_and_toggle(void) {
  CHECK(chess_i18n_normalize(CHESS_LANGUAGE_ENGLISH) == CHESS_LANGUAGE_ENGLISH);
  CHECK(chess_i18n_normalize(CHESS_LANGUAGE_CHINESE) == CHESS_LANGUAGE_CHINESE);
  CHECK(chess_i18n_normalize(255u) == CHESS_LANGUAGE_ENGLISH);
  CHECK(chess_i18n_toggle(CHESS_LANGUAGE_ENGLISH) == CHESS_LANGUAGE_CHINESE);
  CHECK(chess_i18n_toggle(CHESS_LANGUAGE_CHINESE) == CHESS_LANGUAGE_ENGLISH);
  CHECK(chess_i18n_toggle(255u) == CHESS_LANGUAGE_CHINESE);
}

static void test_translation_table_is_complete(void) {
  unsigned lang;
  unsigned key;
  for (lang = CHESS_LANGUAGE_ENGLISH; lang <= CHESS_LANGUAGE_CHINESE; lang++) {
    for (key = 0; key < CHESS_TEXT_COUNT; key++) {
      const char *text = chess_i18n_get((chess_language)lang,
                                        (chess_text_id)key);
      CHECK(text != NULL);
      CHECK(text[0] != '\0');
    }
  }
  CHECK(strcmp(chess_i18n_get(CHESS_LANGUAGE_ENGLISH, CHESS_TEXT_NEW_AI),
               "New vs AI") == 0);
  CHECK(strcmp(chess_i18n_get(CHESS_LANGUAGE_CHINESE, CHESS_TEXT_NEW_AI),
               "人机对弈") == 0);
  CHECK(strcmp(chess_i18n_get(CHESS_LANGUAGE_CHINESE, CHESS_TEXT_LANGUAGE),
               "语言：中文") == 0);
  CHECK(strcmp(chess_i18n_get((chess_language)99, CHESS_TEXT_HOME), "CHESS") ==
        0);
}

int main(void) {
  test_language_normalization_and_toggle();
  test_translation_table_is_complete();
  if (failures != 0) {
    fprintf(stderr, "%d i18n test(s) failed\n", failures);
    return 1;
  }
  puts("i18n tests passed");
  return 0;
}
