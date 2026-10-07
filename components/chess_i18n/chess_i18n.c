#include "chess_i18n.h"

static const char *const s_text[2][CHESS_TEXT_COUNT] = {
    {
        "CHESS", "Continue", "New 2-player", "New vs AI", "Language: English",
        "Easy", "Normal", "Hard", "PAUSE", "Resume", "Claim draw",
        "Claim selected", "Resign", "New game", "Back to home", "Confirm", "Cancel",
        "OK: new game", "LONG: home", "Retry", "White", "Black", "to move",
        "saved", "UNSAVED", "Thinking...", "UP/DOWN piece",
        "UP/DOWN OK LONG", "save failed", "AI fallback", "rejected", "no claim",
        "White wins", "Black wins", "Stalemate", "Dead position", "Fivefold draw",
        "75-move draw", "Draw claimed", "Draw agreed", "Game over", "Error",
        "OK: retry   LONG: home", "Cancel is taking longer", "Play White", "Play Black",
        "Brightness", "UP/DOWN adjust  OK save  LONG cancel",
        "Sleep failed",
    },
    {
        "象棋", "继续对局", "双人对弈", "人机对弈", "语言：中文", "简单", "普通",
        "困难", "暂停", "继续", "申报和棋", "申报选中着", "认输", "新对局",
        "返回首页", "确认", "取消", "确认：新对局", "长按：返回", "重试", "白方",
        "黑方", "走棋", "已保存", "未保存", "思考中…", "上下键选棋子",
        "上下键选择 长按菜单", "保存失败", "AI备用着", "着法无效", "无法申报和棋",
        "白方胜", "黑方胜", "逼和", "和棋局面", "五次重复和棋", "75回合和棋",
        "已申报和棋", "双方同意和棋", "对局结束", "错误", "确认：重试  长按：返回",
        "取消仍在处理", "执白", "执黑",
        "亮度", "上下调节  确认保存  长按取消", "休眠失败",
    },
};

chess_language chess_i18n_normalize(uint8_t language) {
  return language == CHESS_LANGUAGE_CHINESE ? CHESS_LANGUAGE_CHINESE
                                            : CHESS_LANGUAGE_ENGLISH;
}

chess_language chess_i18n_toggle(uint8_t language) {
  return chess_i18n_normalize(language) == CHESS_LANGUAGE_ENGLISH
             ? CHESS_LANGUAGE_CHINESE
             : CHESS_LANGUAGE_ENGLISH;
}

const char *chess_i18n_get(chess_language language, chess_text_id text) {
  if ((unsigned)text >= CHESS_TEXT_COUNT) {
    text = CHESS_TEXT_HOME;
  }
  return s_text[chess_i18n_normalize((uint8_t)language)][text];
}
