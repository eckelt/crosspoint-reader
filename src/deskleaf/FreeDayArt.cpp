#include "FreeDayArt.h"

#if DESKLEAF_CALENDAR

#include <GfxRenderer.h>
#include <Logging.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace deskleaf::art {

namespace {

constexpr size_t kBytesPerRow = kFreeDayWidth / 8;
constexpr size_t kLayerBytes = kBytesPerRow * kFreeDayHeight;

// Expands one RLE layer into a plain 1-bit bitmap.
void decode(const unsigned char* rle, size_t len, uint8_t* out) {
  size_t pos = 0;
  for (size_t i = 0; i + 1 < len && pos < kLayerBytes; i += 2) {
    const size_t n = std::min<size_t>(rle[i], kLayerBytes - pos);
    std::memset(out + pos, rle[i + 1], n);
    pos += n;
  }
  if (pos < kLayerBytes) std::memset(out + pos, 0, kLayerBytes - pos);
}

bool layerBit(const uint8_t* layer, int sx, int sy) {
  return layer[sy * kBytesPerRow + sx / 8] & (0x80 >> (sx % 8));
}

// Stable per-pixel noise 0..255, so a redraw drops the same dots.
uint32_t pixelNoise(int x, int y) {
  uint32_t v = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
  v = (v ^ (v >> 13)) * 1274126177u;
  return (v ^ (v >> 16)) & 0xFF;
}

}  // namespace

int drawFreeDay(const GfxRenderer& r, const int x, const int y, const int maxW, const int maxH) {
  // Scale in 1/256 steps: as tall as fits, at most 1.25x. The widened sides
  // fade out anyway, so they may be clipped by maxW.
  const int scale = std::min(320, maxH * 256 / kFreeDayHeight);
  if (scale <= 0) return 0;
  const int w = kFreeDayWidth * scale / 256;
  const int h = kFreeDayHeight * scale / 256;

  // Three decoded layers, ~90 KB, in PSRAM for the duration of the draw.
  auto* buf = static_cast<uint8_t*>(heap_caps_malloc(3 * kLayerBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) buf = static_cast<uint8_t*>(malloc(3 * kLayerBytes));
  if (!buf) {
    LOG_ERR("DSK", "OOM: free-day art");
    return 0;
  }
  uint8_t* black = buf;
  uint8_t* red = buf + kLayerBytes;
  uint8_t* yellow = buf + 2 * kLayerBytes;
  decode(epd_bitmap_terry_moore_black_rle, epd_bitmap_terry_moore_black_rle_len, black);
  decode(epd_bitmap_terry_moore_red_rle, epd_bitmap_terry_moore_red_rle_len, red);
  decode(epd_bitmap_terry_moore_yellow_rle, epd_bitmap_terry_moore_yellow_rle_len, yellow);

  // The sides are faded in the data. Top and bottom ends fade here: a dot at
  // distance e from the edge stays with chance (e/fade)^2.
  const int fade = h / 16;
  const int fadeSq = fade * fade;

  const int x0 = x + (maxW - w) / 2;
  for (int dy = 0; dy < h; ++dy) {
    const int sy = dy * 256 / scale;
    for (int dx = std::max(0, x - x0); dx < std::min(w, x + maxW - x0); ++dx) {
      const int sx = dx * 256 / scale;
      const int px = x0 + dx;
      const int py = y + dy;
      // Black over red over yellow, as on the 4-color panel. Red becomes a
      // dark gray (3 of 4 dots), yellow a light gray (1 of 4 dots).
      bool ink = layerBit(black, sx, sy);
      if (!ink && layerBit(red, sx, sy)) ink = !((px & 1) && (py & 1));
      else if (!ink && layerBit(yellow, sx, sy)) ink = !(px & 1) && !(py & 1);
      if (ink && fade > 0) {
        const int e = std::min(dy, h - 1 - dy);
        if (e < fade && pixelNoise(px, py) * fadeSq >= static_cast<uint32_t>(e * e) * 256) ink = false;
      }
      if (ink) r.drawPixel(px, py, true);
    }
  }
  free(buf);
  return h;
}

}  // namespace deskleaf::art

#endif  // DESKLEAF_CALENDAR
