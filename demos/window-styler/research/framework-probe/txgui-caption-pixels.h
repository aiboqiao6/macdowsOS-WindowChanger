#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {
struct CaptionPixels {
    int width{}, height{};
    std::vector<DWORD> pixels;
    bool Read(HWND window, RECT rect) {
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
        if (width < 1 || width > 1024 || height < 1 || height > 256)
            return false;
        // Screen capture is restricted to the original system-button row.
        // Refuse covered/offscreen targets; never capture another application.
        for (POINT p : {POINT{rect.left, rect.top}, POINT{rect.right - 1, rect.bottom - 1}})
            if (GetAncestor(WindowFromPoint(p), GA_ROOT) != window)
                return false;
        DwmFlush();
        auto screen = GetDC(nullptr);
        auto dc = screen ? CreateCompatibleDC(screen) : nullptr;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void *bits{};
        auto bitmap = dc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
        auto old = bitmap ? SelectObject(dc, bitmap) : nullptr;
        bool ok = old && old != HGDI_ERROR && BitBlt(dc, 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY);
        GdiFlush();
        pixels.clear();
        if (ok) {
            auto data = static_cast<DWORD *>(bits);
            pixels.assign(data, data + static_cast<size_t>(width) * height);
            for (auto &pixel : pixels)
                pixel &= 0xffffff;
        }
        if (old && old != HGDI_ERROR)
            SelectObject(dc, old);
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
        if (screen)
            ReleaseDC(nullptr, screen);
        return ok;
    }
    bool Save(const std::filesystem::path &path) const {
        if (pixels.size() != static_cast<size_t>(width) * height || pixels.empty())
            return false;
        BITMAPFILEHEADER file{};
        BITMAPINFOHEADER info{};
        file.bfType = 0x4d42;
        file.bfOffBits = sizeof(file) + sizeof(info);
        file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size() * sizeof(DWORD));
        info.biSize = sizeof(info);
        info.biWidth = width;
        info.biHeight = -height;
        info.biPlanes = 1;
        info.biBitCount = 32;
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char *>(&file), sizeof(file));
        stream.write(reinterpret_cast<const char *>(&info), sizeof(info));
        stream.write(reinterpret_cast<const char *>(pixels.data()), pixels.size() * sizeof(DWORD));
        return stream.good();
    }
};
} // namespace
