#include "HebrewClock.h"

// ============================================================================
// UTF-8 Decoding & Custom Glyph Rendering
// ============================================================================

static uint16_t decodeUTF8(const char *&p) {
  uint8_t c = (uint8_t)*p;
  if (c == 0) return 0;

  if (c < 0x80) {
    p++;
    return c;
  } else if ((c & 0xE0) == 0xC0) {
    uint16_t cp = (c & 0x1F) << 6;
    p++;
    cp |= ((uint8_t)*p & 0x3F);
    p++;
    return cp;
  } else if ((c & 0xF0) == 0xE0) {
    uint16_t cp = (c & 0x0F) << 12;
    p++;
    cp |= ((uint8_t)*p & 0x3F) << 6;
    p++;
    cp |= ((uint8_t)*p & 0x3F);
    p++;
    return cp;
  }
  p++;
  return 0xFFFD;
}

// Get xAdvance for a glyph (0 for combining marks like Nikud)
static uint8_t getGlyphAdvance(const GFXfont *font, uint16_t codepoint) {
  uint16_t first = pgm_read_word(&font->first);
  uint16_t last  = pgm_read_word(&font->last);
  if (codepoint < first || codepoint > last) return 0;
  uint16_t idx = codepoint - first;
  GFXglyph *glyph = &((GFXglyph *)pgm_read_ptr(&font->glyph))[idx];
  return pgm_read_byte(&glyph->xAdvance);
}

// Draw a single glyph at position (x, y) baseline
static void drawGlyph(Adafruit_GFX &gfx, const GFXfont *font,
                       uint16_t codepoint, int16_t x, int16_t y,
                       uint16_t color) {
  uint16_t first = pgm_read_word(&font->first);
  uint16_t last  = pgm_read_word(&font->last);
  if (codepoint < first || codepoint > last) return;

  uint16_t idx = codepoint - first;
  GFXglyph *glyph = &((GFXglyph *)pgm_read_ptr(&font->glyph))[idx];
  uint8_t  *bitmap = (uint8_t *)pgm_read_ptr(&font->bitmap);

  uint16_t bo = pgm_read_word(&glyph->bitmapOffset);
  uint8_t  w  = pgm_read_byte(&glyph->width);
  uint8_t  h  = pgm_read_byte(&glyph->height);
  int8_t   xo = pgm_read_byte(&glyph->xOffset);
  int8_t   yo = pgm_read_byte(&glyph->yOffset);

  uint8_t bits = 0, bit = 0;
  for (uint8_t yy = 0; yy < h; yy++) {
    for (uint8_t xx = 0; xx < w; xx++) {
      if (!(bit++ & 7)) {
        bits = pgm_read_byte(&bitmap[bo++]);
      }
      if (bits & 0x80) {
        gfx.drawPixel(x + xo + xx, y + yo + yy, color);
      }
      bits <<= 1;
    }
  }
}

// Fixed space width (roughly 1/4 of yAdvance)
static int16_t getSpaceWidth(const GFXfont *font) {
  return pgm_read_byte(&font->yAdvance) / 4;
}

// Get the horizontal center offset of a glyph relative to its drawing origin
static int16_t getGlyphCenterOffset(const GFXfont *font, uint16_t codepoint) {
  uint16_t first = pgm_read_word(&font->first);
  uint16_t last  = pgm_read_word(&font->last);
  if (codepoint < first || codepoint > last) return 0;
  uint16_t idx = codepoint - first;
  GFXglyph *glyph = &((GFXglyph *)pgm_read_ptr(&font->glyph))[idx];
  uint8_t  w  = pgm_read_byte(&glyph->width);
  int8_t   xo = pgm_read_byte(&glyph->xOffset);
  return xo + (w / 2);
}

// ============================================================================
// Public API: Drawing & Measuring
// ============================================================================

int16_t HebrewClock::measureHebrewText(const GFXfont *font, const char *utf8str) {
  int16_t width = 0;
  const char *p = utf8str;
  while (*p) {
    uint16_t cp = decodeUTF8(p);
    if (cp == 0) break;
    if (cp == ' ' || cp == 0xA0) {
      width += getSpaceWidth(font);
    } else if (cp == '-') {
      width += getSpaceWidth(font);
    } else {
      width += getGlyphAdvance(font, cp);
    }
  }
  return width;
}

void HebrewClock::drawHebrewText(Adafruit_GFX &gfx, const GFXfont *font,
                                  const char *utf8str, int16_t x, int16_t y,
                                  uint16_t color) {
  // RTL rendering: x is the RIGHT edge of the text.
  // Hebrew text: first codepoint in string = rightmost character visually.
  // We draw from right to left: for each base char, subtract its advance
  // from cursor, then draw. Combining marks (xAdvance=0) overlay at same pos.

  int16_t cursorX = x;
  const char *p = utf8str;
  int16_t prevBaseCenterOffset = 0;
  uint16_t prevBaseCp = 0;
  bool hasPrevBase = false;

  while (*p) {
    uint16_t cp = decodeUTF8(p);
    if (cp == 0) break;

    if (cp == ' ' || cp == 0xA0) {
      cursorX -= getSpaceWidth(font);
      hasPrevBase = false;
    } else if (cp == '-') {
      int16_t w = getSpaceWidth(font);
      cursorX -= w;
      // Draw hyphen roughly in the middle of typical letter height
      // For 16pt font, letter height is ~18px, so h ~ 9px
      int16_t h = pgm_read_byte(&font->yAdvance) / 5; // ~8px for 43px yAdvance
      gfx.drawFastHLine(cursorX + 1, y - h, w - 2, color);
      hasPrevBase = false;
    } else {
      uint8_t advance = getGlyphAdvance(font, cp);
      int16_t drawX = cursorX;

      if (advance > 0) {
        // Base character: move cursor LEFT by advance, then draw
        cursorX -= advance;
        drawX = cursorX;
        prevBaseCenterOffset = getGlyphCenterOffset(font, cp);
        prevBaseCp = cp;
        hasPrevBase = true;
      } else {
        // Combining mark (Nikud)
        // Shift it so its center aligns with the previous base character's center
        if (hasPrevBase) {
          int16_t nikkudCenterOffset = getGlyphCenterOffset(font, cp);
          drawX = cursorX + prevBaseCenterOffset - nikkudCenterOffset;
          
          // Visual alignment corrections for Dagesh (U+05BC)
          if (cp == 0x05BC) {
            if (prevBaseCp == 0x05D1) { drawX -= 3; } // Bet: shift left
            else if (prevBaseCp == 0x05DB) { drawX -= 1; } // Kaf: slightly left
            else if (prevBaseCp == 0x05E4) { drawX -= 1; } // Pe: slightly left
            else if (prevBaseCp == 0x05EA) { drawX -= 2; } // Tav: slightly left
          }
          // Visual alignment correction for Shin dot (U+05C1)
          // Ensure it aligns with the upper-right arm of Shin (U+05E9)
          else if (cp == 0x05C1 && prevBaseCp == 0x05E9) {
            drawX += 5; // Shift right to sit on the right arm
          }
          // Visual alignment correction for Sin dot (U+05C2)
          // Ensure it aligns with the upper-left arm of Shin (U+05E9)
          else if (cp == 0x05C2 && prevBaseCp == 0x05E9) {
            drawX -= 3; // Shift left to sit on the left arm
          }
        }
      }
      // Draw glyph (both base chars and combining marks like Nikud)
      drawGlyph(gfx, font, cp, drawX, y, color);
    }
  }
}

// ============================================================================
// Hebrew Time Words with Nikud
// ============================================================================

// Hour names (feminine form, שָׁעָה is feminine)
static const char* const HOUR_NAMES[] = {
  "",                                        // 0 (unused)
  "\xD7\x90\xD6\xB7\xD7\x97\xD6\xB7\xD7\xAA",                    // 1: אַחַת
  "\xD7\xA9\xD7\x81\xD6\xB0\xD7\xAA\xD6\xBC\xD6\xB7\xD7\x99\xD6\xB4\xD7\x9D",  // 2: שְׁתַּיִם
  "\xD7\xA9\xD7\x81\xD6\xB8\xD7\x9C\xD7\x95\xD6\xB9\xD7\xA9\xD7\x81",          // 3: שָׁלוֹשׁ
  "\xD7\x90\xD6\xB7\xD7\xA8\xD6\xB0\xD7\x91\xD6\xBC\xD6\xB7\xD7\xA2",          // 4: אַרְבַּע
  "\xD7\x97\xD6\xB8\xD7\x9E\xD6\xB5\xD7\xA9\xD7\x81",                    // 5: חָמֵשׁ
  "\xD7\xA9\xD7\x81\xD6\xB5\xD7\xA9\xD7\x81",                            // 6: שֵׁשׁ
  "\xD7\xA9\xD7\x81\xD6\xB6\xD7\x91\xD6\xB7\xD7\xA2",                    // 7: שֶׁבַע
  "\xD7\xA9\xD7\x81\xD6\xB0\xD7\x9E\xD7\x95\xD6\xB9\xD7\xA0\xD6\xB6\xD7\x94",  // 8: שְׁמוֹנֶה
  "\xD7\xAA\xD6\xBC\xD6\xB5\xD7\xA9\xD7\x81\xD6\xB7\xD7\xA2",                  // 9: תֵּשַׁע
  "\xD7\xA2\xD6\xB6\xD7\xA9\xD6\xB2\xD7\xA8",                            // 10: עֶשֶׂר
  "\xD7\x90\xD6\xB7\xD7\x97\xD6\xB7\xD7\xAA-\xD7\xA2\xD6\xB6\xD7\xA9\xD6\xB0\xD7\xA8\xD6\xB5\xD7\x94",  // 11: אַחַת-עֶשְׂרֵה
  "\xD7\xA9\xD7\x81\xD6\xB0\xD7\xAA\xD6\xBC\xD6\xB5\xD7\x99\xD7\x9D-\xD7\xA2\xD6\xB6\xD7\xA9\xD6\xB0\xD7\xA8\xD6\xB5\xD7\x94"   // 12: שְׁתֵּים-עֶשְׂרֵה
};

// Ones for minutes (feminine, matching hour forms)
static const char* const ONES_FEM[] = {
  "",
  "\xD7\x90\xD6\xB7\xD7\x97\xD6\xB7\xD7\xAA",          // 1: אַחַת
  "\xD7\xA9\xD7\x81\xD6\xB0\xD7\xAA\xD6\xBC\xD6\xB7\xD7\x99\xD6\xB4\xD7\x9D",  // 2: שְׁתַּיִם
  "\xD7\xA9\xD7\x81\xD6\xB8\xD7\x9C\xD7\x95\xD6\xB9\xD7\xA9\xD7\x81",          // 3: שָׁלוֹשׁ
  "\xD7\x90\xD6\xB7\xD7\xA8\xD6\xB0\xD7\x91\xD6\xBC\xD6\xB7\xD7\xA2",          // 4: אַרְבַּע
  "\xD7\x97\xD6\xB8\xD7\x9E\xD6\xB5\xD7\xA9\xD7\x81",                    // 5: חָמֵשׁ
  "\xD7\xA9\xD7\x81\xD6\xB5\xD7\xA9\xD7\x81",                            // 6: שֵׁשׁ
  "\xD7\xA9\xD7\x81\xD6\xB6\xD7\x91\xD6\xB7\xD7\xA2",                    // 7: שֶׁבַע
  "\xD7\xA9\xD7\x81\xD6\xB0\xD7\x9E\xD7\x95\xD6\xB9\xD7\xA0\xD6\xB6\xD7\x94",  // 8: שְׁמוֹנֶה
  "\xD7\xAA\xD6\xBC\xD6\xB5\xD7\xA9\xD7\x81\xD6\xB7\xD7\xA2",                  // 9: תֵּשַׁע
};

// Tens
static const char* const TENS[] = {
  "",
  "\xD7\xA2\xD6\xB6\xD7\xA9\xD6\xB2\xD7\xA8",                      // 10: עֶשֶׂר
  "\xD7\xA2\xD6\xB6\xD7\xA9\xD6\xB0\xD7\xA8\xD6\xB4\xD7\x99\xD7\x9D",              // 20: עֶשְׂרִים
  "\xD7\xA9\xD7\x81\xD6\xB0\xD7\x9C\xD7\x95\xD6\xB9\xD7\xA9\xD7\x81\xD6\xB4\xD7\x99\xD7\x9D",  // 30: שְׁלוֹשִׁים
  "\xD7\x90\xD6\xB7\xD7\xA8\xD6\xB0\xD7\x91\xD6\xBC\xD6\xB8\xD7\xA2\xD6\xB4\xD7\x99\xD7\x9D",  // 40: אַרְבָּעִים
  "\xD7\x97\xD6\xB2\xD7\x9E\xD6\xB4\xD7\xA9\xD7\x81\xD6\xBC\xD6\xB4\xD7\x99\xD7\x9D",          // 50: חֲמִשִּׁים
};

// Teens (11-19) - feminine forms for time
static const char* const TEENS[] = {
  "",                                                                           // 10: (use TENS[1])
  "\xD7\x90\xD6\xB7\xD7\x97\xD6\xB7\xD7\xAA-\xD7\xA2\xD6\xB6\xD7\xA9\xD6\xB0\xD7\xA8\xD6\xB5\xD7\x94",  // 11: אַחַת-עֶשְׂרֵה
  "\xD7\xA9\xD7\x81\xD6\xB0\xD7\xAA\xD6\xBC\xD6\xB5\xD7\x99\xD7\x9D-\xD7\xA2\xD6\xB6\xD7\xA9\xD6\xB0\xD7\xA8\xD6\xB5\xD7\x94",  // 12: שְׁתֵּים-עֶשְׂרֵה
  "\xD7\xA9\xD7\x81\xD6\xB0\xD7\x9C\xD7\x95\xD6\xB9\xD7\xA9\xD7\x81-\xD7\xA2\xD6\xB6\xD7\xA9\xD6\xB0\xD7\xA8\xD6\xB5\xD7\x94",  // 13: שְׁלוֹשׁ-עֶשְׂרֵה
  "\xD7\x90\xD6\xB7\xD7\xA8\xD6\xB0\xD7\x91\xD6\xBC\xD6\xB7\xD7\xA2-\xD7\xA2\xD6\xB6\xD7\xA9\xD6\xB0\xD7\xA8\xD6\xB5\xD7\x94",  // 14: אַרְבַּע-עֶשְׂרֵה
  "\xD7\x97\xD6\xB2\xD7\x9E\xD6\xB5\xD7\xA9\xD7\x81-\xD7\xA2\xD6\xB6\xD7\xA9\xD6\xB0\xD7\xA8\xD6\xB5\xD7\x94",                  // 15: חֲמֵשׁ-עֶשְׂרֵה
  "\xD7\xA9\xD7\x81\xD6\xB5\xD7\xA9\xD7\x81-\xD7\xA2\xD6\xB6\xD7\xA9\xD6\xB0\xD7\xA8\xD6\xB5\xD7\x94",                          // 16: שֵׁשׁ-עֶשְׂרֵה
  "\xD7\xA9\xD7\x81\xD6\xB0\xD7\x91\xD6\xB7\xD7\xA2-\xD7\xA2\xD6\xB6\xD7\xA9\xD6\xB0\xD7\xA8\xD6\xB5\xD7\x94",                  // 17: שְׁבַע-עֶשְׂרֵה
  "\xD7\xA9\xD7\x81\xD6\xB0\xD7\x9E\xD7\x95\xD6\xB9\xD7\xA0\xD6\xB6\xD7\x94-\xD7\xA2\xD6\xB6\xD7\xA9\xD6\xB0\xD7\xA8\xD6\xB5\xD7\x94",  // 18: שְׁמוֹנֶה-עֶשְׂרֵה
  "\xD7\xAA\xD6\xBC\xD6\xB5\xD7\xA9\xD7\x81\xD6\xB7\xD7\xA2-\xD7\xA2\xD6\xB6\xD7\xA9\xD6\xB0\xD7\xA8\xD6\xB5\xD7\x94",                  // 19: תֵּשַׁע-עֶשְׂרֵה
};

// Time-of-day period names
static const char* PERIOD_MORNING       = "\xD7\x91\xD6\xBC\xD6\xB7\xD7\x91\xD6\xBC\xD7\x95\xD6\xB9\xD7\xA7\xD6\xB6\xD7\xA8";    // בַּבֹּקֶר
static const char* PERIOD_EARLY_MORNING = "\xD7\x9C\xD6\xB4\xD7\xA4\xD6\xB0\xD7\xA0\xD7\x95\xD6\xB9\xD7\xAA \xD7\x91\xD6\xB9\xD6\xBC\xD7\xA7\xD6\xB6\xD7\xA8";  // לִפְנוֹת בֹּקֶר
static const char* PERIOD_NOON          = "\xD7\x91\xD6\xBC\xD6\xB7\xD7\xA6\xD6\xBC\xD6\xB3\xD7\x94\xD6\xB3\xD7\xA8\xD6\xB7\xD7\x99\xD6\xB4\xD7\x9D"; // בַּצָּהֳרַיִם
static const char* PERIOD_AFTERNOON     = "\xD7\x90\xD6\xB7\xD7\x97\xD6\xB7\xD7\xA8-\xD7\x94\xD6\xB7\xD7\xA6\xD6\xB8\xD6\xBC\xD7\x94\xD6\xB3\xD7\xA8\xD6\xB7\xD7\x99\xD6\xB4\xD7\x9D"; // אַחַר-הַצָּהֳרַיִם
static const char* PERIOD_EVENING       = "\xD7\x91\xD6\xBC\xD6\xB8\xD7\xA2\xD6\xB6\xD7\xA8\xD6\xB6\xD7\x91";      // בָּעֶרֶב
static const char* PERIOD_NIGHT         = "\xD7\x91\xD6\xBC\xD6\xB7\xD7\x9C\xD6\xBC\xD6\xB7\xD7\x99\xD6\xB0\xD7\x9C\xD6\xB8\xD7\x94";  // בַּלַּיְלָה

// Conjunction "and" prefix
static const char* VE = "\xD7\x95\xD6\xB0";  // וְ

// ============================================================================
// Time to Words Conversion
// ============================================================================

static const char* getPeriod(int hour24) {
  if (hour24 >= 4 && hour24 < 6)   return PERIOD_EARLY_MORNING; // 4 to 6
  if (hour24 >= 6 && hour24 < 12)  return PERIOD_MORNING;       // 6 to 12
  if (hour24 >= 12 && hour24 < 16) return PERIOD_NOON;          // 12 to 16
  if (hour24 >= 16 && hour24 < 18) return PERIOD_AFTERNOON;     // 16 to 19
  if (hour24 >= 18 && hour24 < 22) return PERIOD_EVENING;       // 19 to 23
  return PERIOD_NIGHT;                                          // 23 to 4
}

static String minutesToHebrew(int minutes) {
  if (minutes == 0) return "";
  
  if (minutes == 1) return "\xD7\x95\xD6\xB0\xD7\x93\xD6\xB7\xD7\xA7\xD6\xBC\xD6\xB8\xD7\x94"; // וְדַקָּה
  if (minutes == 2) return "\xD7\x95\xD6\xBC\xD7\xA9\xD7\x81\xD6\xB0\xD7\xAA\xD6\xB5\xD7\x99 \xD7\x93\xD6\xB7\xD7\xA7\xD6\xBC\xD7\x95\xD6\xB9\xD7\xAA"; // וּשְׁתֵּי דַקּוֹת
  if (minutes == 5) return "\xD7\x95\xD6\xB7\xD7\x97\xD6\xB2\xD7\x9E\xD6\xB4\xD7\xA9\xD7\x81\xD6\xBC\xD6\xB8\xD7\x94"; // וַחֲמִשָּׁה
  if (minutes == 10) return "\xD7\x95\xD6\xB7\xD7\xA2\xD6\xB2\xD7\xA9\xD6\xB8\xD7\x82\xD7\xA8\xD6\xB8\xD7\x94"; // וַעֲשָׂרָה
  if (minutes == 15) return "\xD7\x95\xD6\xB8\xD7\xA8\xD6\xB6\xD7\x91\xD6\xB7\xD7\xA2"; // וָרֶבַע
  if (minutes == 20) return "\xD7\x95\xD6\xB0\xD7\xA2\xD6\xB6\xD7\xA9\xD6\xB0\xD7\xA8\xD6\xB4\xD7\x99\xD7\x9D"; // וְעֶשְׂרִים
  if (minutes == 30) return "\xD7\x95\xD6\xB8\xD7\x97\xD6\xB5\xD7\xA6\xD6\xB4\xD7\x99"; // וָחֵצִי

  String dakot = " \xD7\x93\xD6\xB7\xD7\xA7\xD6\xBC\xD7\x95\xD6\xB9\xD7\xAA"; // " דַקּוֹת"

  // 1-9: וְ + ones
  if (minutes <= 9) return String(VE) + String(ONES_FEM[minutes]) + dakot;

  // 11-19: וְ + teen
  if (minutes <= 19) return String(VE) + String(TEENS[minutes - 10]) + dakot;

  int tens = minutes / 10;
  int ones = minutes % 10;

  // Exact tens (40,50): וְ + tens
  if (ones == 0) return String(VE) + String(TENS[tens]);

  // Tens + ones: tens + וְ + ones  (e.g., אַרְבָּעִים וְחָמֵשׁ)
  // Reversing the order to "tens + ve + ones" (which maps to TENS[tens] + " " + VE + ONES_FEM[ones])
  return String(TENS[tens]) + " " + String(VE) + String(ONES_FEM[ones]);
}

void HebrewClock::getTimeText(int hour24, int minute, String &fullText) {
  int hour12 = hour24 % 12;
  if (hour12 == 0) hour12 = 12;

  String hourStr;
  if (hour24 == 0) {
    hourStr = "\xD7\x97\xD6\xB2\xD7\xA6\xD7\x95\xD6\xB9\xD7\xAA"; // חֲצוֹת
  } else {
    hourStr = HOUR_NAMES[hour12];
  }

  String minStr = minutesToHebrew(minute);
  String periodStr = getPeriod(hour24);

  if (minute == 45) {
    // "Quarter to next hour"
    int nextHour24 = (hour24 + 1) % 24;
    int nextHour12 = nextHour24 % 12;
    if (nextHour12 == 0) nextHour12 = 12;

    if (nextHour24 == 0) {
      // Quarter to Midnight: רֶבַע לַחֲצוֹת (reva la'chatzot)
      fullText = "\xD7\xA8\xD6\xB6\xD7\x91\xD6\xB7\xD7\xA2 \xD7\x9C\xD6\xB7\xD7\x97\xD6\xB2\xD7\xA6\xD7\x95\xD6\xB9\xD7\xAA";
    } else {
      // Quarter to X: רֶבַע לְ... (reva l'...)
      String nextHourStr = HOUR_NAMES[nextHour12];
      periodStr = getPeriod(nextHour24); // Use the period of the next hour
      fullText = "\xD7\xA8\xD6\xB6\xD7\x91\xD6\xB7\xD7\xA2 \xD7\x9C\xD6\xB0" + nextHourStr + " " + periodStr;
    }
  } else if (hour24 == 0) {
    // Midnight drops the period ("בלילה") completely
    if (minute == 0) {
      fullText = hourStr;
    } else {
      fullText = hourStr + " " + minStr;
    }
  } else {
    if (minute == 0) {
      fullText = hourStr + " " + periodStr;
    } else {
      fullText = hourStr + " " + minStr + " " + periodStr;
    }
  }
}

// ============================================================================
// Pixel-balanced line splitting
// ============================================================================
//
// We split a Hebrew time string at spaces or hyphens. Hyphens are kept on the
// left line so a hyphenated compound number reads naturally as a continued
// word ("...שמונה-" / "עשרה...").
//
// The number of lines is chosen automatically: try 2 first, fall back to 3 if
// the widest 2-line option doesn't fit. The vast majority of times fit in 2
// lines; only the long noon/midnight cases (~32% at 28pt) need 3.

namespace {

struct SplitPoint {
  int   index;       // byte index of the split character
  bool  keepOnLeft;  // true for hyphen (drop nothing), false for space (drop it)
};

void collectSplitPoints(const String &text, SplitPoint *out, int &count, int maxCount) {
  count = 0;
  for (int i = 0; i < (int)text.length() && count < maxCount; i++) {
    char c = text[i];
    if (c == ' ') {
      out[count++] = { i, false };
    } else if (c == '-') {
      out[count++] = { i, true };
    }
  }
}

String sliceForLine(const String &text, int start, const SplitPoint &p) {
  // Take from `start` up to (and possibly including) the split char.
  return p.keepOnLeft ? text.substring(start, p.index + 1)
                      : text.substring(start, p.index);
}

} // namespace

void HebrewClock::splitForWidth(const GFXfont *font, const char *utf8str,
                                int16_t maxWidth,
                                String &line1, String &line2, String &line3) {
  String text(utf8str);
  line1 = text;
  line2 = "";
  line3 = "";

  // Cap at 16 split points — far more than any time string produces.
  SplitPoint pts[16];
  int n = 0;
  collectSplitPoints(text, pts, n, 16);

  if (n == 0) return;

  // Try 2 lines: pick split that minimises the widest line.
  int best2Max = INT16_MAX;
  int best2Idx = -1;
  for (int i = 0; i < n; i++) {
    String l1 = sliceForLine(text, 0, pts[i]);
    String l2 = text.substring(pts[i].index + 1);
    int w = max(measureHebrewText(font, l1.c_str()),
                measureHebrewText(font, l2.c_str()));
    if (w < best2Max) { best2Max = w; best2Idx = i; }
  }

  if (best2Max <= maxWidth || n < 2) {
    line1 = sliceForLine(text, 0, pts[best2Idx]);
    line2 = text.substring(pts[best2Idx].index + 1);
    return;
  }

  // 2 lines don't fit — try 3.
  int best3Max = INT16_MAX;
  int best3a = -1, best3b = -1;
  for (int a = 0; a < n - 1; a++) {
    for (int b = a + 1; b < n; b++) {
      String l1  = sliceForLine(text, 0, pts[a]);
      String mid = sliceForLine(text, pts[a].index + 1, pts[b]);
      String l3  = text.substring(pts[b].index + 1);
      int w = max(max(measureHebrewText(font, l1.c_str()),
                      measureHebrewText(font, mid.c_str())),
                      measureHebrewText(font, l3.c_str()));
      if (w < best3Max) { best3Max = w; best3a = a; best3b = b; }
    }
  }

  line1 = sliceForLine(text, 0, pts[best3a]);
  line2 = sliceForLine(text, pts[best3a].index + 1, pts[best3b]);
  line3 = text.substring(pts[best3b].index + 1);
}

