#pragma once
// A QR code as the sleep screen (QR-Codes app: long press pins or unpins one).
// While one is pinned, the calendar sleep screen and its timer syncs pause, so
// nothing draws over the code.
#include <string>

class GfxRenderer;

namespace deskleaf::qrsleep {

bool active();
// The pinned code's link, "" when none.
std::string pinnedLink();
bool pin(const std::string& title, const std::string& description, const std::string& link);
void unpin();
// Draws the pinned code full screen and refreshes the panel.
void render(GfxRenderer& renderer);

}  // namespace deskleaf::qrsleep
