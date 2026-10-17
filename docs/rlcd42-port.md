# ESP32-S3 RLCD 4.2 port

This experimental target is for Kasiin's 400 x 300 ST7305 reflective LCD board.
It is not an e-paper target. The normal reader UI is portrait (300 x 400);
reader rotation still supports all four upstream orientations.

## Source and maintenance

- Fork: https://github.com/Kasiin/crosspoint-reader
- Firmware base: `bb855ef935826e86c65574b5abbd4eab974d53e0` (upstream develop).
- FreeInk SDK base: `7fcd3b34c108f28c086ade3b433924022dbc0998`.
- Environment: `rlcd42` in `platformio.ini`.

The SDK gitlink stays pinned. `scripts/rlcd42_sdk.py` applies
`patches/freeink-sdk-rlcd42.patch` before building this target. It accepts an
already applied patch, rejects a different SDK revision, and refuses conflicting
local edits. Do not update the submodule independently of the patch. After a
deliberate SDK edit, regenerate the patch with
`git -C freeink-sdk diff --binary --output=../patches/freeink-sdk-rlcd42.patch`.
Review/rebase this patch when upgrading upstream; do not bypass the revision check.

## Hardware profile

| Function | Configuration |
| --- | --- |
| MCU / memory | ESP32-S3, 16 MB DIO flash and 8 MB OPI PSRAM at 80 MHz |
| ST7305 SPI | SCLK 11, MOSI 12, CS 40, DC 5, RESET 41; 4 MHz; no BUSY |
| Touch | CST816-compatible, address 0x15; SDA 15, SCL 7, INT 17, RESET 2; Wire1, 100 kHz |
| SD | SDMMC 1-bit, CLK 38, CMD 21, D0 39; 5 MHz |
| Buttons | KEY 18 = Confirm, BOOT 0 = Down, PWR 1 = Power; active LOW |
| Battery | ADC GPIO 4; divider multiplier 3; no charging-status input |
| Audio | Unused; amplifier enable GPIO 46 held LOW |
| USB | Native USB on GPIO 19/20 |

BOOT sits on the GPIO0 strapping pin: holding Down across a reset enters
download mode. This is board hardware, inherited from the original firmware.

The touch driver polls because existing board logs show that INT may remain HIGH
even while coordinates are available. A tap allows 12 pixels of movement and a
swipe requires 28 pixels. Read failures and invalid coordinates cancel an active
contact without turning it into a tap. Normalized panel coordinates feed the
existing input router; no page-turn or book-navigation handler is replaced.

The original ESP-IDF board project uses 40 MHz flash/PSRAM. The pinned Arduino
3.3.11 package supplies its S3 DIO/OPI bootloader and PSRAM libraries at 80 MHz;
this port uses that standard configuration. Setting an INI PSRAM frequency does
not rebuild those libraries. Confirm cold boot and memory stability on this board.
If 80 MHz fails, a custom 40 MHz Arduino library/bootloader build is needed before
the port can be accepted; do not hide that difference with an INI-only override.

The ST7305 driver converts the existing 15,000-byte framebuffer in 75-byte stack
chunks, without a second full-frame conversion buffer. Refresh requests use one
full monochrome LCD write. E-paper partial/full refresh choices do not select
different waveforms here; grayscale is not advertised. The panel cannot show
gray planes, so the driver's `displayGray` is a deliberate no-op: when the
reader's anti-aliasing/image gray pass ends, the already-displayed B/W base
stays on screen (the base `PanelDriver` fallback would re-display the
framebuffer, which at that moment holds a gray plane of the opposite polarity).
`begin()` pushes one all-white frame after display-on because this LCD shows
RAM live; without it, boot noise would stay visible until the first paint.

## UI and fonts

`src/components/RlcdLayout.h` centralizes board-specific dimensions. Menus use
36-pixel rows and touch targets are at least 32 pixels where the shared theme
controls them. The UI's 12-point font slot uses the existing 10-point UI font;
its SD fallback uses the same size. Reader default size is 12 points, with the
normal reader font settings retained. Existing saved font choices are preserved.
Classic/Lyra title wrapping and the three-cover title area are shortened to fit.
RoundedRaff's drawn menu rows and touch rows use the same compact dimensions.
No book/cache/settings file format or EPUB/TXT parsing logic changes.

Built-in fonts do not contain complete Chinese coverage. Use the upstream
[SD card font workflow](sd-card-fonts.md): copy a licensed CJK `.ttf`, `.otf` or
`.ttc` to `/fonts/`, reboot, then select it in Settings > Reader > Reader Font
Family.
Use English for initial setup if the chosen UI language cannot yet render.
The selected SD family also supplies missing UI characters. A bitmap `.cpfont`
family should include 8 and 10 point UI fallbacks and the desired reader sizes.
No proprietary desktop fonts are included in this port.

## Sleep and updates

Deep sleep turns the LCD off: the sleep image and quick-resume page are not
retained visibly. PWR wakes the device; the existing startup/reader flow redraws.
Touch wake is not enabled. Battery runtime and sleep current need measurement.

The firmware board tag is `rlcd42`. OTA checks the Kasiin fork, looking for
`crosspoint-<version>-rlcd42.bin`. Until a matching release is published, use USB
updates; the online check can report no release/error. Do not flash upstream
Xteink, M5, or LilyGo images. The upstream scanner still accepts untagged legacy
images, so board tags are not a complete guard against manually choosing a wrong file.

## Build and local checks

```sh
git submodule update --init --recursive
pio run -e rlcd42
python test/rlcd42/run.py
```

Use Python 3, PlatformIO, and a C++17 host compiler for the checks. On Windows,
set `PYTHONUTF8=1` and `PYTHONIOENCODING=utf-8` before invoking PlatformIO because
the translation generator prints non-ASCII text. Network downloads must finish
before the first build. An ignored `platformio.local.ini` can point to a locally
downloaded copy of the exact pinned platform archive; it is not part of the port.

Host checks exercise the real SDK touch implementation, pixel packing against
the original board's address formula, profile capabilities, compact menu bounds,
and the actual renderer transform functions over every pixel in all four
orientations. They do not simulate the electrical panel or prove rendered text
and every popup fit. Build and hardware results belong in the verification record.

## Hardware acceptance checklist

Back up the existing board firmware before the first upload. Use an expendable
FAT32 SD card initially. `pio run -e rlcd42 -t upload --upload-port <PORT>` writes
the configured bootloader, partition table and application; this is a different
firmware layout from the original ESP-IDF project. Do not upload an app-only
binary at address zero. Use `pio device monitor -b 115200 --port <PORT>` for logs.

1. Cold boot on USB, then battery. Expect readable monochrome UI, correct black/
   white polarity, and no stripes or mirrored text. Record PSRAM and SD init results.
2. Touch all four corners and menu rows. One tap must activate one item. Dragging
   off a row or a failed contact must not activate it. Check KEY, BOOT and PWR.
3. Open a short EPUB and TXT. Check the original left/right page zones and menu
   gesture, long press, selection, and resume position. Repeat in all four reader
   orientations; taps must follow the displayed page, including near edges.
4. Test all five home themes, long book titles/authors, empty recents, file lists,
   Settings, the reader menu, keyboard, and Wi-Fi transfer. Verify every visible
   control matches its touch target and the bottom menu is reachable.
5. Select a CJK SD font. Check Chinese titles, punctuation, mixed Latin/Chinese
   text and every selectable reader size; look for missing glyphs or clipped rows.
6. Turn at least 100 pages, change orientation and font size, reopen a book, and
   try a missing SD card. Watch for resets, stale layout, duplicate page turns,
   or SD errors. Record free internal RAM/PSRAM if logs expose them.
7. Enter sleep by PWR and timeout. Expect a blank LCD; wake with PWR and verify
   the saved location and first useful redraw. Measure awake/sleep battery current
   and compare the displayed battery level with the measured battery voltage.

Do not call this hardware-verified until these results are recorded on the actual
board. UI dimensions and touch thresholds may need a small calibration follow-up.

## Driver provenance

ST7305 initialization and packing are adapted from
`LinIT-L/ESP32-S3-RLCD-BBK`, revision
`1c0549206f128c311103f374ee46c38b95229c91`,
`components/st7305/st7305.c`. Its repository license is preserved in
[`licenses/rlcd-board-source-LICENSE.txt`](../licenses/rlcd-board-source-LICENSE.txt).
The three new ST7305 driver files carry `GPL-2.0-only`; upstream MIT notices
remain in place. The resulting RLCD firmware includes this GPL-covered driver;
do not label the combined binary MIT-only. Keep the matching source, SDK patch,
build instructions and dependency license notices with any distributed build.
