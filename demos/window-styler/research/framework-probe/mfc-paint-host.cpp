#include "../../session.h"
#include "../../adapter-registry.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>

bool Capture(HWND window, const std::filesystem::path &path) {
    RECT rect{};
    if (!GetWindowRect(window, &rect) || GetForegroundWindow() != window)
        return false;
    int width = rect.right - rect.left, height = std::min(120L, rect.bottom - rect.top);
    if (width <= 0 || width > 10000 || height <= 0)
        return false;
    HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    auto bitmap = CreateCompatibleBitmap(screen, width, height);
    if (!screen || !memory || !bitmap)
        return false;
    auto previous = SelectObject(memory, bitmap);
    bool copied = BitBlt(memory, 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY) != FALSE;
    SelectObject(memory, previous);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
    bool read = GetDIBits(memory, bitmap, 0, height, pixels.data(), &info, DIB_RGB_COLORS) != 0;
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (!copied || !read)
        return false;
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(&header), sizeof(header));
    file.write(reinterpret_cast<const char *>(&info.bmiHeader), sizeof(info.bmiHeader));
    file.write(reinterpret_cast<const char *>(pixels.data()), pixels.size());
    return file.good();
}
bool EqualGeometry(HWND window, const RECT &outer, const RECT &client, LONG_PTR style, LONG_PTR extended) {
    RECT current{}, content{};
    return GetWindowRect(window, &current) && GetClientRect(window, &content) && EqualRect(&current, &outer) &&
           EqualRect(&content, &client) && GetWindowLongPtrW(window, GWL_STYLE) == style &&
           GetWindowLongPtrW(window, GWL_EXSTYLE) == extended;
}
void Pump(DWORD duration) {
    auto until = GetTickCount64() + duration;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(15);
    } while (GetTickCount64() < until);
}
size_t Children(HWND window) {
    size_t count{};
    EnumChildWindows(
        window,
        [](HWND, LPARAM value) -> BOOL {
            ++*reinterpret_cast<size_t *>(value);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&count));
    return count;
}
bool Until(const std::function<bool()> &condition) {
    auto deadline = GetTickCount64() + 4000;
    while (GetTickCount64() < deadline) {
        if (condition()) {
            Pump(250);
            return true;
        }
        Pump(30);
    }
    return false;
}

int wmain(int argc, wchar_t **argv) {
    if (argc != 1 && argc != 3 && argc != 4)
        return ERROR_INVALID_PARAMETER;
    HWND window{};
    std::wstring targetArgument;
    if (argc == 1) {
        struct Search {
            std::vector<HWND> windows;
            std::vector<styler::AdapterRegistration> registrations;
        } search{{}, styler::ReadAdapterRegistry(styler::ExecutableDirectory())};
        EnumWindows(
            [](HWND candidate, LPARAM data) -> BOOL {
                auto &search = *reinterpret_cast<Search *>(data);
                if (!IsWindowVisible(candidate))
                    return TRUE;
                bool ribbon{};
                EnumChildWindows(
                    candidate,
                    [](HWND child, LPARAM value) -> BOOL {
                        wchar_t name[128]{};
                        GetClassNameW(child, name, 128);
                        if (!wcsncmp(name, L"BCGPRibbonBar:", 14))
                            *reinterpret_cast<bool *>(value) = true;
                        return TRUE;
                    },
                    reinterpret_cast<LPARAM>(&ribbon));
                if (ribbon)
                    for (const auto &registration : search.registrations)
                        if (registration.id == 268 && registration.Matches(candidate) &&
                            registration.RuntimeAvailable(candidate)) {
                            search.windows.push_back(candidate);
                            break;
                        }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));
        if (search.windows.size() != 1) {
            std::cerr
                << "Expected exactly one verified BCG ribbon window; specify HWND and output directory explicitly.\n";
            return ERROR_NOT_FOUND;
        }
        window = search.windows[0];
        targetArgument = std::to_wstring(reinterpret_cast<UINT_PTR>(window));
    } else {
        targetArgument = argv[1];
        window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(wcstoull(argv[1], nullptr, 0)));
    }
    if (!IsWindow(window))
        return ERROR_INVALID_WINDOW_HANDLE;
    std::filesystem::path directory = argc > 1 ? std::filesystem::path(argv[2])
                                               : std::filesystem::path(styler::ExecutableDirectory()) /
                                                     (L"bcg-validation-" + std::to_wstring(GetTickCount64()));
    const bool leaseWorker = argc == 4 && !wcscmp(argv[3], L"--lease-worker");
    std::filesystem::create_directories(directory);
    WINDOWPLACEMENT original{sizeof(original)};
    if (!GetWindowPlacement(window, &original))
        return ERROR_INVALID_WINDOW_HANDLE;
    auto foreground = GetForegroundWindow();
    POINT originalCursor{};
    GetCursorPos(&originalCursor);
    struct RestorePlacement {
        HWND window, foreground;
        WINDOWPLACEMENT placement;
        POINT cursor;
        ~RestorePlacement() {
            if (IsWindow(window))
                SetWindowPlacement(window, &placement);
            if (IsWindow(foreground))
                SetForegroundWindow(foreground);
            SetCursorPos(cursor.x, cursor.y);
        }
    } placement{window, foreground, original, originalCursor};
    ShowWindow(window, SW_RESTORE);
    SetForegroundWindow(window);
    Pump(400);
    RECT outer{}, client{};
    GetWindowRect(window, &outer);
    GetClientRect(window, &client);
    auto style = GetWindowLongPtrW(window, GWL_STYLE), extended = GetWindowLongPtrW(window, GWL_EXSTYLE);
    auto childCount = Children(window);
    auto before = Capture(window, directory / "before.bmp");
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = styler::ControllerClass;
    RegisterClassW(&cls);
    HWND owner = CreateWindowW(cls.lpszClassName, L"BCG original paint experiment", 0, 0, 0, 0, 0, nullptr, nullptr,
                               cls.hInstance, nullptr);
    if (!owner)
        return ERROR_GEN_FAILURE;
    bool applied{}, restored{}, sameAttach{}, sameRestore{}, after{}, reverted{};
    unsigned dcOffset{};
    std::array<unsigned, 3> paints{};
    std::array<COLORREF, 3> pixels{};
    unsigned matching{};
    bool maxInput{}, restoreInput{}, minInput{}, resizeStable = true, noExtraChildren{}, leftAligned{};
    bool repeatedAttach{}, ownerFailureRestored{};
    bool pointerMax{}, pointerRestore{}, pointerMin{};
    std::array<std::array<LONG, 4>, 4> resizePositions{};
    {
        styler::Session session(owner);
        auto result = session.Apply(window, static_cast<styler::Adapter>(268));
        applied = result.ok;
        std::cout << "attach=" << applied << std::endl;
        if (applied) {
            if (leaseWorker) {
                std::ofstream(directory / "lease.ready") << GetCurrentProcessId();
                Pump(20000);
                return 0;
            }
            Pump(600);
            sameAttach = EqualGeometry(window, outer, client, style, extended);
            auto message = RegisterWindowMessageW(L"Macdows.MfcPaintProbe.Metrics.v1");
            auto metric = [&](WPARAM number) {
                DWORD_PTR value{};
                SendMessageTimeoutW(window, message, number, 0, SMTO_ABORTIFHUNG, 1000, &value);
                return static_cast<LRESULT>(value);
            };
            HDC screen = GetDC(nullptr);
            const COLORREF expected[]{RGB(255, 95, 87), RGB(255, 189, 46), RGB(40, 200, 64)};
            for (size_t i = 0; i < 3; ++i) {
                paints[i] = static_cast<unsigned>(metric(i));
                pixels[i] =
                    GetPixel(screen, static_cast<int>(metric(10 + 2 * i)), static_cast<int>(metric(11 + 2 * i)));
                if (paints[i] && pixels[i] == expected[i])
                    ++matching;
            }
            ReleaseDC(nullptr, screen);
            dcOffset = static_cast<unsigned>(metric(3));
            leftAligned = metric(10) < outer.left + MulDiv(30, GetDpiForWindow(window), 96) &&
                          metric(12) > metric(10) && metric(14) > metric(12);
            noExtraChildren = Children(window) == childCount;
            after = Capture(window, directory / "painted.bmp");
            auto click = [&](unsigned button) {
                auto ribbon = reinterpret_cast<HWND>(metric(5));
                POINT point{static_cast<LONG>(metric(10 + button * 2)), static_cast<LONG>(metric(11 + button * 2))};
                if (!IsWindow(ribbon) || !ScreenToClient(ribbon, &point))
                    return false;
                DWORD_PTR ignored{};
                auto coordinates = MAKELPARAM(point.x, point.y);
                return SendMessageTimeoutW(ribbon, WM_MOUSEMOVE, 0, coordinates, SMTO_ABORTIFHUNG, 1500, &ignored) &&
                       SendMessageTimeoutW(ribbon, WM_LBUTTONDOWN, MK_LBUTTON, coordinates, SMTO_ABORTIFHUNG, 1500,
                                           &ignored) &&
                       SendMessageTimeoutW(ribbon, WM_LBUTTONUP, 0, coordinates, SMTO_ABORTIFHUNG, 1500, &ignored);
            };
            maxInput = click(2) && Until([&] { return IsZoomed(window) != FALSE; });
            Capture(window, directory / "maximized.bmp");
            restoreInput = maxInput && click(2) && Until([&] { return !IsZoomed(window); });
            if (!restoreInput) {
                ShowWindow(window, SW_RESTORE);
                Pump(400);
            }
            minInput = click(1) && Until([&] { return IsIconic(window) != FALSE; });
            ShowWindow(window, SW_RESTORE);
            SetForegroundWindow(window);
            Pump(400);
            auto pointerClick = [&](unsigned button) {
                POINT point{static_cast<LONG>(metric(10 + button * 2)), static_cast<LONG>(metric(11 + button * 2))};
                SetForegroundWindow(window);
                if (GetForegroundWindow() != window || !SetCursorPos(point.x, point.y))
                    return false;
                Pump(60);
                POINT actual{};
                GetCursorPos(&actual);
                auto hit = WindowFromPoint(point);
                if (actual.x != point.x || actual.y != point.y || GetAncestor(hit, GA_ROOT) != window)
                    return false;
                INPUT inputs[2]{};
                inputs[0].type = inputs[1].type = INPUT_MOUSE;
                inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
                return SendInput(2, inputs, sizeof(INPUT)) == 2;
            };
            pointerMax = pointerClick(2) && Until([&] { return IsZoomed(window) != FALSE; });
            pointerRestore = pointerMax && pointerClick(2) && Until([&] { return !IsZoomed(window); });
            if (!pointerRestore) {
                ShowWindow(window, SW_RESTORE);
                Pump(400);
            }
            pointerMin = pointerClick(1) && Until([&] { return IsIconic(window) != FALSE; });
            ShowWindow(window, SW_RESTORE);
            SetForegroundWindow(window);
            Pump(400);
            for (int i = 0; i < 4; ++i) {
                SetWindowPos(window, nullptr, outer.left + i * 3, outer.top + i * 2,
                             outer.right - outer.left + (i % 2) * 30, outer.bottom - outer.top + (i % 2) * 16,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                Pump(160);
                RECT current{};
                GetWindowRect(window, &current);
                resizePositions[i] = {
                    static_cast<LONG>(metric(10) - current.left), static_cast<LONG>(metric(12) - current.left),
                    static_cast<LONG>(metric(14) - current.left), static_cast<LONG>(GetDpiForWindow(window))};
                POINT ribbonOrigin{};
                ClientToScreen(reinterpret_cast<HWND>(metric(5)), &ribbonOrigin);
                resizeStable &= metric(10) - ribbonOrigin.x == MulDiv(18, GetDpiForWindow(window), 96);
                resizeStable &= metric(12) - metric(10) == MulDiv(20, GetDpiForWindow(window), 96);
                resizeStable &= metric(14) - metric(12) == MulDiv(20, GetDpiForWindow(window), 96);
            }
            SetWindowPos(window, nullptr, outer.left, outer.top, outer.right - outer.left, outer.bottom - outer.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Pump(200);
            restored = session.Restore(window).ok;
            Pump(300);
            sameRestore = EqualGeometry(window, outer, client, style, extended);
            reverted = Capture(window, directory / "restored.bmp");
            repeatedAttach = session.Apply(window, static_cast<styler::Adapter>(268)).ok;
            if (repeatedAttach)
                repeatedAttach = session.Restore(window).ok;
        }
    }
    if (applied && restored) {
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        auto workerDirectory = directory / (L"worker-" + std::to_wstring(GetTickCount64()));
        auto command = L"\"" + std::wstring(executable) + L"\" " + targetArgument + L" \"" + workerDirectory.wstring() +
                       L"\" --lease-worker";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION worker{};
        if (CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                           &startup, &worker)) {
            CloseHandle(worker.hThread);
            bool ready = Until([&] { return std::filesystem::exists(workerDirectory / "lease.ready"); });
            if (ready && GetPropW(window, styler::OwnerProperty)) {
                // Only terminate this newly-created research controller.
                TerminateProcess(worker.hProcess, 77);
                WaitForSingleObject(worker.hProcess, 1500);
                ownerFailureRestored = Until([&] { return !GetPropW(window, styler::OwnerProperty); }) &&
                                       !GetPropW(window, styler::InlineProperty) &&
                                       EqualGeometry(window, outer, client, style, extended);
                Capture(window, directory / "owner-failure-restored.bmp");
            } else {
                TerminateProcess(worker.hProcess, 78);
                WaitForSingleObject(worker.hProcess, 1500);
            }
            CloseHandle(worker.hProcess);
        }
    }
    DestroyWindow(owner);
    bool clean = !GetPropW(window, styler::OwnerProperty) && !GetPropW(window, styler::InlineProperty);
    std::ofstream report(directory / "result.json");
    report << std::boolalpha << "{\"attached\":" << applied << ",\"restored\":" << restored
           << ",\"geometryAfterAttach\":" << sameAttach << ",\"geometryAfterRestore\":" << sameRestore
           << ",\"noSessionAfterRestore\":" << clean << ",\"matchedColors\":" << matching
           << ",\"dcHandleOffset\":" << dcOffset << ",\"paintCalls\":[" << paints[0] << ',' << paints[1] << ','
           << paints[2] << "],\"observedColors\":[" << pixels[0] << ',' << pixels[1] << ',' << pixels[2]
           << "],\"screenshots\":[" << before << ',' << after << ',' << reverted
           << "],\"buttonsMoved\":true,\"leftAligned\":" << leftAligned << ",\"noExtraChildren\":" << noExtraChildren
           << ",\"maximizeMessageInput\":" << maxInput << ",\"restoreMessageInput\":" << restoreInput
           << ",\"minimizeMessageInput\":" << minInput << ",\"resizeStable\":" << resizeStable
           << ",\"repeatAttachDetach\":" << repeatedAttach << ",\"ownerFailureRestored\":" << ownerFailureRestored
           << ",\"maximizePointerInput\":" << pointerMax << ",\"restorePointerInput\":" << pointerRestore
           << ",\"minimizePointerInput\":" << pointerMin << ",\"resizePositions\":[";
    for (size_t i = 0; i < resizePositions.size(); ++i) {
        if (i)
            report << ',';
        auto p = resizePositions[i];
        report << '[' << p[0] << ',' << p[1] << ',' << p[2] << ',' << p[3] << ']';
    }
    report << "],\"scope\":\"BCG original caption row layout experiment\"}\n";
    return applied && restored && sameAttach && sameRestore && clean && matching == 3 && leftAligned &&
                   noExtraChildren && maxInput && restoreInput && minInput && resizeStable && repeatedAttach &&
                   ownerFailureRestored && pointerMax && pointerRestore && pointerMin
               ? 0
               : ERROR_NOT_SUPPORTED;
}
