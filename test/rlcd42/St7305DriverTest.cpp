// Host check for the real RLCD42 ST7305 driver against the SDK's recording
// bus stub: the panel is monochrome, so a grayscale pass must not touch the
// bus (the B/W base frame stays on screen), and begin() must end with one
// all-white frame because this LCD shows RAM live.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "src/driver/St7305Driver.h"
#include "src/driver/St7305Packing.h"

using namespace freeink;

static const EpdBus::Write* lastRamWrite(const EpdBus& bus) {
  const EpdBus::Write* found = nullptr;
  for (const auto& w : bus.writes)
    if (w.command == 0x2C) found = &w;
  return found;
}

int main() {
  EpdBus bus;
  PanelDriver& driver = st7305Driver();

  assert(!driver.grayscaleCapabilities().supported());
  assert(!driver.grayscaleCapabilities(GrayscaleMode::Absolute).supported());

  // A grayscale pass keeps the displayed B/W base: no bus traffic at all.
  // The default PanelDriver fallback would re-display this buffer, which the
  // panel would show as a black frame (opposite polarity).
  std::vector<uint8_t> fb(15000, 0x00);
  fb[0] = 0xFF;
  driver.displayGray(bus, fb.data(), false, nullptr, false);
  assert(bus.writes.empty());

  // begin() ends with one all-white frame so boot RAM noise is not visible.
  bus.clear();
  driver.begin(bus);
  const EpdBus::Write* ram = lastRamWrite(bus);
  assert(ram && ram->bytes.size() == 15000);
  for (const uint8_t b : ram->bytes) assert(b == 0xFF);

  // display() packs the framebuffer through the validated address formula.
  bus.clear();
  for (size_t i = 0; i < fb.size(); ++i) fb[i] = uint8_t(i * 37 + (i >> 8));
  driver.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);
  ram = lastRamWrite(bus);
  assert(ram && ram->bytes.size() == 15000);
  for (int pair = 0; pair < 200; ++pair)
    for (int group = 0; group < 75; ++group)
      assert(ram->bytes[pair * 75 + group] == st7305PackedByte(fb.data(), pair, group));

  // deepSleep issues display-off then sleep-in.
  bus.clear();
  driver.deepSleep(bus);
  assert(bus.writes.size() == 2 && bus.writes[0].command == 0x28 && bus.writes[1].command == 0x10);

  std::puts("ST7305 driver: grayscale pass keeps the B/W base, begin() shows white, display() packs correctly");
  return 0;
}
