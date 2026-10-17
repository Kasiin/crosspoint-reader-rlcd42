"""Build and run the RLCD host checks against the real patched SDK input code."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
sdk = root / "freeink-sdk/libs/hardware"
with tempfile.TemporaryDirectory(prefix="crosspoint-rlcd42-") as tmp:
    exe = Path(tmp) / ("port-test.exe" if os.name == "nt" else "port-test")
    command = [os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter",
               "-DFREEINK_DEVICE_RLCD42=1", "-DARDUINO_USB_CDC_ON_BOOT=1"]
    for path in [root / "test/rlcd42/stubs", sdk / "InputManager/test/host/metalio_stubs",
                 sdk / "InputManager/include", sdk / "BoardConfig/include", root]:
        command += ["-I", str(path)]
    command += [str(root / "test/rlcd42/PortTest.cpp"), str(sdk / "InputManager/src/InputManager.cpp"), "-o", str(exe)]
    subprocess.run(command, check=True)
    subprocess.run([str(exe)], check=True)

    # Compile the actual renderer transforms without its firmware-only dependencies.
    source = (root / "lib/GfxRenderer/GfxRenderer.cpp").read_text(encoding="utf-8")

    def function(signature):
        start = source.index(signature)
        brace = source.index("{", start)
        depth = 1
        end = brace + 1
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        return source[start:end]

    rotation = Path(tmp) / "rotation.cpp"
    rotation.write_text('''#include <cstdint>
#include <cassert>
#include <cstdio>
class GfxRenderer {
public:
  enum Orientation { Portrait, PortraitInverted, LandscapeClockwise, LandscapeCounterClockwise };
  Orientation orientation = Portrait;
  uint16_t panelWidth = 400, panelHeight = 300;
  void tapToLogical(float nx, float ny, int& outX, int& outY) const;
};
''' + function("static inline void rotateCoordinates(") + "\n" +
        function("void GfxRenderer::tapToLogical(") + '''
int main() {
  GfxRenderer r;
  for (int o = 0; o < 4; ++o) {
    r.orientation = static_cast<GfxRenderer::Orientation>(o);
    const int w = o < 2 ? 300 : 400, h = o < 2 ? 400 : 300;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
      int px, py, tx, ty;
      rotateCoordinates(r.orientation, x, y, &px, &py, 400, 300);
      assert(px >= 0 && px < 400 && py >= 0 && py < 300);
      r.tapToLogical(float(px) / 399, float(py) / 299, tx, ty);
      assert(tx == x && ty == y);
    }
  }
  std::puts("Actual GfxRenderer render/touch transforms round-trip all pixels in four orientations");
}
''', encoding="utf-8")
    rotation_exe = Path(tmp) / ("rotation.exe" if os.name == "nt" else "rotation")
    subprocess.run([command[0], "-std=c++17", str(rotation), "-o", str(rotation_exe)], check=True)
    subprocess.run([str(rotation_exe)], check=True)

    # Compile the real ST7305 driver against the SDK's recording bus stub.
    import shutil

    display_lib = root / "freeink-sdk/libs/display/FreeInkDisplay"
    stage = Path(tmp) / "st7305"
    for folder in ("src/driver", "src/bus", "include"):
        (stage / folder).mkdir(parents=True, exist_ok=True)
    for name in ("St7305Driver.h", "St7305Driver.cpp", "St7305Packing.h", "PanelDriver.h"):
        shutil.copy2(display_lib / f"src/driver/{name}", stage / f"src/driver/{name}")
    shutil.copy2(display_lib / "include/GrayscaleCapabilities.h", stage / "include/GrayscaleCapabilities.h")
    stubs = display_lib / "test/host/pro_stubs"
    for name in ("BoardConfig.h", "SPI.h", "esp_heap_caps.h", "sdkconfig.h"):
        shutil.copy2(stubs / name, stage / name)
    shutil.copy2(stubs / "EpdBus.h", stage / "src/bus/EpdBus.h")
    # The shared Arduino stub predates drivers that drive the reset line.
    (stage / "Arduino.h").write_text((stubs / "Arduino.h").read_text(encoding="utf-8") +
                                     "#define LOW 0\ninline void digitalWrite(int, int) {}\n", encoding="utf-8")
    st7305_exe = Path(tmp) / ("st7305.exe" if os.name == "nt" else "st7305")
    subprocess.run([command[0], "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter", "-DFREEINK_DRIVER_ST7305=1",
                    "-I", str(stage), str(root / "test/rlcd42/St7305DriverTest.cpp"),
                    str(stage / "src/driver/St7305Driver.cpp"), "-o", str(st7305_exe)], check=True)
    subprocess.run([str(st7305_exe)], check=True)
