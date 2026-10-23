#ifndef OPENMW_STARTUPSCREEN_HPP
#define OPENMW_STARTUPSCREEN_HPP

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#pragma push_macro("NOGDI")
#undef NOGDI
#include <windows.h>
// windows.h may already have been included with NOGDI by another header.
#include <wingdi.h>
#pragma pop_macro("NOGDI")
#undef near
#undef far
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#endif

namespace OMW
{
    // Independent of SDL, OpenGL, the VFS and game state. Only this thread owns
    // its native window and GDI resources; the engine publishes status text.
    class StartupScreen
    {
#ifdef _WIN32
        std::atomic<bool> mStop{false};
        std::mutex mMutex;
        std::wstring mStage = L"Reading configuration";
        std::thread mThread;
        std::chrono::steady_clock::time_point mStart = std::chrono::steady_clock::now();

        void paint(HWND window)
        {
            PAINTSTRUCT ps;
            HDC target = BeginPaint(window, &ps);
            RECT bounds;
            GetClientRect(window, &bounds);
            HDC dc = CreateCompatibleDC(target);
            HBITMAP bitmap = CreateCompatibleBitmap(target, bounds.right, bounds.bottom);
            if (!dc || !bitmap)
            {
                if (dc) DeleteDC(dc);
                if (bitmap) DeleteObject(bitmap);
                EndPaint(window, &ps);
                return;
            }
            HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
            HBRUSH background = CreateSolidBrush(RGB(22, 26, 32));
            FillRect(dc, &bounds, background);
            DeleteObject(background);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(226, 213, 176));
            HFONT title = CreateFontW(-32, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            HGDIOBJ oldFont = SelectObject(dc, title);
            RECT text{32, 30, bounds.right - 32, 80};
            DrawTextW(dc, L"OpenMW", -1, &text, DT_CENTER | DT_SINGLELINE);
            HFONT body = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            SelectObject(dc, body);
            std::wstring stage;
            { const std::lock_guard lock(mMutex); stage = mStage; }
            SetTextColor(dc, RGB(225, 228, 232));
            text = RECT{24, 93, bounds.right - 24, 125};
            DrawTextW(dc, stage.c_str(), -1, &text, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - mStart).count();
            const std::wstring elapsed = L"Starting up  |  " + std::to_wstring(ms / 1000) + L" s";
            text = RECT{24, 137, bounds.right - 24, 165};
            SetTextColor(dc, RGB(154, 164, 178));
            DrawTextW(dc, elapsed.c_str(), -1, &text, DT_CENTER | DT_SINGLELINE);
            RECT track{40, 190, bounds.right - 40, 194};
            HBRUSH dim = CreateSolidBrush(RGB(48, 55, 66));
            FillRect(dc, &track, dim);
            DeleteObject(dim);
            // Activity indicator, deliberately not a percentage estimate.
            const int travel = bounds.right - 160;
            const int phase = static_cast<int>((ms / 6) % (2 * travel));
            const int x = 40 + (phase < travel ? phase : 2 * travel - phase);
            RECT indicator{x, 190, x + 80, 194};
            HBRUSH accent = CreateSolidBrush(RGB(194, 168, 111));
            FillRect(dc, &indicator, accent);
            DeleteObject(accent);
            BitBlt(target, 0, 0, bounds.right, bounds.bottom, dc, 0, 0, SRCCOPY);
            SelectObject(dc, oldFont);
            DeleteObject(body);
            DeleteObject(title);
            SelectObject(dc, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(dc);
            EndPaint(window, &ps);
        }

        static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l)
        {
            auto* self = reinterpret_cast<StartupScreen*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE)
            {
                self = static_cast<StartupScreen*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
            if (self)
            {
                if (message == WM_TIMER)
                {
                    if (self->mStop.load()) DestroyWindow(window);
                    else InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
                if (message == WM_PAINT) { self->paint(window); return 0; }
                if (message == WM_ERASEBKGND) return 1;
                if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
                if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
            }
            return DefWindowProcW(window, message, w, l);
        }

        void run()
        {
            const auto instance = GetModuleHandleW(nullptr);
            WNDCLASSW wc{};
            wc.lpfnWndProc = procedure;
            wc.hInstance = instance;
            wc.lpszClassName = L"OpenMWStartupStatus";
            wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            if (!RegisterClassW(&wc)) return;
            POINT pointer{};
            GetCursorPos(&pointer);
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            GetMonitorInfoW(MonitorFromPoint(pointer, MONITOR_DEFAULTTOPRIMARY), &monitor);
            const RECT area = monitor.rcWork;
            HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                wc.lpszClassName, L"OpenMW is starting", WS_POPUP,
                area.left + (area.right - area.left - 520) / 2,
                area.top + (area.bottom - area.top - 230) / 2,
                520, 230, nullptr, nullptr, instance, this);
            if (window)
            {
                if (!mStop.load() && SetTimer(window, 1, 100, nullptr))
                {
                    ShowWindow(window, SW_SHOWNOACTIVATE);
                    UpdateWindow(window);
                    MSG message;
                    while (GetMessageW(&message, nullptr, 0, 0) > 0)
                    {
                        TranslateMessage(&message);
                        DispatchMessageW(&message);
                    }
                }
                if (IsWindow(window)) DestroyWindow(window);
            }
            UnregisterClassW(wc.lpszClassName, instance);
        }
#endif
    public:
        explicit StartupScreen(bool enabled = true)
        {
#ifdef _WIN32
            if (enabled)
            {
                try { mThread = std::thread([this] { run(); }); }
                catch (...) { /* A cosmetic startup panel must not prevent launch. */ }
            }
#endif
        }
        StartupScreen(const StartupScreen&) = delete;
        StartupScreen& operator=(const StartupScreen&) = delete;
        ~StartupScreen() { finish(); }
        void setStage(const wchar_t* stage)
        {
#ifdef _WIN32
            const std::lock_guard lock(mMutex);
            mStage = stage;
#endif
        }
        void finish()
        {
#ifdef _WIN32
            mStop.store(true);
            if (mThread.joinable()) mThread.join();
#endif
        }
    };
}
#endif
