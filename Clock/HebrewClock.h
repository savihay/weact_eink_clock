#ifndef HEBREW_CLOCK_H
#define HEBREW_CLOCK_H

#include <Arduino.h>
#include <Adafruit_GFX.h>

// Custom UTF-8 text rendering for Hebrew with Nikud on Adafruit GFX displays.
// Standard Adafruit GFX only supports 8-bit characters; this module decodes
// UTF-8 and renders glyphs directly from the font bitmap data.

namespace HebrewClock {

  // Get the Hebrew time string for display (line 1: hour+minutes, line 2: period)
  // Returns two strings via the out parameters.
  void getTimeStrings(int hour, int minute, String &line1, String &line2);

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
