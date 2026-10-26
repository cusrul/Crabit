// Host-only bitmap review window. No watch, radio, UART or SDK access.
#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <string>
#include <vector>

struct Frame {
    std::wstring label, path;
    int count = 1;
    int delay = 50;
};
static std::vector<Frame> frames;
static HWND choices;
static HBITMAP bitmap;
static int selected;
static int animationFrame;
static std::wstring wide(const std::string& value) {
    int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0);
    std::wstring result(count, 0);
    MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), &result[0], count);
    return result;
}
static void loadBitmap() {
    if (bitmap)
        DeleteObject(bitmap);
    auto path = frames[selected].path;
    const auto marker = path.find(L"{frame}");
    if (marker != std::wstring::npos) {
        wchar_t number[16];
        swprintf(number, 16, L"%03d", animationFrame);
        path.replace(marker, 7, number);
    }
    bitmap = (HBITMAP)LoadImageW(nullptr, path.c_str(), IMAGE_BITMAP, 0, 0,
                                 LR_LOADFROMFILE | LR_CREATEDIBSECTION);
}
static void select(HWND window, int index) {
    if (frames.empty())
        return;
    KillTimer(window, 1);
    selected = (index + int(frames.size())) % int(frames.size());
    animationFrame = 0;
    loadBitmap();
    SendMessageW(choices, CB_SETCURSEL, selected, 0);
    if (frames[selected].count > 1)
        SetTimer(window, 1, frames[selected].delay, nullptr);
    InvalidateRect(window, nullptr, TRUE);
}
static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
        case WM_TIMER:
            if (w == 1 && !frames.empty()) {
                animationFrame = (animationFrame + 1) % frames[selected].count;
                loadBitmap();
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_CREATE:
            choices = CreateWindowW(L"COMBOBOX", nullptr,
                                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 16, 16,
                                    680, 400, window, (HMENU)1, nullptr, nullptr);
            for (const auto& frame : frames)
                SendMessageW(choices, CB_ADDSTRING, 0, (LPARAM)frame.label.c_str());
            CreateWindowW(L"BUTTON", L"上一页", WS_CHILD | WS_VISIBLE, 712, 14, 86, 30, window,
                          (HMENU)2, nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"下一页", WS_CHILD | WS_VISIBLE, 810, 14, 86, 30, window,
                          (HMENU)3, nullptr, nullptr);
            select(window, 0);
            return 0;
        case WM_COMMAND:
            if (LOWORD(w) == 1 && HIWORD(w) == CBN_SELCHANGE)
                select(window, int(SendMessageW(choices, CB_GETCURSEL, 0, 0)));
            if (LOWORD(w) == 2)
                select(window, selected - 1);
            if (LOWORD(w) == 3)
                select(window, selected + 1);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window, &paint);
            SetBkMode(dc, TRANSPARENT);
            const wchar_t* largeLabel = L"实际 NanoUI 绘制 / 4 倍像素放大";
            const wchar_t* nativeLabel = L"OLED 原始尺寸 128 × 64";
            TextOutW(dc, 16, 58, largeLabel, lstrlenW(largeLabel));
            TextOutW(dc, 560, 58, nativeLabel, lstrlenW(nativeLabel));
            if (bitmap) {
                HDC memory = CreateCompatibleDC(dc);
                auto previous = SelectObject(memory, bitmap);
                SetStretchBltMode(dc, COLORONCOLOR);
                StretchBlt(dc, 16, 88, 512, 256, memory, 0, 0, 128, 64, SRCCOPY);
                BitBlt(dc, 560, 88, 128, 64, memory, 0, 0, SRCCOPY);
                SelectObject(memory, previous);
                DeleteDC(memory);
            } else {
                RECT error{16, 88, 528, 344};
                DrawTextW(dc, L"预览图片加载失败，请重新生成预览。", -1, &error, DT_WORDBREAK);
            }
            RECT note{560, 180, 900, 330};
#ifdef CRABIT_UI_DESIGN_REVIEW
            DrawTextW(dc,
                      L"设计候选，不是当前手表截图。\n真实 NanoUI "
                      L"控件与点阵字体；时间、天气为模拟数据。\n"
                      L"\n仅 Windows 预览，未修改固件默认界面。\n切换页面比较字号、布局和状态。",
                      -1, &note, DT_WORDBREAK);
#else
            DrawTextW(dc,
                      L"离线状态样例，不连接手表。\n使用真实产品绘制代码和字体，读数为模拟数据。\n"
                      L"\n先审查字号、行距、长文本和按钮提示，再决定是否移到手表。",
                      -1, &note, DT_WORDBREAK);
#endif
            RECT label{16, 366, 900, 430};
            const auto text = frames.empty() ? L"没有预览图片" : frames[selected].label.c_str();
            DrawTextW(dc, text, -1, &label, DT_WORDBREAK);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_DESTROY:
            if (bitmap)
                DeleteObject(bitmap);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR arguments, int show) {
    wchar_t location[MAX_PATH];
    GetModuleFileNameW(nullptr, location, MAX_PATH);
    std::wstring root(location);
    root.resize(root.find_last_of(L"\\/") + 1);
    HANDLE file = CreateFileW((root + L"manifest.tsv").c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return 2;
    DWORD size = GetFileSize(file, nullptr), read = 0;
    std::string content(size, '\0');
    ReadFile(file, &content[0], size, &read, nullptr);
    CloseHandle(file);
    for (size_t start = 0; start < content.size();) {
        auto end = content.find('\n', start);
        if (end == std::string::npos)
            end = content.size();
        auto line = content.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        auto tab = line.find('\t');
        if (tab != std::string::npos) {
            const auto next = line.find('\t', tab + 1);
            Frame frame{wide(line.substr(0, tab)), wide(line.substr(tab + 1, next - tab - 1))};
            if (next != std::string::npos) {
                const auto last = line.find('\t', next + 1);
                frame.count = std::max(
                    1, std::min(600, std::atoi(line.substr(next + 1, last - next - 1).c_str())));
                if (last != std::string::npos)
                    frame.delay =
                        std::max(20, std::min(2000, std::atoi(line.substr(last + 1).c_str())));
            }
            frames.push_back(frame);
        }
        start = end + 1;
    }
    if (frames.empty())
        return 3;
    // Exercise the same Win32 loader as the visible window, including every animation frame.
    if (std::string(arguments) == "--check-images") {
        for (selected = 0; selected < int(frames.size()); ++selected) {
            for (animationFrame = 0; animationFrame < frames[selected].count; ++animationFrame) {
                loadBitmap();
                if (!bitmap)
                    return 4;
            }
        }
        DeleteObject(bitmap);
        return 0;
    }
    WNDCLASSW type{};
    type.hInstance = instance;
    type.lpfnWndProc = procedure;
    type.lpszClassName = L"CrabitUiReview";
    type.hCursor = LoadCursor(nullptr, IDC_ARROW);
    type.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&type);
    HWND window = CreateWindowW(type.lpszClassName, L"Crabit 字号与排版预览 · 离线模拟，不是实机",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 940, 500,
                                nullptr, nullptr, instance, nullptr);
    ShowWindow(window, show);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return int(message.wParam);
}
