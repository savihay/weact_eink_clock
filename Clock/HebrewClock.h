#ifndef HEBREW_CLOCK_H
#define HEBREW_CLOCK_H

#include <Arduino.h>
#include <Adafruit_GFX.h>

// Custom UTF-8 text rendering for Hebrew with Nikud on Adafruit GFX displays.
// Standard Adafruit GFX only supports 8-bit characters; this module decodes
// UTF-8 and renders glyphs directly from the font bitmap data.

namespace HebrewClock {

  // Build the full Hebrew time string for the given hour/minute.
  void getTimeText(int hour, int minute, String &fullText);

  // Split a UTF-8 Hebrew string into 2 or 3 lines that all fit within maxWidth.
  // Splits on space or hyphen; chooses the layout (2 or 3 lines) and split
  // points that minimise the widest line. Falls back to a 2-line best-effort
  // when even a 3-line layout cannot fit (no time string actually triggers
  // this with the current font, but the fallback keeps the code robust).
  // line3 is set to "" when the chosen layout has only two lines.
  void splitForWidth(const GFXfont *font, const char *utf8str,
                     int16_t maxWidth,
                     String &line1, String &line2, String &line3);

  // Draw a UTF-8 string using the given GFX font, right-aligned at (x, y).
  // x is the RIGHT edge of the text area, y is the baseline.
  // This handles multi-byte UTF-8 codepoints for Hebrew (U+0590-U+05FF).
  void drawHebrewText(Adafruit_GFX &gfx, const GFXfont *font,
                      const char *utf8str, int16_t x, int16_t y,
                      uint16_t color);

  // Measure the pixel width of a UTF-8 string using the given GFX font.
  int16_t measureHebrewText(const GFXfont *font, const char *utf8str);

} // namespace HebrewClock

#endif // HEBREW_CLOCK_H
