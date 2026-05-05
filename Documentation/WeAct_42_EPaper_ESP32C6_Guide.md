# WeAct 4.2" E-Paper Module + ESP32-C6 DevKitC-1 — Project Guide

## Module Specifications

| Parameter | Value |
|---|---|
| Display size | 4.2 inches |
| Resolution | 400 × 300 pixels |
| Colors | Black / White (BW) or Black / White / Red (BWR) |
| Driver IC | SSD1683 |
| I/O level | 3.3V |
| Supply voltage | 3.3V – 5V |
| PCB dimensions | 91.8 × 89.8 mm |
| Connector | 2×4 Pin 2.54mm header / MX1.25-8P wire-to-board |
| Interface | 4-line SPI |

**Data & resources:**  
- GitHub repo: <https://github.com/WeActStudio/WeActStudio.EpaperModule>
- AliExpress item ID: `1005007133350270`

---

## Physical Dimensions (for 3D Print Case Design)

### PCB

- **Overall board:** 91.8 × 89.8 mm
- **Mounting holes:** 4× in the corners, M2 screw size
- **Connector location:** 2×4 pin header at the top edge of the board (visible as gold pads in product photo)
- **FPC cable:** The e-paper panel is bonded to the PCB via a fragile FPC ribbon — do NOT bend it perpendicular to the screen or fold it toward the front

### STEP File

The official WeAct GitHub repo contains 3D STEP files in the `Hardware/` folder. Confirmed files include the 2.13" model (`WeAct-EpaperModule-2.13 Board 3D.step`); a 4.2" STEP file should also be present. Clone the repo and check:

```
git clone https://github.com/WeActStudio/WeActStudio.EpaperModule.git
ls Hardware/
```

The STEP file can be imported directly into OpenSCAD (via `import()`) or FreeCAD for precise measurements of mounting hole positions, connector placement, and component clearances.

### Existing STL Files to Remix

1. **Printables — WeAct 4.2" case (WeMos D1 Mini)**  
   <https://www.printables.com/model/1414609-weact-42-epaper-case-wemos-d1-mini>  
   Wall-mounted case designed specifically for the WeAct 4.2" module. Houses a WeMos D1 Mini inside — the internal cavity would need to be modified for the ESP32-C6-DevKitC-1 (which is larger: ~72.6 × 25.4 mm). Two-part design: body + lid, snap-fit. Total STL size ~338 KB.

2. **MakerWorld — WeAct 4.2" Pilot Dashboard**  
   <https://makerworld.com/en/models/2393986>  
   Designed for ESP32-S3 Mini with Li-Ion battery, magnets, and USB-C charger board. Good reference for a compact all-in-one enclosure.

3. **Printables — 4.2" ePaper Display Stand (Waveshare/generic)**  
   <https://www.printables.com/model/670938-42-epaper-display-stand-d1-mini-esp8266-esp32-comp>  
   The original model that the WeAct-specific case (item 1) was remixed from. D1 Mini / ESP32 compatible.

### Design Tips for OpenSCAD

- The ESP32-C6-DevKitC-1 N8 board is approximately **72.6 × 25.4 mm** with two rows of pin headers
- Plan for USB-C access on the ESP32 board (either the UART or USB port, depending on how you'll flash/power it)
- The e-paper display's active area is smaller than the PCB — leave a window/bezel that only exposes the screen
- Consider wall-mount keyhole slots or a desk stand integrated into the case
- The MX1.25-8P wire-to-board connector allows a cable between the display and ESP32, so they don't have to be in the same enclosure

---

## Wiring: ESP32-C6-DevKitC-1 ↔ WeAct 4.2" E-Paper

### Pin Mapping

| E-Paper Pin | Function | ESP32-C6 GPIO | Category | Notes |
|---|---|---|---|---|
| VCC | Power | **3.3V** | Power | Use 3.3V pin, NOT 5V for I/O safety |
| GND | Ground | **GND** | Power | |
| DIN | SPI MOSI | **GPIO7** | SPI (FSPI) | FSPI default MOSI |
| CLK | SPI SCK | **GPIO6** | SPI (FSPI) | FSPI default SCK |
| CS | SPI Chip Select | **GPIO10** | SPI (FSPI) | FSPI default CS |
| DC | Data/Command | **GPIO18** | Control | Safe GPIO, no strapping conflict |
| RST | Reset | **GPIO19** | Control | Safe GPIO, active low |
| BUSY | Busy flag | **GPIO20** | Control (input) | Safe GPIO, HIGH = busy |

### Why These Pins?

**SPI bus (GPIO6, GPIO7, GPIO10):** These are the FSPI default pins on the ESP32-C6. While the C6's SPI is IO-matrixed (you can remap to any GPIO), using the defaults avoids configuration hassle and gives the best performance.

**Control pins (GPIO18, GPIO19, GPIO20):** These are clean GPIOs with no strapping function and no onboard peripheral conflict. They're safe to use without worrying about boot issues.

### Pins to AVOID on ESP32-C6

| GPIO | Reason |
|---|---|
| GPIO4 (MTMS) | Strapping pin — JTAG |
| GPIO5 (MTDI) | Strapping pin — JTAG |
| GPIO8 | Strapping pin + onboard WS2812 RGB LED |
| GPIO9 | Strapping pin + BOOT button |
| GPIO15 | Strapping pin |
| GPIO16 | UART0 TX (USB-UART bridge) |
| GPIO17 | UART0 RX (USB-UART bridge) |
| GPIO12/13 | USB D+/D- (native USB port) |

You CAN use strapping pins after boot for normal I/O, but for a beginner-friendly setup it's simpler to avoid them entirely.

---

## Software Setup

### Arduino IDE Configuration

1. Install the **ESP32 Arduino Core 3.x** (by Espressif) via Boards Manager
2. Select board: **"ESP32C6 Dev Module"** (or "ESP32-C6-DevKitC-1")
3. Settings:
   - Flash Size: **8MB**
   - Flash Mode: **DIO**
   - Upload Speed: **921600**

### PlatformIO Configuration

```ini
[env:esp32-c6-devkitc-1]
platform = espressif32
board = esp32-c6-devkitc-1
framework = arduino
board_build.mcu = esp32c6
board_build.f_cpu = 160000000L
board_build.flash_size = 8MB
board_build.flash_mode = dio

lib_deps =
    zinggjm/GxEPD2@^1.6.0
    adafruit/Adafruit GFX Library
    adafruit/Adafruit BusIO
```

### GxEPD2 Display Classes

For the WeAct 4.2" module with SSD1683 driver:

- **Black/White version:**  
  `GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT>`

- **Black/White/Red version:**  
  `GxEPD2_3C<GxEPD2_420c_GDEY042Z98, GxEPD2_420c_GDEY042Z98::HEIGHT>`

---

## Starter Sketch (Arduino)

```cpp
// WeAct 4.2" E-Paper + ESP32-C6-DevKitC-1 N8
// GxEPD2 Hello World

#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>

// Pin definitions for ESP32-C6
#define CS_PIN    10  // FSPI CS
#define DC_PIN    18  // Data/Command
#define RST_PIN   19  // Reset
#define BUSY_PIN  20  // Busy

// SPI MOSI = GPIO7, SCK = GPIO6 (FSPI defaults, handled by SPI library)

// 4.2" BW display — SSD1683, 400x300
GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT>
  display(GxEPD2_420_GDEY042T81(CS_PIN, DC_PIN, RST_PIN, BUSY_PIN));

// For BWR (3-color) version, use instead:
// GxEPD2_3C<GxEPD2_420c_GDEY042Z98, GxEPD2_420c_GDEY042Z98::HEIGHT>
//   display(GxEPD2_420c_GDEY042Z98(CS_PIN, DC_PIN, RST_PIN, BUSY_PIN));

void setup() {
  Serial.begin(115200);
  Serial.println("WeAct 4.2\" E-Paper — ESP32-C6 init");

  // Initialize SPI on FSPI default pins
  SPI.begin(6, 2, 7, 10);  // SCK=6, MISO=2(unused), MOSI=7, SS=10

  // Initialize display
  // Parameters: baud for debug, initial=true, reset_duration=50ms, pulldown_rst=false
  display.init(115200, true, 50, false);

  helloWorld();

  display.hibernate();
  Serial.println("Display hibernating. Done.");
}

void loop() {
  // Nothing — e-paper retains image without power
}

void helloWorld() {
  display.setRotation(1);  // Landscape: 400 wide × 300 tall
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_BLACK);

  const char text1[] = "Hello World!";
  const char text2[] = "WeAct Studio 4.2\"";
  const char text3[] = "ESP32-C6-DevKitC-1";

  int16_t tbx, tby;
  uint16_t tbw, tbh;

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // Line 1 — centered
    display.getTextBounds(text1, 0, 0, &tbx, &tby, &tbw, &tbh);
    uint16_t x1 = ((display.width() - tbw) / 2) - tbx;
    uint16_t y1 = 80;
    display.setCursor(x1, y1);
    display.print(text1);

    // Line 2
    display.getTextBounds(text2, 0, 0, &tbx, &tby, &tbw, &tbh);
    uint16_t x2 = ((display.width() - tbw) / 2) - tbx;
    uint16_t y2 = 140;
    display.setCursor(x2, y2);
    display.print(text2);

    // Line 3
    display.getTextBounds(text3, 0, 0, &tbx, &tby, &tbw, &tbh);
    uint16_t x3 = ((display.width() - tbw) / 2) - tbx;
    uint16_t y3 = 200;
    display.setCursor(x3, y3);
    display.print(text3);
  } while (display.nextPage());
}
```

### Key Notes

- `SPI.begin(6, 2, 7, 10)` — explicitly sets FSPI pins. GPIO2 is assigned to MISO even though the e-paper doesn't have a MISO line (it's input-only from the MCU's perspective)
- `display.init(115200, true, 50, false)` — the `true` means perform initial full refresh, `50` is reset pulse duration in ms
- `display.hibernate()` — puts the display controller into ultra-low-power mode. The image remains on screen indefinitely
- The 4.2" display framebuffer is ~15KB (400×300 / 8) — well within the C6's RAM

---

## ESPHome Configuration (for Home Assistant)

If you want to use this with Home Assistant instead of Arduino, the native ESPHome component supports the BW version out of the box:

```yaml
esphome:
  name: epaper-display
  platform: ESP32
  board: esp32-c6-devkitc-1

spi:
  clk_pin: GPIO6
  mosi_pin: GPIO7

display:
  - platform: waveshare_epaper
    cs_pin: GPIO10
    dc_pin: GPIO18
    reset_pin: GPIO19
    busy_pin: GPIO20
    model: 4.20in-v2
    update_interval: 60s
    lambda: |-
      it.printf(200, 150, id(my_font), TextAlign::CENTER, "Hello from ESPHome!");

font:
  - file: "gfonts://Roboto"
    id: my_font
    size: 20
```

For the **BWR (3-color) version**, the native ESPHome component does NOT support it yet. You'll need the custom component from:  
<https://github.com/RaceNJason/WeAct-Studio_ePaper>

---

## Troubleshooting

| Issue | Solution |
|---|---|
| Display shows nothing | Verify wiring, especially CS and DC. Try swapping them if labels are ambiguous on your board |
| Display shows garbled image | Wrong GxEPD2 display class — make sure you're using `GDEY042T81` for BW or `GDEY042Z98` for BWR |
| ESP32 won't boot after wiring | You likely connected to a strapping pin. Check that GPIO4, 5, 8, 9, 15 are not wired to the display |
| Compile error on ESP32-C6 | Ensure you're using ESP32 Arduino Core **3.x** (not 2.x) — the C6 is not supported in older cores |
| ESPHome LOG_PIN compile error | Add semicolons after `LOG_PIN(...)` calls — this is a known macro change in recent ESPHome versions |
| Partial update not working | The 4.2" SSD1683 panel may not support fast partial updates. Use `display.setFullWindow()` for reliable full refreshes |

---

## References

- WeAct Studio GitHub: <https://github.com/WeActStudio/WeActStudio.EpaperModule>
- ESP32-C6-DevKitC-1 User Guide: <https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html>
- ESP32-C6 Pinout & Safe GPIOs: <https://esp32.co.uk/esp32-c6-devkitc-1-v1-2-pinout-gpio-reference/>
- GxEPD2 Library: <https://github.com/ZinggJM/GxEPD2>
- ESPHome Waveshare E-Paper: <https://esphome.io/components/display/waveshare_epaper/>
- WeAct Studio ESPHome Custom Component (BWR): <https://github.com/RaceNJason/WeAct-Studio_ePaper>
- HA Community Thread: <https://community.home-assistant.io/t/weact-studio-epaper-screens/916008>
