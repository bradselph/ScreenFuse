#include "../src/common.hpp"
#include "../src/image.hpp"

using namespace screenfuse;

namespace {

void Fill(Image& image, int x0, int y0, int w, int h, uint8_t b, uint8_t g, uint8_t r) {
    for (int y = y0; y < y0 + h && y < image.height; ++y) {
        for (int x = x0; x < x0 + w && x < image.width; ++x) {
            if (x < 0 || y < 0) continue;
            uint8_t* p = image.pixels.data() + (static_cast<size_t>(y) * image.width + x) * 4;
            p[0] = b; p[1] = g; p[2] = r; p[3] = 255;
        }
    }
}

void Outline(Image& image, int x0, int y0, int w, int h, int thickness, uint8_t b, uint8_t g, uint8_t r) {
    Fill(image, x0, y0, w, thickness, b, g, r);
    Fill(image, x0, y0 + h - thickness, w, thickness, b, g, r);
    Fill(image, x0, y0, thickness, h, b, g, r);
    Fill(image, x0 + w - thickness, y0, thickness, h, b, g, r);
}

} // namespace

int main(int argc, char** argv) {
    ComScope com;

    const std::wstring outDir = argc > 1 ? ToWide(argv[1]) : L".";

    Image remote;
    remote.allocate(1920, 1080);
    for (int y = 0; y < remote.height; ++y) {
        const uint8_t sky = static_cast<uint8_t>(200 - (140 * y) / remote.height);
        for (int x = 0; x < remote.width; ++x) {
            uint8_t* p = remote.pixels.data() + (static_cast<size_t>(y) * remote.width + x) * 4;
            p[0] = static_cast<uint8_t>(sky);
            p[1] = static_cast<uint8_t>(sky * 0.8);
            p[2] = static_cast<uint8_t>(sky * 0.6);
            p[3] = 255;
        }
    }
    Fill(remote, 0, 780, 1920, 300, 40, 60, 45);
    Fill(remote, 200, 500, 260, 300, 70, 70, 75);
    Fill(remote, 1300, 420, 380, 380, 25, 30, 35);
    Fill(remote, 900, 640, 120, 160, 90, 110, 130);

    Image overlay;
    overlay.allocate(1280, 720);

    Outline(overlay, 140, 330, 170, 200, 2, 40, 255, 40);
    Fill(overlay, 140, 322, 120, 5, 40, 40, 240);
    Fill(overlay, 640, 719 - 200, 2, 200, 255, 255, 255);
    Outline(overlay, 870, 280, 250, 250, 2, 255, 200, 0);
    Fill(overlay, 150, 300, 90, 12, 30, 30, 30);
    Fill(overlay, 150, 560, 90, 40, 6, 4, 4);
    Fill(overlay, 40, 40, 200, 100, 120, 120, 120);

    std::string error;
    if (!SavePng(outDir + L"\\test_remote.png", remote, error) ||
        !SavePng(outDir + L"\\test_overlay.png", overlay, error)) {
        LogErr("%s", error.c_str());
        return 1;
    }

    LogInfo("wrote test_remote.png (%dx%d) and test_overlay.png (%dx%d)",
        remote.width, remote.height, overlay.width, overlay.height);
    return 0;
}
