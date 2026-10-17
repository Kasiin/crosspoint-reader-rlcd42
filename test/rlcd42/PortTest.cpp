#include <InputManager.h>
#include <Wire.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>

#include "freeink-sdk/libs/display/FreeInkDisplay/src/driver/St7305Packing.h"
#include "src/components/RlcdLayout.h"

static void point(InputManager& input, unsigned x, unsigned y, bool down = true) {
  Wire1.touch = {uint8_t(down), uint8_t(x >> 8), uint8_t(x), uint8_t(y >> 8), uint8_t(y)};
  fakeNow += 25;
  input.update();
}

static void testPacking() {
  std::array<uint8_t, 15000> frame{};
  std::array<uint8_t, 15000> expected{};
  // Reference uses the existing board driver's destination-address formula.
  // Verify all-black, all-white, row/column stripes and a nonsymmetric pattern.
  for (int pattern = 0; pattern < 5; ++pattern) {
    frame.fill(0);
    expected.fill(0);
    for (int y = 0; y < 300; ++y) {
      for (int x = 0; x < 400; ++x) {
        bool white = pattern == 1 || (pattern == 2 && x % 2) || (pattern == 3 && y % 2) ||
                     (pattern == 4 && ((x * 17 + y * 31) % 23 < 9));
        if (!white) continue;
        frame[y * 50 + x / 8] |= 0x80 >> (x % 8);
        int invY = 299 - y;
        expected[(x >> 1) * 75 + (invY >> 2)] |= 1 << (7 - (((invY & 3) << 1) | (x & 1)));
      }
    }
    for (int pair = 0; pair < 200; ++pair)
      for (int group = 0; group < 75; ++group)
        assert(freeink::st7305PackedByte(frame.data(), pair, group) == expected[pair * 75 + group]);
  }
}

static void testTouch() {
  std::fill(std::begin(gpioLevels), std::end(gpioLevels), HIGH);
  InputManager input;
  input.begin();
  assert(input.hasTouch());
  assert(!touchIsr);  // No IRQ dependency: historical board logs show INT stays HIGH.
  float x, y, endX, endY;
  for (const auto corner : {std::array<unsigned, 2>{0, 0}, {399, 0}, {0, 299}, {399, 299}}) {
    point(input, corner[0], corner[1]);
    assert(input.wasTouchPressedAt(x, y));
    assert(std::abs(x - float(corner[0]) / 399) < 0.001f);
    assert(std::abs(y - float(corner[1]) / 299) < 0.001f);
    point(input, corner[0], corner[1], false);
    assert(input.wasTouchTap(x, y));
    point(input, 0, 0, false);
    assert(!input.wasTouchTap(x, y));
  }
  point(input, 200, 150);
  point(input, 150, 150);
  point(input, 150, 150, false);
  assert(input.wasSwipe(x, y, endX, endY));
  assert(endX < x && !input.wasTouchTap(x, y));

  point(input, 200, 150);
  point(input, 4095, 4095);
  assert(!input.wasTouchTap(x, y));
  assert(!input.wasSwipe(x, y, endX, endY));
  point(input, 0, 0, false);

  point(input, 200, 150);
  fakeNow += 800;
  point(input, 200, 150);
  assert(input.wasTouchLongPress(x, y));
  input.suppressTouchContact();
  point(input, 200, 150, false);
  assert(!input.wasTouchTap(x, y));

  point(input, 100, 100);
  Wire1.failTouch = true;
  fakeNow += 150;
  input.update();
  assert(!input.wasTouchTap(x, y));
  assert(!input.wasSwipe(x, y, endX, endY));
  Wire1.failTouch = false;
  point(input, 100, 100, false);
  point(input, 100, 100);
  point(input, 100, 100, false);
  assert(input.wasTouchTap(x, y));
}

int main() {
  static_assert(FREEINK_MCU_S3 && FREEINK_DRIVER_ST7305 && FREEINK_CAP_TOUCH && FREEINK_SD_SDMMC);
  static_assert(!FREEINK_DRIVER_SSD1677 && !FREEINK_CAP_RTC && !FREEINK_CAP_FRONTLIGHT);
  static_assert(BoardConfig::MAX_FRAMEBUFFER_BYTES == 15000);
  static_assert(BoardConfig::RLCD42.input.power == 1 && BoardConfig::RLCD42.batteryAdc == 4);
  static_assert(BoardConfig::RLCD42.sdmmc.busWidth == 1 && BoardConfig::RLCD42.sdmmc.cmd == 21);
  ThemeMetrics m{};
  applyRlcdLayout(m);
  assert(m.homeTopPadding + m.homeCoverTileHeight + m.homeMenuTopOffset + 6 * (m.menuRowHeight + m.menuSpacing) <= 400);
  assert(m.menuRowHeight >= 32 && m.keyboardKeyHeight >= 32);
  testPacking();
  testTouch();
  std::puts("RLCD packing, profile, layout bounds and real SDK touch classification passed");
}
