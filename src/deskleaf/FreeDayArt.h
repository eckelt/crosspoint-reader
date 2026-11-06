#pragma once
// Free-day motif of the Deskleaf display, drawn in black and dithered grays.
#include <cstddef>

class GfxRenderer;

namespace deskleaf::art {

constexpr int kFreeDayWidth = 416;  // 272 px original, widened by 72 per side
constexpr int kFreeDayHeight = 578;

extern const size_t epd_bitmap_terry_moore_black_rle_len;
extern const unsigned char epd_bitmap_terry_moore_black_rle[];
extern const size_t epd_bitmap_terry_moore_red_rle_len;
extern const unsigned char epd_bitmap_terry_moore_red_rle[];
extern const size_t epd_bitmap_terry_moore_yellow_rle_len;
extern const unsigned char epd_bitmap_terry_moore_yellow_rle[];

// Draws the motif scaled to fit maxH (never above 1.25x), horizontally centered
// in [x, x + maxW) and clipped to it. Returns the drawn height, 0 if out of memory.
int drawFreeDay(const GfxRenderer& r, int x, int y, int maxW, int maxH);

}  // namespace deskleaf::art
