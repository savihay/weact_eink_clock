#pragma once

// WeAct 4.2" BW (SSD1683 / GDEY042T81) wired to ESP32-C6-DevKitC-1.
// Confirmed working in HelloWorld; HW reset is broken on the WeAct PCB
// (orange ribbon wire + bypass jumper both proven dead) — GxEPD2's SW reset
// path (cmd 0x12) is what actually drives the panel.

#define EPD_CS    10
#define EPD_DC    18
#define EPD_RST   11   // bypass jumper to WeAct P1 pin 2 — present but non-functional
#define EPD_BUSY  20
#define EPD_SCK    6
#define EPD_MOSI   7
#define EPD_MISO   2   // panel doesn't use MISO; pin reserved by SPI bus
