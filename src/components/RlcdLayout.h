#pragma once

#include "themes/BaseTheme.h"

// 4.2-inch 400x300 glass is about 119 PPI; 32 px is a 6.8 mm touch target.
inline void applyRlcdLayout(ThemeMetrics& m) {
  m.topPadding = 4;
  m.batteryBarHeight = 16;
  m.headerHeight = 52;
  m.headerSidePadding = 8;
  m.verticalSpacing = 6;
  m.contentSidePadding = 8;
  m.listRowHeight = 36;
  m.listWithSubtitleRowHeight = 54;
  m.listInset = 6;
  m.listSidePadding = 6;
  m.listRowGap = 2;
  m.listScrollWidth = 2;
  m.menuRowHeight = 36;
  m.menuSpacing = 2;
  m.tabBarHeight = 36;
  m.coverGridTabBarHeight = 36;
  m.homeTopPadding = 44;
  m.homeCoverHeight = m.homeRecentBooksCount == 3 ? 40 : 80;
  m.homeCoverTileHeight = 96;
  m.homeMenuTopOffset = 8;
  m.progressBarHeight = 10;
  m.statusBarVerticalMargin = 48;  // text-lane reservation: status bar + ~1.3 lines of clearance below
  m.keyboardKeyHeight = 32;
  m.keyboardVerticalOffset = 0;
  m.popupMarginX = 8;
  m.popupMarginY = 6;
  m.optionPopupItemSpacing = 6;
  m.optionPopupInnerPadding = 8;
  m.optionPopupDialogSideMargin = 10;
  m.headerButtonSize = 32;
  m.homeRecentTitleMaxLines = 2;
  m.homeCoverTitleMaxLines = 1;
  m.menuRowHeightFromMetrics = true;
  m.listSelectionStyle = 0;  // solid invert fill instead of a dithered pill
  m.ditheredSelection = false;
}
